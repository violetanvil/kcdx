// CAP-29 — both-phase C++ plugin lifecycle (Load THEN PostGameLoad).
//
// The C++ parity mirror of COMP-11 (the Lua both-phase test) AND the
// first live exercise of the kcdxPlugin_PostGameLoad export. It proves:
//   (1) BOTH exports fired — Load ran (before-game/load wave) AND
//       PostGameLoad ran (after-game phase, first update tick after
//       ApplyZone). That PostGameLoad fires at all proves the export is
//       resolved + dispatched live.
//   (2) Load fired BEFORE PostGameLoad — the phase ordering. A monotonic
//       g_seq counter is incremented in each export; Load records seq=1,
//       PostGameLoad records seq=2. PASS iff Load ran AND postSeq>loadSeq.
//
// The full C++ lifecycle is Preload -> Load -> [before-work applied] ->
// PostGameLoad, then kcdxMessage_InputLoaded. (Interfaces.h ~line 1204.)
//
// WHERE the result is reported: PostGameLoad is the LAST of the two to
// run, so it owns the assertion — it can see its own run AND that Load
// already ran. If PostGameLoad never fired the order-row would silently
// stay PENDING, so kcdxPlugin_Load also registers a kcdxMessage_InputLoaded
// backstop listener: sub-4 wires RunPostGameLoad BEFORE InputLoaded, so by
// InputLoaded PostGameLoad MUST have run; if it hasn't reported by then,
// the listener reports a loud FAIL instead of a silent PENDING.

#include <windows.h>

#include <cstdio>

#include "kcdx/Interfaces.h"

namespace {
const char* kName = "cap_29_both_phase_dll";

const kcdxInterface* g_api  = nullptr;
kcdxPluginHandle     g_self = kcdxInvalidPluginHandle;
kcdxLogger           gLog;

// Monotonic sequence counter, incremented once per export. Load records
// g_loadSeq=1; PostGameLoad records g_postSeq=2.
int  g_seq      = 0;
int  g_loadSeq  = 0;
int  g_postSeq  = 0;
bool g_loadRan  = false;
bool g_postRan  = false;
bool g_reported = false;

// InputLoaded backstop: if PostGameLoad never reported a result by the
// time InputLoaded fires, that means the after-phase C++ export was not
// dispatched — report a loud FAIL rather than leaving a silent PENDING.
void OnMessage(kcdxMessage* msg) {
    if (msg->messageType != kcdxMessage_InputLoaded) return;
    if (g_reported) return;  // PostGameLoad already reported the verdict.
    g_reported = true;

    const char* reason =
        "kcdxPlugin_PostGameLoad did not fire before InputLoaded — the "
        "after-phase C++ export was not dispatched";
    gLog.Error("LIFECYCLE", "FAIL: %s", reason);
    g_api->ReportTestResult(g_self, "CAP-29", 0, reason);
}
}  // namespace

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    g_api  = api;
    g_self = api->GetPluginHandle(kName);
    gLog   = kcdxLogger(api, g_self);

    g_loadSeq = ++g_seq;  // first export to run -> seq 1
    g_loadRan = true;
    gLog.Info("LIFECYCLE", "kcdxPlugin_Load called (seq=%d)", g_loadSeq);

    // Register the InputLoaded backstop so a never-fired PostGameLoad is a
    // loud FAIL, not a silent PENDING.
    auto* m = static_cast<kcdxMessagingInterface*>(
        api->QueryInterface(kcdxInterface_Messaging,
                            kcdxMessagingInterface_Version));
    if (!m) {
        gLog.Error("LIFECYCLE", "QueryInterface(Messaging) returned null");
        api->ReportTestResult(g_self, "CAP-29", 0,
            "QueryInterface(Messaging) returned null — cannot arm the "
            "InputLoaded backstop");
        return true;
    }
    m->RegisterListener(g_self, nullptr, OnMessage);
    return true;
}

extern "C" __declspec(dllexport)
bool kcdxPlugin_PostGameLoad(const kcdxInterface* api) {
    (void)api;  // g_api was cached in Load; this fires later, same DLL.
    g_postSeq = ++g_seq;  // second export to run -> seq 2
    g_postRan = true;
    gLog.Info("LIFECYCLE", "kcdxPlugin_PostGameLoad called (seq=%d)", g_postSeq);

    // ASSERT both halves: Load ran AND PostGameLoad ran strictly after it.
    bool pass = g_loadRan && (g_postSeq > g_loadSeq);

    char reason[256];
    snprintf(reason, sizeof(reason),
        "load_ran=%d load_seq=%d post_seq=%d (PASS iff load_ran && "
        "post_seq>load_seq)",
        g_loadRan ? 1 : 0, g_loadSeq, g_postSeq);
    if (pass) gLog.Info ("LIFECYCLE", "PASS: %s", reason);
    else      gLog.Error("LIFECYCLE", "FAIL: %s", reason);

    g_reported = true;  // disarm the InputLoaded backstop — verdict reported.
    g_api->ReportTestResult(g_self, "CAP-29", pass ? 1 : 0, reason);
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
