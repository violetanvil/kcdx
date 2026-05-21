#pragma once

// === DIAGNOSTIC (PROBE R) ===
//
// Hooks kernel32!CreateFileW. On every call, scans lpFileName for the
// wide substrings "Kingdom Come" or ".dmp" (case-insensitive). On match,
// logs the full path + the return address read from
// _AddressOfReturnAddress() so the call site can be traced back to its
// string-construction code in WHGame.dll. Always calls the original
// CreateFileW so behavior is unchanged.
//
// Used to identify which call site builds BugSplat's colon-bearing dmp
// filename, since the previous static-analysis attribution (LEA at
// 0x1824599e7) was empirically disproven. See
// docs/known-issues/BugSplat dmp files don't reach disk for AV crashes.md
// for the full trail.
//
// Dev-mode-only — Install() is a no-op when kcdx::dev::IsEnabled()
// returns false, so production users get zero overhead.
//
// Lifecycle: temporary. Delete this file + remove the Install() call
// from hooks::Install once the question is answered.

namespace kcdx::probes::createfilew_probe {

// Install the MinHook detour. Idempotent; second call is a no-op.
// Returns true on success, false on dev-mode-off or MinHook failure.
bool Install();

}  // namespace kcdx::probes::createfilew_probe
