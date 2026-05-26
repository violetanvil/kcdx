// CAP-38 — sig-mismatch gate (named target + WRONG explicit signature).
//
// The footgun the gate closes: when an author names a target that carries
// a verified Address-Library ABI AND ALSO hand-writes an explicit
// opts.signature, the engine USED to trust the explicit sig outright and
// never cross-check it against the verified ABI it had in hand — so a
// wrong explicit sig was SILENTLY accepted (an AP12 footgun on the exact
// surface AP12 protects). The gate's core behavior is (c) keep + proceed:
// the engine consults the verified ABI to DETECT the conflict, emits a
// teaching diagnostic, then PROCEEDS with the explicit sig as authored (the
// deliberate-override case stays authoritative — the expert override is
// honored). The diagnostic SEVERITY splits by ClassifyConflict: cap-38's
// mismatch is a return-width delta (i32 vs void) → a HARD conflict → the gate
// logs at ERROR (action explicit_overrides_verified_hard), flagging the
// known-crash-risk override on a live engine function. A soft same-shape
// conflict stays at WARN.
//
// This is the C++ half (kcdxHookInterface). The Lua half is the sibling
// cap-38-sig-mismatch-gate-lua/ plugin (parity-is-tested, lua-api-
// surface.md). Both name `kcdx.luaopen_table` (engine seed id 1173,
// verified ABI "i32 (ptr L)") and pass the deliberately-WRONG explicit
// signature "void (ptr L)" (same arg count, RETURN-WIDTH delta — i32
// collapsed to void → NOT SignaturesCompatible → the gate fires).
//
// Rows owned here:
//
//   * CAP-38-cpp-gate-proceeds (AUTO) — install PROCEEDED with the explicit
//     sig: handle != 0 && IsApplied()==true. This is the cap-33/34/35
//     install-is-the-proof idiom, identical to the Lua peer's applied()
//     assertion. FALSIFIABLE against a hypothetical (a)-REJECT impl: had the
//     gate rejected the named-target + wrong-sig mismatch, the handle would
//     be 0 / IsApplied false and this row FAILS. The row asserts install-
//     proceeds ONLY — it does NOT assert the hook fires (the gate is an
//     INSTALL-TIME check; firing tests nothing about it). cap-36's six
//     own-function rows own the C-dispatch firing proof.
//
//   * CAP-38-cpp-gate-warn ([manual], log-assert; reported by the
//     orchestrator, NOT auto) — the COMP-12 log-assert pattern: the
//     orchestrator confirms the gate's HARD-conflict line fired in the
//     engine log post-run. cap-38's mismatch is an ARG-COUNT delta (1-arg
//     explicit vs 2-arg verified) → ClassifyConflict returns Hard → the
//     gate emits at ERROR level (not WARN). Pre-fix (silent trust) NO line
//     fires at all; the older WARN-only gate emitted action
//     `explicit_overrides_verified` at WARN — so this row catches BOTH a
//     silent-trust regression AND a downgrade-to-WARN regression. The EXACT
//     line (engine log): LEVEL=Error, category HOOK_SIG_GATE, action
//     `explicit_overrides_verified_hard`, keys target / plugin /
//     explicit_sig / verified_sig / used / severity=hard / crash_risk=true /
//     note. This plugin does NOT scrape the log from inside itself (engine
//     scope the gate doesn't need); it only drives the install that makes
//     the line fire.
//
// SAFETY — the target is gameplay-COLD, so the wrong-ABI thunk NEVER FIRES.
// luaopen_table is lualibs[] entry 2, called EXACTLY ONCE at Lua library-init
// (boot) and NEVER during gameplay. cap-38's hook installs at the first-
// update-tick AFTER that single call → the wrong thunk is installed but never
// invoked → no register/stack corruption, no crash. This is the cap-33 cold-
// leaf idiom (cap-33 documents the same no-fire property for luaopen_math).
// The OLD target was the HOT lua_settable, whose wrong-ABI thunk DID fire on
// the live save-load path and crashed the game (the 0xC8 root cause): a wrong-
// ABI thunk corrupts the call frame whenever it fires regardless of how polite
// the observer is — a cold no-fire target, NOT observer politeness, is the
// safety property.

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

kcdxHookHandle g_h_gate   = 0;
bool           g_post_ran = false;
bool           g_reported = false;

// The named target + the WRONG explicit signature.
//   target          = "kcdx.luaopen_table" (engine seed, verified
//                       ABI "i32 (ptr L)" — id 1173)
//   opts.signature  = "void (ptr L)"        (same arg count, RETURN-WIDTH
//                       delta vs the verified i32 return → NOT
//                       SignaturesCompatible)
const char* kTarget   = "kcdx.luaopen_table";
const char* kWrongSig = "void (ptr L)";

// BEFORE shape: void cFn(uintptr_t args[], int* outCount, /* typed
// args... */). A minimal no-op the install requires; NEVER relied upon to
// fire — the target is gameplay-cold (luaopen_table runs once at boot,
// before this hook installs), so this thunk is installed but never invoked.
extern "C" void Cap38_Observer_Cb(uintptr_t args[], int* outCount,
                                  void* L) {
    (void)args;
    (void)L;
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
    if (g_reported) return;
    if (g_post_ran) return;
    g_reported = true;
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
    if (g_reported) return true;
    g_reported = true;

    // PASS = behavior-c (install PROCEEDED): non-zero handle that IsApplied.
    // The falsifiable signal vs a hypothetical (a)-reject impl (handle 0 /
    // IsApplied false → FAIL), mirroring the Lua peer's applied() assertion.
    // (Firing the detour is NOT asserted — the gate is install-time only, and
    // the cold target never fires; cap-36 owns the C-dispatch firing proof.)
    const bool applied = (g_h_gate != 0) && g_hook->IsApplied(g_h_gate);
    const bool pass = (g_h_gate != 0) && applied;
    char reason[420];
    snprintf(reason, sizeof(reason),
        "%s — named target '%s' + WRONG explicit sig '%s' (verified ABI is "
        "'i32 (ptr L)' — return-width delta): handle=%s, IsApplied=%d. behavior-c keeps "
        "the explicit sig authoritative and the install PROCEEDS; had the "
        "gate REJECTED (hypothetical (a)-impl) the handle would be 0 / "
        "IsApplied false and this row FAILS. The gate-WARN itself is the "
        "[manual] CAP-38-cpp-gate-warn row (orchestrator greps HOOK_SIG_GATE "
        "explicit_overrides_verified).",
        pass ? "gate WARN+proceed PASS" : "gate WARN+proceed FAIL",
        kTarget, kWrongSig,
        (g_h_gate != 0) ? "non-zero" : "zero",
        applied ? 1 : 0);
    Report("CAP-38-cpp-gate-proceeds", pass, reason);
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
