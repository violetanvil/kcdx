// See lua_shim.h. P2 step 1: forward the RESOLVED LUA_API/LUALIB_API symbols
// through WHGame.dll's compiled Lua, resolving each by CANONICAL NAME via the
// Address Library (AP1 — never a literal RVA). The inlined/stripped seam
// members are left null this step (P2 step 2 stubs them); Resolve() skips them
// without failing.

#include "lua_shim.h"

#include <cstring>

#include "log.h"
#include "refdb.h"

namespace kcdx::lua_shim {

namespace {

constexpr const char* kCategory = "LUA_SHIM";

// The single table the engine calls through. Zero-initialized: every member
// starts null, so a seam member never forwarded this step is a clean nullptr
// (P2 step 2 fills them; a call through a still-null seam member before then
// is a programming error, not a resolve miss).
LuaApi g_table{};

// Resolve one canonical Lua symbol by NAME and store it into `g_table.<member>`
// with the member's exact function-pointer type. Bails loud on a REQUIRED
// miss: a seeded symbol that does not resolve (0) means WHGame.dll is not
// mapped, the Address Library is not up, or the seed is wrong for this build —
// kcdx must not touch the VM with an incomplete shim (AP14: fail loud, never
// swallow a miss and return true). `name` is the canonical lua.h spelling and
// is the member name, so the grep target + the resolved name are the same
// token.
#define FORWARD(member)                                                        \
    do {                                                                       \
        const uintptr_t va = ::kcdx::refdb::ResolveAddrByName(#member);        \
        if (va == 0) {                                                         \
            LOG_ERROR_KV(kCategory, "required_symbol_unresolved",              \
                ::kcdx::log::KV("symbol", #member),                            \
                ::kcdx::log::KV("hint",                                        \
                    "Address Library miss — WHGame.dll not mapped, refdb not " \
                    "open, or the seed lacks this name on this game build. "   \
                    "kcdx will NOT touch the Lua VM with an incomplete shim.")); \
            return false;                                                      \
        }                                                                      \
        g_table.member =                                                       \
            reinterpret_cast<decltype(g_table.member)>(va);                    \
    } while (0)

// The internal-only set (harvest doc §"What NOT to do"): resolvable into
// g_api for the engine's own VM build/teardown, but NEVER exposed through the
// plugin-facing kcdxLuaApi (the P5 rewire reads IsInternalOnly to gate them) —
// exposing them lets a mod author destroy or replace the game's Lua VM.
constexpr const char* kInternalOnly[] = {
    "lua_newstate",    // build the VM
    "lua_close",       // tear down the VM
    "lua_setallocf",   // replace the allocator
    "lua_atpanic",     // hijack the panic handler
};

}  // namespace

const LuaApi& g_api() { return g_table; }

bool IsInternalOnly(const char* canonical_name) {
    if (!canonical_name) return false;
    for (const char* n : kInternalOnly) {
        if (std::strcmp(n, canonical_name) == 0) return true;
    }
    return false;
}

bool Resolve() {
    // === RESOLVED (90 seeded LUA_API/LUALIB_API symbols) ===
    // Each forwards into WHGame's body by canonical name. FORWARD bails loud on
    // any required miss. Grouped to mirror the lua.h section order + the LuaApi
    // struct layout.

    // state manipulation (lua_newstate is INTERNAL-ONLY but still resolved — the
    // engine builds the VM through it; IsInternalOnly gates the plugin surface)
    FORWARD(lua_newstate);
    FORWARD(lua_close);            // INTERNAL-ONLY

    // basic stack manipulation
    FORWARD(lua_settop);
    FORWARD(lua_pushvalue);
    FORWARD(lua_remove);
    FORWARD(lua_insert);
    FORWARD(lua_replace);
    FORWARD(lua_checkstack);
    FORWARD(lua_xmove);

    // access (stack -> C)
    FORWARD(lua_isnumber);
    FORWARD(lua_isstring);
    FORWARD(lua_iscfunction);
    FORWARD(lua_type);
    FORWARD(lua_typename);
    FORWARD(lua_rawequal);
    FORWARD(lua_lessthan);
    FORWARD(lua_tonumber);
    FORWARD(lua_tointeger);
    FORWARD(lua_toboolean);
    FORWARD(lua_tolstring);
    FORWARD(lua_objlen);
    FORWARD(lua_touserdata);
    FORWARD(lua_tothread);
    FORWARD(lua_topointer);

    // push (C -> stack)
    FORWARD(lua_pushlstring);
    FORWARD(lua_pushstring);
    FORWARD(lua_pushfstring);      // variadic — direct forward
    FORWARD(lua_pushcclosure);

    // get (Lua -> stack)
    FORWARD(lua_gettable);
    FORWARD(lua_getfield);
    FORWARD(lua_rawget);
    FORWARD(lua_rawgeti);
    FORWARD(lua_createtable);
    FORWARD(lua_newuserdata);
    FORWARD(lua_getmetatable);
    FORWARD(lua_getfenv);

    // set (stack -> Lua)
    FORWARD(lua_settable);
    FORWARD(lua_setfield);
    FORWARD(lua_rawset);
    FORWARD(lua_rawseti);
    FORWARD(lua_setmetatable);
    FORWARD(lua_setfenv);

    // load + call
    FORWARD(lua_call);
    FORWARD(lua_pcall);
    FORWARD(lua_load);
    FORWARD(lua_dump);

    // coroutines
    FORWARD(lua_resume);

    // GC
    FORWARD(lua_gc);

    // misc
    FORWARD(lua_error);
    FORWARD(lua_next);
    FORWARD(lua_concat);

    // debug API
    FORWARD(lua_getstack);
    FORWARD(lua_getinfo);
    FORWARD(lua_getlocal);
    FORWARD(lua_setlocal);
    FORWARD(lua_getupvalue);
    FORWARD(lua_setupvalue);
    FORWARD(lua_sethook);

    // standard-library openers
    FORWARD(luaopen_base);
    FORWARD(luaopen_table);
    FORWARD(luaopen_io);
    FORWARD(luaopen_os);
    FORWARD(luaopen_string);
    FORWARD(luaopen_math);
    FORWARD(luaopen_debug);
    FORWARD(luaopen_package);
    FORWARD(luaL_openlibs);

    // auxiliary library (luaL_*)
    FORWARD(luaL_getmetafield);
    FORWARD(luaL_typerror);
    FORWARD(luaL_argerror);
    FORWARD(luaL_checklstring);
    FORWARD(luaL_optlstring);
    FORWARD(luaL_checknumber);
    FORWARD(luaL_checkinteger);
    FORWARD(luaL_optinteger);
    FORWARD(luaL_checkstack);
    FORWARD(luaL_checktype);
    FORWARD(luaL_checkany);
    FORWARD(luaL_checkudata);
    FORWARD(luaL_where);
    FORWARD(luaL_error);           // variadic — direct forward
    FORWARD(luaL_checkoption);
    FORWARD(luaL_ref);
    FORWARD(luaL_loadfile);
    FORWARD(luaL_findtable);
    FORWARD(luaL_prepbuffer);
    FORWARD(luaL_addlstring);
    FORWARD(luaL_addstring);
    FORWARD(luaL_addvalue);
    FORWARD(luaL_pushresult);

    // === SEAM — NOT WIRED THIS STEP (P2 step 2 fills these) ===
    // 34 LUA_API/LUALIB_API symbols are inlined-by-PGO, linker-stripped, or
    // otherwise not present in WHGame.dll as a callable RVA — so they are NOT
    // in the Address Library seed and CANNOT be resolved by name. Resolve()
    // leaves their g_table members null and does NOT bail on them. P2 step 2
    // reimplements each kcdx-side using the verified layout constants
    // (fix-a-drop-static-lua.md §"Stripped or inlined functions") — several
    // GC-pointer writers (lua_pushthread, lua_replace-kin) MUST call
    // luaC_barrierf (harvest §"What NOT to do"). The seam set:
    //
    //   lua_storedebuginfo, lua_isstoredebuginfo, lua_newthread, lua_atpanic,
    //   lua_gettop, lua_isuserdata, lua_equal, lua_tocfunction, lua_pushnil,
    //   lua_pushnumber, lua_pushinteger, lua_pushvfstring, lua_pushboolean,
    //   lua_pushlightuserdata, lua_pushthread, lua_cpcall, lua_yield,
    //   lua_status, lua_getallocf, lua_setallocf, lua_gethook, lua_gethookmask,
    //   lua_gethookcount, luaI_openlib, luaL_register, luaL_callmeta,
    //   luaL_optnumber, luaL_newmetatable, luaL_unref, luaL_loadbuffer,
    //   luaL_loadstring, luaL_newstate, luaL_gsub, luaL_buffinit
    //
    // NOTE for the maintainer: the design's headline "93 resolved / ~24 stubs"
    // does NOT match the current three-file seed (90 LUA_API/LUALIB_API rows
    // resolve by name; 34 are seam). The 90/34 split here is built against the
    // seed as it actually exists today (the authority for what resolves), not
    // the headline count. The 34 seam members are TWO kinds:
    //   - most are P2 step-2 STUBS — inlined/stripped with a catalogued kcdx-side
    //     stub strategy (harvest doc "Stripped or inlined functions").
    //   - luaL_loadbuffer / luaL_loadstring / luaL_gsub are UNCLASSIFIED — neither
    //     seeded NOR catalogued. They are TD-0007: a /research-disassembly pass
    //     classifies each (resolved -> seed it AP18-gated -> forward; inlined ->
    //     add to the step-2 stub catalogue) BEFORE Phase 11 P5 drops static Lua.
    //     See docs/tech-debt/TD-0007-unclassified-lua-loader-symbols.md.

    LOG_INFO_KV(kCategory, "resolve_ok",
        ::kcdx::log::KV("resolved", "90"),
        ::kcdx::log::KV("seam_pending_step2", "34"),
        ::kcdx::log::KV("note", "forward layer up; stubs are P2 step 2"));
    return true;
}

#undef FORWARD

}  // namespace kcdx::lua_shim
