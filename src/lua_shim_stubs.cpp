// lua_shim_stubs — kcdx-side reimplementations of the LUA_API/LUALIB_API symbols
// that WHGame.dll inlined-by-PGO or linker-stripped, so they have NO callable
// RVA to forward to (lua_shim.cpp's FORWARD layer). Each stub reimplements the
// function body per the verified per-function strategy from the RE analysis of
// WHGame's stripped/inlined functions, using the verified layout constants +
// the already-resolved primitives. Resolve() (lua_shim.cpp) wires each stub's
// address into the matching g_api member; this TU defines the bodies.
//
// WHY a separate TU from lua_shim.cpp: that file is the FORWARD layer (resolve a
// canonical name → a WHGame RVA); this is the STUB layer (reimplement a body).
// One concern per file (no-monolith). Same unit; lua_shim.cpp's Resolve() takes
// the addresses below.
//
// LAYOUT FACTS — the stubs read live VM struct fields by their vendor/lua struct
// accessors (L->top, L->base, G(L), L->status, …). WHGame's compiled Lua 5.1 is
// byte-identical to vendor/lua's struct layout for these fields — verified
// against the binary (l_G @ L+0x20, storedebug @ g+0x22, mainthread @ g+0xB0,
// sizeof LG 0x268, TValue 0x10, LUA_NUMBER=float, LUA_INTEGER=ptrdiff_t — the
// canonical Lua 5.1 layout these headers define). The mainthread self-pointer
// invariant ([L->l_G]+0xB0 == L) is validated at Resolve() so a future
// game-update struct shift fails LOUD, not silent. These are struct OFFSETS
// (version-stable layout facts), NOT per-version-volatile addresses — fine as
// the headers' accessors; SOURCE: the Lua 5.1 layout verified against the
// binary. No literal RVA appears here: the one ADDRESS a stub needs
// (luaC_barrierf, luaO_pushvfstring, luaG_runerror, luaC_step) is resolved BY
// NAME through refdb.
//
// NO new kcdx-side static-const Lua sentinel (which would re-create the dual-Lua
// hazard): every stub operates on the LIVE VM's own structures (L->top, the live
// state). None introduces a static const Node/TValue.

#include "lua_shim.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
// Internal struct layouts (lua_State, global_State, TValue, Node, Table) the
// stubs read. vendor/lua/*.h STAYS in the build for exactly these defs (the
// design drops only vendor/lua/*.c). lstate.h pulls lobject.h (TValue, the
// set*value macros) + the G(L) accessor.
#include "lstate.h"
#include "lobject.h"
}

#include <cstdarg>

#include "log.h"
#include "refdb.h"

namespace kcdx::lua_shim {

namespace {

constexpr const char* kCategory = "LUA_SHIM";

// === resolved-by-name internal primitives a few stubs need ==================
// These are INTERNAL helpers (NOT LUA_API), seeded in the Address Library by
// canonical name. They are NOT g_api members (g_api carries only the 117 public
// symbols). A stub that needs one resolves it BY NAME (never a literal RVA)
// into a typed pointer cached here, populated by BindStubPrimitives() which
// Resolve() calls before wiring the stub members. A required miss is a hard
// fail surfaced by Resolve() (the stub would otherwise dereference null).

using luaC_barrierf_fn = void (*)(lua_State* L, void* o, void* v);
using luaC_step_fn     = void (*)(lua_State* L);
using luaO_pushvfstring_fn =
    const char* (*)(lua_State* L, const char* fmt, va_list argp);
using luaG_runerror_fn = void (*)(lua_State* L, const char* fmt, ...);

luaC_barrierf_fn     g_luaC_barrierf = nullptr;   // id 127 — GC write barrier slow path
luaC_step_fn         g_luaC_step = nullptr;       // id  42 — one GC step (luaC_checkGC body)
luaO_pushvfstring_fn g_luaO_pushvfstring = nullptr; // id 109 — lobject.c vfstring builder
luaG_runerror_fn     g_luaG_runerror = nullptr;   // id 112 — ldebug.c runtime-error raiser

// === the stubs ==============================================================
// Each body is the verified vendor/lua source body, with the no-op
// build-config macros (lua_lock / lua_unlock / api_check / lua_assert — all
// ((void)0) in this build, confirmed in llimits.h, no LUA_USER_H override)
// elided. The inlined WHGame copies ran with those same no-op macros, so the
// stub is byte-equivalent behavior.

// --- state manipulation -----------------------------------------------------

// lua_storedebuginfo — inlined single byte-op at g+0x22 (verified against the
// binary). The vendor source sets the storedebug flag; reimplement as the byte
// write. SOURCE: RE analysis, stripped/inlined — `((char*)G(L))[0x22] = e`.
void Stub_lua_storedebuginfo(lua_State* L, int e) {
    G(L)->storedebug = static_cast<lu_byte>(e);
}

// lua_isstoredebuginfo — read the same byte. SOURCE: RE analysis, g+0x22 read.
int Stub_lua_isstoredebuginfo(lua_State* L) {
    return G(L)->storedebug;
}

// lua_newthread — NOT reconstructable from the current evidence. It is inlined
// into luaB_cocreate and ALLOCATES a new thread via luaE_newthread, which is NOT
// seeded in the Address Library (no callable RVA, no by-name resolve) and not
// otherwise characterized. Reconstructing the body would mean guessing the
// thread-alloc internals — a guessed body corrupts the VM, so it is not done.
// Like the other unreconstructable functions (lua_atpanic / lua_getallocf /
// lua_setallocf: not usable), this stub fails LOUD and returns NULL — it never
// silently fabricates a thread. CScriptSystem never calls it (it is inlined at
// its one use site, luaB_cocreate, which has its own WHGame copy), so
// engine-internal use does not need it; a plugin that needs C-API coroutine
// creation hits a loud failure, not a silent broken thread.
lua_State* Stub_lua_newthread(lua_State* L) {
    LOG_ERROR_KV(kCategory, "stub_not_usable",
        ::kcdx::log::KV("symbol", "lua_newthread"),
        ::kcdx::log::KV("reason",
            "not reconstructable: lua_newthread allocates via luaE_newthread, "
            "which is not seeded and not otherwise characterized. "
            "Reconstructing it would be a guessed body. Returns NULL."));
    (void)L;
    return nullptr;
}

// lua_atpanic — linker-stripped (CryEngine uses CryFatalError, never registers a
// panic handler). The real body swaps G(L)->panic; doing so on the live engine
// VM would hijack the engine's error handling. Internal-only + not usable: leave
// the panic handler untouched and return null. SOURCE: RE analysis, "Don't
// expose" + confirmed-stripped.
lua_CFunction Stub_lua_atpanic(lua_State* L, lua_CFunction panicf) {
    (void)L;
    (void)panicf;
    return nullptr;  // never installed; CryEngine owns the panic path
}

// --- basic stack manipulation ----------------------------------------------

// lua_gettop — inlined (1 instruction). SOURCE: RE analysis `(int)(L->top -
// L->base)` + vendor lapi.c::lua_gettop (`cast_int(L->top - L->base)`).
int Stub_lua_gettop(lua_State* L) {
    return cast_int(L->top - L->base);
}

// --- access (stack -> C) ----------------------------------------------------

// lua_isuserdata — inlined macro (lua_type==7 OR lightuserdata==2). SOURCE:
// vendor lapi.c::lua_isuserdata — index2adr then ttisuserdata||ttislightuserdata.
// index2adr is static (inlined everywhere); reimplement its public-index slice
// via the resolved lua_type member (g_api), which gives the same answer for the
// userdata kinds (LUA_TUSERDATA=7, LUA_TLIGHTUSERDATA=2) without needing the
// internal pseudo-index machinery.
int Stub_lua_isuserdata(lua_State* L, int idx) {
    const int t = g_api().lua_type(L, idx);
    return (t == LUA_TUSERDATA || t == LUA_TLIGHTUSERDATA);
}

// lua_equal — RE analysis: varies (inlined/unreached); the vendor body needs the
// internal equalobj (may call a tag method) — NOT a resolved primitive. The
// raw-equal path IS available (lua_rawequal, resolved id 78). Reimplement the
// PRIMITIVE-EQUALITY slice via lua_rawequal: for the no-metamethod case (the
// only case kcdx's surfaces hit), raw equality and full equality coincide. A
// metamethod-bearing comparison is NOT reachable through the stub — surfaced as
// a known narrowing (kcdx surfaces do not invoke __eq through the shim).
// SOURCE: vendor lapi.c::lua_equal (the o1/o2 nilobject guard + equality);
// narrowed to rawequal because equalobj is uncatalogued.
int Stub_lua_equal(lua_State* L, int idx1, int idx2) {
    return g_api().lua_rawequal(L, idx1, idx2);
}

// lua_tocfunction — RE analysis: varies (inlined/unreached). Vendor body:
// index2adr → iscfunction ? clvalue(o)->c.f : NULL. index2adr is static; for a
// VALID stack index the live value is at L->base + (idx-1) (positive) or
// L->top + idx (negative pseudo-index range). Reuse the resolved primitives:
// lua_type tells us it is a function, and a C function is read from the closure.
// We cannot reach the closure pointer without index2adr, so this is reimplemented
// directly against the live stack slot using the SAME index arithmetic vendor
// index2adr uses for the two ordinary ranges (positive + negative), guarding the
// pseudo-index range out (kcdx never reads a C function through a pseudo-index).
lua_CFunction Stub_lua_tocfunction(lua_State* L, int idx) {
    StkId o;
    if (idx > 0) {
        o = L->base + (idx - 1);
        if (o >= L->top) return nullptr;  // out of range → nilobject in vendor
    } else if (idx > LUA_REGISTRYINDEX) {
        o = L->top + idx;
    } else {
        // pseudo-index (registry/globals/environ/upvalue) — kcdx never reads a
        // C function through one. Return NULL rather than reach into the
        // uncatalogued curr_func/upvalue machinery.
        return nullptr;
    }
    // iscfunction(o): a function whose closure is a C closure.
    if (ttype(o) != LUA_TFUNCTION) return nullptr;
    Closure* cl = clvalue(o);
    return cl->c.isC ? cl->c.f : nullptr;
}

// --- push (C -> stack) ------------------------------------------------------
// Every push writes a TValue into the stack slot at L->top and increments
// L->top. The destination is a STACK slot, never a black GC object, so NO
// luaC_barrier is needed — confirmed against the verified vendor bodies
// (lapi.c::lua_pushnil/number/integer/boolean/lightuserdata/thread each call
// api_incr_top with no barrier). The set*value macros are the vendor TValue
// writes. SOURCE: RE analysis "direct TValue write + L->top++" + vendor lapi.c.

void Stub_lua_pushnil(lua_State* L) {
    setnilvalue(L->top);
    L->top++;
}

void Stub_lua_pushnumber(lua_State* L, lua_Number n) {
    setnvalue(L->top, n);
    L->top++;
}

// lua_pushinteger — vendor body is setnvalue(L->top, cast_num(n)) (the integer
// is stored as the lua_Number, LUA_NUMBER=float here). SOURCE: vendor
// lapi.c::lua_pushinteger.
void Stub_lua_pushinteger(lua_State* L, lua_Integer n) {
    setnvalue(L->top, cast_num(n));
    L->top++;
}

void Stub_lua_pushboolean(lua_State* L, int b) {
    setbvalue(L->top, (b != 0));  // ensure true is exactly 1
    L->top++;
}

void Stub_lua_pushlightuserdata(lua_State* L, void* p) {
    setpvalue(L->top, p);
    L->top++;
}

// lua_pushthread — pushes the running thread (L itself) as a thread value at
// L->top, returns whether it is the main thread. The destination is the STACK
// slot — setthvalue writes a GCObject pointer into a stack TValue, which is NOT
// a black GC object, so the VERIFIED vendor body (lapi.c:521-527) calls NO
// barrier. SURFACED: the RE analysis "Don't expose" notes list lua_pushthread
// among the GC-pointer writers to barrier, but the verified source body does
// not barrier a stack write (the barrier is for writing a GCObject INTO an
// already-black collectable — which lua_pushthread does not do). This stub
// follows the VERIFIED body (no barrier on the stack write); luaC_barrierf is
// still resolved for the GC-barrier invariant and is available to any future
// stub that writes into a black object — none of the 31 seam stub members do
// (lua_replace and the setupvalue/setmetatable barrier-callers are RESOLVED,
// not seam). SOURCE: vendor lapi.c::lua_pushthread.
int Stub_lua_pushthread(lua_State* L) {
    setthvalue(L, L->top, L);
    L->top++;
    return (G(L)->mainthread == L);
}

// lua_pushvfstring — inlined into lua_pushfstring. Vendor body: luaC_checkGC(L)
// then luaO_pushvfstring(L, fmt, argp). luaO_pushvfstring is resolved by name
// (id 109); the luaC_checkGC is the GCthreshold compare + luaC_step — done
// manually via the resolved luaC_step (id 42). SOURCE: RE analysis "forward to
// luaO_pushvfstring + manual luaC_step" + vendor lapi.c::lua_pushvfstring.
const char* Stub_lua_pushvfstring(lua_State* L, const char* fmt, va_list argp) {
    // luaC_checkGC(L): step the collector when the threshold is hit. The vendor
    // macro compares G(L)->totalbytes >= G(L)->GCthreshold then luaC_step(L);
    // both fields are live struct reads.
    if (G(L)->totalbytes >= G(L)->GCthreshold) {
        g_luaC_step(L);
    }
    return g_luaO_pushvfstring(L, fmt, argp);
}

// --- load + call ------------------------------------------------------------

// lua_cpcall — RE analysis: varies (inlined/unreached). Vendor body builds a CCallS
// (func + ud) and calls luaD_pcall(L, f_Ccall, &c, savestack(L,L->top), 0).
// f_Ccall and savestack/restorestack are static/internal — NOT resolvable. The
// body cannot be reimplemented from the resolved primitives without guessing the
// protected-call frame internals. NOT usable: fail loud + return a runtime error
// status. lua_cpcall is not reconstructable from the current evidence; CryEngine
// does not call it (it open-codes pcall at its sites), so engine use does not
// need it; a plugin needing it hits a loud failure, not a silent wrong call.
// SOURCE: vendor lapi.c::lua_cpcall (the internals it needs are unresolved).
int Stub_lua_cpcall(lua_State* L, lua_CFunction func, void* ud) {
    (void)func;
    (void)ud;
    LOG_ERROR_KV(kCategory, "stub_not_usable",
        ::kcdx::log::KV("symbol", "lua_cpcall"),
        ::kcdx::log::KV("reason",
            "not reconstructable: lua_cpcall needs the static f_Ccall + "
            "savestack frame internals, neither seeded nor characterized. "
            "Returns LUA_ERRRUN."));
    (void)L;
    return LUA_ERRRUN;  // non-zero (error) — never falsely reports success
}

// --- coroutines -------------------------------------------------------------

// lua_yield — RE analysis: source body is self-contained — replicate it. Vendor
// body (ldo.c:441-450): error if nested in a C call, else set L->base, set
// L->status = LUA_YIELD, return -1. luaG_runerror is resolved by name (id 112);
// luai_userstateyield is a no-op macro (luaconf.h). SOURCE: vendor ldo.c::lua_yield.
int Stub_lua_yield(lua_State* L, int nresults) {
    if (L->nCcalls > 0) {
        g_luaG_runerror(
            L, "attempt to yield across metamethod/C-call boundary");
        // luaG_runerror longjmps; control does not return. The return below is
        // unreachable but keeps the signature total.
    }
    L->base = L->top - nresults;  // protect stack slots below
    L->status = LUA_YIELD;
    return -1;
}

// lua_status — inlined single byte at L+8 (= L->status). SOURCE: RE analysis
// `((unsigned char*)L)[8]` + vendor lapi.c::lua_status (`return L->status`).
int Stub_lua_status(lua_State* L) {
    return L->status;
}

// --- misc -------------------------------------------------------------------

// lua_getallocf — linker-stripped (CryEngine never queries the allocator). The
// real body reads G(L)->frealloc / G(L)->ud — those ARE live struct fields, so
// this CAN be reimplemented faithfully (it is read-only, no VM mutation).
// SOURCE: vendor lapi.c::lua_getallocf.
lua_Alloc Stub_lua_getallocf(lua_State* L, void** ud) {
    if (ud) *ud = G(L)->ud;
    return G(L)->frealloc;
}

// lua_setallocf — INTERNAL-ONLY + stripped: replacing the engine VM's allocator
// would corrupt the live state. Vendor body writes G(L)->ud / G(L)->frealloc;
// kcdx must NEVER do that to the engine's VM (RE analysis "Don't expose" — keep
// internal, do not expose; a no-op is safer than honoring a foreign allocator
// swap on the one shared state). No-op. SOURCE: RE analysis "Don't expose".
void Stub_lua_setallocf(lua_State* L, lua_Alloc f, void* ud) {
    (void)L;
    (void)f;
    (void)ud;
    // intentional no-op: the engine VM's allocator is never swapped by kcdx.
}

// --- debug API --------------------------------------------------------------
// The gethook trio reads live lua_State fields. SOURCE: vendor ldebug.c.

lua_Hook Stub_lua_gethook(lua_State* L) {
    return L->hook;
}

int Stub_lua_gethookmask(lua_State* L) {
    return L->hookmask;
}

int Stub_lua_gethookcount(lua_State* L) {
    return L->basehookcount;
}

// --- auxiliary library (luaL_*) reimplemented from PUBLIC primitives ---------
// These were inlined/open-coded by CryEngine. Each vendor body is built ENTIRELY
// from other public lua_*/luaL_* API calls — so each stub calls through g_api
// (the resolved + stubbed members), reproducing the exact vendor body. No
// internal helper is needed. SOURCE: vendor lauxlib.c bodies.

// libsize — static helper inside luaI_openlib; counts the {name,func} entries.
int StubLib_libsize(const luaL_Reg* l) {
    int size = 0;
    for (; l->name; l++) size++;
    return size;
}

// luaI_openlib — fully inlined into the luaopen_X bodies. Reimplement its vendor
// body verbatim via g_api primitives. SOURCE: vendor lauxlib.c::luaI_openlib.
void Stub_luaI_openlib(lua_State* L, const char* libname, const luaL_Reg* l,
                       int nup) {
    const LuaApi& api = g_api();
    if (libname) {
        const int size = StubLib_libsize(l);
        api.luaL_findtable(L, LUA_REGISTRYINDEX, "_LOADED", size);
        api.lua_getfield(L, -1, libname);           // _LOADED[libname]
        if (api.lua_type(L, -1) != LUA_TTABLE) {     // not a table (incl. nil)?
            api.lua_settop(L, -2);                   // lua_pop(L,1)
            if (api.luaL_findtable(L, LUA_GLOBALSINDEX, libname, size) !=
                nullptr) {
                api.luaL_error(L, "name conflict for module " LUA_QL("%s"),
                               libname);
            }
            api.lua_pushvalue(L, -1);
            api.lua_setfield(L, -3, libname);        // _LOADED[libname] = new tbl
        }
        api.lua_remove(L, -2);                       // remove _LOADED
        api.lua_insert(L, -(nup + 1));               // move lib below upvalues
    }
    for (; l->name; l++) {
        for (int i = 0; i < nup; i++) {
            api.lua_pushvalue(L, -nup);              // copy upvalues to top
        }
        api.lua_pushcclosure(L, l->func, nup);
        api.lua_setfield(L, -(nup + 2), l->name);
    }
    api.lua_settop(L, -(nup + 1));                   // lua_pop(L, nup)
}

// luaL_register — vendor body is luaI_openlib(L, libname, l, 0). SOURCE: vendor
// lauxlib.c::luaL_register.
void Stub_luaL_register(lua_State* L, const char* libname, const luaL_Reg* l) {
    Stub_luaI_openlib(L, libname, l, 0);
}

// abs_index — lauxlib.c macro: convert a stack index to positive. Reimplemented
// as a stub-local helper (uses the stubbed lua_gettop). SOURCE: lauxlib.c.
int StubLib_abs_index(lua_State* L, int i) {
    return (i > 0 || i <= LUA_REGISTRYINDEX) ? i : (Stub_lua_gettop(L) + i + 1);
}

// luaL_callmeta — inlined/open-coded by CryEngine. SOURCE: vendor
// lauxlib.c::luaL_callmeta. luaL_getmetafield is resolved (id 63).
int Stub_luaL_callmeta(lua_State* L, int obj, const char* event) {
    const LuaApi& api = g_api();
    obj = StubLib_abs_index(L, obj);
    if (!api.luaL_getmetafield(L, obj, event)) return 0;  // no metafield
    api.lua_pushvalue(L, obj);
    api.lua_call(L, 1, 1);
    return 1;
}

// luaL_optnumber — lauxlib luaL_opt macro: lua_isnoneornil ? def :
// luaL_checknumber. lua_isnoneornil(L,n) == (lua_type(L,n) <= 0). SOURCE: vendor
// lauxlib.c::luaL_optnumber + lua.h::lua_isnoneornil.
lua_Number Stub_luaL_optnumber(lua_State* L, int narg, lua_Number def) {
    const LuaApi& api = g_api();
    return (api.lua_type(L, narg) <= 0) ? def : api.luaL_checknumber(L, narg);
}

// luaL_newmetatable — open-coded into luaopen_package. SOURCE: vendor
// lauxlib.c::luaL_newmetatable (built from getfield/isnil/pop/newtable/
// pushvalue/setfield public primitives).
int Stub_luaL_newmetatable(lua_State* L, const char* tname) {
    const LuaApi& api = g_api();
    api.lua_getfield(L, LUA_REGISTRYINDEX, tname);   // registry[tname]
    if (api.lua_type(L, -1) != LUA_TNIL) return 0;   // name already in use
    api.lua_settop(L, -2);                           // lua_pop(L,1)
    api.lua_createtable(L, 0, 0);                    // lua_newtable
    api.lua_pushvalue(L, -1);
    api.lua_setfield(L, LUA_REGISTRYINDEX, tname);   // registry[tname] = mt
    return 1;
}

// luaL_unref — open-coded freelist by CryEngine. FREELIST_REF == 0, LUA_REFNIL/
// LUA_NOREF are negative (skipped by the ref>=0 guard). SOURCE: vendor
// lauxlib.c::luaL_unref. lua_pushinteger is itself a stub (above) — wired into
// g_api by Resolve(), so the call through g_api reaches this TU's stub.
void Stub_luaL_unref(lua_State* L, int t, int ref) {
    if (ref >= 0) {
        const LuaApi& api = g_api();
        t = StubLib_abs_index(L, t);
        api.lua_rawgeti(L, t, 0);          // FREELIST_REF == 0
        api.lua_rawseti(L, t, ref);        // t[ref] = t[FREELIST_REF]
        api.lua_pushinteger(L, ref);
        api.lua_rawseti(L, t, 0);          // t[FREELIST_REF] = ref
    }
}

// luaL_newstate — PGO-fused with lua_newstate (RE analysis); INTERNAL-ONLY. The
// engine builds the VM through lua_newstate; luaL_newstate is the lua_open()
// alias. Forward to the resolved lua_newstate member with the default allocator
// args the fused form uses (no allocator — CryEngine's l_alloc is wired in the
// fused body). Internal-only: never exposed via kcdxLuaApi (IsInternalOnly gates
// it when the kcdxLuaApi rewire lands). SOURCE: RE analysis "PGO-fused with
// lua_newstate (id 114)".
lua_State* Stub_luaL_newstate(void) {
    // The fused lua_newstate ignores its allocator args (the RE analysis:
    // callers pass xor ecx/edx/r8d) and wires CryEngine's l_alloc directly.
    // Pass nulls.
    return g_api().lua_newstate(nullptr, nullptr);
}

// luaL_buffinit — inlined. Vendor body: B->L = L; B->p = B->buffer; B->lvl = 0.
// SOURCE: vendor lauxlib.c::luaL_buffinit.
void Stub_luaL_buffinit(lua_State* L, luaL_Buffer* B) {
    B->L = L;
    B->p = B->buffer;
    B->lvl = 0;
}

}  // namespace

// === public to lua_shim.cpp =================================================
// Resolve() (lua_shim.cpp) calls BindStubPrimitives() to resolve the by-name
// internal helpers a few stubs need, then WireStubs() to point the seam g_api
// members at the stub bodies above. Both return a structured failure on a
// required-primitive miss so Resolve() can bail loud — kcdx never touches the VM
// with an incomplete stub layer.

// Resolve the internal primitives by name (never a literal RVA). Returns
// false + logs the first missing one (Resolve() then bails). g_luaC_barrierf is
// resolved here too (the GC-barrier invariant: it must be backed even though no
// seam stub writes into a black object — a future barrier-needing stub
// resolves through this same path; the cap-79 GC-barrier row asserts it is
// non-null).
bool BindStubPrimitives() {
    g_luaC_barrierf = reinterpret_cast<luaC_barrierf_fn>(
        ::kcdx::refdb::ResolveAddrByName("luaC_barrierf"));
    g_luaC_step = reinterpret_cast<luaC_step_fn>(
        ::kcdx::refdb::ResolveAddrByName("luaC_step"));
    g_luaO_pushvfstring = reinterpret_cast<luaO_pushvfstring_fn>(
        ::kcdx::refdb::ResolveAddrByName("luaO_pushvfstring"));
    g_luaG_runerror = reinterpret_cast<luaG_runerror_fn>(
        ::kcdx::refdb::ResolveAddrByName("luaG_runerror"));

    struct Prim { const char* name; const void* p; };
    const Prim prims[] = {
        {"luaC_barrierf", reinterpret_cast<const void*>(g_luaC_barrierf)},
        {"luaC_step", reinterpret_cast<const void*>(g_luaC_step)},
        {"luaO_pushvfstring",
         reinterpret_cast<const void*>(g_luaO_pushvfstring)},
        {"luaG_runerror", reinterpret_cast<const void*>(g_luaG_runerror)},
    };
    for (const Prim& pr : prims) {
        if (pr.p == nullptr) {
            LOG_ERROR_KV(kCategory, "stub_primitive_unresolved",
                ::kcdx::log::KV("symbol", pr.name),
                ::kcdx::log::KV("hint",
                    "an internal Lua helper a seam stub needs did not resolve "
                    "by name — WHGame.dll not mapped, refdb not open, or the "
                    "seed lacks this name on this build. kcdx will NOT touch the "
                    "Lua VM with an incomplete stub layer."));
            return false;
        }
    }
    return true;
}

// True iff luaC_barrierf resolved (the GC-barrier invariant the cap-79 row
// asserts structurally — a GC-pointer-writing stub has its barrier available).
bool GcBarrierBacked() {
    return g_luaC_barrierf != nullptr;
}

// Validate the mainthread self-pointer invariant against the live state:
// G(L)->mainthread == L (the verified g+0xB0 offset). Every stub reads live
// struct fields by these offsets (L->top, L->base, G(L)->storedebug, L->status,
// …); a future game-update struct shift would make those reads silently wrong.
// This is the one cheap invariant that ties the WHOLE layout to ground truth —
// if l_G (L+0x20) and mainthread (g+0xB0) are where the RE analysis verified,
// the state is the layout the stubs assume. Fails LOUD (logs + returns false) so
// the cap-79 row goes red on a shift, never a silent wrong read.
bool ValidateLayout(lua_State* L) {
    if (L == nullptr) {
        LOG_ERROR_KV(kCategory, "layout_validate_null_state",
            ::kcdx::log::KV("reason",
                "ValidateLayout called with a null L — the live VM is not "
                "captured yet; the caller must pass a live state."));
        return false;
    }
    global_State* g = G(L);  // L->l_G (L+0x20)
    if (g == nullptr) {
        LOG_ERROR_KV(kCategory, "layout_validate_null_global",
            ::kcdx::log::KV("reason",
                "L->l_G (L+0x20) is null — the lua_State layout does not match "
                "the verified offsets; a stub reading G(L) would corrupt the "
                "VM. kcdx must NOT proceed on a mismatched layout."));
        return false;
    }
    if (g->mainthread != L) {  // g+0xB0 == L (the self-pointer invariant)
        LOG_ERROR_KV(kCategory, "layout_validate_mainthread_mismatch",
            ::kcdx::log::KV("L", reinterpret_cast<const void*>(L)),
            ::kcdx::log::KV("mainthread",
                reinterpret_cast<const void*>(g->mainthread)),
            ::kcdx::log::KV("reason",
                "G(L)->mainthread (g+0xB0) != L — the mainthread self-pointer "
                "invariant is broken. WHGame's lua_State/global_State layout no "
                "longer matches the verified offsets (a game update shifted a "
                "struct field). Every shim stub reading these offsets is now "
                "wrong; kcdx must NOT touch the VM."));
        return false;
    }
    return true;
}

// Point each catalogued seam g_api member at its stub body. The 3 unclassified
// members (luaL_loadbuffer / luaL_loadstring / luaL_gsub) are NOT wired here —
// they stay null (lua_shim.cpp keeps their [stub seam] marker).
void WireStubs(LuaApi& api) {
    // state manipulation
    api.lua_storedebuginfo   = &Stub_lua_storedebuginfo;
    api.lua_isstoredebuginfo = &Stub_lua_isstoredebuginfo;
    api.lua_newthread        = &Stub_lua_newthread;       // not usable (fail-loud)
    api.lua_atpanic          = &Stub_lua_atpanic;         // internal/stripped

    // basic stack
    api.lua_gettop = &Stub_lua_gettop;

    // access (stack -> C)
    api.lua_isuserdata  = &Stub_lua_isuserdata;
    api.lua_equal       = &Stub_lua_equal;
    api.lua_tocfunction = &Stub_lua_tocfunction;

    // push (C -> stack)
    api.lua_pushnil           = &Stub_lua_pushnil;
    api.lua_pushnumber        = &Stub_lua_pushnumber;
    api.lua_pushinteger       = &Stub_lua_pushinteger;
    api.lua_pushvfstring      = &Stub_lua_pushvfstring;
    api.lua_pushboolean       = &Stub_lua_pushboolean;
    api.lua_pushlightuserdata = &Stub_lua_pushlightuserdata;
    api.lua_pushthread        = &Stub_lua_pushthread;

    // load + call
    api.lua_cpcall = &Stub_lua_cpcall;                    // not-usable (surfaced)

    // coroutines
    api.lua_yield  = &Stub_lua_yield;
    api.lua_status = &Stub_lua_status;

    // misc
    api.lua_getallocf = &Stub_lua_getallocf;
    api.lua_setallocf = &Stub_lua_setallocf;              // internal/no-op

    // debug API
    api.lua_gethook      = &Stub_lua_gethook;
    api.lua_gethookmask  = &Stub_lua_gethookmask;
    api.lua_gethookcount = &Stub_lua_gethookcount;

    // auxiliary library
    api.luaI_openlib     = &Stub_luaI_openlib;
    api.luaL_register    = &Stub_luaL_register;
    api.luaL_callmeta    = &Stub_luaL_callmeta;
    api.luaL_optnumber   = &Stub_luaL_optnumber;
    api.luaL_newmetatable = &Stub_luaL_newmetatable;
    api.luaL_unref       = &Stub_luaL_unref;
    api.luaL_newstate    = &Stub_luaL_newstate;           // internal-only
    api.luaL_buffinit    = &Stub_luaL_buffinit;

    // NOT wired (unclassified — stay null, pending an RE classification pass
    // before the static Lua is dropped):
    //   api.luaL_loadbuffer / api.luaL_loadstring / api.luaL_gsub
}

}  // namespace kcdx::lua_shim
