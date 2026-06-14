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
| [1.1 — correct KI-0019/KI-0006 routing](step-1-ki-routing.md) | NOT STARTED | — |
| [1.2 — remove PROBE F + capture](step-2-probe-f-removal.md) | NOT STARTED | — |
| [1.3 — probe P1 (CCryPak construction timing)](step-3-probe-ctor-timing.md) | NOT STARTED | — |
| [1.4 — stub vtable + swap + probe P2/P4](step-4-stub-vtable-swap.md) | NOT STARTED | — |

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
