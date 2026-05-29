// CAP-64 — C++ peer of CAP-59 (kcdx.hook on lua_pcall via the C++ surface).
//
// Single row, CAP-64-fires: installs a kcdxHookInterface::Before on the
// curated `lua_pcall` engine seed. Pre-migration this install would have
// failed MH_ERROR_ALREADY_CREATED (the engine's own production lua_pcall
// hook owned the MinHook slot at this VA via raw MH_CreateHook). Post-
// migration the engine's hook is itself a chain entry (registered via
// hook_chain::AddCEngine in src/hooks.cpp), so this plugin install chains
// alongside as a second Before — the falsifiable proof that the migration
// closed the latent C++-author-cannot-hook-engine-target bug.
//
// Self-reports PASS from the FIRST callback fire (one-shot guarded);
// InputLoaded backstop reports loud FAIL if the callback never fires
// (lua_pcall is called continuously by every Lua-from-C dispatch from
// plugin load onward, so a missed fire by InputLoaded means the chain
// install never reached the dispatch path).

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>

#include "kcdx/Interfaces.h"

namespace {

constexpr const char* kName = "cap_64_cpp_lua_pcall_fires";
constexpr const char* kRow  = "CAP-64-fires";

const kcdxInterface*     g_api  = nullptr;
const kcdxHookInterface* g_hook = nullptr;
kcdxPluginHandle         g_self = kcdxInvalidPluginHandle;
kcdxLogger               g_log;

kcdxHookHandle           g_handle = 0;
std::atomic<bool>        g_fired{false};

// Before-mode ABI per BuildCDispatchThunk:
//   void cFn(uintptr_t args[], int* outCount, /* typed args... */)
// lua_pcall signature: "i32 (ptr L, i32 nargs, i32 nresults, i32 errfunc)".
// We never mutate args (outCount stays 0); we self-report on the first fire.
extern "C" void Cap64_LuaPcall_Before(uintptr_t /*args*/[], int* /*outCount*/,
                                      void* /*L*/, int /*nargs*/,
                                      int /*nresults*/, int /*errfunc*/) {
    bool expected = false;
    if (!g_fired.compare_exchange_strong(expected, true,
                                         std::memory_order_acq_rel)) {
        return;  // one-shot guard
    }
    if (g_api && g_self != kcdxInvalidPluginHandle) {
        g_api->ReportTestResult(
            g_self, kRow, 1,
            "C++ kcdxHookInterface::Before(\"lua_pcall\", ...) installed AND "
            "fired post-engine-direct-migration — the engine's own lua_pcall "
            "hook is now a chain entry (hook_chain::AddCEngine), so a plugin "
            "install at the same target VA chains alongside instead of "
            "failing MH_ERROR_ALREADY_CREATED");
    }
}

void OnMessage(kcdxMessage* msg) {
    if (msg->messageType != kcdxMessage_InputLoaded) return;
    if (g_fired.load(std::memory_order_acquire)) return;
    // The Before callback never fired between plugin load and
    // InputLoaded — falsifiable signal that the chain install did not
    // wire the dispatch path (or the engine-direct migration regressed).
    char reason[480];
    bool applied = (g_handle != 0) && g_hook && g_hook->IsApplied(g_handle);
    const char* reasonStr = (g_handle != 0 && g_hook && !applied)
        ? g_hook->GetReason(g_handle) : nullptr;
    std::snprintf(reason, sizeof(reason),
        "C++ Before(\"lua_pcall\") callback did NOT fire between Plugin_Load "
        "and InputLoaded — handle=%llu IsApplied=%d reason='%s'. lua_pcall "
        "fires continuously from plugin load onward; a missed fire by "
        "InputLoaded means the chain install did not wire the dispatch path "
        "(possibly the engine-direct migration regressed: the engine's own "
        "lua_pcall hook is back to raw MH_CreateHook and the plugin install "
        "is hitting MH_ERROR_ALREADY_CREATED again)",
        (unsigned long long)g_handle, applied ? 1 : 0,
        reasonStr ? reasonStr : "(null)");
    g_api->ReportTestResult(g_self, kRow, 0, reason);
}

}  // namespace

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    g_api  = api;
    g_self = api->GetPluginHandle(kName);
    g_log  = kcdxLogger(api, g_self);
    g_log.Info("INIT", "kcdxPlugin_Load called");

    g_hook = static_cast<const kcdxHookInterface*>(
        api->QueryInterface(kcdxInterface_Hook, kcdxHookInterface_Version));
    if (!g_hook) {
        api->ReportTestResult(g_self, kRow, 0,
            "QueryInterface(Hook) returned null at Plugin_Load");
        return true;
    }

    auto* messaging = static_cast<kcdxMessagingInterface*>(
        api->QueryInterface(kcdxInterface_Messaging,
                            kcdxMessagingInterface_Version));
    if (messaging) {
        messaging->RegisterListener(g_self, nullptr, OnMessage);
    }

    // Install Before on the curated engine seed `lua_pcall`. The name
    // resolves both address AND verified signature; no explicit
    // opts.signature needed.
    kcdxHookOptions opts = {};
    opts.owningPlugin = g_self;
    opts.name         = "cap64_lua_pcall_before";
    g_handle = g_hook->Before("lua_pcall",
                              reinterpret_cast<void*>(&Cap64_LuaPcall_Before),
                              &opts);
    if (g_handle == 0) {
        // Install machinery returned 0 (engine logged the teaching reason);
        // this is the loud-on-failure path — without an install we will
        // never fire, so report FAIL synchronously rather than rely on the
        // InputLoaded backstop.
        api->ReportTestResult(g_self, kRow, 0,
            "kcdxHookInterface::Before(\"lua_pcall\", ...) returned 0 at "
            "Plugin_Load — install machinery rejected the registration "
            "(see HOOK_INTERFACE engine log for the teaching reason). "
            "If the failure is the pre-migration MH_ERROR_ALREADY_CREATED "
            "(the engine's own lua_pcall hook is back to raw MH_CreateHook), "
            "this is the engine-direct migration regressing — the whole "
            "point of the migration was to make this install succeed.");
        return true;
    }
    g_log.Info("INIT",
        "installed kcdxHookInterface::Before on `lua_pcall` (handle=%llu); "
        "awaiting first callback fire to self-report PASS",
        (unsigned long long)g_handle);
    return true;
}
