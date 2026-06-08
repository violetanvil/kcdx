# Phase 4 — foreign-hook coexistence

**Intent.** Make kcdx a good citizen alongside another mod that hooks the same
function. Two ordered steps: detect a pre-existing foreign hook (the prologue
classifier — clean / kcdx-trampoline / foreign), then chain onto it (follow the
foreign jump, capture the foreign detour as kcdx's "original") so both mods'
hooks fire. The `comp-NN` two-mod fixture proves both fire in a defined order.
This is the one capability ADDED above the patcher (design §4.3, §6) — the chain
itself is unchanged for the kcdx-vs-kcdx case.

Shared spec: [`../context.md`](../context.md).

## Status ledger (step-grain)

| Step | Status | Commit |
|---|---|---|
| Step 7 — foreign-hook detection (prologue classifier) | NOT STARTED | — |
| Step 8 — foreign-hook chaining + comp-NN fixture | NOT STARTED | — |

## Verification gate

- Step 7: a unit-level classifier test (synthetic prologues — clean game bytes,
  a jump into a kcdx trampoline range, a foreign E9/FF25 into an unknown range)
  classifies each correctly; runnable at this step. Build green; full cap-NN
  suite unregressed.
- Step 8: the `comp-NN` two-mod fixture (a synthetic foreign E9 on a target, then
  kcdx hooks it) shows BOTH detours fire in the defined order
  (game → kcdx → foreign → original); agent builds+deploys, user launches, agent
  reads the log.
