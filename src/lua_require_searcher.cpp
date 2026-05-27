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

// The "current owning plugin" for the in-flight synchronous plugin chunk
// load. ENGINE-SIDE C++ state (NOT a Lua registry slot the GC touches).
// Main-thread only: loads are sequential on the main thread, so a plain
// non-atomic trio
// needs no lock — only the loader (set/clear via OwnerScope) and the kcdx
// require closure (read, while the owner is still set because the load is
// synchronous) touch it, both on the main thread. The closure binds the
// owner from HERE at the moment it runs (mechanism: owner-from-OwnerScope,
// NOT fenv-propagation — see header), so nested requires within one
// synchronous load resolve to the same correct owner.
bool          g_haveOwner = false;
std::string   g_ownerName;
fs::path      g_ownerFolder;

std::atomic<bool> g_installed{false};

// Registry refs (luaL_ref into LUA_REGISTRYINDEX) pinning the two
// GC-managed Lua tables this module owns on the live state:
//   * the namespaced module cache (keyed "<owner>:<modname>"), and
//   * the shared kcdx env table (fenv for every plugin chunk; its
//     `require` is the kcdx closure, __index falls through to _G).
// Registry refs, NOT kcdx-side static-const sentinels. LUA_NOREF until
// Install runs.
int g_cacheRef = LUA_NOREF;
int g_envRef   = LUA_NOREF;

// Forward decl: the kcdx require C closure (installed as the kcdx env's
// `require`).
int KcdxRequire(lua_State* L);

// Push the kcdx namespaced module cache table onto the stack. Pre: Install
// has run (g_cacheRef valid). Returns false (pushes nothing) if not set up.
bool PushCacheTable(lua_State* L) {
    if (g_cacheRef == LUA_NOREF) return false;
    lua_rawgeti(L, LUA_REGISTRYINDEX, g_cacheRef);  // push the cache table
    return lua_istable(L, -1);
}

// ResolveAndCompile — THE single file-resolve + compile + attribute path.
// Shared by the kcdx require closure (helpers) and conceptually mirrors
// what the entrypoint loader does for entrypoints. Resolves `modname`
// against the CURRENT OWNING PLUGIN's folder ONLY (never global
// package.path, never another plugin's folder — two plugins may both ship
// helper.lua and must not collide), compiles it, stamps attribution at the
// compile point, and leaves the compiled chunk FUNCTION on top of the
// stack on success.
//
// Returns true on success (chunk on top). On failure returns false and
// leaves a human-readable reason string on top (caller decides how to
// surface it — the closure raises a Lua error so the author sees WHY).
bool ResolveAndCompile(lua_State* L, const char* modname) {
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
        // File not in THIS plugin's folder. Plugin-scoped means this
        // plugin's folder only (locked decision).
        lua_pushfstring(L, "no file '%s' (kcdx plugin-scoped require: '%s')",
                        resolvedStr.c_str(), g_ownerName.c_str());
        return false;
    }

    // THIS is the compile point kcdx controls. Compile it.
    if (luaL_loadfile(L, resolvedStr.c_str()) != 0) {
        // luaL_loadfile left an error string on top. Reframe it.
        const char* loadErr = lua_tostring(L, -1);
        lua_pushfstring(L, "kcdx: error loading '%s': %s",
                        resolvedStr.c_str(),
                        loadErr ? loadErr : "<no message>");
        lua_remove(L, -2);  // drop the raw luaL_loadfile error; keep ours
        return false;
    }

    // STAMP ATTRIBUTION at the compile point. We register the SAME path
    // form handed to luaL_loadfile (resolvedStr) so the chunk's
    // debug.getinfo `source` (which Lua sets to "@" + that path) matches
    // what OwningPluginForCurrentCall later normalizes + looks up in
    // g_scriptOwners. Making g_scriptOwners COMPLETE (entrypoint + every
    // require'd helper) keeps kcdx.* calls from inside a helper resolving
    // to the plugin, never "<anon>". RegisterScriptOwner is the existing
    // exported sink (lua_registry.h:184); we only feed it one more entry.
    kcdx::lua_registry::RegisterScriptOwner(resolvedStr, g_ownerName);

    LOG_DEBUG_KV("REQUIRE", "plugin-scoped require resolved",
                 kcdx::log::KV("plugin", g_ownerName.c_str()),
                 kcdx::log::KV("module", modname),
                 kcdx::log::KV("file", resolvedStr.c_str()));

    return true;  // compiled chunk function on top
}

// Set the shared kcdx env table as the fenv of the function at the top of
// the stack. Internal worker for the exported SetKcdxEnvOnChunkAtTop and
// for the require closure (helper chunks). Pushes the env, lua_setfenv pops
// it — net stack height unchanged, chunk still on top.
void SetKcdxEnvOnTopInternal(lua_State* L) {
    if (g_envRef == LUA_NOREF) return;
    if (!lua_isfunction(L, -1)) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, g_envRef);  // push kcdx env table
    // lua_setfenv expects the env on top and the target function just below
    // it (index -2). It pops the env. Per vendor/lua/lua.h:199.
    lua_setfenv(L, -2);
}

// The kcdx require closure — the `require` plugin chunks see in their fenv.
// Lua calls it require(modname). It owns the namespaced cache + bypasses
// _LOADED entirely for plugin chunks (see header, mechanism b).
int KcdxRequire(lua_State* L) {
    const char* modname = luaL_checkstring(L, 1);

    // Bind the owner from the LIVE OwnerScope at the moment we run. The
    // load is synchronous + main-thread-only, so this is correct for the
    // whole load INCLUDING nested requires (owner-from-OwnerScope — NOT
    // fenv-propagation; see header).
    if (!g_haveOwner) {
        // No owner => this require was reached from non-plugin Lua. The
        // kcdx env (and thus this closure) is only set on kcdx-loaded
        // chunks, so this should be unreachable from ad-hoc/console/pak
        // Lua — but if reached, fall back to stock _G.require so ad-hoc
        // Lua still works (and never touch the kcdx cache without an
        // owner). Per header: "if reached with no owner, fall back to
        // stock require."
        lua_getfield(L, LUA_GLOBALSINDEX, "require");  // _G.require
        if (!lua_isfunction(L, -1)) {
            return luaL_error(L,
                "kcdx require: no owning plugin and no stock _G.require to "
                "fall back to for module '%s'", modname);
        }
        lua_pushvalue(L, 1);    // the modname
        lua_call(L, 1, 1);      // _G.require(modname) -> result
        return 1;               // forward its single result
    }

    const std::string key = g_ownerName + ":" + modname;

    // (b) Namespaced cache lookup — BYPASS _LOADED. A WITHIN-PLUGIN second
    // require("helper") (same owner, same key) hits here and returns the
    // SAME table; a different plugin's require("helper") has a different
    // key and never collides.
    if (PushCacheTable(L)) {
        lua_getfield(L, -1, key.c_str());  // cache[key]
        if (!lua_isnil(L, -1)) {
            // Hit: return the cached module. Stack: [...][cache][module].
            lua_remove(L, -2);  // drop the cache table; keep the module
            return 1;
        }
        lua_pop(L, 1);  // drop the nil; keep the cache table on top
    } else {
        // Cache table missing (Install never ran). This is a setup bug —
        // surface it rather than silently mis-caching.
        return luaL_error(L,
            "kcdx require: module cache not initialized (Install was not "
            "called) while loading '%s'", modname);
    }
    // Stack now: [...][cache table].

    // Miss: resolve+compile the FILE from this plugin's folder.
    if (!ResolveAndCompile(L, modname)) {
        // Failure reason string is on top. Raise it as a Lua error so the
        // author sees WHERE/WHY (errors-teach), naming the module.
        const char* reason = lua_tostring(L, -1);
        return luaL_error(L, "kcdx require: cannot load module '%s': %s",
                          modname, reason ? reason : "<no reason>");
    }
    // Stack: [...][cache table][chunk function].

    // (a.1) Set the kcdx fenv on the helper chunk BEFORE running it, so the
    // helper's OWN bare requires (nested) route back to kcdx too.
    SetKcdxEnvOnTopInternal(L);

    // Run the chunk: module = chunk(modname). Lua's module convention
    // passes the module name as the single arg (vendor/lua/loadlib.c:481).
    // The load is synchronous and main-thread-only, so OwnerScope stays
    // correct across this nested call.
    lua_pushstring(L, modname);
    lua_call(L, 1, 1);  // -> module result (or nil if the chunk returned nothing)
    // Stack: [...][cache table][module-or-nil].

    // A module that returns nothing yields nil here. Stock require stores
    // `true` in that case (loadlib.c:486-490) so a re-require is still a
    // cache hit rather than a reload. Mirror that semantics in the kcdx
    // cache: if the chunk returned nil, cache `true`. This keeps the
    // within-plugin cache-hit contract (a second require returns the same
    // value) for side-effect-only modules.
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);          // drop the nil
        lua_pushboolean(L, 1);  // cache value = true
    }
    // Stack: [...][cache table][module].

    // Store cache[key] = module, leaving the module as our return value.
    lua_pushvalue(L, -1);                  // [...][cache][module][module]
    lua_setfield(L, -3, key.c_str());      // cache[key] = module; pops one
    // Stack: [...][cache table][module].
    lua_remove(L, -2);                     // drop the cache table; keep module
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

    // (b) Create the namespaced module cache: a fresh GC-managed Lua table
    // pinned by a registry ref (NOT _LOADED, NOT a static-const sentinel —
    // use the live Lua C API). Survives the whole session, so a within-plugin
    // require across DIFFERENT load windows (e.g. cap-27's plugin.lua in
    // RunAll + after.lua in RunAfterEntrypoints both require "state") hits
    // the same cache slot and gets the SAME table.
    lua_newtable(L);
    g_cacheRef = luaL_ref(L, LUA_REGISTRYINDEX);  // pops the table, pins it

    // (a) Build the shared kcdx env table: a fresh table whose `require`
    // is the kcdx closure and whose __index falls through to _G for every
    // other global. One shared env reused as the fenv of every plugin
    // chunk (entrypoints + helpers). Pinned by a registry ref.
    //
    // __index -> _G means a plugin chunk reads all normal globals (string,
    // table, kcdx, ...) plus kcdx's require. We do NOT set __newindex: a
    // plugin chunk that writes a bare global (`myglobal = x`) writes into
    // this env table (visible to other plugin chunks), NOT into _G. Writing
    // a bare global from plugin.lua is discouraged (one-global-`kcdx` rule,
    // the one-global-`kcdx` rule); no plugin or test relies on a plugin chunk
    // mutating _G, so the write lands on the kcdx env (not silently
    // swallowed) and the chunk still reads it back. (cap-27 / cap-25 share
    // cross-file state through a require'd module + locals, never _G.)
    lua_newtable(L);                              // the env table
    lua_pushcfunction(L, &KcdxRequire);           // raw C API; no sentinel
    lua_setfield(L, -2, "require");               // env.require = kcdx closure

    lua_newtable(L);                              // its metatable
    lua_pushvalue(L, LUA_GLOBALSINDEX);           // push _G
    lua_setfield(L, -2, "__index");               // mt.__index = _G
    lua_setmetatable(L, -2);                      // setmetatable(env, mt); pops mt

    g_envRef = luaL_ref(L, LUA_REGISTRYINDEX);    // pops the env, pins it

    lua_settop(L, top);
    log::InfoF("lua_require_searcher: kcdx plugin-scoped require installed "
               "(per-chunk fenv + namespaced cache; _LOADED bypassed for "
               "plugin chunks)");
}

void SetKcdxEnvOnChunkAtTop(lua_State* L) {
    if (!L) return;
    SetKcdxEnvOnTopInternal(L);
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
