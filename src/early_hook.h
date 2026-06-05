#pragma once

#include <cstdint>

// === early_hook — author-parameterized DllMain/LDR-timing detour install ===
//
// WHAT: a MinHook detour installed early enough to intercept a function the
// game's init code calls before kcdx's worker thread runs. The caller declares
// a target by NAME (module + exported symbol) plus the ABI signature and the
// detour function; the engine resolves the address and installs the hook —
// immediately if the module is already mapped, or the instant it maps (via an
// LdrRegisterDllNotification callback, pre-the-module's-own-DllMain) if not.
//
// WHY by-name, not by-RVA: the disassembler test — the caller names what it
// wants; the engine carries the address resolution (GetModuleHandleW +
// GetProcAddress on the mangled export). A foreign third-party DLL's exports are
// not in the reference DB and cannot be name-resolved through it, so the ABI
// signature is the caller's (the legitimate expert hatch for what the engine
// cannot name). No per-version-volatile RVA literal ever appears.
//
// INVARIANT (loader-lock safety): the install path runs under the Windows
// loader lock during a DllMain. It does ONLY GetModuleHandleW + idempotent
// MH_Initialize + GetProcAddress + MH_CreateHook + MH_EnableHook +
// LdrRegisterDllNotification — all confirmed loader-lock-safe live for the
// MiniDmpSender ctor target. It NEVER LoadLibrary's, spawns a thread, or runs
// heavy heap/log-file work under the lock.
//
// SAFETY (conflict engine): this is a DllMain-timing install that runs BEFORE
// the conflict engine and the apply-pass exist, so it deliberately does NOT
// route through the conflict engine (which is not up yet). This is the same
// documented before_game carve-out the before_game byte-patch apply path
// already has (kcdx::ldr_notify) — first-applied-wins by install order, not a
// conflict-arbitrated chain. It is NOT a hook installed outside the conflict
// engine in the after_game sense; it is the engine half of the before_game-hook
// timing.
//
// This is engine-internal VM/boot plumbing. The author-facing before_game-hook
// surface (a before_game-zoned plugin installing a hook from its own DllMain) is
// a LATER consumer that drives this primitive; it is not part of this unit.

namespace kcdx::early_hook {

// One author-parameterized early-install request.
//
// Every field is owned by the caller for process lifetime — module / exportName
// are string literals (never freed); detour / signature point at caller code /
// static data. The primitive copies nothing.
struct InstallRequest {
    // Module the target export lives in (e.g. L"BugSplat64.dll"). Resolved via
    // GetModuleHandleW; if not yet mapped, the install is deferred to the LDR
    // notification for this module.
    const wchar_t* module = nullptr;

    // Mangled exported symbol name within `module` (e.g. the C++ ctor's mangled
    // name). Resolved via GetProcAddress. The engine has no name table for a
    // foreign module's exports — the export name IS the locator here.
    const char* exportName = nullptr;

    // The raw __fastcall detour. Its signature MUST match the target's ABI
    // (the caller's responsibility — the expert hatch; see the header note).
    void* detour = nullptr;

    // Receives the original (trampoline) pointer MinHook produces. The detour
    // calls through this to reach the unhooked target. Non-null required.
    void** trampoline = nullptr;

    // Human-readable ABI / purpose, for the install log line + the modification
    // inventory entry (e.g. "i32 (ptr,wstr,wstr,wstr,wstr,u32)" or a short
    // purpose tag). Nullable; defaults to the export name in the log if null.
    const char* signature = nullptr;

    // Inventory tag (a process-lifetime literal) recorded into the modification
    // inventory on a successful install so a crash at/after the target has a
    // fault-time owner record (e.g. "bugsplat_ctor"). Nullable.
    const char* inventoryTag = nullptr;
};

// Install the detour for `req` if its module is currently mapped. Idempotent
// per request (a per-request latch; a second call after success is a no-op
// returning true). Requires `req.module`'s module to already be mapped — used
// both as the immediate-install path and as the body the LDR callback runs once
// the module appears.
//
// Returns true on a successful (or already-installed) install; false if the
// module is not mapped, the export does not resolve, or MinHook fails (each
// logged with its reason — never a silent failure). On a false return the
// request's latch is rolled back so a later attempt (e.g. the LDR callback) can
// retry.
bool Install(const InstallRequest& req);

// Arm the early install for `req`: if `req.module` is already mapped at call
// time, install immediately; otherwise register an LdrRegisterDllNotification
// callback that calls Install(req) the instant `req.module` is mapped, BEFORE
// that module's own DllMain runs.
//
// Safe to call from kcdx.dll DllMain (loader-lock-safe — see the header
// INVARIANT). Idempotent per request. `req` MUST have process lifetime: a
// deferred install reads it from inside the LDR callback long after this call
// returns (pass a pointer to a static/process-lifetime InstallRequest, never a
// stack temporary).
//
// Returns true if either the immediate install or the LDR registration
// succeeded.
bool Arm(const InstallRequest& req);

}  // namespace kcdx::early_hook

// === First consumer: the BugSplat colon-filename ctor hook ===
//
// A baked instance of the generalized primitive — the engine's first-party use
// of the before_game-hook install. Log-only today (logs the ctor's szApp at
// DllMain time; calls the original unchanged), dev-gated, installed at the same
// timing the proven prototype used. The before_game-hook builtin work changes
// the detour body to rewrite the colon out of szApp; this is the relocated,
// generalized-primitive-backed install of that proven log-only behavior.
namespace kcdx::early_hook::bugsplat {

// Arm the BugSplat ctor hook. Dev-gated (no-op when dev mode is off). Safe to
// call from kcdx.dll DllMain. Idempotent. Returns true on a successful arm
// (immediate install or LDR registration).
bool Arm();

}  // namespace kcdx::early_hook::bugsplat
