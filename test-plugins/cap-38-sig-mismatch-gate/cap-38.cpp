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
// mismatch is an arg-count delta (1 vs 2) → a HARD conflict → the gate logs
// at ERROR (action explicit_overrides_verified_hard), flagging the
// known-crash-risk override on a live engine function. A soft same-shape
// conflict stays at WARN.
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
//   * CAP-38-cpp-gate-proceeds (AUTO) — install PROCEEDED with the explicit
//     sig: handle != 0 && IsApplied()==true. This is the cap-33/34/35
//     install-is-the-proof idiom, identical to the Lua peer's applied()
//     assertion. FALSIFIABLE against a hypothetical (a)-REJECT impl: had the
//     gate rejected the named-target + wrong-sig mismatch, the handle would
//     be 0 / IsApplied false and this row FAILS.
//
//     Why NOT "the hook fires": the C-dispatch FIRING of a before-observer
//     is already proven by cap-36's six own-function rows (Before/After/
//     Around/Replace/uninstall/crosslang each hook a PLUGIN-LOCAL function
//     and INVOKE it to observe the callback). cap-38 cannot reuse that
//     pattern: the gate requires a NAMED target (only a game seed carries a
//     verified ABI to conflict against), and a plugin cannot safely invoke
//     WHGame's lua_settable on demand to trigger the detour — PROBE A
//     (docs/known-issues/cap-38 cpp before-observer never fires on a named
//     game target.md) established that a plugin-driven call into WHGame's
//     lua_settable raises a Lua error across the dual-Lua boundary
//     (lua-bridge.md): it longjmp's back to the nearest lua_pcall, never
//     returns to the caller, and is uninstrumentable from the plugin side.
//     So firing-on-this-named-target is not observably testable from a
//     plugin; install-proceeded IS the behavior-c proof, and cap-36 owns the
//     C-dispatch firing proof.
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

kcdxHookHandle g_h_gate   = 0;
bool           g_post_ran = false;
bool           g_reported = false;

// The named target + the WRONG explicit signature.
//   target          = "kcdx.lua_settable"  (engine seed, verified
//                       ABI "void (ptr L, i32 idx)" — id 1186)
//   opts.signature  = "void (ptr L)"        (1 arg — arg-count delta vs
//                       the verified 2-arg ABI → NOT SignaturesCompatible)
const char* kTarget   = "kcdx.lua_settable";
const char* kWrongSig = "void (ptr L)";

// BEFORE shape: void cFn(uintptr_t args[], int* outCount, /* typed
// args... */). Pure observer — set *outCount=0 (commit nothing back), do
// not touch args. Present to document the wrong-sig-is-safe property (the
// detour preserves the original's registers); the row does NOT assert this
// fires (see the header note — firing-on-this-named-target is not
// plugin-observable).
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
    // (Firing the detour is NOT asserted — see the header note + PROBE A.)
    const bool applied = (g_h_gate != 0) && g_hook->IsApplied(g_h_gate);
    const bool pass = (g_h_gate != 0) && applied;
    char reason[420];
    snprintf(reason, sizeof(reason),
        "%s — named target '%s' + WRONG explicit sig '%s' (verified ABI is "
        "'void (ptr L, i32 idx)'): handle=%s, IsApplied=%d. behavior-c keeps "
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
