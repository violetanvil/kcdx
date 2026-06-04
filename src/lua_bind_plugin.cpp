// kcdx.plugin.* — Lua-side plugin introspection AND cross-plugin
// navigation.
//
// A GROUPED capability DOMAIN (kcdx.plugin.*, like kcdx.cosave.* /
// kcdx.dev.*, NOT a top-level verb — plugin introspection is a query
// domain, not one of the closed-set core registration verbs).
//
// TWO surfaces on the one `kcdx.plugin` table, coexisting:
//
//   (1) The QUERY accessor (function member, unchanged):
//
//   kcdx.plugin.is_rejected(name) -> (bool, reason_or_nil)
//       Was the named plugin rejected by zone_gate this session?
//       `name` is the full prefixed plugin name "<author>.<plugin>"
//       (every plugin's identity is the two-component pair). Returns:
//         (true,  reason_string) -- rejected; reason teaches why
//         (false, nil)           -- not rejected (loaded normally,
//                                   or user-disabled, or unknown)
//         (nil,   teaching_err)  -- bad input (the kcdx-binder error
//                                   idiom; arg missing / wrong type /
//                                   empty)
//
//   (2) The NAVIGABLE cross-plugin NAMESPACE (the __index resolver
//       chain — the general primitive, not asset-only):
//
//   kcdx.plugin.<author>.<plugin>.<surface>...
//       Each dot is a resolution hop against engine-side namespace data
//       (g_plugins). The SAME chained-__index mechanism kcdx.hook.<name>
//       uses (src/lua_bind_hook.cpp), NOT a new architecture:
//         kcdx.plugin.<author>  -> __index on the kcdx.plugin table:
//             resolves <author> to an AUTHOR resolver (a userdata) iff
//             at least one loaded plugin declares that [plugin].author.
//             Miss -> nil (so the next .<plugin> raises Lua's stock
//             "attempt to index a nil value (field '<author>')",
//             naming the typoed segment — the kcdx.hook navigation-miss
//             idiom, NOT the is_rejected (nil,err) bad-ARG idiom).
//         <author resolver>.<plugin>  -> __index on that userdata:
//             resolves <plugin> to a PLUGIN HANDLE (a userdata carrying
//             the resolved author+plugin+engine handle) iff (author,
//             plugin) names a loaded plugin. Miss -> nil (same stock
//             index-error idiom names the segment).
//         <plugin handle>.assets / .<future surface>  -> __index on the
//             handle userdata: the step-6 deliverable is the handle that
//             resolves the author+plugin segments; what `.assets`
//             RETURNS is step 8's surface. Until step 8 wires it, the
//             handle's __index returns nil for .assets (the same
//             navigation-miss idiom) so the chain composes cleanly when
//             step 8 attaches the asset operations.
//
//   Resolution honors self > engine > other (naming-namespaces.md): a
//   plugin's own (author, plugin) wins when present; there is no engine
//   "kcdx" pseudo-plugin in g_plugins, so the engine tier is a no-op for
//   navigation today; "other" is any other loaded plugin. The bare
//   navigable form is for referencing ANOTHER plugin, so it reads
//   g_plugins directly by (author, plugin) — own-plugin access needs no
//   namespace at all (kcdx.assets.* against the caller, step 8).
//
// Useful for a plugin to degrade gracefully when a dependency it
// expected was rejected at load time, OR to reach another mod's
// published surface by a native dotted path.
//
// Threading: pure read of zone_gate's / plugin-registry's in-process
// maps; no callback is fired. Called from plugin.lua / require() in the
// main thread.
//
// Lua bridge: raw Lua C API, no static-const sentinel (AP5). The
// navigable resolvers use lua_newuserdata + an envtable for the
// resolved identity (the kcdx.hook pattern) — no static Node/TValue
// singletons. Returns are strings / handles, NOT pointers, so the
// lua_Number=float precision caveat does not apply.

#include "lua_bind_plugin.h"

#include <string>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include "plugin_loader.h"
#include "zone_gate.h"

namespace kcdx::lua_bind_plugin {

namespace {

// kcdx.plugin.is_rejected(name) -> (bool, reason_or_nil)
//
// On a bad call returns (nil, teaching_error) — the kcdx-binder error
// idiom. The error names the field and the call shape so the author
// can fix it without consulting docs.
int Lua_IsRejected(lua_State* L) {
    // Arg 1 must be a string. luaL_checkstring would raise — we want
    // the soft (nil, err) return instead so authors can pcall this in
    // a guard without an unwind.
    if (lua_type(L, 1) != LUA_TSTRING) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.plugin.is_rejected(name): `name` (string) is required "
            "— the full prefixed plugin name '<author>.<plugin>' "
            "(e.g. \"redmoon.outfit\"). Call shape: "
            "kcdx.plugin.is_rejected(\"author.plugin\")");
        return 2;
    }
    size_t len = 0;
    const char* s = lua_tolstring(L, 1, &len);
    if (!s || len == 0) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.plugin.is_rejected(name): `name` must be non-empty — "
            "the full prefixed plugin name '<author>.<plugin>'. Call "
            "shape: kcdx.plugin.is_rejected(\"author.plugin\")");
        return 2;
    }

    const std::string name(s, len);
    const bool rejected = kcdx::zone_gate::IsRejected(name);
    lua_pushboolean(L, rejected ? 1 : 0);
    if (rejected) {
        // RejectReason returns a stable std::string (the gate's owned
        // reason text, recorded at evaluation time). Push it by
        // .c_str() — Lua copies the bytes into its own string.
        const std::string& reason = kcdx::zone_gate::RejectReason(name);
        lua_pushlstring(L, reason.data(), reason.size());
    } else {
        // Not rejected — second return is nil. Covers both "plugin
        // loaded normally" and "plugin is unknown / user-disabled":
        // either way zone_gate has no rejection on file. The predicate
        // is the source of truth; nil reason just means "no reason".
        lua_pushnil(L);
    }
    return 2;
}

// =============================================================================
// Navigable cross-plugin namespace — kcdx.plugin.<author>.<plugin>.*
// =============================================================================
//
// Mirrors the kcdx.hook.<name>.<mode> smart-resolver mechanism
// (src/lua_bind_hook.cpp): a table/userdata with an __index metamethod
// resolves one dotted SEGMENT per hop against engine-side data, returning
// a userdata that carries the resolved identity for the next hop, and
// returning nil on a miss so the next access raises Lua's stock "attempt
// to index a nil value (field '<segment>')" — the navigation-miss idiom
// that names the typoed segment (AP14: a miss fails loud, never a silent
// nil that surfaces confusingly downstream; the author sees WHICH segment
// did not resolve). is_rejected's (nil, teaching_err) is the bad-ARG
// idiom — a different situation; this matches the navigation-miss idiom
// kcdx.hook established, NOT a third idiom.

// Registry keys for the per-hop metatables (installed once, idempotent).
constexpr const char* kAuthorResolverMt = "kcdx.plugin.author";
constexpr const char* kPluginHandleMt   = "kcdx.plugin.handle";

// Does any LOADED plugin declare this [plugin].author? Engine-side
// namespace data is g_plugins (the plugin registry). A read-only scan;
// no callback, main thread.
bool AuthorExists(const std::string& author) {
    if (author.empty()) return false;
    for (const auto& p : kcdx::plugins::g_plugins) {
        if (p.manifest.author == author) return true;
    }
    return false;
}

// Resolve (author, plugin) to the loaded plugin's engine handle.
// Returns kcdxInvalidPluginHandle when no loaded plugin matches the
// pair. The pair is the two-component identity (naming-namespaces.md);
// HandleOf() keys on [plugin].name ALONE, so it cannot disambiguate two
// authors that reuse a plugin name — we match on BOTH components here.
kcdxPluginHandle HandleForAuthorPlugin(const std::string& author,
                                       const std::string& plugin) {
    if (author.empty() || plugin.empty()) return kcdxInvalidPluginHandle;
    for (const auto& p : kcdx::plugins::g_plugins) {
        if (p.manifest.author == author && p.manifest.name == plugin) {
            return p.handle;
        }
    }
    return kcdxInvalidPluginHandle;
}

// __index on a PLUGIN-HANDLE userdata. The step-6 deliverable is the
// handle that resolves the author+plugin segments (reached here means
// both resolved). What `.assets` (and any future cross-plugin surface)
// RETURNS is step 8's deliverable: until step 8 attaches the asset
// operations, every access on the handle is a navigation miss -> nil,
// so kcdx.plugin.<a>.<p>.assets raises the stock index error at the
// .assets slot (the chain composes cleanly when step 8 wires the leaf).
int Lua_PluginHandleIndex(lua_State* L) {
    // arg 1 = the plugin-handle userdata; arg 2 = the accessed key
    // (.assets, or a future surface). Step 8 attaches the .assets leaf
    // here, reading (author, plugin, handle) off the userdata's envtable
    // (lua_getfenv(L, 1)). Step 6 ships the resolved handle; the leaf is
    // not yet wired, so a miss is correct (nil -> stock index error).
    lua_pushnil(L);
    return 1;
}

// __index on an AUTHOR-RESOLVER userdata. arg 1 = the author userdata;
// arg 2 = the <plugin> segment. Recovers the resolved <author> from the
// userdata's envtable, resolves (author, <plugin>) to a loaded plugin's
// handle, and returns a PLUGIN-HANDLE userdata (or nil on a miss so the
// next .<surface> raises the stock index error naming the bad <plugin>
// segment).
int Lua_PluginAuthorIndex(lua_State* L) {
    if (lua_type(L, 2) != LUA_TSTRING) { lua_pushnil(L); return 1; }
    const char* pluginCStr = lua_tostring(L, 2);
    std::string plugin = pluginCStr ? pluginCStr : "";
    if (plugin.empty()) { lua_pushnil(L); return 1; }

    // Recover <author> from the author userdata's envtable.
    lua_getfenv(L, 1);
    lua_getfield(L, -1, "author");
    std::string author = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
    lua_pop(L, 2);  // field + envtable

    const kcdxPluginHandle handle = HandleForAuthorPlugin(author, plugin);
    if (handle == kcdxInvalidPluginHandle) {
        // No loaded plugin named (author, plugin). Miss -> nil; the next
        // .<surface> access raises "attempt to index a nil value
        // (field '<plugin>')", naming the segment that did not resolve.
        lua_pushnil(L);
        return 1;
    }

    // Hit: return a plugin-handle userdata carrying (author, plugin,
    // handle) on its envtable. The one-byte payload is the kcdx.hook
    // ResolvedHookUd pattern — the metatable identity + the envtable do
    // the work; no std::string members on the C struct (no __gc dance).
    lua_newuserdata(L, 1);
    luaL_getmetatable(L, kPluginHandleMt);
    lua_setmetatable(L, -2);
    lua_newtable(L);
    lua_pushstring(L, author.c_str()); lua_setfield(L, -2, "author");
    lua_pushstring(L, plugin.c_str()); lua_setfield(L, -2, "plugin");
    lua_pushinteger(L, static_cast<lua_Integer>(handle));
    lua_setfield(L, -2, "handle");
    lua_setfenv(L, -2);
    return 1;
}

// __index on the kcdx.plugin TABLE. arg 1 = the kcdx.plugin table; arg 2
// = the <author> segment. Reached ONLY for keys not present as raw table
// members — the function members (is_rejected) shadow this metamethod,
// so kcdx.plugin.is_rejected is the C function, never an author lookup.
//
// Resolves <author> to an author-resolver userdata iff some loaded
// plugin declares that author. Miss -> nil (the next .<plugin> raises
// the stock index error naming the bad <author> segment).
int Lua_PluginTableIndex(lua_State* L) {
    if (lua_type(L, 2) != LUA_TSTRING) { lua_pushnil(L); return 1; }
    const char* authorCStr = lua_tostring(L, 2);
    std::string author = authorCStr ? authorCStr : "";
    if (author.empty()) { lua_pushnil(L); return 1; }

    if (!AuthorExists(author)) {
        // No loaded plugin declares this author. Miss -> nil; the next
        // .<plugin> access raises "attempt to index a nil value
        // (field '<author>')", naming the segment that did not resolve.
        lua_pushnil(L);
        return 1;
    }

    // Hit: return an author-resolver userdata carrying <author> on its
    // envtable, for the next hop's __index to read.
    lua_newuserdata(L, 1);
    luaL_getmetatable(L, kAuthorResolverMt);
    lua_setmetatable(L, -2);
    lua_newtable(L);
    lua_pushstring(L, author.c_str()); lua_setfield(L, -2, "author");
    lua_setfenv(L, -2);
    return 1;
}

// Install the per-hop metatables in LUA_REGISTRYINDEX. Idempotent
// (luaL_newmetatable is a no-op when the name is already registered).
// Each metatable hides itself from pak Lua via __metatable (the
// kcdx.hook pattern — a getmetatable() returns the string, not the
// table, so plugin code can't tamper with the resolver wiring).
void EnsureNavMetatables(lua_State* L) {
    if (luaL_newmetatable(L, kAuthorResolverMt) != 0) {
        lua_pushcfunction(L, Lua_PluginAuthorIndex);
        lua_setfield(L, -2, "__index");
        lua_pushstring(L, kAuthorResolverMt);
        lua_setfield(L, -2, "__metatable");
    }
    lua_pop(L, 1);

    if (luaL_newmetatable(L, kPluginHandleMt) != 0) {
        lua_pushcfunction(L, Lua_PluginHandleIndex);
        lua_setfield(L, -2, "__index");
        lua_pushstring(L, kPluginHandleMt);
        lua_setfield(L, -2, "__metatable");
    }
    lua_pop(L, 1);
}

const luaL_Reg kFunctions[] = {
    {"is_rejected", Lua_IsRejected},
    {nullptr, nullptr},
};

}  // namespace

// Called from lua_bind.cpp::RegisterKcdxTable, with the kcdx global
// table on top of the stack. Creates the `plugin` sub-table inside it.
// Stack effect: 0.
//
// The table carries the function members (is_rejected) as RAW table
// entries AND an __index metamethod (the navigable cross-plugin
// namespace resolver). Lua fires __index only on a raw-table MISS, so a
// function member always shadows the resolver: kcdx.plugin.is_rejected
// is the C function, kcdx.plugin.<unknown-key> falls through to
// Lua_PluginTableIndex which resolves <key> as an <author> segment.
// (The same coexistence kcdx.hook uses — a table with members AND
// metamethods.)
void bind(lua_State* L) {
    int kcdx_idx = lua_gettop(L);

    EnsureNavMetatables(L);

    // The kcdx.plugin metatable: __index = the author-segment resolver,
    // __metatable hidden from pak Lua so plugin code can't tamper with
    // the resolver wiring.
    if (luaL_newmetatable(L, "kcdx.plugin.domain") != 0) {
        lua_pushcfunction(L, Lua_PluginTableIndex);
        lua_setfield(L, -2, "__index");
        lua_pushstring(L, "kcdx.plugin.domain");
        lua_setfield(L, -2, "__metatable");
    }
    lua_pop(L, 1);

    lua_newtable(L);
    for (const luaL_Reg* f = kFunctions; f->name; ++f) {
        lua_pushcfunction(L, f->func);
        lua_setfield(L, -2, f->name);
    }
    luaL_getmetatable(L, "kcdx.plugin.domain");
    lua_setmetatable(L, -2);
    lua_setfield(L, kcdx_idx, "plugin");
}

}  // namespace kcdx::lua_bind_plugin
