#pragma once

// Ctor bracket — kcdx OWNS the C_ModManager construction. Replaces the prior
// SELECT detour (which let the native ctor + SELECT run, then wholesale-
// repointed the enabled-list vector) with a FULL REPLACEMENT of the native
// ctor: kcdx allocates the 0x68-byte C_ModManager via WHGame's allocator,
// writes the four "ctor" slots itself (vtable / sys / modsDir CryString /
// init-flag), and writes the enabled-list vector triple kcdx pre-built on
// the worker thread directly. The native ctor + the native SELECT are NEVER
// called.
//
// Layout written into the 0x68 block (verified live two-boot against the
// running binary):
//
//   +0x00 vtable        ← refdb("C_ModManager_vtable")
//   +0x08 sys           ← arg2 of the ctor call (rdx)
//   +0x10 modsDir       ← CryString built in-place via refdb("CryString_
//                          init_from_string") + refdb("CryString_placement_
//                          construct"), seeded from arg3 (a ptr to the
//                          modsDir char string)
//   +0x18..+0x28 (zero) ← scanned-list triple; MOUNT does not iterate it
//   +0x30 enabled.begin ← &g_enabledList[0]   (kcdx's pre-built I_Mod* array)
//   +0x38 enabled.end   ← &g_enabledList[N]
//   +0x40 enabled.cap   ← &g_enabledList[N]   (vector at-capacity)
//   +0x48..+0x58 (zero) ← unused in both observed boots; left zero
//   +0x60 init flag     ← byte 1
//
// The kcdx-built enabled-list (g_enabledList) is owned process-lifetime by
// select_detour.cpp (the worker-side machinery that survived step 4); the
// bracket only reads its data()/size() under the readiness event that
// signals the build is complete.
//
// THREADING:
//   - Install runs on the worker thread (before WHGame's CSystem::Init can
//     reach ModManager_ctor) — same race-window concern that drove the
//     CreateReadyEvent ordering.
//   - HookedCtor fires on the GAME's main thread, inside CSystem::Init. It
//     acquire-loads the readiness event handle (created earlier by the
//     worker via mod_absorb::CreateReadyEvent), waits INFINITE, then writes
//     all 0x68 bytes and returns.
//
// PRODUCTION, not a probe. This is the live mod-loader takeover.

namespace kcdx::mod_absorb {

// Install the MinHook detour on ModManager_ctor (refdb curated name
// "ModManager_ctor"). Idempotent — a second call returns the cached result.
// Logs install failure LOUD under MOD_ABSORB and returns false; the rest of
// boot continues (an inactive bracket means the native ctor + SELECT will
// run, but the kcdx enabled-list build still occurs on the worker — the live
// game just sees the vanilla list).
//
// Must run on the worker thread, AFTER CreateReadyEvent (so the readiness
// event handle exists by the time HookedCtor can fire on the game thread)
// and BEFORE BuildEnabledListOnWorker (so HookedCtor's wait is guaranteed
// to be on a building, not a built, list — the wait stays correct either
// way; this just preserves the same install-before-build ordering the prior
// SELECT-detour install used).
//
// Returns true on a successful install OR if an install already succeeded
// earlier this session. False on any failure (refdb lookup miss, MinHook
// init failure, hook create/enable failure).
bool InstallCtorBracket();

}  // namespace kcdx::mod_absorb
