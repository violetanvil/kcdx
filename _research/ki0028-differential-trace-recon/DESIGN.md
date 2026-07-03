# KI-0028 — the differential execution trace (METHOD RESET, 2026-07-03)

## Why this exists — the method was wrong for ~50 probes

Every KI-0028 probe to date (Z1–Z9, P/R/S/U/W/M, etc.) was a **spot-check**: "is it THIS gate / flag / slot?"
— a single localized yes/no, then pick the next spot to check when it's no. Fifty spot-checks across a 90MB
no-PDB binary produce fifty dead ends BY CONSTRUCTION: nothing forces consecutive checks to be adjacent on the
real execution path. Each probe was individually falsifiable (results-driven's letter) but the SEQUENCE was
theory-hopping (results-driven's spirit, violated). We kept observing a SYMPTOM site (main's stack, draw_indexed=0,
a flag) and inferring the cause BACKWARD — and backward-inference from one symptom always yields many
plausible-but-wrong theories. Latest casualty: Reframe 15/16 read "2 identical invasive samples" as "stuck" when
the wait was per-frame (Z9 falsified it, Reframe 17).

**User decision (2026-07-03): STOP spot-checking. Build a differential execution trace.** We have a known-GOOD
arm (swap-OFF → menu, draw_indexed>0) and a known-BAD arm (swap-ON → black, draw_indexed=0). The single decisive
artifact is not another probe — it is the **ordered call sequence on BOTH arms, DIFFED to find the FIRST
divergence point.** The divergence point IS the answer, mechanically, with no theory.

## What still stands (proven; do NOT re-litigate — these are the trace's anchors)

- Geometry buffers ARE created swap-ON (Z8: geo_buf=262, 614MB) but no index buffer is ever BOUND
  (ia_set_ib=0) and no indexed draw recorded (draw_indexed=0). Present advances ~310fps; frames are black.
- The FS-takeover vtable SWAP is the differentiator (P-F: swap-OFF→menu, swap-ON→wedge). Every kcdx SERVED
  output (bytes, handles, enum counts, the 4 slot contracts) is correct/identical (§8, §9.5, §9.6 — FS exonerated).
- The CShaderMan condvar wait 0x1c1e7e0/0x9ace14 is PER-FRAME and COMPLETES (Z9) — a dead end, NOT the stall.
- Nearest-export cdb labels are NOISE; per-frame stack frames are traps (§13). The bug is a kcdx-perturbed
  STATE/init-ORDER the geometry-build depends on (§9 net), NOT a served output.

## The trace design (to build — this is the spec, not yet built)

GOAL: find the FIRST engine call/decision that differs between swap-ON and swap-OFF, downstream of the seat,
on the path from "geometry created" to "indexed draw recorded" (or wherever it actually diverges).

Design constraints (learned the hard way):
- **Ordered + comparable across arms.** Each arm emits a sequence of (site, arg-summary) records to its own log,
  timestamped + sequence-numbered, so an offline DIFF aligns them and names the first divergence.
- **Armed BEFORE the kcdx-noswap early-return** (the PROBE W/K/P A/B pattern in seating_hook.cpp) so swap-ON
  and swap-OFF instrument the SAME phase. swap-OFF emits no kcdx FS trace, so the trace must key on ENGINE
  sites, not FS slots.
- **Bounded, not "trace everything."** 90MB of instructions is not traceable naively. Scope to the render/
  geometry-submission subsystem: instrument the call edges BETWEEN "shader system ready" (0x9ace14 completes)
  and "IASetIndexBuffer" (the drawcall_probe's ia_set_ib site) — the window where the two arms MUST diverge
  (good arm reaches the bind, bad arm doesn't). Candidate anchor set to resolve first (reuse-first ladder):
  the render-item append leaf, the scene-traversal/cull entry, the draw-record dispatcher upstream of
  IASetIndexBuffer. A static Ghidra call-graph from IASetIndexBuffer BACKWARD names the real edges to instrument
  (so the trace targets real adjacent edges, not guesses).
- **Survives no-PDB.** Record module-relative RVAs (VA − WHGame base), resolve offline. Never trust cdb's
  nearest-export symbol.
- **Per-frame-safe.** The good arm records indexed draws every frame; the bad arm doesn't. Capture the FIRST
  frame's divergence, or a one-shot latch per site, so 14k per-frame repeats don't bury the signal (the Z9 lesson).

## Next actions (fresh context)

1. Static FIRST (reuse-first, no launch): Ghidra call-graph BACKWARD from IASetIndexBuffer (and/or the
   drawcall_probe's recorded draw-record site) to build the real render-submission edge list on BOTH the
   geometry-create side and the draw-record side. This names WHICH edges the trace instruments — no guessing.
2. Build the ordered differential tracer over those edges (armed pre-noswap for A/B), emitting sequenced RVA
   records per arm.
3. Run swap-ON + swap-OFF, DIFF the two sequences, name the FIRST divergence. That is the answer.

## Uncommitted probe state (no-residue bookkeeping)
- `producer_ready_probe.{h,cpp}` (Z9) — question ANSWERED (condvar exonerated). Retire (remove from source/
  seating/CMake) OR keep briefly; finding is in the recon FINDINGS + Reframe 17.
- `drawcall_probe.cpp` (Z8 + ia_set_ib/draw_indexed counters) — KEEP; ia_set_ib/draw_indexed are the trace's
  confirm signal + the draw-record site is a trace anchor.
