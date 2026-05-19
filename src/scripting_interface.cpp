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
// Most members are direct passthroughs to lua_* with a (potential)
// signature/typedef reshape. A few (PushCFunction, ErrorF) need glue.

int         api_GetTop        (lua_State* L)                                   { return lua_gettop(L); }
void        api_SetTop        (lua_State* L, int idx)                          { lua_settop(L, idx); }
void        api_PushValue     (lua_State* L, int idx)                          { lua_pushvalue(L, idx); }
void        api_Remove        (lua_State* L, int idx)                          { lua_remove(L, idx); }
void        api_Insert        (lua_State* L, int idx)                          { lua_insert(L, idx); }
void        api_Replace       (lua_State* L, int idx)                          { lua_replace(L, idx); }
int         api_CheckStack    (lua_State* L, int n)                            { return lua_checkstack(L, n); }

int         api_Type          (lua_State* L, int idx)                          { return lua_type(L, idx); }
int         api_IsNumber      (lua_State* L, int idx)                          { return lua_isnumber(L, idx); }
int         api_IsString      (lua_State* L, int idx)                          { return lua_isstring(L, idx); }
int         api_IsBoolean     (lua_State* L, int idx)                          { return lua_isboolean(L, idx); }
int         api_IsNil         (lua_State* L, int idx)                          { return lua_isnil(L, idx); }
int         api_IsCFunction   (lua_State* L, int idx)                          { return lua_iscfunction(L, idx); }
int         api_IsTable       (lua_State* L, int idx)                          { return lua_istable(L, idx); }
int         api_IsFunction    (lua_State* L, int idx)                          { return lua_isfunction(L, idx); }
int         api_IsUserdata    (lua_State* L, int idx)                          { return lua_isuserdata(L, idx); }

const char* api_ToString      (lua_State* L, int idx)                          { return lua_tostring(L, idx); }
const char* api_ToLString     (lua_State* L, int idx, size_t* len)             { return lua_tolstring(L, idx, len); }
double      api_ToNumber      (lua_State* L, int idx)                          { return (double)lua_tonumber(L, idx); }
long long   api_ToInteger     (lua_State* L, int idx)                          { return (long long)lua_tointeger(L, idx); }
int         api_ToBoolean     (lua_State* L, int idx)                          { return lua_toboolean(L, idx); }
const void* api_ToPointer     (lua_State* L, int idx)                          { return lua_topointer(L, idx); }
void*       api_ToUserdata    (lua_State* L, int idx)                          { return lua_touserdata(L, idx); }

void        api_PushString    (lua_State* L, const char* s)                    { lua_pushstring(L, s); }
void        api_PushLString   (lua_State* L, const char* s, size_t len)        { lua_pushlstring(L, s, len); }
void        api_PushNumber    (lua_State* L, double n)                         { lua_pushnumber(L, (lua_Number)n); }
void        api_PushInteger   (lua_State* L, long long n)                      { lua_pushinteger(L, (lua_Integer)n); }
void        api_PushBoolean   (lua_State* L, int b)                            { lua_pushboolean(L, b); }
void        api_PushNil       (lua_State* L)                                   { lua_pushnil(L); }
void        api_PushLightUserdata(lua_State* L, void* p)                       { lua_pushlightuserdata(L, p); }

// PushCFunction: install a shim that calls back through the
// kcdxLuaCFunction signature. Same upvalue trick as the registry
// shim, scoped to this single push.
void        api_PushCFunction (lua_State* L, kcdxLuaCFunction fn, void* ud) {
    // Pack (fn, ud) into a 2-element lightuserdata block in g_pushcclosures.
    // For simplicity we allocate a stable Registration-shaped struct via
    // the same g_applied storage. This means each PushCFunction call
    // adds one entry to g_applied; over a long session that's bounded
    // by the cap (1024).
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_applied.size() + g_pending.size() >= 1024) {
        // Push a Lua error if we're out of slots; can't return failure
        // any other way from this API.
        luaL_error(L, "kcdx scripting: registration cap reached, "
                      "cannot bind anonymous PushCFunction closure");
        return;
    }
    g_applied.push_back({0, "<inline>", "<closure>", fn, ud});
    lua_pushlightuserdata(L, &g_applied.back());
    lua_pushcclosure(L, LuaDispatchShim, 1);
}

void        api_NewTable      (lua_State* L)                                   { lua_newtable(L); }
void        api_GetField      (lua_State* L, int idx, const char* k)           { lua_getfield(L, idx, k); }
void        api_SetField      (lua_State* L, int idx, const char* k)           { lua_setfield(L, idx, k); }
void        api_RawGetI       (lua_State* L, int idx, int n)                   { lua_rawgeti(L, idx, n); }
void        api_RawSetI       (lua_State* L, int idx, int n)                   { lua_rawseti(L, idx, n); }
void        api_GetGlobal     (lua_State* L, const char* name)                 { lua_getglobal(L, name); }
void        api_SetGlobal     (lua_State* L, const char* name)                 { lua_setglobal(L, name); }

int         api_Error         (lua_State* L)                                   { return lua_error(L); }
int         api_ErrorF        (lua_State* L, const char* fmt, ...) {
    // luaL_error in Lua 5.1 doesn't take varargs directly; it uses
    // lua_pushvfstring internally. Replicate that pattern:
    va_list ap;
    va_start(ap, fmt);
    lua_pushvfstring(L, fmt, ap);
    va_end(ap);
    return lua_error(L);
}

const kcdxLuaApi g_lua_api = {
    /*GetTop=*/             api_GetTop,
    /*SetTop=*/             api_SetTop,
    /*PushValue=*/          api_PushValue,
    /*Remove=*/             api_Remove,
    /*Insert=*/             api_Insert,
    /*Replace=*/            api_Replace,
    /*CheckStack=*/         api_CheckStack,

    /*Type=*/               api_Type,
    /*IsNumber=*/           api_IsNumber,
    /*IsString=*/           api_IsString,
    /*IsBoolean=*/          api_IsBoolean,
    /*IsNil=*/              api_IsNil,
    /*IsCFunction=*/        api_IsCFunction,
    /*IsTable=*/            api_IsTable,
    /*IsFunction=*/         api_IsFunction,
    /*IsUserdata=*/         api_IsUserdata,

    /*ToString=*/           api_ToString,
    /*ToLString=*/          api_ToLString,
    /*ToNumber=*/           api_ToNumber,
    /*ToInteger=*/          api_ToInteger,
    /*ToBoolean=*/          api_ToBoolean,
    /*ToPointer=*/          api_ToPointer,
    /*ToUserdata=*/         api_ToUserdata,

    /*PushString=*/         api_PushString,
    /*PushLString=*/        api_PushLString,
    /*PushNumber=*/         api_PushNumber,
    /*PushInteger=*/        api_PushInteger,
    /*PushBoolean=*/        api_PushBoolean,
    /*PushNil=*/            api_PushNil,
    /*PushCFunction=*/      api_PushCFunction,
    /*PushLightUserdata=*/  api_PushLightUserdata,

    /*NewTable=*/           api_NewTable,
    /*GetField=*/           api_GetField,
    /*SetField=*/           api_SetField,
    /*RawGetI=*/            api_RawGetI,
    /*RawSetI=*/            api_RawSetI,
    /*GetGlobal=*/          api_GetGlobal,
    /*SetGlobal=*/          api_SetGlobal,

    /*Error=*/              api_Error,
    /*ErrorF=*/             api_ErrorF,
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
