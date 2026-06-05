# 3.2 [ENG] The 5 static non-function kind checks + the anchor-dependency ordering

## What

Implement the 5 static non-function per-kind checks in the C++ engine (under the step-1
dispatch): callsite (AOB scan of `.text`), string_anchor (`.rdata` literal search +
`expect_unique` xref), instruction_anchor (resolver-chain re-derivation), data_slot (derivation
re-run, no content hash), vtable_base (table-shape: N qwords each → `.text`) — plus the
**anchor-dependency ordering** (run survival in dependency order; a dependent kind whose anchor
is Changed is transitively CannotCheck). vtable_index returns CannotCheck (population deferred).
This makes the engine the full per-kind static authority the JS browser checker mirrors (D27).

## Scope

One commit in kcdx `src/`: the 5 non-function static check implementations + the
dependency-ordered survival walk + the vtable_index → CannotCheck path, on the step-1 dispatch
(adding the pe_helpers infra step 0.3 flagged as missing, if any). No live functional check
(step 3); no agreement test (step 4).

## Test bar

A kcdx test-suite plugin row asserting each of the 5 static checks returns the correct verdict
for known rows (Unchanged + a Changed case + the callsite multiple-hit → Ambiguous + a transitive
CannotCheck via a Changed anchor + vtable_index → CannotCheck), matched against the Phase-0
fixture's expected verdicts where applicable. The agent builds, deploys (all relevant trees),
hash-verifies, enables dev mode; the user launches; the agent reads PASS from `kcdx-dev.log`. A
matrix row is recorded. Runnable at this step (the dispatch + the section infra exist) —
`.claude/rules/test-discipline.md`, `.claude/rules/incremental-delivery.md`.

## Dependencies

- **3.1** — the per-kind dispatch + payload model + Ambiguous status.
- **0.3** — the pe_helpers scoping finding (whether section spans + a disp32 follower are reused
  or built here).

## Reference

[`../plan-spec.md`](../plan-spec.md) — Group B (the 5 non-function static checks + anchor
ordering); the deferred vtable_index population is Group I.

## Design authority

`data/maintainer-tool/fingerprint-per-kind.md` §callsite / §string_anchor / §instruction_anchor
/ §data_slot / §vtable_base (each check's "the check") + §"The anchor dependency (cross-row
survival)" (the dependency-order walk) + §vtable_index / §"Status flag" (CannotCheck, population
deferred). The Ambiguous-callsite posture is **D31a**. Build to these sections, not to this
doc's summary.

## UX

Not a UI step (engine code). The only user gesture is the game launch
(`.claude/rules/agent-builds-and-deploys.md`).

## Disassembler-test / author-burden

None — the engine carries the per-kind scan/derivation; the author's authored row is verified
for them. No author hand-hex.
