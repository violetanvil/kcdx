#pragma once

// kcdx::lua_require_searcher — plugin-scoped `require` for plugin.lua.
//
// PROBLEM (settled by the require-routing probe, committed 7640fde):
// stock Lua 5.1 `require("helper")` from a plugin's plugin.lua ERRORS
// `module 'helper' not found`. package.path (`;.\?.lua;!\lua\?.lua;...`)
// reaches the EXE dir and cwd but NEVER a plugin's own install folder, so
// a plugin shipping a sibling helper.lua cannot load it. "Publish from a
// require'd helper" was therefore never reachable for plugins.
//
// FIX (locked by the user): install ONE kcdx searcher into the live
// `package.loaders` table (Lua 5.1; package.searchers does not exist
// until 5.2 — confirmed live, 4 entries). The searcher resolves a module
// name against ONLY the CURRENT OWNING PLUGIN's folder — never the global
// package.path, never another plugin's folder (two plugins may both ship
// helper.lua and must not collide). Plugin-scoped, not flat.
//
// ATTRIBUTION FALLS OUT OF THE FIX. kcdx owns the compile point: the
// searcher calls luaL_loadfile on the helper, so it can call
// lua_registry::RegisterScriptOwner at that exact moment. The require'd
// helper's source then lands in g_scriptOwners owned by the plugin, so a
// kcdx.publish / kcdx.on / kcdx.hook call made from inside the helper
// resolves to the plugin via the EXISTING (unmodified)
// OwningPluginForCurrentCall frame walk — never "<anon>". This closes the
// publisher-identity gap at its source.
//
// SCOPED OWNER. "Which plugin owns the current load" is ENGINE-SIDE C++
// state (a file-scope variable here), NOT a Lua registry slot the GC
// touches (lua-bridge.md / AP5). The plugin loader sets it immediately
// before its SYNCHRONOUS guard::Call that loads plugin.lua and clears it
// immediately after (RAII) — so a require(...) executed inside the chunk
// runs while the owner is still set. Main-thread only (loads are
// main-thread; lua-callback-threading.md / AP6).
//
// The searcher itself is a plain lua_CFunction pushed via
// lua_pushcfunction (raw Lua C API) — it introduces NO kcdx-side
// static-const Lua sentinel (lua-bridge.md / AP5; PROBE Q stays zero).

#include <filesystem>
#include <string>

extern "C" {
#include "lua.h"
}

namespace kcdx::lua_require_searcher {

// Install the kcdx plugin-scoped searcher into package.loaders, once,
// against the live shared lua_State. Called once before the plugin loop
// in lua_plugin_loader::RunAll. Idempotent (internal latch) so a second
// call is a no-op. No-op on a null L.
void Install(lua_State* L);

// RAII scope: set the "current owning plugin" for the duration of a
// synchronous plugin.lua load, clear it on every exit path (clean
// return, Lua error, or SEH fault swallowed by guard::Call — the
// destructor still runs because guard::Call returns rather than
// unwinding). The folderPath is the plugin's absolute install dir; the
// name is the plugin's stable name. A require(...) inside the loaded
// chunk reads this owner via the file-scope state.
//
// Owners do NOT nest in practice (loads are sequential on the main
// thread), but the scope saves+restores the previous owner so a future
// nested load would behave correctly rather than clobbering.
struct OwnerScope {
    OwnerScope(std::string name, std::filesystem::path folderPath);
    ~OwnerScope();

    OwnerScope(const OwnerScope&)            = delete;
    OwnerScope& operator=(const OwnerScope&) = delete;

private:
    std::string                 prevName_;
    std::filesystem::path       prevFolder_;
    bool                        prevHadOwner_ = false;
};

}  // namespace kcdx::lua_require_searcher
