// kcdx.on(event, fn) — Lua-side lifecycle / event subscription.
//
// Phase 2b sub-7 of the manifest-only restructure (see
// docs/outstanding-work/ready-event-and-handle-assert.md). A core
// authoring verb per .claude/rules/lua-api-surface.md: top-level (like
// kcdx.hook), positional "do a thing" args (event, fn).
//
//   kcdx.on("ready", function()
//       -- runs once, after THIS plugin's hooks/bytes are applied.
//       -- Every handle the plugin captured now has a final
//       -- :applied() / :reason() — the post-apply moment a plugin
//       -- cannot observe in straight-line plugin.lua.
//       assert(my_hook:applied() == true)
//   end)
//
// SCOPE (this sub): the kcdx.on binder + the "ready" event ONLY. "ready"
// is the post-apply callback — it fires after lua_registry::ApplyZone
// has transitioned every entry in the plugin's zone, so handle:applied()
// is final. The full lifecycle-event bridge (input_loaded, save_game,
// ...) and kcdx.publish pub/sub are SEPARATE follow-on subs; any other
// event string returns (nil, teaching error) here.
//
// The "ready" callback takes NO arguments: it is a "your zone is applied
// now" signal. The plugin reads its OWN captured handles inside it. It
// runs once, per-plugin, after that plugin's zone apply pass completes.

#include "lua_bind_on.h"

#include <string>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include "log.h"
#include "lua_registry.h"

namespace kcdx::lua_bind_on {

namespace {

// kcdx.on(event, fn)
//
//   event (string)   : the lifecycle event to subscribe to. Only "ready"
//                      is wired today.
//   fn    (function) : the callback. For "ready" it takes no arguments.
//
// On success returns nothing (the subscription is registered). On a bad
// argument or an unknown event, returns (nil, teaching error) per the
// kcdx-binder error convention (lua-api-surface.md §"errors teach").
int Lua_On(lua_State* L) {
    // --- Validate `event` is a string ---
    if (lua_type(L, 1) != LUA_TSTRING) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.on(event, fn): `event` must be a string — the lifecycle "
            "event name (e.g. \"ready\"). Call shape: "
            "kcdx.on(\"ready\", function() ... end)");
        return 2;
    }
    std::string event = lua_tostring(L, 1);

    // --- Validate `fn` is a function ---
    if (!lua_isfunction(L, 2)) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.on(\"%s\", fn): `fn` must be a function — the callback "
            "to run when the event fires. Call shape: kcdx.on(\"%s\", "
            "function() ... end)",
            event.c_str(), event.c_str());
        return 2;
    }

    if (event == "ready") {
        // Attribute to the owning plugin the same way the hook binder
        // does (OwningPluginForCurrentCall walks the Lua callstack to the
        // plugin.lua source). Anonymous callers (console, pak Lua) get ""
        // → fired in the after_game zone, matching how ApplyZone defaults
        // anonymous ENTRIES.
        std::string callSiteFile;
        int callSiteLine = 0;
        std::string pluginName =
            kcdx::lua_registry::OwningPluginForCurrentCall(
                L, callSiteFile, callSiteLine);

        // Take a GC-safe registry ref to `fn` so the closure survives
        // until the ready dispatch (which runs after plugin.lua returns).
        // luaL_ref pops the value off the stack — push a copy first so we
        // don't disturb the caller's args.
        lua_pushvalue(L, 2);
        int ref = luaL_ref(L, LUA_REGISTRYINDEX);
        if (ref == LUA_NOREF || ref == LUA_REFNIL) {
            lua_pushnil(L);
            lua_pushstring(L,
                "kcdx.on(\"ready\", fn): internal error — failed to "
                "retain the callback (see kcdx.log)");
            return 2;
        }

        kcdx::lua_registry::RegisterReadyCallback(pluginName, ref);
        log::InfoF("kcdx.on: registered \"ready\" callback for plugin='%s' "
                   "site=%s:%d (ref=%d)",
                   pluginName.empty() ? "<anon>" : pluginName.c_str(),
                   callSiteFile.empty() ? "?" : callSiteFile.c_str(),
                   callSiteLine, ref);
        return 0;
    }

    // Any other event string: not wired yet. Teach what IS available and
    // what is coming, naming "ready" as the only wired event.
    lua_pushnil(L);
    lua_pushfstring(L,
        "kcdx.on: event \"%s\" is not available yet — only \"ready\" is "
        "wired today (fires after your hooks/bytes are applied, so "
        "handle:applied() is final). Lifecycle events (input_loaded, "
        "save_game, ...) and custom events arrive in a later kcdx version.",
        event.c_str());
    return 2;
}

}  // namespace

void bind(lua_State* L) {
    // kcdx.on is a TOP-LEVEL verb (like kcdx.hook), NOT a sub-table — the
    // kcdx table is at the top of the stack; register the function
    // directly on it.
    int kcdx_idx = lua_gettop(L);
    lua_pushcfunction(L, Lua_On);
    lua_setfield(L, kcdx_idx, "on");
}

}  // namespace kcdx::lua_bind_on
