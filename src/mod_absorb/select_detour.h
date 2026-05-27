#pragma once

// SELECT detour — the production mod-loader takeover. STEP 4 of the mod-loader
// absorb. docs/mod-loader-absorb.md "Step 4".
//
// kcdx IS the mod loader. The native engine still constructs the manager
// (wh::C_ModManager) and still MOUNTs, but kcdx owns WHICH mods load and in
// what ORDER. This module detours the SELECT orchestrator (ModManager_Select,
// Address Library id 3100): it lets the original SELECT run (which builds the
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
// target is Address Library id 3100 (ModManager_Select). Idempotent (a no-op
// returning true if already installed this session). Returns true on a
// successful install (or already-installed), false if WHGame.dll is not mapped,
// the address does not resolve, or MinHook fails. Called from dllmain at the
// ModLoaderTakeoverArmed init phase — after discovery + load_order::Resolve
// (ctx-A) and the pak-mod version gate (ctx-B VersionDetected) have run, so by
// the time the detour FIRES the resolved enabled state is ready to read.
bool InstallSelectDetour();

}  // namespace kcdx::mod_absorb
