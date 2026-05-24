// kcdx.alias(short, target) — Lua-side declaration of a per-plugin local
// name handle.
//
// A "doing" call per .claude/rules/lua-api-surface.md: top-level, POSITIONAL
// (two obvious args), NOT a configuring {named table}. Per
// .claude/rules/naming-namespaces.md §Aliasing:
//
//   kcdx.alias("inv", "redmoon.open_inventory")
//
// declares a LOCAL handle scoped to the CALLING plugin: thereafter that plugin
// can write "inv" wherever a name is expected (a kcdx.hook{ target = } /
// kcdx.bytes{ target_symbol = } value) and the engine substitutes
// "redmoon.open_inventory" before resolving.
//
// DESIGN LOCKS:
//   * LOCAL handle, plugin-scoped. The alias resolves ONLY in the declaring
//     plugin's space (storage is keyed owningPlugin -> short -> fullname in
//     the address_library alias map). It CANNOT shadow an engine name or
//     another plugin's bare name — substitution only fires when the calling
//     plugin owns an alias by that exact short name, so it only ADDS a handle,
//     never displaces resolution (naming-namespaces.md §Aliasing). It cannot
//     collide with the reserved "kcdx." root (the short name's [a-z0-9_]
//     charset forbids a dot, and "kcdx" itself is just a 4-char handle that
//     resolves to its target — it does NOT touch the engine seed).
//   * LAUNCH-TIME ONLY. The alias is recorded once at the kcdx.alias call
//     (plugin-load time) and read only during the apply pass (name
//     resolution), exactly like author-targets — never on a hook-fire /
//     per-frame path, so it adds zero runtime overhead.
//   * OWNER IDENTITY via lua_registry::OwningPluginForCurrentCall (the same
//     mechanism kcdx.code / kcdx.command / publish / on / hook use). An
//     anonymous caller (console / pak Lua) resolves to "" — an alias has no
//     plugin to scope to, so address_library::RegisterAlias REJECTS it with a
//     teaching error.
//
// Returns `true` on success; on bad input returns (false, teaching error) —
// the standard kcdx-binder error idiom for a "doing" call (the C++ alias/
// symbol-namespace mirror is a later restructure phase; this is Lua-first with
// a tracked parity gap, not a broken C++ surface).

#include "lua_bind_alias.h"

#include <string>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include "address_library.h"  // kcdx::address_library::RegisterAlias
#include "log.h"
#include "lua_registry.h"     // kcdx::lua_registry::OwningPluginForCurrentCall

namespace kcdx::lua_bind_alias {

namespace {

// kcdx.alias(short, target)
//
//   short  (string, required) : the local handle to declare. A bare name
//                               ([a-z0-9_], 2-32) — validated by RegisterAlias.
//   target (string, required) : the full name it aliases (bare or
//                               "<plugin>.<name>"); non-empty.
int Lua_Alias(lua_State* L) {
    // --- arg 1: short handle (string) ---
    if (lua_type(L, 1) != LUA_TSTRING) {
        lua_pushboolean(L, 0);
        lua_pushstring(L,
            "kcdx.alias(short, target): `short` (the local handle, arg 1) must "
            "be a string. Call shape: kcdx.alias(\"inv\", "
            "\"redmoon.open_inventory\").");
        return 2;
    }
    std::string shortName = lua_tostring(L, 1);

    // --- arg 2: target (string) ---
    if (lua_type(L, 2) != LUA_TSTRING) {
        lua_pushboolean(L, 0);
        lua_pushstring(L,
            "kcdx.alias(short, target): `target` (the full name to alias, arg "
            "2) must be a string. Call shape: kcdx.alias(\"inv\", "
            "\"redmoon.open_inventory\").");
        return 2;
    }
    std::string target = lua_tostring(L, 2);

    // --- Resolve owner identity (same mechanism as kcdx.code / kcdx.command).
    std::string callSiteFile;
    int callSiteLine = 0;
    kcdx::lua_registry::OwningPlugin owner =
        kcdx::lua_registry::OwningPluginForCurrentCall(
            L, callSiteFile, callSiteLine);

    // --- Register (validates owner / short / target; an anonymous caller is
    // rejected — an alias must be scoped to a plugin). ---
    //
    // The binder now threads the real (author, plugin) pair from
    // OwningPluginForCurrentCall (step 4 of the 2-dot namespace refactor).
    // When the manifest's [plugin].author is still empty (the corpus state
    // before step 6) the alias is scoped under the legacy 1-dot key
    // (<plugin>) — exactly how the existing corpus already declares +
    // resolves aliases. The alias's `target` string can be 1-dot OR 3-dot;
    // substitution is re-resolved through the standard name pipeline,
    // which handles both shapes (address_library.cpp).
    std::string err;
    if (!kcdx::address_library::RegisterAlias(owner.author.c_str(),
                                              owner.plugin.c_str(),
                                              shortName.c_str(),
                                              target.c_str(), err)) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, err.c_str());
        return 2;
    }

    LOG_DEBUG("NAMESPACE", "[%s] alias '%s' -> '%s' (site=%s:%d)",
              owner.plugin.c_str(), shortName.c_str(), target.c_str(),
              callSiteFile.empty() ? "?" : callSiteFile.c_str(), callSiteLine);

    lua_pushboolean(L, 1);
    return 1;
}

}  // namespace

void bind(lua_State* L) {
    // kcdx.alias is a TOP-LEVEL "doing" verb (positional) per
    // lua-api-surface.md — the kcdx table is at the top of the stack; register
    // the function directly on it (like kcdx.on / kcdx.code).
    int kcdx_idx = lua_gettop(L);
    lua_pushcfunction(L, Lua_Alias);
    lua_setfield(L, kcdx_idx, "alias");
}

}  // namespace kcdx::lua_bind_alias
