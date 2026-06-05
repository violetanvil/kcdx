// lua_shim — kcdx routes every lua_*/luaL_* through WHGame.dll's ONE compiled
// Lua body (restructure Phase 11 / FIX A). This header declares the
// function-pointer table (LuaApi) the engine calls instead of the
// statically-linked vendor/lua. Phase-11 §6.1 + docs/outstanding-work/
// fix-a-drop-static-lua.md are the design authority.
//
// WHY a shim (not a direct static link): two independently-compiled Lua 5.1
// bodies on one lua_State each carry their own .rdata sentinels (dummynode,
// empty-array) and free each other's via a mismatched address compare → the
// dual-Lua heap-corruption hazard (lua-bridge.md). Routing every symbol
// through WHGame's single body kills the hazard by construction.
//
// SCOPE OF THIS STEP (P2 step 1): the 93 RESOLVED LUA_API/LUALIB_API symbols
// are forwarded — each populated by resolving its CANONICAL NAME through the
// Address Library (refdb::ResolveAddrByName), never a literal RVA (AP1). The
// ~24 inlined/stripped symbols are NOT wired here — they are the NEXT step
// (P2 step 2), which fills the kStubSeam-marked members with kcdx-side stubs.
// Resolve() leaves the seam members null and does NOT bail on them.

#pragma once

#include <cstdarg>
#include <cstddef>

// vendor/lua/*.h STAYS in the build for the struct/typedef definitions
// (lua_State, lua_Number, lua_Integer, lua_Alloc, lua_CFunction, lua_Reader,
// lua_Writer, lua_Hook, lua_Debug, luaL_Reg, luaL_Buffer). The design drops
// only vendor/lua/*.c (P5); the headers remain the canonical type source for
// these forwarder signatures + PROBE Q.
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

namespace kcdx::lua_shim {

// The function-pointer table over WHGame.dll's compiled Lua. Sized for ALL
// 117 LUA_API/LUALIB_API symbols so P2 step 2's stubs slot into the SAME
// struct (no struct shape change between steps).
//
// Each member's signature is the canonical lua.h/lauxlib.h/lualib.h
// prototype, exactly. A member is one of:
//   - RESOLVED (this step): populated by Resolve() from
//     refdb::ResolveAddrByName("<canonical name>"). Forwards directly into
//     WHGame's body.
//   - STUB SEAM (P2 step 2): left null here, marked `[stub seam P2 step 2]`.
//     The symbol is inlined-by-PGO or linker-stripped in WHGame.dll, so it has
//     no callable RVA; step 2 reimplements it kcdx-side using the resolved
//     primitives + the verified layout constants. Resolve() SKIPS these
//     without failing this step.
//
// INTERNAL-ONLY members (lua_newstate / lua_close / lua_setallocf /
// lua_atpanic) ARE populated (the engine needs them to build + tear down the
// VM), but the gating helper IsInternalOnly() marks them so the plugin-facing
// kcdxLuaApi surface never exposes them — exposing them would let a mod author
// destroy or replace the game's VM (fix-a-drop-static-lua.md §"What NOT to
// do"; lua-bridge.md). This step DECLARES the gating; the kcdxLuaApi rewire to
// route through this shim is P5 (scripting_interface.cpp is untouched here).
struct LuaApi {
    // --- state manipulation ---
    lua_State* (*lua_newstate)(lua_Alloc f, void* ud);           // INTERNAL-ONLY (id 114; engine VM build)
    void       (*lua_storedebuginfo)(lua_State* L, int e);       // [stub seam P2 step 2] inlined byte-op at g+0x22
    int        (*lua_isstoredebuginfo)(lua_State* L);            // [stub seam P2 step 2] inlined byte-op at g+0x22
    void       (*lua_close)(lua_State* L);                       // INTERNAL-ONLY (id 123; engine VM teardown)
    lua_State* (*lua_newthread)(lua_State* L);                   // [stub seam P2 step 2] inlined into luaB_cocreate
    lua_CFunction (*lua_atpanic)(lua_State* L, lua_CFunction panicf); // INTERNAL-ONLY (linker-stripped; step-2 stub returns nullptr)

    // --- basic stack manipulation ---
    int   (*lua_gettop)(lua_State* L);                           // [stub seam P2 step 2] (int)(L->top - L->base)
    void  (*lua_settop)(lua_State* L, int idx);                  // id 32
    void  (*lua_pushvalue)(lua_State* L, int idx);               // id 36
    void  (*lua_remove)(lua_State* L, int idx);                  // id 27
    void  (*lua_insert)(lua_State* L, int idx);                  // id 26
    void  (*lua_replace)(lua_State* L, int idx);                 // id 124 (GC-barrier: calls luaC_barrierf)
    int   (*lua_checkstack)(lua_State* L, int sz);               // id 46
    void  (*lua_xmove)(lua_State* from, lua_State* to, int n);   // id 82

    // --- access functions (stack -> C) ---
    int           (*lua_isnumber)(lua_State* L, int idx);        // id 45
    int           (*lua_isstring)(lua_State* L, int idx);        // id 74
    int           (*lua_iscfunction)(lua_State* L, int idx);     // id 73
    int           (*lua_isuserdata)(lua_State* L, int idx);      // [stub seam P2 step 2] inlined macro (lua_type==7)
    int           (*lua_type)(lua_State* L, int idx);            // id 28
    const char*   (*lua_typename)(lua_State* L, int tp);         // id 47
    int           (*lua_equal)(lua_State* L, int idx1, int idx2);     // [stub seam P2 step 2] varies (inlined/unreached)
    int           (*lua_rawequal)(lua_State* L, int idx1, int idx2);  // id 78
    int           (*lua_lessthan)(lua_State* L, int idx1, int idx2);  // id 75
    lua_Number    (*lua_tonumber)(lua_State* L, int idx);        // id 44
    lua_Integer   (*lua_tointeger)(lua_State* L, int idx);       // id 51
    int           (*lua_toboolean)(lua_State* L, int idx);       // id 49
    const char*   (*lua_tolstring)(lua_State* L, int idx, size_t* len); // id 39
    size_t        (*lua_objlen)(lua_State* L, int idx);          // id 48
    lua_CFunction (*lua_tocfunction)(lua_State* L, int idx);     // [stub seam P2 step 2] varies (inlined/unreached)
    void*         (*lua_touserdata)(lua_State* L, int idx);      // id 33
    lua_State*    (*lua_tothread)(lua_State* L, int idx);        // id 81
    const void*   (*lua_topointer)(lua_State* L, int idx);       // id 110

    // --- push functions (C -> stack) ---
    void        (*lua_pushnil)(lua_State* L);                            // [stub seam P2 step 2] direct TValue write + L->top++
    void        (*lua_pushnumber)(lua_State* L, lua_Number n);           // [stub seam P2 step 2] direct TValue write + L->top++
    void        (*lua_pushinteger)(lua_State* L, lua_Integer n);         // [stub seam P2 step 2] direct TValue write + L->top++
    void        (*lua_pushlstring)(lua_State* L, const char* s, size_t l); // id 30
    void        (*lua_pushstring)(lua_State* L, const char* s);          // id 34
    const char* (*lua_pushvfstring)(lua_State* L, const char* fmt, va_list argp); // [stub seam P2 step 2] -> luaO_pushvfstring (id 109) + manual luaC_step
    const char* (*lua_pushfstring)(lua_State* L, const char* fmt, ...);  // id 77 (variadic — direct forward)
    void        (*lua_pushcclosure)(lua_State* L, lua_CFunction fn, int n); // id 53
    void        (*lua_pushboolean)(lua_State* L, int b);                 // [stub seam P2 step 2] direct TValue write + L->top++
    void        (*lua_pushlightuserdata)(lua_State* L, void* p);         // [stub seam P2 step 2] direct TValue write + L->top++
    int         (*lua_pushthread)(lua_State* L);                         // [stub seam P2 step 2] inlined (GC-barrier: writes GC ptr → luaC_barrierf)

    // --- get functions (Lua -> stack) ---
    void  (*lua_gettable)(lua_State* L, int idx);                // id 40
    void  (*lua_getfield)(lua_State* L, int idx, const char* k); // id 55
    void  (*lua_rawget)(lua_State* L, int idx);                  // id 42
    void  (*lua_rawgeti)(lua_State* L, int idx, int n);          // id 29
    void  (*lua_createtable)(lua_State* L, int narr, int nrec);  // id 35
    void* (*lua_newuserdata)(lua_State* L, size_t sz);           // id 52
    int   (*lua_getmetatable)(lua_State* L, int objindex);       // id 31
    void  (*lua_getfenv)(lua_State* L, int idx);                 // id 96

    // --- set functions (stack -> Lua) ---
    void  (*lua_settable)(lua_State* L, int idx);                // id 111
    void  (*lua_setfield)(lua_State* L, int idx, const char* k); // id 56
    void  (*lua_rawset)(lua_State* L, int idx);                  // id 43
    void  (*lua_rawseti)(lua_State* L, int idx, int n);          // id 50
    int   (*lua_setmetatable)(lua_State* L, int objindex);       // id 37
    int   (*lua_setfenv)(lua_State* L, int idx);                 // id 79

    // --- load + call ---
    void (*lua_call)(lua_State* L, int nargs, int nresults);             // id 62
    int  (*lua_pcall)(lua_State* L, int nargs, int nresults, int errfunc); // id 1
    int  (*lua_cpcall)(lua_State* L, lua_CFunction func, void* ud);      // [stub seam P2 step 2] varies (inlined/unreached)
    int  (*lua_load)(lua_State* L, lua_Reader reader, void* dt, const char* chunkname); // id 66
    int  (*lua_dump)(lua_State* L, lua_Writer writer, void* data);       // id 95

    // --- coroutines ---
    int  (*lua_yield)(lua_State* L, int nresults);               // [stub seam P2 step 2] inlined; replicate self-contained body
    int  (*lua_resume)(lua_State* L, int narg);                  // id 94
    int  (*lua_status)(lua_State* L);                            // [stub seam P2 step 2] inlined byte at L+8

    // --- GC ---
    int (*lua_gc)(lua_State* L, int what, int data);             // id 57

    // --- misc ---
    int       (*lua_error)(lua_State* L);                        // id 93
    int       (*lua_next)(lua_State* L, int idx);                // id 38
    void      (*lua_concat)(lua_State* L, int n);                // id 64
    lua_Alloc (*lua_getallocf)(lua_State* L, void** ud);         // INTERNAL-ONLY (linker-stripped; step-2 stub returns nullptr)
    void      (*lua_setallocf)(lua_State* L, lua_Alloc f, void* ud); // INTERNAL-ONLY (linker-stripped; step-2 stub no-op)

    // --- debug API ---
    int         (*lua_getstack)(lua_State* L, int level, lua_Debug* ar); // id 59
    int         (*lua_getinfo)(lua_State* L, const char* what, lua_Debug* ar); // id 60
    const char* (*lua_getlocal)(lua_State* L, const lua_Debug* ar, int n);     // id 91
    const char* (*lua_setlocal)(lua_State* L, const lua_Debug* ar, int n);     // id 92
    const char* (*lua_getupvalue)(lua_State* L, int funcindex, int n);   // id 76
    const char* (*lua_setupvalue)(lua_State* L, int funcindex, int n);   // id 80
    int         (*lua_sethook)(lua_State* L, lua_Hook func, int mask, int count); // id 58
    lua_Hook    (*lua_gethook)(lua_State* L);                    // [stub seam P2 step 2] varies (inlined/unreached)
    int         (*lua_gethookmask)(lua_State* L);                // [stub seam P2 step 2] varies (inlined/unreached)
    int         (*lua_gethookcount)(lua_State* L);               // [stub seam P2 step 2] varies (inlined/unreached)

    // --- standard-library openers (LUALIB_API) ---
    int  (*luaopen_base)(lua_State* L);                          // id 100
    int  (*luaopen_table)(lua_State* L);                         // id 98
    int  (*luaopen_io)(lua_State* L);                            // id 104 (WHGame stubs io to a ret-0 thunk)
    int  (*luaopen_os)(lua_State* L);                            // id 103
    int  (*luaopen_string)(lua_State* L);                        // id 101
    int  (*luaopen_math)(lua_State* L);                          // id 97
    int  (*luaopen_debug)(lua_State* L);                         // id 99
    int  (*luaopen_package)(lua_State* L);                       // id 102
    void (*luaL_openlibs)(lua_State* L);                         // id 115

    // --- auxiliary library (luaL_*) ---
    void        (*luaI_openlib)(lua_State* L, const char* libname, const luaL_Reg* l, int nup); // [stub seam P2 step 2] fully inlined into luaopen_X
    void        (*luaL_register)(lua_State* L, const char* libname, const luaL_Reg* l);         // [stub seam P2 step 2] reimplement luaI_openlib body
    int         (*luaL_getmetafield)(lua_State* L, int obj, const char* e); // id 63
    int         (*luaL_callmeta)(lua_State* L, int obj, const char* e);     // [stub seam P2 step 2] inlined (open-coded by CryEngine)
    int         (*luaL_typerror)(lua_State* L, int narg, const char* tname); // id 89
    int         (*luaL_argerror)(lua_State* L, int numarg, const char* extramsg); // id 84
    const char* (*luaL_checklstring)(lua_State* L, int numArg, size_t* l);  // id 67
    const char* (*luaL_optlstring)(lua_State* L, int numArg, const char* def, size_t* l); // id 87
    lua_Number  (*luaL_checknumber)(lua_State* L, int numArg);   // id 61
    lua_Number  (*luaL_optnumber)(lua_State* L, int nArg, lua_Number def); // [stub seam P2 step 2] inlined luaL_opt macro
    lua_Integer (*luaL_checkinteger)(lua_State* L, int numArg);  // id 69
    lua_Integer (*luaL_optinteger)(lua_State* L, int nArg, lua_Integer def); // id 68
    void        (*luaL_checkstack)(lua_State* L, int sz, const char* msg);   // id 70
    void        (*luaL_checktype)(lua_State* L, int narg, int t);           // id 25
    void        (*luaL_checkany)(lua_State* L, int narg);                   // id 71
    int         (*luaL_newmetatable)(lua_State* L, const char* tname);      // [stub seam P2 step 2] inlined into luaopen_package
    void*       (*luaL_checkudata)(lua_State* L, int ud, const char* tname); // id 118
    void        (*luaL_where)(lua_State* L, int lvl);            // id 90
    int         (*luaL_error)(lua_State* L, const char* fmt, ...); // id 86 (variadic — direct forward)
    int         (*luaL_checkoption)(lua_State* L, int narg, const char* def, const char* const lst[]); // id 85
    int         (*luaL_ref)(lua_State* L, int t);                // id 125 (rare; one instance found)
    void        (*luaL_unref)(lua_State* L, int t, int ref);     // [stub seam P2 step 2] open-coded by CryEngine
    int         (*luaL_loadfile)(lua_State* L, const char* filename); // id 3
    int         (*luaL_loadbuffer)(lua_State* L, const char* buff, size_t sz, const char* name); // [stub seam P2 step 2] not seeded (no callable RVA harvested)
    int         (*luaL_loadstring)(lua_State* L, const char* s); // [stub seam P2 step 2] not seeded (no callable RVA harvested)
    lua_State*  (*luaL_newstate)(void);                          // [stub seam P2 step 2] PGO-fused with lua_newstate (id 114) — internal-only
    const char* (*luaL_gsub)(lua_State* L, const char* s, const char* p, const char* r); // [stub seam P2 step 2] not seeded (no callable RVA harvested)
    const char* (*luaL_findtable)(lua_State* L, int idx, const char* fname, int szhint); // id 54
    void        (*luaL_buffinit)(lua_State* L, luaL_Buffer* B);  // [stub seam P2 step 2] inlined
    char*       (*luaL_prepbuffer)(luaL_Buffer* B);              // id 88
    void        (*luaL_addlstring)(luaL_Buffer* B, const char* s, size_t l); // id 72
    void        (*luaL_addstring)(luaL_Buffer* B, const char* s);            // id 108
    void        (*luaL_addvalue)(luaL_Buffer* B);                            // id 83
    void        (*luaL_pushresult)(luaL_Buffer* B);                          // id 65
};

// The populated table. Members are null until Resolve() runs; the engine never
// touches the VM before Resolve() returns true.
const LuaApi& g_api();

// Populate g_api from the Address Library. For each RESOLVED symbol, set the
// member from refdb::ResolveAddrByName("<canonical name>"). BAILS LOUD: a
// required resolved symbol that misses (returns 0) → log a structured error
// naming the symbol + return false. The kStubSeam members (inlined/stripped,
// no callable RVA) are SKIPPED this step (left null) and never cause a bail —
// P2 step 2 fills them.
//
// Call once, after WHGame.dll is mapped + the Address Library is up
// (refdb::Open has run), before any Lua VM touch. Idempotent (re-running
// re-resolves). Returns true iff every required symbol resolved.
bool Resolve();

// True iff `member_offset` names an internal-only function the plugin-facing
// kcdxLuaApi must NOT expose (lua_newstate / lua_close / lua_setallocf /
// lua_atpanic). Used by the P5 kcdxLuaApi rewire; declared here so the gating
// lives with the shim it gates. The list is the harvest doc's "Don't expose"
// set (fix-a-drop-static-lua.md §"What NOT to do").
bool IsInternalOnly(const char* canonical_name);

}  // namespace kcdx::lua_shim
