# Phase 1 — keystone probe (observe before wiring)

The mechanism is settled (kcdx owns the VM, the engine adopts it); WHICH interception
point is safe, whether the boot-asset swap is reachable, and which early-slot shape
runs safely are **checkable unknowns**. This phase observes them under a force-load
BEFORE any later phase builds on them (`.claude/rules/results-driven.md`). Its output
is the decided mechanism detail every later phase consumes.

The probe is agent-written, agent-built, agent-deployed; the user only launches
(`.claude/rules/agent-builds-and-deploys.md`). Its finding + wiring are captured to
`_research/probe-archive/` and the probe leaves NO residue in live source
(`.claude/rules/working-artifacts.md`).

## Step ledger (step-grain)

Status: `NOT STARTED` · `BLOCKED` · `DONE` · `NEEDS REWORK`. Commit = short hash when
`DONE`, `—` otherwise.

| Step | Status | Commit |
|---|---|---|
| [1 — probe Init + lua_newstate; settle intercept/boot-swap/slot](step-1-probe-init-vm-adoption.md) | NOT STARTED | — |

## Phase verification gate

- The probe fires under a force-load and its `kcdx-dev.log` lines answer all four §4
  questions (intercept-point safety, single-VM validation, boot-swap reachability,
  early-slot shape), each against its pre-committed outcome map.
- The intercept point, the boot-swap-reachability verdict, and the early-slot shape
  are recorded back into [`../lua-vm-design.md`](../lua-vm-design.md) §4/§5 + its
  changelog (the design's probe-gated decisions resolve to observed answers).
- Finding + wiring archived to `_research/probe-archive/`; live source byte-identical
  to its pre-probe state (no residue).
- **A boot-swap-reachability FAIL (§4.3 outcome 2/3) is a HALT** — the user-required
  KI-0005 capability is not delivered by the mechanism as designed; surface before
  proceeding to later phases that assume it.
