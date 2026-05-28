#pragma once

// === ModManager_ctor read-only state probe — TRANSIENT ======================
//
// Observe-only MinHook detour on wh::C_ModManager ctor (Address Library id
// 3101). Resolves the address by ID, snapshots the input args (sys, modsDir),
// lets the original ctor run unchanged, then dumps every non-zero 8-byte slot
// of the resulting 0x68-byte C_ModManager state to the dev log under the
// category "MOD_ABSORB_PROBE". One-shot per session (a second fire is
// guarded). No behavior change — the original ctor runs exactly as it would
// without the probe.
//
// The state snapshot captures STATE only — bytes written into the
// C_ModManager. The ctor's documented out-of-band side effect (registering
// the IConsole command `wh_mod_GenerateReport`) is NOT visible in the
// C_ModManager bytes and is NOT captured here. That side effect is verified
// out-of-band, by the eventual step-5 test plugin running the command via the
// console and confirming the response. The probe addresses only the question
// the bytes can answer: WHICH FIELDS DOES THE CTOR WRITE?
//
// === Why this probe exists (the falsifiable question) ======================
//
// A later step replaces the original ctor + SELECT call entirely with a
// kcdx-owned init bracket. Before that replacement lands, we must verify the
// seed.csv id 3101 prose is COMPLETE — that the only effects of the original
// ctor on the C_ModManager state are: vptr (+0x00), sys (+0x08), modsDir
// (+0x10), a sub-object vptr at +0x18, the mod-list pointer fields (zero-
// initialized), and the call to SELECT (which we already handle separately).
//
// The probe asks: *Does ModManager_ctor write any C_ModManager field other
// than vptr(+0x00), sys(+0x08), modsDir(+0x10), sub-vptr(+0x18), and the
// list-pointer slots (zeroed)?*
//
// === Outcome map (falsifiable; copy verbatim into launch instructions) =====
//
//   Outcome A — exit-state diff shows ONLY writes inside the seed-predicted
//     range: vptr(+0x00), sys(+0x08), modsDir(+0x10), sub-vptr(+0x18), and
//     the zero-init slots +0x20..+0x50 (the ctor's "zero-inits +0x18..+0x58"
//     range per seed row 3101 — these read zero, the writes are the act of
//     zeroing). +0x58..+0x68 is all zero. → ctor is fully replaceable; step 2
//     proceeds.
//
//   Outcome B — exit-state diff shows a non-zero write at +0x58 or +0x60 (the
//     post-zero-init slots the seed prose does NOT predict), OR any write at
//     all outside the seed-predicted +0x00..+0x58 range → step 4 must
//     replicate that write OR explain why kcdx can skip it. Surface to user
//     before step 2 lands.
//
//   Outcome C — vtable values at +0x00 / +0x18 do not match Address Library
//     id 3105 / 3106 resolution → seed evidence is stale; surface
//     immediately (do not proceed with this architecture until corrected).
//
// === Lifetime ==============================================================
//
// This probe is TRANSIENT. Once a later step of the init-cycle-ownership
// feature replaces the original ctor entirely (the bracket replaces the call,
// so kcdx becomes the writer of C_ModManager state), this probe's question is
// moot — there is no engine-side state to compare against because kcdx writes
// every field itself. Delete this file (.h + .cpp + the install call in
// dllmain.cpp) at that point. Tracked in
// docs/outstanding-work/init-cycle-ownership.md.

namespace kcdx::mod_absorb::ctor_probe {

// Install the read-only ctor probe detour. Worker-thread call (B-context,
// after EngineHooksInstalled, before InstallSelectDetour). Idempotent:
// repeated calls return the cached result. Logs Install failure loudly under
// category "MOD_ABSORB_PROBE" and returns false; the rest of boot continues
// (an inactive probe never affects gameplay — it would only fail to ANSWER
// the question this boot).
//
// Returns true on a successful install OR if an install already succeeded
// earlier this session. False on any failure (id 3101 did not resolve,
// MH_Initialize failed, MH_CreateHook/Enable failed).
bool Install();

}  // namespace kcdx::mod_absorb::ctor_probe
