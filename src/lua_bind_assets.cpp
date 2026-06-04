// kcdx.assets.* — the asset authoring DOMAIN (Lua surface).
//
// A GROUPED capability DOMAIN (kcdx.assets.*, like kcdx.cvar.* / kcdx.plugin.*,
// NOT a top-level verb — asset operations are a capability domain, not one of
// the closed-set core registration verbs). design asset-replacement.md §5.
//
// THIS STEP ships ONE verb — the pure read get_by_path. The four runtime verbs
// (get_by_name / declare / register / replace) need a runtime store that does
// not exist yet (design §5.1); they are a later step. Their contract is pinned
// by NYI doc entries + deliberately-failing matrix rows (docs/lua/assets.md +
// test-plugins/README.md), NOT a stub here — a stub that returned a default
// would be a silent-success defect (AP14). The author who calls one today gets
// Lua's stock "attempt to call a nil value" (the verb is genuinely not bound),
// which is the honest signal until the verb lands.
//
//   kcdx.assets.get_by_path(path) -> loadable path | (nil, err)
//
// get_by_path resolves the CALLING plugin's OWN asset (no owner prefix — the
// engine knows who you are, naming-namespaces.md "never type your own prefix")
// to a loadable on-disk path: the absolute disk path the asset-resolution seam
// (HOOK 2) opens to serve the file. It is a PURE READ — it mutates no store and
// depends on none of the §5.1 runtime store; it joins the calling plugin's
// assets/ root with the relative path and confirms the file exists, reusing the
// SAME resolution the sidecar parser uses (folderPath / assetsEntrypointRel /
// rel, '..'-traversal rejected, is_regular_file). A path to a file NOT in the
// plugin's assets/ returns a TEACHING ERROR naming the missing path (AP14),
// never a silent nil.
//
//   local icon = kcdx.assets.get_by_path("icons/my_icon.dds")
//   -- icon is a loadable path you hand to a game asset API.
//
// CROSS-PLUGIN form — kcdx.plugin.<author>.<plugin>.assets.get_by_path(path) —
// resolves through the step-6 navigable namespace (lua_bind_plugin.cpp). The
// .assets leaf on a resolved plugin handle is wired by PushPluginAssetsDomain
// (below): it binds get_by_path to the NAVIGATED (author, plugin), so the same
// resolver serves another mod's asset by path. The path stays a quoted string
// (it is data); the namespace is bare dotted (§6).
//
//   local shirt = kcdx.plugin.redmoon.outfit_swap.assets.get_by_path("male/shirt.dds")
//
// Lua bridge (lua-bridge.md): raw Lua C API only — lua_pushcfunction +
// lua_setfield, a registry-keyed metatable for the cross-plugin assets userdata.
// NO kcdx-side static-const sentinel (AP5); the frealloc canary stays zero.
// Returns a STRING (a path), never a pointer — the lua_Number=float precision
// hazard (lua-precision.md) does NOT apply.

#include "lua_bind_assets.h"

#include <filesystem>
#include <string>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include "log.h"
#include "lua_registry.h"   // OwningPluginForCurrentCall — the calling-plugin seam
#include "plugin_loader.h"  // g_plugins, PluginManifest (folderPath / assetsEntrypointRel)

namespace fs = std::filesystem;

namespace kcdx::lua_bind_assets {

namespace {

// Stable log category for the resolution teaches (greppable in kcdx-dev.log;
// the agent reads these for cap-75's falsifiable rows).
constexpr const char* kCat = "ASSET_GET";

// Registry key for the cross-plugin assets-domain userdata metatable (the
// userdata kcdx.plugin.<a>.<p>.assets resolves to). Installed once, idempotent.
constexpr const char* kPluginAssetsMt = "kcdx.plugin.assets";

// Find the loaded plugin's manifest by (author, plugin). REUSES the
// match-on-both-components discipline lua_bind_plugin::HandleForAuthorPlugin
// uses — FindByName keys on [plugin].name ALONE, so it cannot disambiguate two
// authors reusing a plugin name; the (author, plugin) pair is the identity
// (naming-namespaces.md). An empty `author` is the legacy 1-dot tier (the
// corpus today; [plugin].author not yet declared everywhere) — match on
// `plugin` alone then. Returns nullptr on no match.
const plugins::PluginManifest* FindManifest(const std::string& author,
                                            const std::string& plugin) {
    if (plugin.empty()) return nullptr;
    for (const auto& p : kcdx::plugins::g_plugins) {
        if (p.manifest.name != plugin) continue;
        if (author.empty() || p.manifest.author == author) {
            return &p.manifest;
        }
    }
    return nullptr;
}

// kcdx.assets.get_by_path(path) — the own-asset reader. Resolves the CALLING
// plugin (OwningPluginForCurrentCall) and its asset path to a loadable disk
// path. Two failure shapes, mirroring the kcdx-binder idiom
// (lua_bind_cvar.cpp / lua_bind_plugin.cpp):
//   * BAD ARGUMENT (path not a string / missing / empty) -> (nil, teaching
//     error), return 2. The call shape itself was wrong; teach the author.
//   * RESOLUTION FAILURE (no calling plugin / path not in the plugin's
//     assets/) -> (nil, teaching error), return 2. A path to a file NOT in the
//     plugin's assets/ is a typo, NOT a silent nil (AP14 — fail loud naming the
//     missing path in the author's terms).
int Lua_AssetsGetByPath(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.assets.get_by_path(path): expects a single string argument — "
            "the path to YOUR OWN asset, relative to your plugin's assets/ "
            "folder (e.g. kcdx.assets.get_by_path(\"icons/my_icon.dds\")). "
            "Returns a loadable path you hand to a game asset API. To reference "
            "ANOTHER mod's asset, navigate the namespace: "
            "kcdx.plugin.<author>.<plugin>.assets.get_by_path(path).");
        return 2;
    }
    size_t len = 0;
    const char* s = lua_tolstring(L, 1, &len);
    const std::string relPath = (s && len) ? std::string(s, len) : std::string();
    if (relPath.empty()) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.assets.get_by_path(path): `path` must be non-empty — the path "
            "to your own asset relative to your plugin's assets/ folder "
            "(e.g. \"icons/my_icon.dds\").");
        return 2;
    }

    // Resolve the CALLING plugin (no owner prefix — the engine knows who you
    // are). OwningPluginForCurrentCall walks the Lua callstack to the nearest
    // attributed plugin script; {"",""} for an anonymous caller (console / pak
    // Lua / ad-hoc), which has no own assets/ to resolve against.
    std::string callSiteFile;  // unused here (the resolver carries its own teach)
    int callSiteLine = 0;
    const kcdx::lua_registry::OwningPlugin owner =
        kcdx::lua_registry::OwningPluginForCurrentCall(L, callSiteFile,
                                                       callSiteLine);
    if (owner.plugin.empty()) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.assets.get_by_path(path): no calling plugin — get_by_path "
            "resolves YOUR OWN asset, so it must be called from a plugin's "
            "plugin.lua (or a file it require()s). An anonymous caller "
            "(console / ad-hoc Lua) has no assets/ folder of its own; to reach "
            "a specific plugin's asset use "
            "kcdx.plugin.<author>.<plugin>.assets.get_by_path(path).");
        return 2;
    }

    std::string disk, err;
    if (!ResolveAssetPath(owner.author, owner.plugin, relPath, disk, err)) {
        lua_pushnil(L);
        lua_pushlstring(L, err.data(), err.size());
        return 2;
    }
    lua_pushlstring(L, disk.data(), disk.size());
    return 1;
}

// The cross-plugin get_by_path closure body. A C closure carrying the navigated
// (author, plugin) as upvalues (1 = author, 2 = plugin). It reads its OWN target
// identity from the upvalues — NOT OwningPluginForCurrentCall (that resolves the
// CALLER, the wrong plugin for a cross-plugin read). Same two failure shapes +
// teaching errors as the own-form Lua_AssetsGetByPath, against the bound plugin.
int Lua_CrossPluginGetByPath(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING) {
        lua_pushnil(L);
        lua_pushstring(L,
            "assets.get_by_path(path): expects a single string argument — the "
            "path to the asset relative to that plugin's assets/ folder "
            "(e.g. ...assets.get_by_path(\"male/shirt.dds\")).");
        return 2;
    }
    size_t plen = 0;
    const char* ps = lua_tolstring(L, 1, &plen);
    const std::string relPath =
        (ps && plen) ? std::string(ps, plen) : std::string();
    if (relPath.empty()) {
        lua_pushnil(L);
        lua_pushstring(L,
            "assets.get_by_path(path): `path` must be non-empty — the asset "
            "path relative to that plugin's assets/ folder.");
        return 2;
    }
    const char* aC = lua_tostring(L, lua_upvalueindex(1));
    const char* pC = lua_tostring(L, lua_upvalueindex(2));
    const std::string author = aC ? aC : "";
    const std::string plugin = pC ? pC : "";

    std::string disk, err;
    if (!ResolveAssetPath(author, plugin, relPath, disk, err)) {
        lua_pushnil(L);
        lua_pushlstring(L, err.data(), err.size());
        return 2;
    }
    lua_pushlstring(L, disk.data(), disk.size());
    return 1;
}

// __index on a cross-plugin assets-domain userdata. arg 1 = the userdata
// (carrying the navigated author+plugin on its envtable); arg 2 = the accessed
// key. The ONLY surface this step exposes is get_by_path; any other key is a
// navigation miss -> nil (the kcdx.hook / step-6 navigation-miss idiom — the
// next access raises Lua's stock index error naming the bad surface segment).
// The four runtime verbs are NOT exposed here (later step), so e.g.
// kcdx.plugin.<a>.<p>.assets.get_by_name resolves to nil today, consistent with
// the own-form's not-yet-bound verbs.
int Lua_PluginAssetsIndex(lua_State* L) {
    if (lua_type(L, 2) != LUA_TSTRING) { lua_pushnil(L); return 1; }
    const char* keyC = lua_tostring(L, 2);
    const std::string key = keyC ? keyC : "";
    if (key != "get_by_path") {
        lua_pushnil(L);
        return 1;
    }
    // Return Lua_CrossPluginGetByPath bound to the navigated (author, plugin)
    // carried on the userdata's envtable (as upvalues 1 = author, 2 = plugin).
    lua_getfenv(L, 1);
    lua_getfield(L, -1, "author");
    lua_getfield(L, -2, "plugin");
    // Stack: ... envtable, author, plugin. lua_pushcclosure pops author+plugin
    // as the closure's 2 upvalues; pop the leftover envtable after.
    lua_pushcclosure(L, Lua_CrossPluginGetByPath, 2);
    // Stack now: ... envtable, closure. Tidy the envtable out from under it.
    lua_remove(L, -2);
    return 1;
}

// Install the cross-plugin assets-domain metatable. Idempotent
// (luaL_newmetatable no-ops when already registered). Hides itself from pak Lua
// via __metatable (the kcdx.hook / step-6 pattern — getmetatable() returns the
// string, plugin code can't tamper with the resolver wiring).
void EnsurePluginAssetsMetatable(lua_State* L) {
    if (luaL_newmetatable(L, kPluginAssetsMt) != 0) {
        lua_pushcfunction(L, Lua_PluginAssetsIndex);
        lua_setfield(L, -2, "__index");
        lua_pushstring(L, kPluginAssetsMt);
        lua_setfield(L, -2, "__metatable");
    }
    lua_pop(L, 1);
}

const luaL_Reg kFunctions[] = {
    {"get_by_path", Lua_AssetsGetByPath},
    {nullptr, nullptr},
};

}  // namespace

bool ResolveAssetPath(const std::string& author, const std::string& plugin,
                      const std::string& relPath, std::string& outDisk,
                      std::string& outErr) {
    const plugins::PluginManifest* m = FindManifest(author, plugin);
    if (!m) {
        const std::string ident =
            author.empty() ? plugin : (author + "." + plugin);
        outErr = "kcdx.assets.get_by_path: no loaded plugin '" + ident +
                 "' — the asset's owner is not a plugin loaded this session "
                 "(check the <author>.<plugin> in the namespace path).";
        LOG_WARN_KV(kCat, "rejected",
            kcdx::log::KV("reason", std::string("no such plugin")),
            kcdx::log::KV("author", author),
            kcdx::log::KV("plugin", plugin),
            kcdx::log::KV("path", relPath));
        return false;
    }

    // No assets/ entrypoint declared -> the plugin ships no asset tree, so no
    // path resolves. Teach with the plugin's identity.
    if (m->assetsEntrypointRel.empty()) {
        outErr = "kcdx.assets.get_by_path(\"" + relPath + "\"): plugin '" +
                 (m->author.empty() ? m->name : m->author + "." + m->name) +
                 "' declares no assets/ entrypoint (set [entrypoints].assets = "
                 "\"assets/\" in its kcdx.toml) — it has no asset folder to "
                 "resolve a path against.";
        LOG_WARN_KV(kCat, "rejected",
            kcdx::log::KV("reason", std::string("no assets entrypoint")),
            kcdx::log::KV("plugin", m->name),
            kcdx::log::KV("path", relPath));
        return false;
    }

    // Path safety (input-validation.md §Paths): reject a '..' segment so a read
    // cannot escape the plugin's assets/ subtree. SAME reject the sidecar
    // resolver applies (asset_sidecar.cpp ResolveDeclaringFile) — reused, not
    // re-derived.
    const fs::path relP = fs::path(relPath).lexically_normal();
    for (const auto& seg : relP) {
        if (seg == "..") {
            outErr = "kcdx.assets.get_by_path(\"" + relPath + "\"): the path "
                     "escapes the plugin's assets/ folder ('..' traversal is "
                     "rejected — name an asset inside assets/, not above it).";
            LOG_WARN_KV(kCat, "rejected",
                kcdx::log::KV("reason", std::string("'..' traversal")),
                kcdx::log::KV("plugin", m->name),
                kcdx::log::KV("path", relPath));
            return false;
        }
    }

    // Join: <plugin folderPath> / <assetsEntrypointRel> / <relPath>. The SAME
    // join the sidecar's assetsRoot uses (asset_sidecar.cpp ~358:
    // folderPath / assetsEntrypointRel). The result is the absolute disk path
    // the HOOK-2 open serves — the loadable path the author hands a game API.
    const fs::path assetsRoot = m->folderPath / m->assetsEntrypointRel;
    const fs::path disk = (assetsRoot / relP).lexically_normal();

    std::error_code ec;
    if (!fs::is_regular_file(disk, ec)) {
        // Missing-target teach (AP14): a path to a file NOT in the plugin's
        // assets/ is a typo, never a silent nil. Name the missing path in the
        // author's terms (the relative path they wrote AND where it was looked
        // for) so they can fix it without consulting docs.
        outErr = "kcdx.assets.get_by_path(\"" + relPath + "\"): no such asset "
                 "in plugin '" +
                 (m->author.empty() ? m->name : m->author + "." + m->name) +
                 "' — '" + relPath + "' is not a file under its assets/ folder "
                 "(looked for " + disk.string() + "). A file's presence makes "
                 "it referenceable; check the path against your assets/ tree — "
                 "a typo here is a loud error, never a silent nil.";
        LOG_WARN_KV(kCat, "rejected",
            kcdx::log::KV("reason", std::string("no such asset")),
            kcdx::log::KV("plugin", m->name),
            kcdx::log::KV("path", relPath),
            kcdx::log::KV("looked_for", disk.string()));
        return false;
    }

    outDisk = disk.string();
    return true;
}

int PushPluginAssetsDomain(lua_State* L, const std::string& author,
                           const std::string& plugin) {
    // A userdata carrying (author, plugin) on its envtable; its metatable's
    // __index (Lua_PluginAssetsIndex) exposes get_by_path bound to that pair.
    // The one-byte payload is the step-6 ResolvedHookUd pattern — the metatable
    // identity + the envtable do the work; no std::string members on the C
    // struct (no __gc dance).
    EnsurePluginAssetsMetatable(L);
    lua_newuserdata(L, 1);
    luaL_getmetatable(L, kPluginAssetsMt);
    lua_setmetatable(L, -2);
    lua_newtable(L);
    lua_pushstring(L, author.c_str()); lua_setfield(L, -2, "author");
    lua_pushstring(L, plugin.c_str()); lua_setfield(L, -2, "plugin");
    lua_setfenv(L, -2);
    return 1;
}

void bind(lua_State* L) {
    // kcdx.assets.* — a GROUPED capability domain (NOT a top-level verb). Built
    // exactly like kcdx.cvar.* (lua_bind_cvar.cpp): a lua_newtable, per-fn
    // lua_pushcfunction/lua_setfield, then one lua_setfield onto the kcdx table.
    // The kcdx table stays at kcdx_idx throughout (lua_setfield pops the
    // sub-table it consumes), leaving the stack balanced for the next binder.
    int kcdx_idx = lua_gettop(L);

    // Install the cross-plugin assets metatable up front so the leaf is ready
    // when kcdx.plugin.<a>.<p>.assets resolves it (lua_bind_plugin calls
    // PushPluginAssetsDomain, which also EnsurePluginAssetsMetatable's
    // idempotently — both orderings are safe).
    EnsurePluginAssetsMetatable(L);

    lua_newtable(L);
    for (const luaL_Reg* f = kFunctions; f->name; ++f) {
        lua_pushcfunction(L, f->func);
        lua_setfield(L, -2, f->name);
    }
    lua_setfield(L, kcdx_idx, "assets");
}

}  // namespace kcdx::lua_bind_assets
