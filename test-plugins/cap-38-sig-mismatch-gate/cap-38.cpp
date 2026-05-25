// CAP-38 — sig-mismatch gate (named target + WRONG explicit signature).
//
// The footgun the gate closes: when an author names a target that carries
// a verified Address-Library ABI AND ALSO hand-writes an explicit
// opts.signature, the engine USED to trust the explicit sig outright and
// never cross-check it against the verified ABI it had in hand — so a
// wrong explicit sig was SILENTLY accepted (an AP12 footgun on the exact
// surface AP12 protects). The gate's core behavior is (c) WARN + keep the
// explicit sig: the engine consults the verified ABI to DETECT the
// conflict, emits a teaching WARN, then PROCEEDS with the explicit sig as
// authored (the deliberate-override case stays authoritative).
//
// This is the C++ half (kcdxHookInterface). The Lua half is the sibling
// cap-38-sig-mismatch-gate-lua/ plugin (parity-is-tested, lua-api-
// surface.md). Both name `kcdx.lua_settable` (engine seed id 1186,
// verified ABI "void (ptr L, i32 idx)") and pass the deliberately-WRONG
// explicit signature "void (ptr L)" (arg-count delta → NOT
// SignaturesCompatible → the gate fires).
//
// Rows owned here:
//
//   * CAP-38-cpp-gate-proceeds (AUTO) — install PROCEEDED with the
//     explicit sig: handle != 0, IsApplied()==true, and the hook actually
//     FIRES (a before observer flips a flag; lua_settable is called all
//     over boot, so by PostGameLoad the flag is set — the hook is live
//     and marshalling per the explicit 1-arg sig). FALSIFIABLE against a
//     hypothetical (a)-REJECT implementation: had the gate rejected the
//     mismatch, the handle would be 0 / IsApplied false / the observer
//     never fires, and this row FAILS.
//
//   * CAP-38-cpp-gate-warn ([manual], log-assert; reported by the
//     orchestrator, NOT auto) — the COMP-12 log-assert pattern: the
//     orchestrator confirms the gate-WARN KV line fired in the engine log
//     post-run. Pre-fix (silent trust) NO warn line fires, so the manual
//     row FAILS — that is the falsifiable signal the gate exists. The
//     EXACT line (engine log): category HOOK_SIG_GATE, action
//     explicit_overrides_verified, keys target / plugin / explicit_sig /
//     verified_sig / used. This plugin does NOT scrape the log from inside
//     itself (engine scope the gate doesn't need); it only drives the
//     install that makes the line fire.
//
// SAFETY — why hooking the hot lua_settable with a wrong sig is safe: the
// before observer reads nothing harmful, mutates no args (*outCount=0),
// and does not skip the original. The 1-arg explicit sig's before-thunk
// loads only RCX (L, always valid); the detour preserves the original's
// real registers (RCX=L, RDX=idx), so lua_settable runs untouched after
// the observer returns. The wrong sig mis-DESCRIBES the ABI (the gate's
// whole point) but the observer never acts on the missing arg, so no Lua
// state is corrupted.

#include <windows.h>

#include <cstdint>
#include <cstdio>

#include "kcdx/Interfaces.h"

namespace {

// Manifest bare name — must match [plugin].name in kcdx.toml.
const char* kName = "cap_38_sig_mismatch_gate";

const kcdxInterface*     g_api  = nullptr;
const kcdxHookInterface* g_hook = nullptr;
kcdxPluginHandle         g_self = kcdxInvalidPluginHandle;
kcdxLogger               g_log;

kcdxHookHandle g_h_gate      = 0;
bool           g_observer_fired = false;
bool           g_post_ran    = false;

// The named target + the WRONG explicit signature.
//   target          = "kcdx.lua_settable"  (engine seed, verified
//                       ABI "void (ptr L, i32 idx)" — id 1186)
//   opts.signature  = "void (ptr L)"        (1 arg — arg-count delta vs
//                       the verified 2-arg ABI → NOT SignaturesCompatible)
const char* kTarget       = "kcdx.lua_settable";
const char* kWrongSig     = "void (ptr L)";

// BEFORE shape: void cFn(uintptr_t args[], int* outCount, /* typed
// args... */). Pure observer — set *outCount=0 (commit nothing back), do
// not touch args. The typed pass-through `L` is the one arg the wrong
// 1-arg sig declares; we read nothing from it. lua_settable runs
// untouched after this returns.
extern "C" void Cap38_Observer_Cb(uintptr_t args[], int* outCount,
                                  void* L) {
    (void)args;
    (void)L;
    g_observer_fired = true;
    *outCount = 0;  // commit no arg mutation
}

void Report(const char* row, bool pass, const char* reason) {
    if (pass) g_log.Info ("CAP38", "PASS %s: %s", row, reason);
    else      g_log.Error("CAP38", "FAIL %s: %s", row, reason);
    g_api->ReportTestResult(g_self, row, pass ? 1 : 0, reason);
}

// InputLoaded backstop — loud FAIL if PostGameLoad never fired (the
// cap-29 / cap-36 design). Only the auto row is backstopped; the manual
// log-assert row is the orchestrator's job.
void OnMessage(kcdxMessage* msg) {
    if (msg->messageType != kcdxMessage_InputLoaded) return;
    if (g_post_ran) return;
    const char* reason =
        "kcdxPlugin_PostGameLoad did not fire before kcdxMessage_"
        "InputLoaded — the after-phase C++ export was not dispatched; "
        "CAP-38-cpp-gate-proceeds reported FAIL via the InputLoaded backstop";
    g_log.Error("CAP38", "FAIL backstop: %s", reason);
    g_api->ReportTestResult(g_self, "CAP-38-cpp-gate-proceeds", 0, reason);
}

}  // namespace

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    g_api  = api;
    g_self = api->GetPluginHandle(kName);
    g_log  = kcdxLogger(api, g_self);
    g_log.Info("INIT", "kcdxPlugin_Load called (engine v0x%08X)",
               api->kcdxVersion);

    g_hook = static_cast<const kcdxHookInterface*>(
        api->QueryInterface(kcdxInterface_Hook, kcdxHookInterface_Version));
    if (!g_hook) {
        g_log.Error("INIT",
            "QueryInterface(Hook, v%u) returned null — CAP-38-cpp-gate-"
            "proceeds will FAIL", kcdxHookInterface_Version);
        api->ReportTestResult(g_self, "CAP-38-cpp-gate-proceeds", 0,
            "QueryInterface(Hook) returned null at Plugin_Load");
        return true;
    }

    auto* messaging = static_cast<kcdxMessagingInterface*>(
        api->QueryInterface(kcdxInterface_Messaging,
                            kcdxMessagingInterface_Version));
    if (messaging) {
        messaging->RegisterListener(g_self, nullptr, OnMessage);
    }

    // Install: NAMED target + WRONG explicit signature. The gate must WARN
    // (named verified ABI vs wrong explicit sig) then PROCEED with the
    // explicit sig — the install SUCCEEDS (non-zero handle).
    {
        kcdxHookOptions opts = {};
        opts.owningPlugin = g_self;
        opts.signature    = kWrongSig;   // wrong on purpose
        opts.name         = "cap38_gate";
        g_h_gate = g_hook->Before(kTarget, (void*)&Cap38_Observer_Cb, &opts);
        if (g_h_gate == 0) {
            g_log.Error("CAP38",
                "install on named target '%s' + wrong sig '%s' returned 0 — "
                "the gate REJECTED the mismatch instead of WARN+proceed "
                "(behavior-c violated); CAP-38-cpp-gate-proceeds will FAIL",
                kTarget, kWrongSig);
            // PostGameLoad's assertion catches it; report nothing here.
        }
    }
    return true;
}

extern "C" __declspec(dllexport)
bool kcdxPlugin_PostGameLoad(const kcdxInterface* api) {
    (void)api;
    g_post_ran = true;

    bool applied = (g_h_gate != 0) && g_hook->IsApplied(g_h_gate);
    char reason[400];
    const bool pass = (g_h_gate != 0) && applied && g_observer_fired;
    snprintf(reason, sizeof(reason),
        "%s — named target '%s' + WRONG explicit sig '%s' (verified ABI is "
        "'void (ptr L, i32 idx)'): handle=%s, IsApplied=%d, observer "
        "fired=%d. behavior-c keeps the explicit sig authoritative and the "
        "install PROCEEDS; had the gate REJECTED (hypothetical (a)-impl) the "
        "handle would be 0 / IsApplied false / observer never fires and this "
        "row FAILS. The gate-WARN itself is the [manual] CAP-38-cpp-gate-warn "
        "row (orchestrator greps HOOK_SIG_GATE explicit_overrides_verified).",
        pass ? "gate WARN+proceed PASS" : "gate WARN+proceed FAIL",
        kTarget, kWrongSig,
        (g_h_gate != 0) ? "non-zero" : "zero",
        applied ? 1 : 0, g_observer_fired ? 1 : 0);
    Report("CAP-38-cpp-gate-proceeds", pass, reason);
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
