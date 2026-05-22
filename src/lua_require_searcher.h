#pragma once

// kcdx::lua_require_searcher — plugin-scoped, plugin-isolated `require`
// for plugin chunks (entrypoints AND require'd helpers).
//
// PROBLEM 1 (settled by the require-routing probe, committed 7640fde):
// stock Lua 5.1 `require("helper")` from a plugin's plugin.lua ERRORS
// `module 'helper' not found`. package.path (`;.\?.lua;!\lua\?.lua;...`)
// reaches the EXE dir and cwd but NEVER a plugin's own install folder, so
// a plugin shipping a sibling helper.lua cannot load it.
//
// PROBLEM 2 (the cross-plugin require-cache collision — KNOWN correctness
// gap, AP13, fixed at the source here): there is ONE shared lua_State for
// all plugins, so Lua's _LOADED table (the registry-global module cache)
// is SHARED across every plugin. Stock ll_require (vendor/lua/loadlib.c:
// 449-485) checks `_LOADED[name]` keyed by the BARE module name at :454
// BEFORE any loader runs and returns the cached hit at :458, then writes
// `_LOADED[name]=result` at :484. A per-plugin file-resolver only runs on
// a _LOADED MISS and CANNOT change the bare-name cache key. So plugin A's
// require("helper") caches _LOADED["helper"]=A's-module, and plugin B's
// require("helper") then HITS that and silently gets A's module — a
// cross-plugin mis-resolution that defeats the per-plugin-folder promise
// at the cache layer even though the file layer was correct.
//
// FIX (locked by the user): kcdx OWNS the `require` that plugin chunks
// call, via a per-chunk environment (fenv), and namespaces the module
// cache by the owning plugin — BYPASSING _LOADED entirely for plugin
// chunks. A bare require("helper") from plugin A resolves to A's
// helper.lua and A's cache slot; the same call from plugin B resolves to
// B's. Idiomatic bare names for the author; structural per-plugin
// isolation underneath.
//
// MECHANISM (a) — PER-CHUNK FENV (not a global require replacement).
// When kcdx loads a plugin chunk (an entrypoint via the loader, AND a
// require'd helper via the kcdx require closure), it sets the chunk's
// environment (lua_setfenv, lua.h:199) to a kcdx env table BEFORE running
// it. That env table's `require` field is a kcdx C closure; everything
// else falls through to _G via an __index metamethod pointing at _G — so
// the chunk sees all normal globals plus kcdx's require.
//   VERIFIED FACT: require resolves through the RUNNING CHUNK's
//   environment, not a fixed _G — OP_GETGLOBAL reads `cl->env`
//   (vendor/lua/lvm.c:431). So a chunk whose fenv has require=kcdx_closure
//   routes the author's bare require("helper") to kcdx.
//
//   WHY NESTED requires ARE owner-scoped (the corrected rationale —
//   NOT "the fenv propagates", which is FALSE). Env inheritance via
//   OP_CLOSURE (vendor/lua/lvm.c:725) is LEXICAL ONLY: a function DEFINED
//   inside a chunk inherits that chunk's env, but a helper pulled in by a
//   NESTED require("util") is a SEPARATELY loaded chunk (luaL_loadfile +
//   lua_call), NOT lexically nested — it does NOT inherit the entrypoint's
//   fenv. Owner-scoping of nested requires comes from TWO things instead:
//     1. the kcdx require closure SETS the kcdx fenv on EVERY chunk it
//        loads (so a require'd helper ALSO runs under a kcdx fenv → the
//        helper's own require("util") routes to kcdx too), AND
//     2. OwnerScope is set for the WHOLE synchronous load (below), so the
//        live owner is correct throughout — including nested requires.
//
// MECHANISM — OWNER BINDING. The kcdx require closure binds the owner from
// the LIVE OwnerScope (the file-scope owner state) at the moment it runs —
// exactly as the file-resolver reads it. The load is synchronous and
// main-thread-only (lua-callback-threading.md / AP6), so the live owner is
// correct for the whole load including nested requires. This is the
// owner-binding guarantee: it comes from OwnerScope, NOT from fenv
// inheritance and NOT from compile time.
//
// MECHANISM (b) — NAMESPACED CACHE, BYPASS _LOADED. A kcdx-owned cache
// table (a GC-managed Lua table held by a registry ref — NOT _LOADED, NOT
// a kcdx-side static-const sentinel; lua-bridge.md / AP5), keyed
// "<owner>:<modname>". The kcdx require closure: read owner from
// OwnerScope; key = owner..":"..modname; if the cache has the key → return
// the cached module (a WITHIN-PLUGIN second require("helper") hits this —
// same owner, same key); else resolve+compile the FILE from this plugin's
// folder, set the kcdx fenv on the chunk, run it, store the result under
// the key, return it. _LOADED is never read or written for plugin chunks —
// mirror-writing _LOADED[modname] would re-introduce the exact
// cross-plugin clobber this fix kills. Stock non-plugin code not seeing a
// plugin's module under the bare name IS the isolation, not a regression.
//
// ATTRIBUTION FALLS OUT OF THE FIX (unchanged). kcdx owns the compile
// point: ResolveAndCompile calls luaL_loadfile on the helper, so it calls
// lua_registry::RegisterScriptOwner at that exact moment. The require'd
// helper's source then lands in g_scriptOwners owned by the plugin, so a
// kcdx.publish / kcdx.on / kcdx.hook call made from inside the helper
// resolves to the plugin via the EXISTING (unmodified)
// OwningPluginForCurrentCall frame walk — never "<anon>".
//
// SCOPED OWNER. "Which plugin owns the current load" is ENGINE-SIDE C++
// state (a file-scope variable here), NOT a Lua registry slot the GC
// touches (lua-bridge.md / AP5). The plugin loader sets it immediately
// before its SYNCHRONOUS guard::Call that loads an entrypoint and clears
// it immediately after (RAII) — so a require(...) executed inside the
// chunk runs while the owner is still set. Main-thread only (loads are
// main-thread; lua-callback-threading.md / AP6).
//
// NO STATIC-CONST SENTINEL. The require closure and the traceback-style
// C functions are plain lua_CFunctions pushed via lua_pushcfunction /
// lua_pushcclosure (raw Lua C API). The cache table is a live Lua table
// pinned by luaL_ref into LUA_REGISTRYINDEX. kcdx introduces NO new
// static-const Lua sentinel (lua-bridge.md / AP5; PROBE Q stays zero).

#include <filesystem>
#include <string>

extern "C" {
#include "lua.h"
}

namespace kcdx::lua_require_searcher {

// One-time per-session setup against the live shared lua_State: create the
// kcdx namespaced module cache (a fresh Lua table pinned by a registry
// ref). Called once before the plugin loop in lua_plugin_loader::RunAll
// (and idempotently from RunAfterEntrypoints). Idempotent (internal
// latch). No-op on a null L.
//
// NOTE: kcdx does NOT install a package.loaders searcher. Plugin chunks
// run under a kcdx fenv whose `require` is the kcdx closure, so they never
// reach stock ll_require / package.loaders — there is ONE resolution path
// (ResolveAndCompile, used by the closure and by entrypoint loading), not
// two divergent ones. Non-plugin Lua (console, pak scripts) keeps its
// stock _G.require + package.loaders untouched.
void Install(lua_State* L);

// Set the kcdx environment table as the fenv of the chunk at the top of
// the stack (stack index -1 must be the compiled chunk function). The
// kcdx env's `require` is the kcdx require closure; all other globals fall
// through to _G via __index. The plugin loader calls this on a freshly
// luaL_loadfile'd ENTRYPOINT chunk, BEFORE lua_pcall, so the entrypoint's
// bare requires route to kcdx; the require closure calls it on every
// helper chunk it loads (mechanism a.1). Leaves the chunk on top (the
// stack is unchanged in height — lua_setfenv pops the env it pushed
// internally). No-op on a null L or a non-function at -1.
void SetKcdxEnvOnChunkAtTop(lua_State* L);

// RAII scope: set the "current owning plugin" for the duration of a
// synchronous entrypoint load, clear it on every exit path (clean return,
// Lua error, or SEH fault swallowed by guard::Call — the destructor still
// runs because guard::Call returns rather than unwinding). The folderPath
// is the plugin's absolute install dir; the name is the plugin's stable
// name. A require(...) inside the loaded chunk (and any nested require)
// reads this owner via the file-scope state.
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
