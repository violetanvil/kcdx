# 3.3 [ENG] The LIVE functional check — resolve via the real engine path, confirm it lands on real working code

## What

Implement the LIVE functional check (D25) — the half the browser cannot do: resolve a row's
address via the REAL engine resolve path in the running game, then confirm it lands on real
working code (the body-hash at the *resolved runtime address* matches; not `0`/garbage; where
feasible a probe fires / a vtable slot points at a real method). This catches "resolves to
wrong/dead code even if the static bytes look right" — the verdict the static check cannot
produce. Per row it yields `resolves+works` / `dead` / `wrong-target` / `cannot-check`. This is
the engine-only authority the in-game batch plugin (Phase 4) drives.

## Scope

One commit in kcdx `src/`: the live functional check — resolve via the engine path, confirm the
resolved runtime address lands on real working code, classify the verdict — as an engine entry
point a test plugin can call. No batch plugin (Phase 4); no JSON report (Phase 4); no agreement
test (step 4).

## Test bar

A kcdx test-suite plugin row asserting the live check returns `resolves+works` for a known-good
row and `dead` / `wrong-target` for a synthetically-broken row (the discriminating signal 0.4
de-risked). The agent builds, deploys, hash-verifies, enables dev mode; the user launches; the
agent reads PASS from `kcdx-dev.log` (`.claude/rules/agent-builds-and-deploys.md`). A matrix row
is recorded. Runnable at this step (the engine resolve path + the 0.4-confirmed signal exist) —
`.claude/rules/test-discipline.md`, `.claude/rules/incremental-delivery.md`.

## Dependencies

- **3.1** — the per-kind dispatch (the live check joins it).
- **0.4** — the live-functional signal probe (confirmed the in-game signal exists + discriminates
  resolves+works / dead / wrong-target).

## Reference

[`../plan-spec.md`](../plan-spec.md) — Group B (the LIVE functional check); cross-step invariant
3 (the live-functional half is engine-only by nature, D27).

## Design authority

`data/maintainer-tool/design.md` **D25** (the LIVE FUNCTIONAL meaning — resolve via the real
engine path in the running game, confirm it lands on real working code; the verdicts
`resolves+works` / `dead` / `wrong-target`) + **US-11** §"Live functional, in bulk, in-game".
Build to D25's definition, not to this doc's summary.

## UX

Not a UI step (engine code). The only user gesture is the game launch
(`.claude/rules/agent-builds-and-deploys.md`).

## Disassembler-test / author-burden

None — the engine resolves + confirms; the author authors nothing new. The live check is the
strongest evidence the engine produces FOR the author, not a burden ON them.
