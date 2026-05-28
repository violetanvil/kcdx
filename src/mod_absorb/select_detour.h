#pragma once

// SELECT detour — the production mod-loader takeover. STEP 4 of the mod-loader
// absorb. docs/mod-loader-absorb.md "Step 4".
//
// kcdx IS the mod loader. The native engine still constructs the manager
// (wh::C_ModManager) and still MOUNTs, but kcdx owns WHICH mods load and in
// what ORDER. This module detours the SELECT driver (refdb curated name
// 'ModManager_Select'): it lets the original SELECT run (which builds the
// native records + runs the per-mod validation pass), THEN wholesale-REPLACES
// the engine's enabled-list vector (C_ModManager+0x30/+0x38/+0x40 =
// begin/end/end_of_storage) with a kcdx-OWNED array of synthesized I_Mod*
// pointers, in kcdx's resolved load order (enabled_list_builder::
// BuildEnabledList). The native MOUNT (id 3102) is NOT detoured — it runs
// verbatim over kcdx's rebuilt list, mounting each record's folder.
//
// PRODUCTION, not a probe: this mutates the live enabled-list vector every boot
// — it IS the feature. It runs in production (no dev-mode gate on the install).
// Verbose per-record logging is dev-log-routed; the takeover summary is INFO.
//
// The wholesale-REPLACE (repoint the vector at a kcdx-allocated array, AFTER
// the native validation pass already ran) is the mechanism the binary accepts:
// a kcdx-allocated I_Mod record with the resolved I_Mod vtable pair mounts
// cleanly. Growing the live vector mid-validation does NOT work; this module
// never appends — it replaces.

namespace kcdx::mod_absorb {

// Install the production SELECT detour against the running WHGame.dll. The
// target resolves via refdb curated name 'ModManager_Select'. Idempotent (a
// no-op returning true if already installed this session). Returns true on a
// successful install (or already-installed), false if WHGame.dll is not mapped,
// the address does not resolve, or MinHook fails. Called from dllmain at the
// ModLoaderTakeoverArmed init phase — after discovery + load_order::Resolve
// (ctx-A) and the pak-mod version gate (ctx-B VersionDetected) have run, so by
// the time the detour FIRES the resolved enabled state is ready to read.
bool InstallSelectDetour();

// Create the readiness event the SELECT detour callback waits on. MUST be
// called once on the worker thread BEFORE InstallSelectDetour — the SELECT
// detour goes live the moment InstallSelectDetour returns, and the game thread
// can reach HookedSelect within milliseconds; the event handle must already
// exist by then so HookedSelect's gate is TRUE and the wait blocks (rather
// than falling through to a not-yet-built g_enabledList). Idempotent — a
// second call returns immediately. The event is manual-reset, initially
// unsignaled; BuildEnabledListOnWorker signals it after the build completes.
// Event creation is co-located with the event consumer (this file) so the
// lifetime is owned end-to-end by the same translation unit.
void CreateReadyEvent();

// Build the enabled I_Mod* list on the WORKER thread (eager), populating the
// module-static g_enabledList + diagnostic g_entries, then SetEvent on the
// manual-reset readiness event (created earlier by CreateReadyEvent). Called
// once from the worker thread immediately after DiscoverAndLoad finishes (so
// the plugin manifests are populated) and before save_load_hooks::Install (so
// the worker's hot path "install hooks -> discover -> build list -> signal" is
// contiguous). Idempotent — a second call returns immediately. The event is
// manual-reset, so once signaled it stays signaled; a SELECT-detour callback
// that arrives after the signal returns from its wait immediately. Error-logs
// if the readiness event was not created (programming error — the worker must
// call CreateReadyEvent first).
//
// Decouples the BUILD from the FIRE (the inline build inside HookedSelect ran
// on the game's main thread, blocking it for the construction time). The
// game-thread observable outcome is unchanged: the wholesale-replace happens at
// the same point in time inside HookedSelect, with the same list. Only the
// thread that BUILT the list moved.
void BuildEnabledListOnWorker();

}  // namespace kcdx::mod_absorb
