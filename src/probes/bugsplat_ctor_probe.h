#pragma once

// KEEP — not a throwaway probe. This install machinery is the PROVEN
// before_game-hook prototype: a later relocation moves it into the
// permanent engine home (e.g. src/early_hook.{h,cpp} or an ldr_notify
// extension) and generalizes ArmLdrInstall + the detour into a
// parameterized, author-driven install primitive. The lifecycle framing
// below ("this answers …") is the original diagnostic context, retained
// for the investigation trail — the code stays live.
//
// === Worker-thread install path ===
//
// Hooks BugSplat64.dll!MiniDmpSender::MiniDmpSender (export ordinal 3,
// RVA 0xC914 against the live game) and logs the call timing + the
// wide-string passed as `szApp` (arg 2). Always calls the original
// ctor without mutating any argument — this answers the
// "is worker-thread install timing sufficient?" question, not the
// "does the substitution work?" question.
//
// Outcome reading:
//   * Log line emitted → worker-thread install path catches BugSplat's
//     ctor → final fix can ship via the normal (after_game) hook
//     installation path.
//   * No log line, yet the dmp-path check still catches the broken dmp
//     path at crash time → ctor ran before our MinHook detour was active
//     → final fix needs before_game timing (load_order's
//     LdrRegisterDllNotification path, extended to support hooks not
//     just patches).
//
// Dev-mode-only.

namespace kcdx::probes::bugsplat_ctor_probe {

// Install the MinHook detour. Idempotent. Returns true on success.
// Requires BugSplat64.dll to already be mapped. Used by both the
// worker-thread install path and the LDR-notification install path
// once the DLL appears.
bool Install();

// === LDR-notification install ===
//
// Arm an LdrRegisterDllNotification callback that calls Install()
// the moment BugSplat64.dll is mapped, BEFORE its own DllMain runs
// and BEFORE WHGame.dll's init code reaches the ctor call. If
// BugSplat64.dll is already mapped at the time this function is
// called, Install() runs immediately.
//
// Safe to call from kcdx.dll DllMain (loader-lock-safe: only does
// GetModuleHandleW + MinHook init + GetProcAddress + LDR registration).
// Idempotent.
//
// Returns true if either the immediate-install or the LDR registration
// succeeded.
bool ArmLdrInstall();

}  // namespace kcdx::probes::bugsplat_ctor_probe
