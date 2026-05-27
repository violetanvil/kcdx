#pragma once

// kcdx::lua_lifecycle — the kcdx.on lifecycle-event bridge.
//
// An earlier step built the kcdx.on(name, fn) verb + the "ready"
// per-plugin post-apply signal (which lives in lua_registry, fired from
// inside ApplyZone). This module adds the OTHER kcdx.on names: the 9
// game-lifecycle events that mirror the existing engine kcdxMessage_*
// catalog.
//
// It is a PURE BRIDGE. The kcdxMessage_* lifecycle messages already fire
// (C++ plugins subscribe to them via the messaging interface today). This
// module:
//   1. Stores Lua callbacks per event NAME (RegisterLifecycleCallback),
//      each a luaL_ref into LUA_REGISTRYINDEX (GC-safe), attributed to the
//      owning plugin for log lines.
//   2. Fans a fired kcdxMessage_* out to that name's subscribers
//      (FireLifecycle), each via lua_pcall — a throwing callback logs
//      loud (with the plugin name) and does NOT abort the others or the
//      engine, mirroring lua_registry::FireReadyForZone.
//
// Placement: a sibling TU (not lua_registry) because lua_registry is the
// deferred-apply queue + handle metatable (already ~525 lines) and its
// "ready" dispatch is coupled INTO ApplyZone. The lifecycle bridge is
// fired from an engine messaging listener instead — no coupling to the
// apply queue — so co-locating would mix two unrelated dispatch concerns
// in an already-oversized file (one file, one concern).
//
// Threading: FireLifecycle is invoked only from the engine messaging
// listener, which runs on the main thread for every one of the bridged
// messages (the update tick for InputLoaded/NewGame; the load wave for
// PostLoad/PostPostLoad; the main save/load thread for the save/load
// events — the same path CAP-08 + CAP-12's Lua-adjacent cosave work
// already run on safely). Main-thread-only contract upheld.

#include <string>

extern "C" {
#include "lua.h"
}

namespace kcdx::lua_lifecycle {

// Is `eventName` one of the 9 bridged lifecycle events? The kcdx.on binder
// calls this to decide whether to route a name here (vs the "ready" branch
// in lua_registry, vs the unknown-event teaching error). Does NOT include
// "ready" (that stays in lua_registry).
bool IsLifecycleEvent(const std::string& eventName);

// A comma-separated list of the bridged event names, for the kcdx.on
// unknown-event teaching error (so the message can name what IS available).
const char* EventNameList();

// Register a Lua callback for a lifecycle event. `callbackRef` is a
// luaL_ref into LUA_REGISTRYINDEX (the kcdx.on binder takes it, exactly as
// the "ready" path does). `pluginName` is the attributed owner ("" for
// anonymous callers) — used only for log lines. `eventName` must be one of
// the 9 (IsLifecycleEvent true); callers validate first.
//
// Callbacks accumulate per event name and fire in LOAD-ORDER PRIORITY
// (priority asc, with registration order as the tiebreak) — the owning
// plugin's effective priority is captured here at registration via
// load_order::Of. They fire EVERY time the mapped message fires (NOT once —
// unlike "ready").
// Refs are NOT released after firing: a lifecycle subscription is durable
// for the process lifetime (the message can fire again).
void RegisterLifecycleCallback(const std::string& eventName,
                               const std::string& pluginName,
                               int callbackRef);

// --- Custom-event pub/sub (kcdx.publish) ---
//
// kcdx.publish is a Lua-NATIVE layer SHARING this same g_subscribers
// registry: a custom event is just another event NAME, keyed by the full
// "<publisher-plugin>:<event>" string. The kcdx.on binder routes any name
// containing ':' (a cross-plugin "plugin:event" subscription) here via
// RegisterCustomCallback; kcdx.publish stamps the publisher's namespace and
// fans out via FirePublish. This is NOT the C++ kcdxMessage wire format — a
// Lua table/value rides the Lua stack by reference, never serialized to
// bytes (design lock).
//
// Register a Lua callback for a custom (cross-plugin) event. `eventName` is
// the FULL "plugin:event" key (already validated to contain ':' by the
// binder). Same durable-ref + plugin-attribution semantics as
// RegisterLifecycleCallback — they share g_subscribers, so the only
// difference from a lifecycle subscription is the key shape and the fire
// path (FirePublish, which passes an arbitrary Lua value vs a basename
// string). Refs are never released (a custom event can fire repeatedly).
void RegisterCustomCallback(const std::string& eventName,
                            const std::string& pluginName,
                            int callbackRef);

// Fire every callback registered for the custom event `fullEventName`
// ("<publisher>:<event>"), in LOAD-ORDER PRIORITY (priority asc, registration
// order as the tiebreak), against the live state `L`. `payloadIdx` is the
// absolute stack index (1-based) of the payload
// value to pass to each subscriber, or 0 to fire with NO argument.
//
// PAYLOAD-BY-REFERENCE: the payload is NOT copied or
// serialized. For each subscriber we lua_pushvalue(payloadIdx) — a fresh
// stack reference to the SAME underlying Lua value — and pcall(cb, 1). A
// table is therefore shared by reference across all subscribers and the
// publisher (publish-immutable-by-convention; kcdx does NOT deep-copy). The
// payload lives on the publish caller's stack for the whole loop, so
// payloadIdx stays valid across iterations (we push a fresh copy per
// subscriber and the pcall consumes it).
//
// Each callback runs under lua_pcall; a throw is logged with the
// subscriber's plugin name and does NOT abort the remaining callbacks
// (mirrors FireLifecycle / FireReadyForZone). Main-thread-only: publish runs
// from plugin.lua / a kcdx.on callback, both main-thread.
//
// Returns the number of subscribers fired (0 if none registered).
int FirePublish(const std::string& fullEventName, lua_State* L, int payloadIdx);

// Fire every callback registered for `eventName`, in LOAD-ORDER PRIORITY
// (priority asc, registration order as the tiebreak). Called from the engine
// messaging bridge listener when a mapped kcdxMessage_* fires.
//
// `basename` is the save basename for the save/load events
// (save_game / load_game_selected / delete_game) — pushed as the
// callback's single Lua string arg (a COPY; the engine owns the const
// char* only for the dispatch duration, so we never retain it). For the
// other 6 events `basename` is nullptr and the callback fires with NO
// args. (Note: at the SaveGame/LoadGameSelected fire sites the engine
// passes a real basename; the DeleteGame fire site currently passes none —
// we degrade to no-arg when basename is null, never pushing a NULL string
// to Lua.)
//
// Each callback runs under lua_pcall; a throw is logged with the plugin
// name and does NOT abort the remaining callbacks or the engine (mirrors
// FireReadyForZone). Fires against the live lua_State (scripting::lua_state).
void FireLifecycle(const std::string& eventName, const char* basename);

// The engine-internal bridge entry point. Called from
// messaging::FireEngineMessage for EVERY engine message (the same direct
// dispatch path serialization::OnEngineMessage uses — engine-internal
// subsystems can't register as plugin listeners, which require a valid
// PluginHandle). Maps the kcdxMessage_* type to its kcdx.on name and fans
// it out via FireLifecycle, extracting the save basename from `data` for
// the save/load events. A messageType with no kcdx.on mapping (e.g.
// kcdxMessage_LuaReady, or any plugin-defined >= 0x10000) is ignored.
//
// `data` / `dataLen` are the kcdxMessage payload as passed to
// FireEngineMessage (for save/load events, `data` is the const char*
// basename; we treat it as a NUL-terminated C string only when present).
void OnEngineMessage(uint32_t messageType, const void* data, uint32_t dataLen);

}  // namespace kcdx::lua_lifecycle
