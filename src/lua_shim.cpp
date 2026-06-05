// See lua_shim.h. Forward the RESOLVED LUA_API/LUALIB_API symbols through
// WHGame.dll's compiled Lua, resolving each by CANONICAL NAME via the Address
// Library — never a literal RVA. The inlined/stripped seam members are filled
// by the stub layer (lua_shim_stubs.cpp); a still-unclassified symbol is left
// null and skipped by Resolve() without failing.

#include "lua_shim.h"

#include <cstring>

#include "log.h"
#include "refdb.h"

namespace kcdx::lua_shim {

namespace {

constexpr const char* kCategory = "LUA_SHIM";

// The single table the engine calls through. Zero-initialized: every member
// starts null, so a seam member never forwarded in the forward layer is a clean
// nullptr (the stub layer fills them; a call through a still-null seam member
// before then is a programming error, not a resolve miss).
LuaApi g_table{};

// Resolve one canonical Lua symbol by NAME and store it into `g_table.<member>`
// with the member's exact function-pointer type. Bails loud on a REQUIRED
// miss: a seeded symbol that does not resolve (0) means WHGame.dll is not
// mapped, the Address Library is not up, or the seed is wrong for this build —
// kcdx must not touch the VM with an incomplete shim (fail loud on a miss,
// never swallow it and return true). `name` is the canonical lua.h spelling and
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

// The internal-only set (the "Don't expose" set from the RE analysis of
// WHGame's Lua): resolvable into g_api for the engine's own VM build/teardown,
// but NEVER exposed through the plugin-facing kcdxLuaApi (the rewire reads
// IsInternalOnly to gate them) — exposing them lets a mod author destroy or
// replace the game's Lua VM.
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

    // === SEAM — STUB LAYER ===
    // 34 LUA_API/LUALIB_API symbols are inlined-by-PGO, linker-stripped, or
    // otherwise not present in WHGame.dll as a callable RVA — so they are NOT in
    // the Address Library seed and CANNOT be resolved by name. They split TWO
    // ways:
    //   - 31 are CATALOGUED stubs — inlined/stripped with a verified per-function
    //     kcdx-side strategy (from the RE analysis of WHGame's stripped/inlined
    //     functions). Reimplemented in lua_shim_stubs.cpp from the verified
    //     layout constants + the resolved primitives; wired below.
    //   - 3 are UNCLASSIFIED — luaL_loadbuffer / luaL_loadstring / luaL_gsub,
    //     neither seeded NOR reconstructable from the catalogue. An RE
    //     classification pass settles each (resolved in the binary → seed it →
    //     forward; inlined → add to the stub catalogue) BEFORE the static Lua is
    //     dropped. They stay NULL here (skipped by WireStubs, kept at their
    //     [stub seam] marker).
    //
    // NOTE: the actual split, built against the seed as it exists today (the
    // authority for what resolves): 90 forwarded + 31 stubbed + 3 unclassified.
    // The stubs rely on the live VM's struct layout matching the offsets verified
    // against the binary — ValidateLayout() (the mainthread self-pointer
    // invariant) is the falsifiable guard the self-test runs against the live
    // state so a future game-update struct shift fails LOUD.

    // Resolve the by-name internal helpers the stubs call (luaC_barrierf,
    // luaC_step, luaO_pushvfstring, luaG_runerror). A required miss is a hard
    // bail — kcdx must not wire a stub that would dereference a null primitive
    // (fail loud, never proceed on a missing primitive). lua_shim_stubs.cpp
    // logs which one missed.
    if (!BindStubPrimitives()) {
        return false;
    }

    // Point the 31 catalogued seam members at their kcdx-side stub bodies. The 3
    // unclassified members stay null.
    WireStubs(g_table);

    LOG_INFO_KV(kCategory, "resolve_ok",
        ::kcdx::log::KV("resolved", "90"),
        ::kcdx::log::KV("stubbed", "31"),
        ::kcdx::log::KV("unclassified_pending", "3"),
        ::kcdx::log::KV("note",
            "forward + stub layers up; 3 unclassified symbols stay null "
            "(pending an RE classification pass). Layout validated against the "
            "live VM at first self-test (mainthread self-pointer invariant)."));
    return true;
}

#undef FORWARD

}  // namespace kcdx::lua_shim
