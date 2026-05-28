#pragma once

// === C_ModManager init-cycle observation probe — TRANSIENT ==================
//
// A comprehensive read-only probe over the C_ModManager construction flow.
// Three one-shot capture points, atomic-guarded; one MinHook detour (id 3101
// ctor entry/exit) plus one in-line call from the existing SELECT detour
// (id 3100, see select_detour.cpp). The probe never mutates the engine.
//
//   POINT A — ctor entry (before the original ctor runs). Logs args
//             (outResult, sys, modsDir), the raw 0x68 bytes of *outResult
//             (uninitialized caller stack alloc; logged for diff against
//             POINT C), and a SEH-guarded 0x40-byte dump at *modsDir (the
//             ctor reads `[r14]` so arg3 is a pointer-to-something).
//
//   POINT B — SELECT entry (before the original SELECT runs). Inside the
//             ctor body, post-zero-init, pre-SELECT-body. Logs the full
//             0x68 C_ModManager state so we see what the ctor itself wrote
//             BEFORE SELECT had any chance to populate fields. Reached by a
//             call into ctor_probe::OnSelectEntry() at the top of
//             select_detour.cpp's HookedSelect — NOT a second MinHook on
//             id 3100. One hook per site.
//
//   POINT C — ctor return (existing capture point — kept). The full 0x68
//             dump WITH per-slot deref + classification + SEH-guarded
//             follow-through (CryString header at +0x10, vector walks at
//             +0x18/+0x20 and +0x30/+0x38/+0x40, vtable validation at
//             +0x00 / +0x18 / any image-pointer slot).
//
// ALL three points are guarded by separate atomics; a defensive re-fire
// forwards to the original without re-dumping.
//
// === Outcome map (TWO-BOOT comparison) =====================================
//
// Run the probe TWICE: boot A = no mods installed in <game>/mods/;
// boot B = the user's normal mods setup. Compare the two captures by:
//
//   - Field stability — values identical in both boots are STATIC state
//     (vtable, sub-vtable, sub-object init constants); values that differ
//     are per-boot DATA.
//   - Count fields — +0x48 == 15 in normal-mods + 0 in no-mods → "enabled
//     count." Same value in both boots → not a count.
//   - Vector content — Walk 1 (enabled list at +0x30/+0x38/+0x40) should
//     yield count=0 in the no-mods boot, count=N in normal-mods. Walk 2
//     (scanned list at +0x18/+0x20) similarly.
//   - ASCII at +0x30 — if "mods" appears in BOTH boots, +0x30 is a stable
//     inline string or a stable pointer; if only in one boot, +0x30 holds
//     per-boot data.
//
// Outcomes that resolve the architecture:
//
//   Outcome 1 — Walk 1 yields a valid I_Mod* vector in the normal-mods
//     boot, count matches the number of mods, *begin's vtable matches
//     Address Library id 3105. → +0x30/+0x38/+0x40 IS the enabled-list
//     vector (confirms the absorb-doc); the "mods" ASCII observed in the
//     no-mods boot was from an EMPTY vector's begin pointer being a stale
//     or sentinel value.
//
//   Outcome 2 — Walk 1 yields garbage or AVs in BOTH boots. → +0x30/+0x38/
//     +0x40 is NOT a vector triple as the absorb-doc claims. The existing
//     select_detour.cpp is reading wrong offsets and happens to work by
//     accident. Step 4 must find the real enabled-list offsets.
//
//   Outcome 3 — Walk 1 works in normal-mods, but the POINT B (SELECT
//     entry) capture shows +0x30/+0x38/+0x40 NON-ZERO before SELECT runs.
//     → The ctor populates the vector itself (not SELECT). Re-read
//     disassembly.
//
//   Outcome 4 — +0x10 CryString header reads garbage (nLength > 1000 or
//     nRefs > 1000 or chars do not form a valid string). → modsDir is not
//     a CryString IN-PLACE at +0x10; it is something else. Step 4's
//     replacement ctor must NOT placement-construct a CryString at +0x10.
//
//   Outcome 5 — +0x00 vtable's first 8 function pointers include any
//     non-code pointer. → +0x00 is not a typical vtable, or the slot
//     table has data interleaved. Step 4's replacement ctor must use a
//     different vtable layout.
//
//   Outcome 6 — +0x48 / +0x50 / +0x58 / +0x60 differ between boots in
//     patterns consistent with counts/timestamps/refcounts. → These are
//     per-boot DATA fields step 4 must populate; classify by delta.
//
// Any other surprise — surface to user.
//
// === Lifetime ==============================================================
//
// This probe is TRANSIENT. Once a later step of the init-cycle-ownership
// feature replaces the original ctor entirely (kcdx becomes the writer of
// C_ModManager state), this probe's question is moot. Delete this file
// (.h + .cpp + the install call in dllmain.cpp + the OnSelectEntry call
// in select_detour.cpp) at that point. Tracked in
// docs/outstanding-work/init-cycle-ownership.md.

namespace kcdx::mod_absorb::ctor_probe {

// Install the read-only ctor probe detour (POINTS A and C). Worker-thread
// call (B-context, after EngineHooksInstalled, before InstallSelectDetour).
// Idempotent: repeated calls return the cached result. Logs install failure
// loudly under category "MOD_ABSORB_PROBE" and returns false; the rest of
// boot continues (an inactive probe never affects gameplay — it only fails
// to ANSWER the question this boot).
//
// Returns true on a successful install OR if an install already succeeded
// earlier this session. False on any failure (id 3101 did not resolve,
// MH_Initialize failed, MH_CreateHook/Enable failed).
bool Install();

// POINT B entry — called from select_detour.cpp's HookedSelect at the TOP
// of dispatch, BEFORE the captured original SELECT runs. One-shot; a second
// fire returns immediately without dumping. SEH-guards every deref; safe to
// call with any `self` value (a null self is logged and returns).
//
// This is NOT a second MinHook on id 3100 — the production SELECT detour
// owns the only hook at that site, and this is a plain call into the probe
// from inside that detour. One hook per site.
void OnSelectEntry(void* self);

}  // namespace kcdx::mod_absorb::ctor_probe
