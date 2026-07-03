# KI-0028 PROBE Z9 — shader-system ready-flag ground truth (RETIRED, question answered)

**Retired:** 2026-07-03 · **Verdict:** the condvar-stall thesis is FALSIFIED.

## Question

Reframe 16 theorized: Main is stuck in a CShaderMan `condition_variable::wait_for` on a ready-flag
`[wait_obj+0x50]` that never flips swap-ON → shader system never ready → `draw_indexed=0` / black.
Z9 hooked the wait-loop entry `0x9ace14`, captured the wait object, and a watcher polled
`[wait_obj+0x50]` + the producer singleton `[0x492b8c8]`.

## Result (run `kcdx-dev_2026-07-03_10-16-32.log`, swap-ON black arm)

`READY_PROBE_VERDICT wait_entered=1 flag_ever_set=1 producer_installed=1 wait_enters=13981`
- **`READY_FLAG_SET wall_ms=7140`** — the ready-flag FLIPPED to non-zero 7.1 s in. Shader system
  BECAME READY on the black arm.
- The wait loop was entered **13,981 times** over ~2 min ≈ per-frame cadence.

## Meaning (theory FALSIFIED)

`0x9ace14` is a **PER-FRAME** shader-coordination wait that COMPLETES every frame — NOT a one-shot
boot gate that hangs. The two byte-identical invasive samples that spawned the "Main is stuck"
reading caught this recurring per-frame wait mid-block (the FULL-HANDOFF §13 per-frame trap, same
illusion PROBE M killed for `0x869c39`). The CShaderMan condvar wait is EXONERATED as the wedge;
`draw_indexed=0` is downstream of / unrelated to it. Full detail:
`_research/ki0028-mainthread-condvar-wait-recon/FINDINGS.md` §"PROBE Z9" + KI-0028 Reframe 17.

## Reusable wiring (for the next investigation)

The scratch-RVA MinHook after-hook pattern on a WHGame function prologue + a bounded Sleep-cadence
watcher with a terminal verdict + operator done-signal (window retitle + taskbar flash). Reconstruct
from the co-located source below, NOT from live `src/` (removed on retirement per no-residue).

- `ki0028-probeZ9-producer_ready_probe.cpp` — the hook + watcher (verbatim copy of the retired
  `src/fs_takeover/producer_ready_probe.cpp`).
- `ki0028-probeZ9-producer_ready_probe.h` — the WHY + outcome→meaning map.

Static read it rested on: `_research/ki0028-mainthread-condvar-wait-recon/disasm_condvar_wait.py`
→ `_condvar_owner.txt` (the CShaderMan pump-loop `0x9ace14` body).
