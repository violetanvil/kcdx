// === DIAGNOSTIC (PROBE M) — KI-0028 window/display-mode loop exit-condition read ===
//
// WHY: static disassembly (FINDING-real-rva-window-mode-loop.md) pinned the wedge to
// the window/display-mode fn at WHGame RVA 0x869c39, whose outer loop re-runs WHILE a
// completion counter is != -1. Static cannot settle WHICH task's completion the counter
// tracks, who the producer is, or how the FS-takeover swap stalls it — those are runtime
// facts (results-driven §4). This probe OBSERVES the loop's exit-condition globals live,
// swap-on vs swap-off, so the comparison falsifies "the swap leaves this counter stuck".
//
// It reads 6 WHGame .data globals (resolved as WHGame_base + RVA) on a bounded diagnostic
// cadence and logs their RAW values — no theory-shaped test, ground truth first:
//   0x56628d8 / 0x56628dc  (dword)  — the two loop retry counters (loop EXITS when == -1)
//   0x556d080  (byte)               — a result flag the window-mgr vtable calls set
//   0x556d084  (dword)              — a result flag the window-mgr vtable calls set
//   0x492b890 / 0x492b8c0 (qword)   — window-manager singleton pointers (null => not built)
//
// DIAGNOSTIC-POLL: the watcher thread reads on a timer. User-approved per polling.md for
// THIS probe (2026-06-21), as a probe, NOT production code — removed with the probe on
// retire. The established KI-0028 diagnostic-thread shape (boot_watch / present_probe).
//
// Outcome→meaning map (pre-committed, flat — read swap-ON vs swap-OFF):
//   counter(s) hold a fixed nonzero value the whole swap-ON run, but reach -1 (or cycle to
//       -1) on the swap-OFF run → the swap leaves a registered task uncompleted → that
//       counter names the task; next: who registers it (0x1c1e91c caller) + who should
//       complete it.
//   counter(s) reach -1 on BOTH runs → this loop is NOT the wedge (it exits both times);
//       the per-frame re-entry from above is → widen up the stack.
//   counters identical on both runs but a FLAG (0x556d080/084) or a SINGLETON
//       (0x492b890-family) differs → the differing value is the perturbation; next: what
//       writes it.
//   a singleton is null swap-ON but populated swap-OFF → the swap prevents the window-mgr
//       object's construction → next: what builds it and why the swap blocks it.
//
// NO-RESIDUE: on retire, capture the readings to _research/probe-archive/ then remove this
// file + its two arm calls in seating_hook.cpp (it must NOT survive in live source).

#pragma once

namespace kcdx::fs_takeover {

// Arm the loop-state watcher thread once. Idempotent. Safe to call on BOTH the
// swap-ON and swap-OFF (kcdx-noswap) seating paths — the probe reads the same
// globals regardless of whether kcdx owns the filesystem this boot.
void LoopStateProbeStart();

}  // namespace kcdx::fs_takeover
