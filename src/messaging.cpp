#include "messaging.h"

#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "crash_guard.h"
#include "log.h"
#include "plugin_loader.h"
#include "serialization.h"
#include "test.h"

namespace kcdx::messaging {

namespace {

// One registered listener.
struct Listener {
    kcdxPluginHandle      handle;      // Which plugin registered this
    std::string           sender;      // Empty string == subscribe to engine (null sender);
                                       // non-empty == subscribe to messages from that name
    kcdxMessagingCallback callback;
};

// Process-wide listener registry. Mutex-protected because Dispatch may be
// called from any thread (e.g. a hook detour on the game's worker pool),
// and listeners are added/looked up from possibly-different threads.
std::mutex          g_mutex;
std::vector<Listener> g_listeners;

// Resolve a PluginHandle to its stable name. Used by Dispatch to fill the
// sender field of kcdxMessage. Returns null if the handle is invalid; the
// caller should refuse the dispatch in that case.
const char* HandleToName(kcdxPluginHandle h) {
    if (h == kcdxInvalidPluginHandle) return nullptr;
    for (const auto& p : plugins::g_plugins) {
        if (p.handle == h && !p.manifest.name.empty()) {
            return p.manifest.name.c_str();
        }
    }
    return nullptr;
}

bool Thunk_RegisterListener(kcdxPluginHandle listener,
                            const char* sender,
                            kcdxMessagingCallback callback) {
    if (!callback) {
        log::Warn("Messaging::RegisterListener: null callback");
        return false;
    }
    if (listener == kcdxInvalidPluginHandle) {
        log::Warn("Messaging::RegisterListener: invalid listener handle");
        return false;
    }
    // Validate that the listener handle actually corresponds to a loaded plugin.
    if (!HandleToName(listener)) {
        log::WarnF("Messaging::RegisterListener: unknown listener handle %u",
                   listener);
        return false;
    }

    Listener l;
    l.handle = listener;
    l.sender = (sender ? sender : "");
    l.callback = callback;

    std::lock_guard<std::mutex> lock(g_mutex);
    g_listeners.push_back(std::move(l));
    return true;
}

bool Thunk_Dispatch(kcdxPluginHandle sender,
                    uint32_t messageType,
                    const void* data,
                    uint32_t dataLen,
                    const char* receiver) {
    const char* senderName = HandleToName(sender);
    if (!senderName) {
        log::WarnF("Messaging::Dispatch: unknown sender handle %u", sender);
        return false;
    }

    // Snapshot listener list under the lock, then dispatch without the lock
    // held. Avoids re-entrancy issues if a callback calls back into
    // Dispatch or RegisterListener. Capture handle + name alongside the
    // callback so the crash-guard can name the offending plugin in the
    // fault log without needing a second lock-acquire after a SEH unwind.
    struct Target {
        kcdxMessagingCallback callback;
        kcdxPluginHandle      handle;
        std::string           name;  // copied under the lock — stable for use after
    };
    std::vector<Target> targets;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (const auto& l : g_listeners) {
            if (l.sender != senderName) continue;
            if (receiver) {
                const char* listenerName = HandleToName(l.handle);
                if (!listenerName || std::strcmp(listenerName, receiver) != 0) {
                    continue;
                }
            }
            Target t;
            t.callback = l.callback;
            t.handle   = l.handle;
            const char* nm = HandleToName(l.handle);
            if (nm) t.name = nm;
            targets.push_back(std::move(t));
        }
    }

    if (targets.empty()) return false;

    log::InfoF("messaging: dispatch from '%s' type=%u: %zu listener(s)",
               senderName, messageType, targets.size());

    kcdxMessage msg;
    msg.sender = senderName;
    msg.messageType = messageType;
    msg.data = data;
    msg.dataLen = dataLen;

    // Per-call closure that the guard invokes. The guard can't take a
    // lambda directly (it's a function pointer + userdata), so we pass
    // the kcdxMessage as the userdata payload.
    struct Ctx {
        kcdxMessagingCallback cb;
        kcdxMessage*          arg;
    };

    size_t okCount = 0;
    for (auto& t : targets) {
        kcdxMessage perCallback = msg;
        Ctx ctx{t.callback, &perCallback};
        bool ok = guard::Call(
            "messaging.dispatch",
            t.name.empty() ? nullptr : t.name.c_str(),
            [](void* ud) {
                Ctx* c = static_cast<Ctx*>(ud);
                c->cb(c->arg);
            },
            &ctx);
        if (ok) ++okCount;
        // Continue the broadcast even if one listener faulted — other
        // plugins shouldn't be silently denied a message because of a
        // peer's bug.
    }

    log::InfoF("messaging: dispatch from '%s' type=%u complete (%zu/%zu ok)",
               senderName, messageType, okCount, targets.size());
    return true;
}

const kcdxMessagingInterface g_iface = {
    /*RegisterListener=*/ Thunk_RegisterListener,
    /*Dispatch=*/         Thunk_Dispatch,
};

}  // namespace

const kcdxMessagingInterface* GetInterface() {
    return &g_iface;
}

void FireEngineMessage(uint32_t messageType,
                       const void* data,
                       uint32_t dataLen) {
    // Snapshot listeners subscribed to the engine (empty sender string).
    // Capture handle + name alongside callback so a fault gets named.
    struct Target {
        kcdxMessagingCallback callback;
        kcdxPluginHandle      handle;
        std::string           name;
    };
    std::vector<Target> targets;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (const auto& l : g_listeners) {
            if (!l.sender.empty()) continue;
            Target t;
            t.callback = l.callback;
            t.handle   = l.handle;
            const char* nm = HandleToName(l.handle);
            if (nm) t.name = nm;
            targets.push_back(std::move(t));
        }
    }

    log::InfoF("messaging: broadcast engine type=%u: %zu listener(s)",
               messageType, targets.size());

    if (!targets.empty()) {
        kcdxMessage msg;
        msg.sender = nullptr;     // engine-originated
        msg.messageType = messageType;
        msg.data = data;
        msg.dataLen = dataLen;

        struct Ctx {
            kcdxMessagingCallback cb;
            kcdxMessage*          arg;
        };

        size_t okCount = 0;
        for (auto& t : targets) {
            kcdxMessage perCallback = msg;
            Ctx ctx{t.callback, &perCallback};
            bool ok = guard::Call(
                "messaging.broadcast",
                t.name.empty() ? nullptr : t.name.c_str(),
                [](void* ud) {
                    Ctx* c = static_cast<Ctx*>(ud);
                    c->cb(c->arg);
                },
                &ctx);
            if (ok) ++okCount;
        }
        log::InfoF("messaging: broadcast engine type=%u complete (%zu/%zu ok)",
                   messageType, okCount, targets.size());
    }

    // Route to engine-internal serialization handler too. Engine-side
    // subsystems can't reasonably register as plugin listeners (no
    // PluginHandle), so this is the dispatch path for them. Guarded
    // separately under an "engine.serialization" site name.
    {
        struct EngCtx {
            kcdxMessage msg;
        };
        EngCtx ectx;
        ectx.msg.sender = nullptr;
        ectx.msg.messageType = messageType;
        ectx.msg.data = data;
        ectx.msg.dataLen = dataLen;
        guard::Call(
            "engine.serialization",
            nullptr,
            [](void* ud) {
                EngCtx* c = static_cast<EngCtx*>(ud);
                kcdx::serialization::OnEngineMessage(&c->msg);
            },
            &ectx);
    }

    // After dispatch completes, give the test-suite aggregator a chance
    // to emit its "Test suite: X/Y passing as of <message>" roll-up.
    // No-op when dev mode is off or no tests have reported.
    test::EmitSummary(test::MessageLabel(messageType));
}

}  // namespace kcdx::messaging
