// kcdx::lua_require_searcher — see header for the contract.

#include "lua_require_searcher.h"

#include <atomic>
#include <filesystem>
#include <string>
#include <system_error>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include "log.h"
#include "lua_registry.h"  // RegisterScriptOwner (the existing, unmodified
                           // attribution sink — we only feed it more entries).

namespace fs = std::filesystem;

namespace kcdx::lua_require_searcher {

namespace {

// The "current owning plugin" for the in-flight synchronous plugin.lua
// load. ENGINE-SIDE C++ state (lua-bridge.md / AP5 — NOT a Lua registry
// slot the GC touches). Main-thread only: loads are sequential on the
// main thread (lua-callback-threading.md / AP6), so a plain non-atomic
// pair needs no lock — only the loader (set/clear via OwnerScope) and the
// searcher (read, while the owner is still set because the load is
// synchronous) touch it, both on the main thread.
bool          g_haveOwner = false;
std::string   g_ownerName;
fs::path      g_ownerFolder;

std::atomic<bool> g_installed{false};

// The searcher pushed into package.loaders. Lua 5.1 calls it with the
// requested module name as the single string argument at stack index 1
// (vendor/lua/loadlib.c:470-471: `lua_pushstring(L, name); lua_call(L,1,1)`).
//
// Loader return protocol (vendor/lua/loadlib.c::ll_require :472-477 and
// the stock loader_Lua :378-386): a loader returns EXACTLY ONE value —
//   * a FUNCTION (the compiled chunk)  -> require runs + caches it,
//   * a STRING (why this loader missed) -> require accumulates it into the
//     "module '<name>' not found:<reasons>" message,
//   * anything else                     -> require ignores and tries the
//     next loader.
// So we push exactly one result: the chunk on success, else a "\n\tno
// file '<path>'" string (matching findfile's :365 format) so the
// not-found message names the plugin folder kcdx searched (errors-teach,
// cornerstones.md).
int KcdxPluginSearcher(lua_State* L) {
    const char* modname = luaL_checkstring(L, 1);

    // No owner set => require was called OUTSIDE a plugin entrypoint load
    // (ad-hoc / console Lua). This searcher does not apply there; return a
    // string so require keeps trying the other (stock) loaders and, if all
    // miss, surfaces a not-found that explains why kcdx didn't match.
    if (!g_haveOwner) {
        lua_pushstring(L,
            "\n\tno kcdx plugin-scoped match (require called outside a "
            "plugin's entrypoint load)");
        return 1;
    }

    // Lua module convention (vendor/lua/loadlib.c::findfile :353): dots in
    // the module name map to the directory separator. require("a.b") ->
    // a/b.lua; the common flat require("helper") -> helper.lua.
    std::string rel = modname;
    for (char& c : rel) {
        if (c == '.') c = '/';
    }
    rel += ".lua";

    const fs::path resolved = g_ownerFolder / fs::path(rel);
    const std::string resolvedStr = resolved.string();

    std::error_code ec;
    if (!fs::exists(resolved, ec) || ec) {
        // File not in THIS plugin's folder. Return the per-loader miss
        // string naming exactly where kcdx looked. We do NOT fall back to
        // global package.path or another plugin's folder — plugin-scoped
        // means this plugin's folder only (locked decision: two plugins
        // may both ship helper.lua and must not collide).
        lua_pushfstring(L, "\n\tno file '%s' (kcdx plugin-scoped: '%s')",
                        resolvedStr.c_str(), g_ownerName.c_str());
        return 1;
    }

    // The file exists in the owning plugin's folder. THIS is the compile
    // point kcdx controls. Compile it; on a compile error return the error
    // per the loader protocol (don't silently swallow — errors-teach).
    if (luaL_loadfile(L, resolvedStr.c_str()) != 0) {
        // luaL_loadfile left an error string on top. Reframe it as a
        // loader-miss string so require surfaces it in the not-found
        // accumulation (the author sees WHY their helper wouldn't load).
        const char* loadErr = lua_tostring(L, -1);
        lua_pushfstring(L, "\n\tkcdx: error loading '%s': %s",
                        resolvedStr.c_str(),
                        loadErr ? loadErr : "<no message>");
        lua_remove(L, -2);  // drop the raw luaL_loadfile error; keep ours
        return 1;
    }

    // STAMP ATTRIBUTION at the compile point. We register the SAME path
    // form handed to luaL_loadfile (resolvedStr) so the chunk's
    // debug.getinfo `source` (which Lua sets to "@" + that path) matches
    // what OwningPluginForCurrentCall later normalizes + looks up in
    // g_scriptOwners. We do NOT touch OwningPluginForCurrentCall — making
    // g_scriptOwners COMPLETE (entrypoint + every require'd helper) is the
    // whole fix. RegisterScriptOwner is the existing exported sink
    // (lua_registry.h:184); we only feed it one more entry.
    kcdx::lua_registry::RegisterScriptOwner(resolvedStr, g_ownerName);

    LOG_DEBUG_KV("REQUIRE", "plugin-scoped require resolved",
                 kcdx::log::KV("plugin", g_ownerName.c_str()),
                 kcdx::log::KV("module", modname),
                 kcdx::log::KV("file", resolvedStr.c_str()));

    // The compiled chunk-function is on top — return it as the loader's
    // single result. require (ll_require :481-482) then calls it with the
    // modname and caches the result in package.loaded.
    return 1;
}

}  // namespace

void Install(lua_State* L) {
    if (!L) return;
    bool expected = false;
    if (!g_installed.compare_exchange_strong(expected, true,
                                             std::memory_order_acq_rel)) {
        return;  // already installed this session
    }

    const int top = lua_gettop(L);

    // package.loaders is a LIVE table (Lua 5.1; confirmed by the
    // require-routing probe — package.searchers is nil here, that's 5.2+).
    // Append our C function as a new loader at the end (index N+1) using
    // the raw Lua C API.
    lua_getfield(L, LUA_GLOBALSINDEX, "package");  // _G.package
    if (!lua_istable(L, -1)) {
        log::Warn("lua_require_searcher: _G.package is not a table; "
                  "plugin-scoped require NOT installed");
        lua_settop(L, top);
        return;
    }
    lua_getfield(L, -1, "loaders");                // package.loaders
    if (!lua_istable(L, -1)) {
        log::Warn("lua_require_searcher: package.loaders is not a table "
                  "(Lua 5.1 expected); plugin-scoped require NOT installed");
        lua_settop(L, top);
        return;
    }

    // Append at #loaders + 1. lua_objlen gives the current array length.
    const int n = static_cast<int>(lua_objlen(L, -1));
    lua_pushcfunction(L, &KcdxPluginSearcher);  // raw C API; no sentinel
    lua_rawseti(L, -2, n + 1);                  // loaders[n+1] = searcher

    lua_settop(L, top);
    log::InfoF("lua_require_searcher: plugin-scoped require installed "
               "(package.loaders entry #%d)", n + 1);
}

OwnerScope::OwnerScope(std::string name, fs::path folderPath) {
    prevHadOwner_ = g_haveOwner;
    prevName_     = g_ownerName;
    prevFolder_   = g_ownerFolder;

    g_haveOwner   = true;
    g_ownerName   = std::move(name);
    g_ownerFolder = std::move(folderPath);
}

OwnerScope::~OwnerScope() {
    g_haveOwner   = prevHadOwner_;
    g_ownerName   = std::move(prevName_);
    g_ownerFolder = std::move(prevFolder_);
}

}  // namespace kcdx::lua_require_searcher
