#include "messaging.h"

#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "log.h"
#include "plugin_loader.h"
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
    // Dispatch or RegisterListener.
    std::vector<kcdxMessagingCallback> targets;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (const auto& l : g_listeners) {
            // Sender filter: listener subscribed to this specific sender?
            if (l.sender != senderName) continue;
            // Receiver filter: if dispatch specified a receiver, only that
            // plugin's listeners get it.
            if (receiver) {
                const char* listenerName = HandleToName(l.handle);
                if (!listenerName || std::strcmp(listenerName, receiver) != 0) {
                    continue;
                }
            }
            targets.push_back(l.callback);
        }
    }

    if (targets.empty()) return false;

    kcdxMessage msg;
    msg.sender = senderName;
    msg.messageType = messageType;
    msg.data = data;
    msg.dataLen = dataLen;
    for (auto cb : targets) {
        // Each callback gets its own kcdxMessage copy on the stack — protects
        // against a callback mutating the struct and confusing the next.
        kcdxMessage perCallback = msg;
        cb(&perCallback);
    }
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
    std::vector<kcdxMessagingCallback> targets;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (const auto& l : g_listeners) {
            if (l.sender.empty()) targets.push_back(l.callback);
        }
    }

    if (!targets.empty()) {
        kcdxMessage msg;
        msg.sender = nullptr;     // engine-originated
        msg.messageType = messageType;
        msg.data = data;
        msg.dataLen = dataLen;
        for (auto cb : targets) {
            kcdxMessage perCallback = msg;
            cb(&perCallback);
        }
    }

    // After dispatch completes, give the test-suite aggregator a chance
    // to emit its "Test suite: X/Y passing as of <message>" roll-up.
    // No-op when dev mode is off or no tests have reported.
    test::EmitSummary(test::MessageLabel(messageType));
}

}  // namespace kcdx::messaging
