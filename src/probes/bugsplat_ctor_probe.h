#pragma once

// === DIAGNOSTIC (PROBE S) ===
//
// Hooks BugSplat64.dll!MiniDmpSender::MiniDmpSender (export ordinal 3,
// RVA 0xC914 against the live game) and logs the call timing + the
// wide-string passed as `szApp` (arg 2). Always calls the original
// ctor without mutating any argument — this probe answers the
// "is worker-thread install timing sufficient?" question, not the
// "does the substitution work?" question.
//
// Outcome reading:
//   * Log line emitted → worker-thread install path catches BugSplat's
//     ctor → final fix can ship via the normal (after_game) hook
//     installation path.
//   * No log line, yet PROBE R still catches the broken dmp path at
//     crash time → ctor ran before our MinHook detour was active →
//     final fix needs before_game timing (load_order's
//     LdrRegisterDllNotification path, extended to support hooks not
//     just patches).
//
// See docs/known-issues/BugSplat dmp files don't reach disk for AV
// crashes.md for the full investigation.
//
// Dev-mode-only.

namespace kcdx::probes::bugsplat_ctor_probe {

// Install the MinHook detour. Idempotent. Returns true on success.
// Requires BugSplat64.dll to already be mapped. Used by both the
// worker-thread install path (PROBE S) and the LDR-notification
// install path (PROBE T) once the DLL appears.
bool Install();

// === DIAGNOSTIC (PROBE T) ===
//
// Arm an LdrRegisterDllNotification callback that calls Install()
// the moment BugSplat64.dll is mapped, BEFORE its own DllMain runs
// and BEFORE WHGame.dll's init code reaches the ctor call. If
// BugSplat64.dll is already mapped at the time this function is
// called, Install() runs immediately.
//
// Safe to call from kcdx.asi DllMain (loader-lock-safe: only does
// GetModuleHandleW + MinHook init + GetProcAddress + LDR registration).
// Idempotent.
//
// Returns true if either the immediate-install or the LDR registration
// succeeded.
bool ArmLdrInstall();

}  // namespace kcdx::probes::bugsplat_ctor_probe
