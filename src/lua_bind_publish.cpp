// kcdx.publish(event, payload) — Lua-side cross-plugin pub/sub broadcast.
//
// Phase 2b sub-9 of the manifest-only restructure — the last kcdx.on-series
// item. A core authoring verb per .claude/rules/lua-api-surface.md:
// top-level (like kcdx.on / kcdx.hook), positional "do a thing" args
// (event, payload). The broadcast counterpart to kcdx.on subscription.
//
//   -- plugin "violetanvil" broadcasts a custom event:
//   kcdx.publish("outfit_changed", { slot = 2, name = "Noble" })
//
//   -- another plugin hears it (events are stamped with the publisher's
//   -- name, so subscribers use the "<publisher>:<event>" form):
//   kcdx.on("violetanvil:outfit_changed", function(payload)
//       kcdx.log.info("MOD", "outfit -> %s", payload.name)
//   end)
//
// DESIGN LOCKS (sub-9):
//   * Lua-NATIVE layer — NOT the C++ kcdxMessage wire format. A Lua table
//     has no byte form, so it cannot ride the messaging Thunk_Dispatch path
//     (const void* + uint32_t). kcdx.publish shares the kcdx.on subscriber
//     registry (lua_lifecycle's g_subscribers): a custom event is just more
//     event names in the same registry.
//   * PAYLOAD BY REFERENCE (fork 1): the payload Lua value is passed
//     DIRECTLY to each subscriber's pcall via lua_pushvalue — no copy, no
//     serialize. A table is shared by reference (mutation is visible to
//     other subscribers + the publisher): publish-immutable-by-convention,
//     the Lua norm. payload is OPTIONAL — kcdx.publish("evt") fires
//     subscribers with no arg.
//   * NAMESPACING (fork 2): bare is MINE, prefix is THEIRS. The publisher
//     names the BARE event; the engine prepends the owning plugin →
//     "<publisher>:<event>". A subscriber hears it via
//     kcdx.on("<publisher>:<event>", fn). The sender string IS the
//     reference (no opaque reference-ID layer).
//   * CALLER IDENTITY: the publishing plugin is resolved via
//     lua_registry::OwningPluginForCurrentCall — the same mechanism kcdx.on
//     (ready + lifecycle) uses. An anonymous publisher (resolves to "") is
//     NOT dropped: we warn + fire under "<anon>:<event>" so the broadcast
//     is observable (a subscriber can kcdx.on("<anon>:event", fn) — useful
//     for console / pak Lua and for surfacing a RegisterScriptOwner gap).
//
// Threading: publish runs from plugin.lua or a kcdx.on callback, both
// main-thread — the payload value rides the publish caller's Lua stack and
// FirePublish fires each subscriber on that same thread (AP6 ok). No stored
// sentinel (AP5): the payload is a stack value passed by lua_pushvalue.

#include "lua_bind_publish.h"

#include <string>

extern "C" {
#include "lua.h"
}

#include "log.h"
#include "lua_lifecycle.h"
#include "lua_registry.h"

namespace kcdx::lua_bind_publish {

namespace {

// kcdx.publish(event [, payload])
//
//   event   (string)   : the BARE custom-event name (no namespace prefix —
//                        the engine stamps the publisher's plugin name).
//   payload (any, opt.) : any Lua value (table/string/number/nested),
//                        passed BY REFERENCE to each subscriber. Omit it to
//                        fire subscribers with no argument.
//
// On a bad `event` returns (nil, teaching error) per the kcdx-binder error
// convention. On success returns the number of subscribers fired (an
// integer) — handy for a publisher that wants to know if anyone listened
// (0 = no subscribers, not an error).
int Lua_Publish(lua_State* L) {
    // --- Validate `event` is a string ---
    if (lua_type(L, 1) != LUA_TSTRING) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.publish(event, payload): `event` must be a string — the "
            "custom event name to broadcast (e.g. \"outfit_changed\"). Call "
            "shape: kcdx.publish(\"my_event\", { x = 1 }). Subscribers hear "
            "it via kcdx.on(\"<your_plugin>:my_event\", fn).");
        return 2;
    }
    std::string bareEvent = lua_tostring(L, 1);

    // Resolve the publishing plugin (same mechanism as kcdx.on ready +
    // lifecycle). Anonymous (console / pak Lua / an un-attributed require'd
    // module) resolves to "" — we do NOT drop it: warn + fire under
    // "<anon>:<event>" so the broadcast stays observable.
    std::string callSiteFile;
    int callSiteLine = 0;
    std::string publisher = kcdx::lua_registry::OwningPluginForCurrentCall(
        L, callSiteFile, callSiteLine);

    std::string nsLabel = publisher.empty() ? "<anon>" : publisher;
    std::string fullEvent = nsLabel + ":" + bareEvent;
    if (publisher.empty()) {
        log::WarnF("kcdx.publish: anonymous publisher (no attributed plugin) "
                   "for event \"%s\" at site=%s:%d — firing under \"%s\". "
                   "Subscribers hear it via kcdx.on(\"%s\", fn).",
                   bareEvent.c_str(),
                   callSiteFile.empty() ? "?" : callSiteFile.c_str(),
                   callSiteLine, fullEvent.c_str(), fullEvent.c_str());
    }

    // The payload (if any) is at stack index 2. payloadIdx 0 = no arg.
    // lua_gettop(L) < 2 means the caller passed only the event; an explicit
    // nil (LUA_TNIL at index 2) is a real argument — fire with nil so the
    // subscriber's single param is nil (matches "nil-arg is fine").
    int payloadIdx = (lua_gettop(L) >= 2) ? 2 : 0;

    // Fire against the CALLING state `L`: the payload value lives on this
    // call's Lua stack, and KCD2 runs a single shared lua_State (the binder
    // is invoked on the same VM scripting::lua_state() returns), so the
    // subscriber refs in LUA_REGISTRYINDEX resolve here too. We must pass
    // the state that owns the payload slot, not re-fetch a (same) handle.
    int fired = kcdx::lua_lifecycle::FirePublish(fullEvent, L, payloadIdx);

    log::InfoF("kcdx.publish: \"%s\" by plugin='%s' site=%s:%d -> %d "
               "subscriber(s)",
               fullEvent.c_str(), nsLabel.c_str(),
               callSiteFile.empty() ? "?" : callSiteFile.c_str(),
               callSiteLine, fired);

    lua_pushinteger(L, fired);
    return 1;
}

}  // namespace

void bind(lua_State* L) {
    // kcdx.publish is a TOP-LEVEL verb (like kcdx.on / kcdx.hook), NOT a
    // sub-table — the kcdx table is at the top of the stack; register the
    // function directly on it.
    int kcdx_idx = lua_gettop(L);
    lua_pushcfunction(L, Lua_Publish);
    lua_setfield(L, kcdx_idx, "publish");
}

}  // namespace kcdx::lua_bind_publish
