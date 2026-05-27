// kcdx.on(event, fn) — Lua-side lifecycle / event subscription.
//
// Part of the manifest-only restructure. A core
// authoring verb: top-level (like
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
// SCOPE: the kcdx.on binder + the "ready" event + the 9
// game-lifecycle events + custom cross-plugin events. Any
// name containing '.' is a qualified "<author>.<plugin>.<event>" (or the
// legacy 2-segment "<plugin>.<event>" while a publisher's [plugin].author
// is still empty during the corpus transition) subscription to a
// kcdx.publish event, routed to the shared lua_lifecycle registry. The
// legacy colon form "<plugin>:<event>" is rejected with a teaching error
// pointing at the new dot form; a bare non-lifecycle name returns
// (nil, teaching error) here.
//
// "ready" is the post-apply callback — it fires after
// lua_registry::ApplyZone has transitioned every entry in the plugin's
// zone, so handle:applied() is final. It takes NO arguments (a "your zone
// is applied now" signal — the plugin reads its OWN captured handles
// inside it), runs ONCE, per-plugin, after that plugin's zone apply pass.
//
// The 9 lifecycle events (post_load, post_post_load, input_loaded,
// new_game, pre_load_game, post_load_game, save_game, load_game_selected,
// delete_game) are a PURE BRIDGE over the existing engine kcdxMessage_*
// catalog (see lua_lifecycle.{h,cpp}). They fire EVERY time the mapped
// message fires (e.g. save_game on every save). The 3 save/load events
// (save_game, load_game_selected, delete_game) pass the save basename
// (e.g. "save561.whs") as the callback's single arg; the other 6 fire
// with no args.

#include "lua_bind_on.h"

#include <string>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include "log.h"
#include "lua_lifecycle.h"
#include "lua_registry.h"

namespace kcdx::lua_bind_on {

namespace {

// kcdx.on(event, fn)
//
//   event (string)   : the lifecycle event to subscribe to — "ready" or
//                      one of the 9 game-lifecycle events.
//   fn    (function) : the callback. "ready" + the 6 no-arg lifecycle
//                      events take no arguments; the 3 save/load events
//                      (save_game, load_game_selected, delete_game) get
//                      the save basename string.
//
// On success returns nothing (the subscription is registered). On a bad
// argument or an unknown event, returns (nil, teaching error) per the
// kcdx-binder error convention (errors teach, in the author's terms).
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
        // kcdx.on's lifecycle / ready paths attribute callbacks to the
        // owning plugin by [plugin].name only; the author component is
        // not consulted here, so we read only `.plugin` off the owner
        // struct. (The struct return is the single-stack-walk source of
        // truth — see lua_registry.h.)
        std::string pluginName =
            kcdx::lua_registry::OwningPluginForCurrentCall(
                L, callSiteFile, callSiteLine).plugin;

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

    // One of the 9 game-lifecycle events: a pure bridge over the engine's
    // existing kcdxMessage_* catalog (lua_lifecycle.{h,cpp}). Same
    // attribution + GC-safe ref pattern as the "ready" branch above; the
    // difference is the callback fires EVERY time the mapped message fires
    // (registered in a durable per-name registry, not the one-shot ready
    // map).
    if (kcdx::lua_lifecycle::IsLifecycleEvent(event)) {
        std::string callSiteFile;
        int callSiteLine = 0;
        std::string pluginName =
            kcdx::lua_registry::OwningPluginForCurrentCall(
                L, callSiteFile, callSiteLine).plugin;

        // luaL_ref pops the value — push a copy first so the caller's args
        // aren't disturbed (same as the "ready" path).
        lua_pushvalue(L, 2);
        int ref = luaL_ref(L, LUA_REGISTRYINDEX);
        if (ref == LUA_NOREF || ref == LUA_REFNIL) {
            lua_pushnil(L);
            lua_pushfstring(L,
                "kcdx.on(\"%s\", fn): internal error — failed to retain "
                "the callback (see kcdx.log)",
                event.c_str());
            return 2;
        }

        kcdx::lua_lifecycle::RegisterLifecycleCallback(event, pluginName, ref);
        log::InfoF("kcdx.on: registered \"%s\" callback for plugin='%s' "
                   "site=%s:%d (ref=%d)",
                   event.c_str(),
                   pluginName.empty() ? "<anon>" : pluginName.c_str(),
                   callSiteFile.empty() ? "?" : callSiteFile.c_str(),
                   callSiteLine, ref);
        return 0;
    }

    // Legacy colon form — rejected with a teaching error pointing at the
    // canonical dot. Prior to the 2-dot namespace refactor, kcdx.publish
    // stamped events as "<publisher>:<event>" and subscribers wrote that
    // same colon string. The canonical separator for every shared name is
    // now the dot; the colon form is no longer
    // accepted. We catch it explicitly so authors mid-migration get one
    // teaching error rather than a silent "no subscribers fired."
    if (event.find(':') != std::string::npos) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.on(\"%s\", fn): the \"<publisher>:<event>\" colon form is "
            "no longer accepted — use the canonical dot form "
            "\"<author>.<plugin>.<event>\" instead (e.g. "
            "\"walkabout.violetanvil.outfit_changed\"). The publisher in "
            "kcdx.publish stamps the dot form automatically; subscribers "
            "subscribe to the same dot string. The dot is the canonical "
            "namespace separator.",
            event.c_str());
        return 2;
    }

    // A custom cross-plugin event: any name containing '.' is a qualified
    // "<author>.<plugin>.<event>" (or the legacy 2-segment
    // "<plugin>.<event>" while a publisher's [plugin].author is still
    // empty during the corpus transition) subscription to another (or the
    // same) plugin's kcdx.publish event. It shares the
    // SAME lifecycle subscriber registry, keyed by the full dotted string,
    // with the same GC-safe luaL_ref + OwningPluginForCurrentCall
    // attribution as the lifecycle/ready branches. (A published event is
    // always stamped with the dot form, so a subscriber always uses the
    // dot form to hear it — a bare non-lifecycle name below stays a
    // teaching error.)
    if (event.find('.') != std::string::npos) {
        std::string callSiteFile;
        int callSiteLine = 0;
        std::string pluginName =
            kcdx::lua_registry::OwningPluginForCurrentCall(
                L, callSiteFile, callSiteLine).plugin;

        // luaL_ref pops the value — push a copy first so the caller's args
        // aren't disturbed (same as the lifecycle/ready paths).
        lua_pushvalue(L, 2);
        int ref = luaL_ref(L, LUA_REGISTRYINDEX);
        if (ref == LUA_NOREF || ref == LUA_REFNIL) {
            lua_pushnil(L);
            lua_pushfstring(L,
                "kcdx.on(\"%s\", fn): internal error — failed to retain "
                "the callback (see kcdx.log)",
                event.c_str());
            return 2;
        }

        kcdx::lua_lifecycle::RegisterCustomCallback(event, pluginName, ref);
        log::InfoF("kcdx.on: registered custom-event \"%s\" callback for "
                   "plugin='%s' site=%s:%d (ref=%d)",
                   event.c_str(),
                   pluginName.empty() ? "<anon>" : pluginName.c_str(),
                   callSiteFile.empty() ? "?" : callSiteFile.c_str(),
                   callSiteLine, ref);
        return 0;
    }

    // Any other event string: a bare non-lifecycle name. Custom events are
    // heard via the "<author>.<plugin>.<event>" dot form (above); teach
    // that, plus the full set of built-in names ("ready" + the 9 lifecycle
    // events).
    lua_pushnil(L);
    lua_pushfstring(L,
        "kcdx.on: event \"%s\" is not available. To hear another plugin's "
        "custom event, subscribe with "
        "kcdx.on(\"<author>.<plugin>.<event>\", fn) (the published event is "
        "stamped with its publisher's qualified <author>.<plugin> "
        "namespace, dot-separated). The built-in lifecycle events are: %s. "
        "\"ready\" fires after your hooks/bytes are applied, so "
        "handle:applied() is final.",
        event.c_str(), kcdx::lua_lifecycle::EventNameList());
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
