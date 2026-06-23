#pragma once

// === DIAGNOSTIC (PROBE U) — KI-0028 post-seat CCryPak vtable-reswap watcher ===
//
// HYPOTHESIS (HANDLE-STRADDLE-LEAD.md CORRECTION 4 + vtable_swap.cpp read): kcdx's
// CCryPak swap is ONE-SHOT (g_swapped CAS at the construct-store seat) and never
// re-checks the object's vtable pointer afterward. If the engine RE-POINTS the
// CCryPak vtable to a render/streaming-specialized ICryPak AFTER the seat, OR
// constructs a SEPARATE CCryPak the geometry path uses, kcdx's ownership is
// silently lost mid-boot — invisible to every served-output probe (they only
// measure kcdx's OWN serve, which works on the base path).
//
// WHAT IT OBSERVES (read-only on engine memory; no hook, no engine offset hand-
// written — both addresses come from kcdx's existing resolved seat state):
//   1. [swappedObj + 0x00] — the swapped CCryPak object's vtable pointer. kcdx
//      wrote g_kcdxVtable here at the seat. If it EVER reads back as something
//      else, the engine re-pointed the object's vtable (kcdx ownership lost).
//   2. *(gEnv_pCryPak slot VA) — the global CCryPak pointer. If it EVER reads a
//      DIFFERENT object than the one kcdx swapped, a second/replacement CCryPak
//      is now the global, and the geometry path uses an object kcdx never owned.
//
// OUTCOME MAP (pre-committed, flat — theory-INDEPENDENT):
//   vtable pointer changes away from kcdx's  → the engine RE-SWAPPED the object's
//     vtable post-seat; kcdx ownership was silently lost (mechanism FOUND — next:
//     what re-swaps, when, to what).
//   gEnv slot points at a DIFFERENT object   → a separate/replacement CCryPak is
//     the live global; the geometry path uses an object kcdx never swapped
//     (mechanism FOUND — next: where that object is constructed).
//   neither ever changes (both stay kcdx's)  → kcdx owns the ONE CCryPak end-to-
//     end for the whole boot; the reswap/second-object theory is FALSIFIED and
//     the divergence is elsewhere (a non-CCryPak state the swap perturbs).
//
// Armed BEFORE the PROBE F noswap early-return so the seat capture + watcher run;
// the watcher samples a bounded count then stops (same sanctioned bounded-poll
// shape as PROBE K/W — a diagnostic interval read, not a production loop).
//
// NO-RESIDUE: removed when KI-0028 closes.

namespace kcdx::fs_takeover {

// Capture the seat ground truth + start the bounded watcher. Called from the
// seating hook right AFTER the swap takes (so swappedObj + the kcdx vtable
// address are known) and the gEnv slot VA is resolvable. `swappedObj` is the
// CCryPak object kcdx swapped; `kcdxVtable` is the address kcdx wrote into
// [swappedObj+0x00]; `gEnvSlotVa` is the VA of the global pCryPak slot (the same
// slot ReadPublishedCCryPak reads). A null arg disables the probe (logged).
void ReswapProbeStartAtSeat(void* swappedObj, void* kcdxVtable,
                            const void* gEnvSlotVa);

}  // namespace kcdx::fs_takeover
