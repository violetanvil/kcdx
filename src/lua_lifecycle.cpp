// kcdx::lua_lifecycle — see header for the design contract.

#include "lua_lifecycle.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" {
#include "lauxlib.h"
}

#include "kcdx/Interfaces.h"  // kcdxMessageType enum
#include "load_order.h"       // load_order::Of — owning plugin's effective
                              // priority, captured per-callback at register
#include "log.h"
#include "scripting.h"   // scripting::lua_state() — the live VM the
                         // lifecycle callbacks fire against (same source
                         // the "ready" dispatch in lua_registry uses).

namespace kcdx::lua_lifecycle {

namespace {

// The 9 bridged events. The set is CLOSED (per the locked sub-8 design):
// each maps to an existing engine kcdxMessage_*. "ready" is intentionally
// absent — it stays a per-plugin post-apply signal in lua_registry.
// kcdxMessage_LuaReady (9) is also absent — it's an internal pak-Lua
// signal, not a kcdx.on name.
//
// Comma-joined name list for the unknown-event teaching error. Keep in
// sync with kEventNames below.
constexpr const char* kEventNameList =
    "post_load, post_post_load, input_loaded, new_game, pre_load_game, "
    "post_load_game, save_game, load_game_selected, delete_game";

bool IsKnownEvent(const std::string& e) {
    return e == "post_load" || e == "post_post_load" ||
           e == "input_loaded" || e == "new_game" || e == "pre_load_game" ||
           e == "post_load_game" || e == "save_game" ||
           e == "load_game_selected" || e == "delete_game";
}

// One registered lifecycle callback.
struct Callback {
    std::string pluginName;  // attributed owner ("" = anonymous), for logs
    int         ref;         // luaL_ref into LUA_REGISTRYINDEX
    int         priority;    // owning plugin's load-order priority (0..100),
                             // captured at registration via load_order::Of.
                             // Fire order is (priority asc, registration asc):
                             // load order decides who fires first.
};

// Subscribers keyed by event NAME. A plugin (or several) may register more
// than one callback for an event; they accumulate here in registration
// (push_back) order. FIRE order is computed at fire time — both fire
// functions stable_sort a COPY by (priority asc, registration asc) so
// callbacks fire in load-order priority (load order decides who fires
// first; the stored vector order is irrelevant once we sort on fire).
// Durable — refs are never released (the message fires repeatedly).
std::unordered_map<std::string, std::vector<Callback>> g_subscribers;

// Guards g_subscribers. RegisterLifecycleCallback runs at plugin.lua time;
// FireLifecycle runs from the engine listener — both main-thread, but the
// mutex keeps the snapshot-then-fire discipline simple and matches
// messaging.cpp / lua_registry's coarse-lock model.
std::mutex g_mu;

}  // namespace

bool IsLifecycleEvent(const std::string& eventName) {
    return IsKnownEvent(eventName);
}

const char* EventNameList() {
    return kEventNameList;
}

void RegisterLifecycleCallback(const std::string& eventName,
                               const std::string& pluginName,
                               int callbackRef) {
    // Capture the owning plugin's load-order priority at REGISTRATION time.
    // load_order::Resolve() runs inside LoadAllConfigs (DllMain), well before
    // RunAll executes any plugin.lua / kcdx.on registration on the first
    // update tick — so Of() returns the real Effective row here, not the
    // priority=50 default. An anonymous ("") owner correctly maps to the
    // default Effective (priority 50). Stored on the Callback for fire-order.
    int priority = kcdx::load_order::Of(pluginName).priority;
    std::lock_guard<std::mutex> lock(g_mu);
    g_subscribers[eventName].push_back(
        Callback{pluginName, callbackRef, priority});
}

void RegisterCustomCallback(const std::string& eventName,
                            const std::string& pluginName,
                            int callbackRef) {
    // Custom (cross-plugin) events share g_subscribers with the lifecycle
    // events — the registry keys by an arbitrary string and never validates
    // against the 9 (IsKnownEvent is only consulted by the kcdx.on binder),
    // so a "<publisher>:<event>" key slots in alongside lifecycle keys with
    // no relaxing required.
    // Same load-order priority capture as RegisterLifecycleCallback (see the
    // ordering note there): Of() is populated by Resolve() before any
    // registration runs. Custom events share g_subscribers, so they carry the
    // same priority key and fire in the same (priority asc, registration asc)
    // order as lifecycle callbacks.
    int priority = kcdx::load_order::Of(pluginName).priority;
    std::lock_guard<std::mutex> lock(g_mu);
    g_subscribers[eventName].push_back(
        Callback{pluginName, callbackRef, priority});
}

int FirePublish(const std::string& fullEventName, lua_State* L,
                int payloadIdx) {
    // Snapshot subscribers under the lock, fire without it held (same
    // re-entrancy discipline as FireLifecycle).
    std::vector<Callback> toFire;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        auto it = g_subscribers.find(fullEventName);
        if (it != g_subscribers.end()) {
            toFire = it->second;  // copy
        }
    }
    if (toFire.empty()) return 0;

    // Fire in load-order priority: (priority asc, registration asc). The
    // engine sorts plugins "(zone asc, priority asc, name asc)" (load_order.h)
    // — lower priority = earlier — so subscribers fire in that same order.
    // std::stable_sort keeps the original (registration / push_back) order for
    // equal priorities, which IS the tiebreak (no separate sequence field
    // needed). We sort the COPY (toFire), never g_subscribers — stored order
    // is irrelevant once we sort on fire.
    std::stable_sort(toFire.begin(), toFire.end(),
                     [](const Callback& a, const Callback& b) {
                         return a.priority < b.priority;
                     });

    if (!L) {
        log::ErrorF("lua_lifecycle: publish \"%s\" fired but no live "
                    "lua_State; dropping %zu callback(s)",
                    fullEventName.c_str(), toFire.size());
        return 0;
    }

    int fired = 0;
    for (const Callback& cb : toFire) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, cb.ref);
        if (!lua_isfunction(L, -1)) {
            lua_pop(L, 1);  // not a function (shouldn't happen) — discard
            continue;
        }
        // PAYLOAD-BY-REFERENCE: push a fresh stack reference to the SAME
        // underlying value (lua_pushvalue copies the stack slot, not the
        // GC object) so each subscriber gets the identical table/value. The
        // payload lives on the publish caller's stack across the whole loop,
        // so payloadIdx stays valid each iteration; the pcall consumes the
        // copy we just pushed. payloadIdx == 0 → fire with no argument.
        int nargs = 0;
        if (payloadIdx != 0) {
            lua_pushvalue(L, payloadIdx);
            nargs = 1;
        }
        int status = lua_pcall(L, nargs, 0, 0);
        if (status != 0) {
            const char* msg = lua_tostring(L, -1);
            log::ErrorF("lua_lifecycle: publish \"%s\" callback for plugin "
                        "'%s' threw: %s",
                        fullEventName.c_str(),
                        cb.pluginName.empty() ? "<anon>" : cb.pluginName.c_str(),
                        msg ? msg : "(no message)");
            lua_pop(L, 1);  // pop the error message
        }
        ++fired;
    }
    return fired;
}

void FireLifecycle(const std::string& eventName, const char* basename) {
    // Snapshot the subscribers for this event under the lock, then fire
    // without the lock held — a callback that re-enters (calls kcdx.on, or
    // anything that touches g_subscribers) can't deadlock or observe a
    // half-mutated list. We copy out (plugin, ref) pairs; refs stay
    // registered (durable — the message can fire again).
    std::vector<Callback> toFire;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        auto it = g_subscribers.find(eventName);
        if (it != g_subscribers.end()) {
            toFire = it->second;  // copy
        }
    }
    if (toFire.empty()) return;

    // Fire in load-order priority: (priority asc, registration asc). Same
    // sort as FirePublish (see the note there) — lower priority fires first,
    // matching the engine's "(zone asc, priority asc, name asc)" plugin sort.
    // stable_sort preserves registration order for equal priorities (the
    // tiebreak). Sorted on the COPY; g_subscribers is untouched.
    std::stable_sort(toFire.begin(), toFire.end(),
                     [](const Callback& a, const Callback& b) {
                         return a.priority < b.priority;
                     });

    lua_State* L = kcdx::scripting::lua_state();
    if (!L) {
        // No live VM — shouldn't happen for these messages (they all fire
        // after RegisterKcdxTable on the first update tick), but log loudly
        // rather than touch a null state.
        log::ErrorF("lua_lifecycle: \"%s\" fired but no live lua_State; "
                    "dropping %zu callback(s)",
                    eventName.c_str(), toFire.size());
        return;
    }

    for (const Callback& cb : toFire) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, cb.ref);
        if (!lua_isfunction(L, -1)) {
            lua_pop(L, 1);  // not a function (shouldn't happen) — discard
            continue;
        }
        // Save/load events carry a basename arg; the rest fire with none.
        // Push a COPY of the engine-owned const char* (lua_pushstring
        // copies into the VM) — we never retain the pointer. A null
        // basename (e.g. the current DeleteGame fire site) degrades to the
        // no-arg call; we never push a NULL string to Lua.
        int nargs = 0;
        if (basename != nullptr) {
            lua_pushstring(L, basename);
            nargs = 1;
        }
        int status = lua_pcall(L, nargs, 0, 0);
        if (status != 0) {
            const char* msg = lua_tostring(L, -1);
            log::ErrorF("lua_lifecycle: \"%s\" callback for plugin '%s' "
                        "threw: %s",
                        eventName.c_str(),
                        cb.pluginName.empty() ? "<anon>" : cb.pluginName.c_str(),
                        msg ? msg : "(no message)");
            lua_pop(L, 1);  // pop the error message
        }
    }
}

void OnEngineMessage(uint32_t messageType, const void* data,
                     uint32_t /*dataLen*/) {
    // Map the engine message to its kcdx.on name. The 3 save/load events
    // carry a basename in `data`; the rest fire with no arg. Anything
    // without a mapping (kcdxMessage_LuaReady (9), plugin-defined types)
    // is silently ignored — kcdx.on does not expose those.
    const char* name = nullptr;
    bool carriesBasename = false;
    switch (messageType) {
        case kcdxMessage_PostLoad:        name = "post_load";          break;
        case kcdxMessage_PostPostLoad:    name = "post_post_load";     break;
        case kcdxMessage_InputLoaded:     name = "input_loaded";       break;
        case kcdxMessage_NewGame:         name = "new_game";           break;
        case kcdxMessage_PreLoadGame:     name = "pre_load_game";      break;
        case kcdxMessage_PostLoadGame:    name = "post_load_game";     break;
        case kcdxMessage_SaveGame:        name = "save_game";
                                          carriesBasename = true;      break;
        case kcdxMessage_DeleteGame:      name = "delete_game";
                                          carriesBasename = true;      break;
        case kcdxMessage_LoadGameSelected: name = "load_game_selected";
                                          carriesBasename = true;      break;
        default:                          return;  // no kcdx.on mapping
    }

    // The basename is the const char* in `data` for the save/load events.
    // FireLifecycle copies it (lua_pushstring) and never retains it. A null
    // basename degrades to the no-arg call (the current DeleteGame fire
    // site passes none).
    const char* basename =
        carriesBasename ? static_cast<const char*>(data) : nullptr;
    FireLifecycle(name, basename);
}

}  // namespace kcdx::lua_lifecycle
