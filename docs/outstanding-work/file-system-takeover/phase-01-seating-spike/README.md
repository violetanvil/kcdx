# Phase 1 — seating spike + in-flight cleanup

Prove the load-bearing takeover mechanism cheaply, and clear the in-flight
residue, BEFORE the large build. The vtable swap, the thunk approach, and the
construction-timing assumption are all provisional (design §8) — this phase
proves them on a minimal all-thunks stub vtable so a wrong outcome costs one cheap
phase, not the whole takeover.

Shared spec: [`../plan-spec.md`](../plan-spec.md).

## Step-grain ledger

| Step | Status | Commit |
|---|---|---|
| [1.1 — correct KI-0019/KI-0006 routing](step-1-ki-routing.md) | DONE | 4164a4e |
| [1.2 — remove PROBE F + capture](step-2-probe-f-removal.md) | DONE | a65c3f2 |
| [1.3 — probe P1 (CCryPak construction timing)](step-3-probe-ctor-timing.md) | DONE | f28cd39 |
| [1.4 — stub vtable + swap + probe P2/P4](step-4-stub-vtable-swap.md) | DONE | 3be161a |

## Verification gate (phase done when)

- 1.1/1.2: docs + source clean — KI docs route to this design; no PROBE F residue
  in `src/asset_overlay.cpp` (the no-residue rule); build green.
- 1.3 (P1): a launch logs `*(gEnv+0x50)` timing vs the ready-bracket + first-file
  call — the swap window is established (or the design's swap-point assumption is
  falsified and the seating anchor is revised before 1.4).
- 1.4 (P2/P4): a launch with the stub vtable swapped in fires the slot-36 marker
  on first vanilla open (P2 — engine dispatches into kcdx) AND the game boots +
  reaches the world normally (P4 — every thunked slot runs correctly against the
  swapped object). Both are agent-read from `kcdx-dev.log` via the
  `ACCEPT-RESULT`/matrix signal; the user performs only the launch.

A wrong P1/P2/P4 outcome HALTS the plan here — the real-slot build (Phase 3) does
not proceed on an unproven seating mechanism.

### 1.4 outcome — swap-mechanism gate MET (run 2026-06-15, `3be161a`)

P2 and P4 both PASS: the launch (run 2026-06-15 10:06:34) seated the swap and
went live — `FS_TAKEOVER vtable_swapped` then `swap_live_first_open
first_vpath=./system.cfg` (P2: the engine dispatched its first vanilla open into
kcdx's slot 36), and the boot reached the world through the 101 thunked slots,
`cap-108-fs-takeover-seating` PASS / `ACCEPT-SUITE 1/1` (P4: the thunks are sound
against the swapped, layout-preserved object). The swap MECHANISM is proven —
kcdx seats + holds, the engine dispatches into kcdx, the game boots.

The phase's KI-0019/KI-0006 RESOLUTION half is **NOT** met by this spike, and was
never going to be: only slot 36 is kcdx-owned; the read family (FSeek/FClose/
FRead, …) is still `THUNK(original)` on the engine's CRT, so the cross-CRT
inventory-open `fseek` crash STILL reproduced with the swap active (32×
`engine.ccrypak_fopen` FAULTED_FIRE + a `ucrtbase!fseek → null EACCES write`
dump, AFTER the swap — KI-0019 Trail H; evidence `_research/probe-archive/p2-p4-seating-and-ki0019-persists.md`).
That crash resolves at step 4.2 (read-family slots flipped THUNK→KCDX), per
design §9. So the seating-mechanism gate is the green half; the crash-resolution
gate stays open until 4.2 — the phase-grain row is NOT flipped to DONE on this
step alone.
