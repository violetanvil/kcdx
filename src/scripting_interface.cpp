// See scripting_interface.h.
#include "scripting_interface.h"

#include <cstdarg>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include "dev.h"
#include "log.h"

namespace kcdx::scripting_interface {

namespace {

// One registered function. The kcdxLuaCFunction is the plugin's
// C function pointer; we wrap it in a Lua-side shim
// (LuaDispatchShim below) that calls the plugin function with the
// captured user_data.
struct Registration {
    kcdxPluginHandle owner       = 0;
    std::string      table_name;
    std::string      fn_name;
    kcdxLuaCFunction fn          = nullptr;
    void*            user_data   = nullptr;
};

// Locked because plugins may call from kcdxPlugin_Load on the main
// thread but multiple plugin DLLs can be loading concurrently in
// theory (kcdx doesn't currently load DLLs in parallel, but lock
// anyway as future-proofing).
std::mutex                 g_lock;
std::vector<Registration>  g_pending;     // queued before kcdx table exists
std::vector<Registration>  g_applied;     // landed registrations (keeps
                                          // Registrations alive for the
                                          // session; the shim's upvalue
                                          // is a pointer into this vector)
bool                       g_table_ready = false;
lua_State*                 g_lua_state   = nullptr;  // captured during apply

// Lua-side shim. Lua calls this; we look up the Registration via
// upvalue and dispatch.
//
// Upvalue layout:
//   1. lightuserdata pointing at the Registration in g_applied
int LuaDispatchShim(lua_State* L) {
    void* p = lua_touserdata(L, lua_upvalueindex(1));
    if (!p) {
        KCDX_DEV("SCRIPTING", "SHIM/null-upvalue",
            kcdx::dev::KV("L", (const void*)L));
        return luaL_error(L, "kcdx scripting shim: null upvalue");
    }
    auto* reg = static_cast<Registration*>(p);
    KCDX_DEV("SCRIPTING", "SHIM/enter",
        kcdx::dev::KV("L",         (const void*)L),
        kcdx::dev::KV("table",     reg->table_name),
        kcdx::dev::KV("fn",        reg->fn_name),
        kcdx::dev::KV("nargs",     lua_gettop(L)),
        kcdx::dev::KV("plugin_fn", (const void*)reg->fn));
    if (!reg->fn) return luaL_error(L, "kcdx scripting shim: null fn pointer");
    int nresults = reg->fn(L, reg->user_data);
    KCDX_DEV("SCRIPTING", "SHIM/exit",
        kcdx::dev::KV("table",    reg->table_name),
        kcdx::dev::KV("fn",       reg->fn_name),
        kcdx::dev::KV("nresults", nresults));
    return nresults;
}

// Ensure kcdx.<table_name> exists. Assumes kcdx table is at stack top.
// Leaves the sub-table on top. Stack effect: 0 net (pops the kcdx
// table, leaves the sub-table). Caller manages the rest.
void GetOrCreateSubtable(lua_State* L, const char* table_name) {
    // Stack: [..., kcdx]
    lua_getfield(L, -1, table_name);  // [..., kcdx, kcdx[name]]
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);                 // [..., kcdx]
        lua_newtable(L);               // [..., kcdx, newtable]
        lua_pushvalue(L, -1);          // [..., kcdx, newtable, newtable]
        lua_setfield(L, -3, table_name); // sets kcdx[name] = newtable; [..., kcdx, newtable]
    }
    // [..., kcdx, subtable] — remove kcdx, leave subtable
    lua_remove(L, -2);
    // [..., subtable]
}

// Apply one registration to a live Lua state. Assumes the kcdx
// global is at LUA_GLOBALSINDEX. Logs the result. Caller holds g_lock.
void ApplyOne(lua_State* L, Registration* reg) {
    // Push the kcdx global onto the stack
    lua_getglobal(L, "kcdx");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        log::ErrorF("scripting_interface: kcdx global not a table when applying "
                    "'%s.%s' (owner=%u)",
                    reg->table_name.c_str(), reg->fn_name.c_str(), reg->owner);
        return;
    }
    GetOrCreateSubtable(L, reg->table_name.c_str());
    // Stack: [..., subtable]

    // Check for name collision (a function with same name already there).
    lua_getfield(L, -1, reg->fn_name.c_str());
    if (!lua_isnil(L, -1)) {
        lua_pop(L, 2);  // pop the existing value + subtable
        log::WarnF("scripting_interface: '%s.%s' already exists, "
                   "skipping registration from owner=%u (first-wins)",
                   reg->table_name.c_str(), reg->fn_name.c_str(), reg->owner);
        return;
    }
    lua_pop(L, 1);  // pop the nil

    KCDX_DEV("SCRIPTING", "REGISTER",
        kcdx::dev::KV("L",          (const void*)L),
        kcdx::dev::KV("table",      reg->table_name),
        kcdx::dev::KV("fn",         reg->fn_name),
        kcdx::dev::KV("owner",      (unsigned)reg->owner),
        kcdx::dev::KV("shim",       (const void*)&LuaDispatchShim),
        kcdx::dev::KV("upvalue",    (const void*)reg),
        kcdx::dev::KV("plugin_fn",  (const void*)reg->fn));

    // Push C closure with the Registration pointer as upvalue.
    // Storage stability: g_applied reserve(1024) ahead of time.
    lua_pushlightuserdata(L, reg);
    lua_pushcclosure(L, LuaDispatchShim, 1);

    // Right-after-push observation: type + cclosure heap object addr +
    // c.f field via tocfunction. Confirms what we just pushed.
    {
        int t = lua_type(L, -1);
        const void* p_obj = lua_topointer(L, -1);
        lua_CFunction f_cf = lua_iscfunction(L, -1) ? lua_tocfunction(L, -1) : nullptr;
        KCDX_DEV("SCRIPTING", "REGISTER/post-push",
            kcdx::dev::KV("table",       reg->table_name),
            kcdx::dev::KV("fn",          reg->fn_name),
            kcdx::dev::KV("lua_type",    t),
            kcdx::dev::KV("topointer",   p_obj),
            kcdx::dev::KV("tocfunction", (const void*)f_cf),
            kcdx::dev::KV("expected_shim", (const void*)&LuaDispatchShim));
    }

    // Stack: [..., subtable, cclosure]
    lua_setfield(L, -2, reg->fn_name.c_str());
    lua_pop(L, 1);  // pop subtable

    // Readback via _G.kcdx.<table>.<fn> after the assignment — confirms
    // what landed in the table matches what we pushed.
    lua_getglobal(L, "kcdx");
    GetOrCreateSubtable(L, reg->table_name.c_str());
    lua_getfield(L, -1, reg->fn_name.c_str());
    {
        int t = lua_type(L, -1);
        const void* p_obj = lua_topointer(L, -1);
        lua_CFunction f_cf = lua_iscfunction(L, -1) ? lua_tocfunction(L, -1) : nullptr;
        KCDX_DEV("SCRIPTING", "REGISTER/readback",
            kcdx::dev::KV("table",       reg->table_name),
            kcdx::dev::KV("fn",          reg->fn_name),
            kcdx::dev::KV("lua_type",    t),
            kcdx::dev::KV("topointer",   p_obj),
            kcdx::dev::KV("tocfunction", (const void*)f_cf),
            kcdx::dev::KV("expected_shim", (const void*)&LuaDispatchShim));
    }
    lua_pop(L, 2);  // pop the field value + subtable
}

// Validate a name string. Must be a valid Lua identifier:
// [A-Za-z_][A-Za-z0-9_]*, non-empty, <= 64 chars.
bool ValidName(const char* s) {
    if (!s || !*s) return false;
    size_t len = strlen(s);
    if (len > 64) return false;
    auto isFirst = [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
    };
    auto isRest = [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
               (c >= '0' && c <= '9') || c == '_';
    };
    if (!isFirst(s[0])) return false;
    for (size_t i = 1; i < len; ++i) if (!isRest(s[i])) return false;
    return true;
}

int Thunk_RegisterFunction(kcdxPluginHandle owner,
                           const char*      table_name,
                           const char*      fn_name,
                           kcdxLuaCFunction fn,
                           void*            user_data) {
    if (!ValidName(table_name)) {
        log::ErrorF("RegisterFunction: invalid table_name '%s' (owner=%u)",
                    table_name ? table_name : "(null)", owner);
        return 0;
    }
    if (!ValidName(fn_name)) {
        log::ErrorF("RegisterFunction: invalid fn_name '%s' (owner=%u)",
                    fn_name ? fn_name : "(null)", owner);
        return 0;
    }
    if (!fn) {
        log::ErrorF("RegisterFunction: null fn pointer (owner=%u, name=%s.%s)",
                    owner, table_name, fn_name);
        return 0;
    }

    std::lock_guard<std::mutex> lock(g_lock);

    // g_applied uses push_back; we need stable pointers because the
    // LuaDispatchShim's upvalue is a raw pointer into this storage.
    // std::vector::push_back may reallocate. We reserve aggressively
    // at first call to keep the pointer stable through reasonable
    // plugin growth. 1024 entries should cover any realistic plugin
    // ecosystem; if exceeded, we log and fail rather than silently
    // invalidating earlier pointers.
    if (g_applied.empty()) {
        g_applied.reserve(1024);
        g_pending.reserve(1024);
    }
    if (g_applied.size() + g_pending.size() >= 1024) {
        log::ErrorF("RegisterFunction: registration cap (1024) reached, "
                    "rejecting '%s.%s' from owner=%u",
                    table_name, fn_name, owner);
        return 0;
    }

    if (g_table_ready && g_lua_state) {
        g_applied.push_back({owner, table_name, fn_name, fn, user_data});
        ApplyOne(g_lua_state, &g_applied.back());
    } else {
        g_pending.push_back({owner, table_name, fn_name, fn, user_data});
        log::DebugF("RegisterFunction: queued '%s.%s' from owner=%u "
                    "(kcdx global not ready yet)",
                    table_name, fn_name, owner);
    }
    return 1;
}

// --- kcdxLuaApi function-pointer table ----------------------------------
//
// Most members are direct passthroughs to lua_* / luaL_* with a
// (potential) signature/typedef reshape — kcdxLuaDebug and kcdxLuaLBuffer
// are layout-identical to Lua's lua_Debug / luaL_Buffer (verified against
// vendor/lua headers) and reinterpret_cast safely.
//
// Precision-lossy entries (PushNumber/PushInteger/ToNumber/ToInteger and
// the L-prefixed Check/Opt Number/Integer variants) cast through the
// public double/long long ABI to/from Lua's internal lua_Number (float
// on KCD2's build) and lua_Integer (ptrdiff_t). The lossiness is documented
// at the struct field and in docs/lua-number-precision.md.
//
// PushCFunction needs the closure-shim glue; everything else is a
// one-line forward.

// State manipulation
lua_State* api_NewState           (kcdxLuaAlloc f, void* ud)                   { return lua_newstate(reinterpret_cast<lua_Alloc>(f), ud); }
void       api_Close              (lua_State* L)                               { lua_close(L); }
lua_State* api_NewThread          (lua_State* L)                               { return lua_newthread(L); }
kcdxLuaRawCFunction api_AtPanic   (lua_State* L, kcdxLuaRawCFunction panicf)   { return reinterpret_cast<kcdxLuaRawCFunction>(lua_atpanic(L, reinterpret_cast<lua_CFunction>(panicf))); }
void       api_StoreDebugInfo     (lua_State* L, int enable)                  { lua_storedebuginfo(L, enable); }
int        api_IsStoreDebugInfo   (lua_State* L)                              { return lua_isstoredebuginfo(L); }

// Basic stack manipulation
int  api_GetTop      (lua_State* L)                                            { return lua_gettop(L); }
void api_SetTop      (lua_State* L, int idx)                                   { lua_settop(L, idx); }
void api_PushValue   (lua_State* L, int idx)                                   { lua_pushvalue(L, idx); }
void api_Remove      (lua_State* L, int idx)                                   { lua_remove(L, idx); }
void api_Insert      (lua_State* L, int idx)                                   { lua_insert(L, idx); }
void api_Replace     (lua_State* L, int idx)                                   { lua_replace(L, idx); }
int  api_CheckStack  (lua_State* L, int n)                                     { return lua_checkstack(L, n); }
void api_XMove       (lua_State* from, lua_State* to, int n)                   { lua_xmove(from, to, n); }

// Access (stack -> C)
int         api_IsNumber    (lua_State* L, int idx)                            { return lua_isnumber(L, idx); }
int         api_IsString    (lua_State* L, int idx)                            { return lua_isstring(L, idx); }
int         api_IsCFunction (lua_State* L, int idx)                            { return lua_iscfunction(L, idx); }
int         api_IsUserdata  (lua_State* L, int idx)                            { return lua_isuserdata(L, idx); }
int         api_IsBoolean   (lua_State* L, int idx)                            { return lua_isboolean(L, idx); }
int         api_IsNil       (lua_State* L, int idx)                            { return lua_isnil(L, idx); }
int         api_IsTable     (lua_State* L, int idx)                            { return lua_istable(L, idx); }
int         api_IsFunction  (lua_State* L, int idx)                            { return lua_isfunction(L, idx); }
int         api_IsLightUserdata(lua_State* L, int idx)                         { return lua_islightuserdata(L, idx); }
int         api_IsThread    (lua_State* L, int idx)                            { return lua_isthread(L, idx); }
int         api_IsNone      (lua_State* L, int idx)                            { return lua_isnone(L, idx); }
int         api_IsNoneOrNil (lua_State* L, int idx)                            { return lua_isnoneornil(L, idx); }
int         api_Type        (lua_State* L, int idx)                            { return lua_type(L, idx); }
const char* api_TypeName    (lua_State* L, int tp)                             { return lua_typename(L, tp); }
int         api_Equal       (lua_State* L, int idx1, int idx2)                 { return lua_equal(L, idx1, idx2); }
int         api_RawEqual    (lua_State* L, int idx1, int idx2)                 { return lua_rawequal(L, idx1, idx2); }
int         api_LessThan    (lua_State* L, int idx1, int idx2)                 { return lua_lessthan(L, idx1, idx2); }
double      api_ToNumber    (lua_State* L, int idx)                            { return (double)lua_tonumber(L, idx); }
long long   api_ToInteger   (lua_State* L, int idx)                            { return (long long)lua_tointeger(L, idx); }
int         api_ToBoolean   (lua_State* L, int idx)                            { return lua_toboolean(L, idx); }
const char* api_ToString    (lua_State* L, int idx)                            { return lua_tostring(L, idx); }
const char* api_ToLString   (lua_State* L, int idx, size_t* len)               { return lua_tolstring(L, idx, len); }
size_t      api_ObjLen      (lua_State* L, int idx)                            { return lua_objlen(L, idx); }
kcdxLuaRawCFunction api_ToCFunction (lua_State* L, int idx)                    { return reinterpret_cast<kcdxLuaRawCFunction>(lua_tocfunction(L, idx)); }
void*       api_ToUserdata  (lua_State* L, int idx)                            { return lua_touserdata(L, idx); }
lua_State*  api_ToThread    (lua_State* L, int idx)                            { return lua_tothread(L, idx); }
const void* api_ToPointer   (lua_State* L, int idx)                            { return lua_topointer(L, idx); }

// Push (C -> stack)
void        api_PushNil           (lua_State* L)                               { lua_pushnil(L); }
void        api_PushNumber        (lua_State* L, double n)                     { lua_pushnumber(L, (lua_Number)n); }
void        api_PushInteger       (lua_State* L, long long n)                  { lua_pushinteger(L, (lua_Integer)n); }
void        api_PushLString       (lua_State* L, const char* s, size_t len)    { lua_pushlstring(L, s, len); }
void        api_PushString        (lua_State* L, const char* s)                { lua_pushstring(L, s); }
const char* api_PushVFString      (lua_State* L, const char* fmt, va_list ap)  { return lua_pushvfstring(L, fmt, ap); }
const char* api_PushFString       (lua_State* L, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const char* r = lua_pushvfstring(L, fmt, ap);
    va_end(ap);
    return r;
}
void        api_PushBoolean       (lua_State* L, int b)                        { lua_pushboolean(L, b); }
void        api_PushLightUserdata (lua_State* L, void* p)                      { lua_pushlightuserdata(L, p); }
int         api_PushThread        (lua_State* L)                               { return lua_pushthread(L); }

// PushCFunction: install a shim that calls back through the
// kcdxLuaCFunction signature. Same upvalue trick as the registry
// shim, scoped to this single push.
void        api_PushCFunction (lua_State* L, kcdxLuaCFunction fn, void* ud) {
    // Pack (fn, ud) into a stable Registration via g_applied — the
    // upvalue is a raw pointer into that vector, so storage must not
    // move. g_applied reserve(1024) handles this for the session.
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_applied.size() + g_pending.size() >= 1024) {
        luaL_error(L, "kcdx scripting: registration cap reached, "
                      "cannot bind anonymous PushCFunction closure");
        return;
    }
    g_applied.push_back({0, "<inline>", "<closure>", fn, ud});
    lua_pushlightuserdata(L, &g_applied.back());
    lua_pushcclosure(L, LuaDispatchShim, 1);
}

// PushCClosure: raw Lua semantics — caller has already pushed `n`
// upvalues onto the stack; lua_pushcclosure pops them and binds.
void        api_PushCClosure (lua_State* L, kcdxLuaRawCFunction fn, int n)     { lua_pushcclosure(L, reinterpret_cast<lua_CFunction>(fn), n); }

// Get (Lua -> stack)
void  api_GetTable      (lua_State* L, int idx)                                { lua_gettable(L, idx); }
void  api_GetField      (lua_State* L, int idx, const char* k)                 { lua_getfield(L, idx, k); }
void  api_RawGet        (lua_State* L, int idx)                                { lua_rawget(L, idx); }
void  api_RawGetI       (lua_State* L, int idx, int n)                         { lua_rawgeti(L, idx, n); }
void  api_CreateTable   (lua_State* L, int narr, int nrec)                     { lua_createtable(L, narr, nrec); }
void  api_NewTable      (lua_State* L)                                         { lua_newtable(L); }
void* api_NewUserdata   (lua_State* L, size_t sz)                              { return lua_newuserdata(L, sz); }
int   api_GetMetatable  (lua_State* L, int objindex)                           { return lua_getmetatable(L, objindex); }
void  api_GetFEnv       (lua_State* L, int idx)                                { lua_getfenv(L, idx); }
void  api_GetGlobal     (lua_State* L, const char* name)                       { lua_getglobal(L, name); }

// Set (stack -> Lua)
void  api_SetTable      (lua_State* L, int idx)                                { lua_settable(L, idx); }
void  api_SetField      (lua_State* L, int idx, const char* k)                 { lua_setfield(L, idx, k); }
void  api_RawSet        (lua_State* L, int idx)                                { lua_rawset(L, idx); }
void  api_RawSetI       (lua_State* L, int idx, int n)                         { lua_rawseti(L, idx, n); }
int   api_SetMetatable  (lua_State* L, int objindex)                           { return lua_setmetatable(L, objindex); }
int   api_SetFEnv       (lua_State* L, int idx)                                { return lua_setfenv(L, idx); }
void  api_SetGlobal     (lua_State* L, const char* name)                       { lua_setglobal(L, name); }

// Load and run
void api_Call  (lua_State* L, int nargs, int nresults)                         { lua_call(L, nargs, nresults); }
int  api_PCall (lua_State* L, int nargs, int nresults, int errfunc)            { return lua_pcall(L, nargs, nresults, errfunc); }
int  api_CPCall(lua_State* L, kcdxLuaRawCFunction func, void* ud)              { return lua_cpcall(L, reinterpret_cast<lua_CFunction>(func), ud); }
int  api_Load  (lua_State* L, kcdxLuaReader reader, void* dt, const char* chunkname) { return lua_load(L, reinterpret_cast<lua_Reader>(reader), dt, chunkname); }
int  api_Dump  (lua_State* L, kcdxLuaWriter writer, void* data)                { return lua_dump(L, reinterpret_cast<lua_Writer>(writer), data); }

// Coroutines
int api_Yield  (lua_State* L, int nresults)                                    { return lua_yield(L, nresults); }
int api_Resume (lua_State* L, int narg)                                        { return lua_resume(L, narg); }
int api_Status (lua_State* L)                                                  { return lua_status(L); }

// GC
int api_GC (lua_State* L, int what, int data)                                  { return lua_gc(L, what, data); }

// Misc
int          api_Error      (lua_State* L)                                     { return lua_error(L); }
int          api_Next       (lua_State* L, int idx)                            { return lua_next(L, idx); }
void         api_Concat     (lua_State* L, int n)                              { lua_concat(L, n); }
kcdxLuaAlloc api_GetAllocF  (lua_State* L, void** ud)                          { return reinterpret_cast<kcdxLuaAlloc>(lua_getallocf(L, ud)); }
void         api_SetAllocF  (lua_State* L, kcdxLuaAlloc f, void* ud)           { lua_setallocf(L, reinterpret_cast<lua_Alloc>(f), ud); }

// Debug API
int         api_GetStack     (lua_State* L, int level, kcdxLuaDebug* ar)       { return lua_getstack(L, level, reinterpret_cast<lua_Debug*>(ar)); }
int         api_GetInfo      (lua_State* L, const char* what, kcdxLuaDebug* ar){ return lua_getinfo(L, what, reinterpret_cast<lua_Debug*>(ar)); }
const char* api_GetLocal     (lua_State* L, const kcdxLuaDebug* ar, int n)     { return lua_getlocal(L, reinterpret_cast<const lua_Debug*>(ar), n); }
const char* api_SetLocal     (lua_State* L, const kcdxLuaDebug* ar, int n)     { return lua_setlocal(L, reinterpret_cast<const lua_Debug*>(ar), n); }
const char* api_GetUpvalue   (lua_State* L, int funcindex, int n)              { return lua_getupvalue(L, funcindex, n); }
const char* api_SetUpvalue   (lua_State* L, int funcindex, int n)              { return lua_setupvalue(L, funcindex, n); }
int         api_SetHook      (lua_State* L, kcdxLuaHook func, int mask, int count) { return lua_sethook(L, reinterpret_cast<lua_Hook>(func), mask, count); }
kcdxLuaHook api_GetHook      (lua_State* L)                                    { return reinterpret_cast<kcdxLuaHook>(lua_gethook(L)); }
int         api_GetHookMask  (lua_State* L)                                    { return lua_gethookmask(L); }
int         api_GetHookCount (lua_State* L)                                    { return lua_gethookcount(L); }

// Auxiliary library (luaL_*)
void         api_LOpenLib       (lua_State* L, const char* libname, const kcdxLuaLReg* l, int nup) { luaI_openlib(L, libname, reinterpret_cast<const luaL_Reg*>(l), nup); }
void         api_LRegister      (lua_State* L, const char* libname, const kcdxLuaLReg* l) { luaL_register(L, libname, reinterpret_cast<const luaL_Reg*>(l)); }
int          api_LGetMetafield  (lua_State* L, int obj, const char* e)         { return luaL_getmetafield(L, obj, e); }
int          api_LCallMeta      (lua_State* L, int obj, const char* e)         { return luaL_callmeta(L, obj, e); }
int          api_LTypeError     (lua_State* L, int narg, const char* tname)    { return luaL_typerror(L, narg, tname); }
int          api_LArgError      (lua_State* L, int numarg, const char* extramsg) { return luaL_argerror(L, numarg, extramsg); }
const char*  api_LCheckLString  (lua_State* L, int numArg, size_t* l)          { return luaL_checklstring(L, numArg, l); }
const char*  api_LOptLString    (lua_State* L, int numArg, const char* def, size_t* l) { return luaL_optlstring(L, numArg, def, l); }
double       api_LCheckNumber   (lua_State* L, int numArg)                     { return (double)luaL_checknumber(L, numArg); }
double       api_LOptNumber     (lua_State* L, int nArg, double def)           { return (double)luaL_optnumber(L, nArg, (lua_Number)def); }
long long    api_LCheckInteger  (lua_State* L, int numArg)                     { return (long long)luaL_checkinteger(L, numArg); }
long long    api_LOptInteger    (lua_State* L, int nArg, long long def)        { return (long long)luaL_optinteger(L, nArg, (lua_Integer)def); }
void         api_LCheckStack    (lua_State* L, int sz, const char* msg)        { luaL_checkstack(L, sz, msg); }
void         api_LCheckType     (lua_State* L, int narg, int t)                { luaL_checktype(L, narg, t); }
void         api_LCheckAny      (lua_State* L, int narg)                       { luaL_checkany(L, narg); }
int          api_LNewMetatable  (lua_State* L, const char* tname)              { return luaL_newmetatable(L, tname); }
void*        api_LCheckUdata    (lua_State* L, int ud, const char* tname)      { return luaL_checkudata(L, ud, tname); }
void         api_LWhere         (lua_State* L, int lvl)                        { luaL_where(L, lvl); }
int          api_LError         (lua_State* L, const char* fmt, ...) {
    // luaL_error in Lua 5.1 is varargs; replicate using lua_pushvfstring + lua_error.
    va_list ap;
    va_start(ap, fmt);
    luaL_where(L, 1);
    lua_pushvfstring(L, fmt, ap);
    va_end(ap);
    lua_concat(L, 2);
    return lua_error(L);
}
int          api_LCheckOption   (lua_State* L, int narg, const char* def, const char* const lst[]) { return luaL_checkoption(L, narg, def, lst); }
int          api_LRef           (lua_State* L, int t)                          { return luaL_ref(L, t); }
void         api_LUnref         (lua_State* L, int t, int ref)                 { luaL_unref(L, t, ref); }
int          api_LLoadFile      (lua_State* L, const char* filename)           { return luaL_loadfile(L, filename); }
int          api_LLoadBuffer    (lua_State* L, const char* buff, size_t sz, const char* name) { return luaL_loadbuffer(L, buff, sz, name); }
int          api_LLoadString    (lua_State* L, const char* s)                  { return luaL_loadstring(L, s); }
lua_State*   api_LNewState      (void)                                         { return luaL_newstate(); }
const char*  api_LGSub          (lua_State* L, const char* s, const char* p, const char* r) { return luaL_gsub(L, s, p, r); }
const char*  api_LFindTable     (lua_State* L, int idx, const char* fname, int szhint) { return luaL_findtable(L, idx, fname, szhint); }
void         api_LBuffInit      (lua_State* L, kcdxLuaLBuffer* B)              { luaL_buffinit(L, reinterpret_cast<luaL_Buffer*>(B)); }
char*        api_LPrepBuffer    (kcdxLuaLBuffer* B)                            { return luaL_prepbuffer(reinterpret_cast<luaL_Buffer*>(B)); }
void         api_LAddLString    (kcdxLuaLBuffer* B, const char* s, size_t l)   { luaL_addlstring(reinterpret_cast<luaL_Buffer*>(B), s, l); }
void         api_LAddString     (kcdxLuaLBuffer* B, const char* s)             { luaL_addstring(reinterpret_cast<luaL_Buffer*>(B), s); }
void         api_LAddValue      (kcdxLuaLBuffer* B)                            { luaL_addvalue(reinterpret_cast<luaL_Buffer*>(B)); }
void         api_LPushResult    (kcdxLuaLBuffer* B)                            { luaL_pushresult(reinterpret_cast<luaL_Buffer*>(B)); }

const kcdxLuaApi g_lua_api = {
    // state manipulation
    /*NewState=*/             api_NewState,
    /*Close=*/                api_Close,
    /*NewThread=*/            api_NewThread,
    /*AtPanic=*/              api_AtPanic,
    /*StoreDebugInfo=*/       api_StoreDebugInfo,
    /*IsStoreDebugInfo=*/     api_IsStoreDebugInfo,

    // basic stack manipulation
    /*GetTop=*/               api_GetTop,
    /*SetTop=*/               api_SetTop,
    /*PushValue=*/            api_PushValue,
    /*Remove=*/               api_Remove,
    /*Insert=*/               api_Insert,
    /*Replace=*/              api_Replace,
    /*CheckStack=*/           api_CheckStack,
    /*XMove=*/                api_XMove,

    // access functions
    /*IsNumber=*/             api_IsNumber,
    /*IsString=*/             api_IsString,
    /*IsCFunction=*/          api_IsCFunction,
    /*IsUserdata=*/           api_IsUserdata,
    /*IsBoolean=*/            api_IsBoolean,
    /*IsNil=*/                api_IsNil,
    /*IsTable=*/              api_IsTable,
    /*IsFunction=*/           api_IsFunction,
    /*IsLightUserdata=*/      api_IsLightUserdata,
    /*IsThread=*/             api_IsThread,
    /*IsNone=*/               api_IsNone,
    /*IsNoneOrNil=*/          api_IsNoneOrNil,
    /*Type=*/                 api_Type,
    /*TypeName=*/             api_TypeName,
    /*Equal=*/                api_Equal,
    /*RawEqual=*/             api_RawEqual,
    /*LessThan=*/             api_LessThan,
    /*ToNumber=*/             api_ToNumber,
    /*ToInteger=*/            api_ToInteger,
    /*ToBoolean=*/            api_ToBoolean,
    /*ToString=*/             api_ToString,
    /*ToLString=*/            api_ToLString,
    /*ObjLen=*/               api_ObjLen,
    /*ToCFunction=*/          api_ToCFunction,
    /*ToUserdata=*/           api_ToUserdata,
    /*ToThread=*/             api_ToThread,
    /*ToPointer=*/            api_ToPointer,

    // push functions
    /*PushNil=*/              api_PushNil,
    /*PushNumber=*/           api_PushNumber,
    /*PushInteger=*/          api_PushInteger,
    /*PushLString=*/          api_PushLString,
    /*PushString=*/           api_PushString,
    /*PushVFString=*/         api_PushVFString,
    /*PushFString=*/          api_PushFString,
    /*PushBoolean=*/          api_PushBoolean,
    /*PushLightUserdata=*/    api_PushLightUserdata,
    /*PushThread=*/           api_PushThread,
    /*PushCFunction=*/        api_PushCFunction,
    /*PushCClosure=*/         api_PushCClosure,

    // get functions
    /*GetTable=*/             api_GetTable,
    /*GetField=*/             api_GetField,
    /*RawGet=*/               api_RawGet,
    /*RawGetI=*/              api_RawGetI,
    /*CreateTable=*/          api_CreateTable,
    /*NewTable=*/             api_NewTable,
    /*NewUserdata=*/          api_NewUserdata,
    /*GetMetatable=*/         api_GetMetatable,
    /*GetFEnv=*/              api_GetFEnv,
    /*GetGlobal=*/            api_GetGlobal,

    // set functions
    /*SetTable=*/             api_SetTable,
    /*SetField=*/             api_SetField,
    /*RawSet=*/               api_RawSet,
    /*RawSetI=*/              api_RawSetI,
    /*SetMetatable=*/         api_SetMetatable,
    /*SetFEnv=*/              api_SetFEnv,
    /*SetGlobal=*/            api_SetGlobal,

    // load + call
    /*Call=*/                 api_Call,
    /*PCall=*/                api_PCall,
    /*CPCall=*/               api_CPCall,
    /*Load=*/                 api_Load,
    /*Dump=*/                 api_Dump,

    // coroutines
    /*Yield=*/                api_Yield,
    /*Resume=*/               api_Resume,
    /*Status=*/               api_Status,

    // GC
    /*GC=*/                   api_GC,

    // misc
    /*Error=*/                api_Error,
    /*Next=*/                 api_Next,
    /*Concat=*/               api_Concat,
    /*GetAllocF=*/            api_GetAllocF,
    /*SetAllocF=*/            api_SetAllocF,

    // debug API
    /*GetStack=*/             api_GetStack,
    /*GetInfo=*/              api_GetInfo,
    /*GetLocal=*/             api_GetLocal,
    /*SetLocal=*/             api_SetLocal,
    /*GetUpvalue=*/           api_GetUpvalue,
    /*SetUpvalue=*/           api_SetUpvalue,
    /*SetHook=*/              api_SetHook,
    /*GetHook=*/              api_GetHook,
    /*GetHookMask=*/          api_GetHookMask,
    /*GetHookCount=*/         api_GetHookCount,

    // auxiliary library (luaL_*)
    /*LOpenLib=*/             api_LOpenLib,
    /*LRegister=*/            api_LRegister,
    /*LGetMetafield=*/        api_LGetMetafield,
    /*LCallMeta=*/            api_LCallMeta,
    /*LTypeError=*/           api_LTypeError,
    /*LArgError=*/            api_LArgError,
    /*LCheckLString=*/        api_LCheckLString,
    /*LOptLString=*/          api_LOptLString,
    /*LCheckNumber=*/         api_LCheckNumber,
    /*LOptNumber=*/           api_LOptNumber,
    /*LCheckInteger=*/        api_LCheckInteger,
    /*LOptInteger=*/          api_LOptInteger,
    /*LCheckStack=*/          api_LCheckStack,
    /*LCheckType=*/           api_LCheckType,
    /*LCheckAny=*/            api_LCheckAny,
    /*LNewMetatable=*/        api_LNewMetatable,
    /*LCheckUdata=*/          api_LCheckUdata,
    /*LWhere=*/               api_LWhere,
    /*LError=*/               api_LError,
    /*LCheckOption=*/         api_LCheckOption,
    /*LRef=*/                 api_LRef,
    /*LUnref=*/               api_LUnref,
    /*LLoadFile=*/            api_LLoadFile,
    /*LLoadBuffer=*/          api_LLoadBuffer,
    /*LLoadString=*/          api_LLoadString,
    /*LNewState=*/            api_LNewState,
    /*LGSub=*/                api_LGSub,
    /*LFindTable=*/           api_LFindTable,
    /*LBuffInit=*/            api_LBuffInit,
    /*LPrepBuffer=*/          api_LPrepBuffer,
    /*LAddLString=*/          api_LAddLString,
    /*LAddString=*/           api_LAddString,
    /*LAddValue=*/            api_LAddValue,
    /*LPushResult=*/          api_LPushResult,
};

const kcdxScriptingInterface g_iface = {
    /*RegisterFunction=*/ Thunk_RegisterFunction,
    /*lua=*/              &g_lua_api,
};

}  // namespace

const kcdxScriptingInterface* GetInterface() {
    return &g_iface;
}

void ApplyPendingToTable(lua_State* L) {
    std::lock_guard<std::mutex> lock(g_lock);
    g_lua_state   = L;
    g_table_ready = true;

    if (g_pending.empty()) return;

    log::InfoF("scripting_interface: applying %zu queued registration(s)",
               g_pending.size());

    // Move pending into applied (stable pointers), then ApplyOne each.
    for (auto& reg : g_pending) {
        g_applied.push_back(std::move(reg));
        ApplyOne(L, &g_applied.back());
    }
    g_pending.clear();
}

}  // namespace kcdx::scripting_interface
