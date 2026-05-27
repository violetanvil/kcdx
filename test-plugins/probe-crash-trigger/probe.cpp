// PROBE-CRASH-TRIGGER — dev-only on-demand AV crash.
//
// Registers a `kcdx_crash_now` console command. When invoked, derefs
// NULL → 0xC0000005 ACCESS_VIOLATION. Used to baseline + verify the
// BugSplat filename fix. Also exercises the kcdx in-process
// MiniDumpWriteDump path and the watchdog crash-bundle pipeline.
//
// Plugin is suite-gated; dev_mode off => never registers the
// command. Production installs see nothing.

#include <windows.h>

#include <cstdint>

#include "kcdx/Interfaces.h"

namespace {
const char* kName = "probe_crash_trigger";

const kcdxInterface*        g_api     = nullptr;
const kcdxConsoleInterface* g_console = nullptr;
kcdxPluginHandle            g_self    = kcdxInvalidPluginHandle;
kcdxLogger                  gLog;

// The actual crash. Marked volatile + non-static so the compiler can't
// optimize the deref away.
__declspec(noinline) DWORD WINAPI CrashThread(LPVOID) {
    volatile int* p = nullptr;
    // Force a write so the AV is unambiguous (read could be elided).
    *p = 0xDEADBEEF;
    return 0;  // unreachable
}

void OnCrashCommand(const kcdxConsoleCmdArgs* /*args*/) {
    gLog.Error("CRASH", "kcdx_crash_now invoked — spawning crash thread "
                        "to escape kcdx::guard::InvokeGuarded's __try");
    // Spawn a worker thread to do the deref. Why: kcdx wraps console
    // command dispatch in a per-site __try / __except (see
    // src/crash_guard.cpp::InvokeGuarded), so an AV inside the
    // command handler thread gets swallowed and the game continues.
    // The new thread has no __try on its stack — the unwound AV
    // reaches the process-wide unhandled-exception filter, which is
    // exactly what we need to exercise BugSplat's filename construction.
    HANDLE h = CreateThread(nullptr, 0, CrashThread, nullptr, 0, nullptr);
    if (h) CloseHandle(h);  // don't wait — main thread returns normally,
                            // crash thread takes the process down.
}

void OnMessage(kcdxMessage* msg) {
    if (msg->messageType != kcdxMessage_InputLoaded) return;
    if (!g_console) {
        gLog.Error("INIT", "no Console interface; cannot register "
                           "kcdx_crash_now");
        return;
    }
    bool ok = g_console->RegisterCommand(
        g_self,
        "kcdx_crash_now",
        "Deliberately deref NULL to trigger a 0xC0000005 AV crash. "
        "DEV-ONLY. Used to test BugSplat + watchdog crash-bundle "
        "pipeline.",
        OnCrashCommand);
    if (ok) {
        gLog.Info("INIT", "registered: kcdx_crash_now (type it in the "
                          "in-game console to crash on demand)");
    } else {
        gLog.Error("INIT", "RegisterCommand(kcdx_crash_now) returned "
                           "false (console surface not armed?)");
    }
}
}  // namespace

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    g_api  = api;
    g_self = api->GetPluginHandle(kName);
    gLog   = kcdxLogger(api, g_self);

    gLog.Info("INIT", "kcdxPlugin_Load called");

    g_console = static_cast<const kcdxConsoleInterface*>(
        api->QueryInterface(kcdxInterface_Console,
                            kcdxConsoleInterface_Version));
    // null check happens in OnMessage when kInputLoaded fires.

    auto* m = static_cast<kcdxMessagingInterface*>(
        api->QueryInterface(kcdxInterface_Messaging,
                            kcdxMessagingInterface_Version));
    if (!m) {
        gLog.Error("INIT", "QueryInterface(Messaging) returned null");
        return true;
    }
    m->RegisterListener(g_self, nullptr, OnMessage);
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
