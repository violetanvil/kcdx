#pragma once

// === ARCHIVED PROBE B (2026-05-28): the engine dispatched on the C_ModManager vtable VA itself, not on kcdx's heap obj — the bracket built a modMgr the engine never read through this path.
// === Root cause: ctor bracket returned the heap obj instead of outResult; the install helper's `mov rax, [rdx]` then loaded the vtable VA from the heap block's +0x00 and installed it as the modMgr pointer.
// === See: docs/known-issues/post-step-4 AV at WHGame+0x2440C85.md §Resolution.
// === Revive by flipping #if 0 -> #if 1.
#if 0

// PROBE B — post-step-4 AV at WHGame+0x2440C85 known-issue investigation.
//
// Observe-only MinHook detour on the WHGame function at RVA 0x019C6268 (frame
// 4 of the crash stack: the one that does
//   `mov rcx,[global];  call vt[+0xB8];  ... ; lea rcx,[rbx+0x30]; call FUN_1DBBE20`,
// i.e. the "look up a name in the modMgr's enabled-list" call site that's
// preceded by the AV inside FUN_2440C6C).
//
// The detour logs once on first entry (acq-rel one-shot) the following raw
// fact set, side-by-side, then tails to the original:
//
//   - rcx_arg                            (the modMgr `this`)
//   - rdx_arg                            (the secondary arg)
//   - kcdx_obj                           (the modMgr kcdx synthesized)
//   - g_enabledList_data                 (the begin pointer kcdx wrote into modMgr+0x30)
//   - g_enabledList_size
//   - [rcx+0x00] (vtable)
//   - [rcx+0x08]
//   - [rcx+0x10] (modsDir CryString data ptr)
//   - [rcx+0x18..+0x28] (scanned list triple)
//   - [rcx+0x30..+0x40] (enabled list triple)
//   - [rcx+0x60] (init flag)
//
// Every memory deref outside rcx itself is SEH-guarded so a bad rcx (or a bad
// inner pointer) yields a "FAULTED" line rather than another bugsplat — the
// probe must survive what the AV showed us.
//
// THREADING. Install runs on the kcdx worker thread, after InstallCtorBracket
// (so the bracket-install path is unchanged). Fire happens on the game's main
// thread inside CSystem::Init, the same thread the AV happened on.
//
// PROBE LIFECYCLE. This file gets reverted before the next probe per
// .claude/rules/results-driven.md §Probe-revert hygiene. NOT a production
// surface; do not consume from anywhere else in kcdx.

namespace kcdx::mod_absorb {

// Install the post-bracket probe MinHook detour on the frame-4 function at
// RVA 0x019C6268. Idempotent. Logs install failure LOUD under MOD_ABSORB
// category and returns false; the bracket / boot continues.
//
// Returns true on a successful install OR on a no-op repeat call. False on any
// failure (MinHook init failure, hook create/enable failure, WHGame.dll not
// mapped).
bool InstallPostBracketProbe();

}  // namespace kcdx::mod_absorb

#endif  // ARCHIVED PROBE B
