# 3.3 [FE] The s08 reconciliation display + close→needs-action flag (E3, E4)

## What

Wire the s08 worklist's report-vs-DB reconciliation display, consuming the Phase-2 classification
(2.2): a re-imported row whose recommended action ALREADY landed (the preview marked it
`already_acted` / no-action) renders in the s08 "no further action" state — moved out of its actionable
block, surfaced + auditable, no checkbox, not in any batch — so the actionable blocks show only rows
still needing the action (no no-op confirm). AND the close→needs-action flag: a close-intervals action
that ORPHANS an entity surfaces it as needs-action (it appears in the s09 standing view) — the s08
close stays atomic, the orphan surfaced not silent. In the SEPARATE frontend repo.

## Scope

One commit in the frontend repo (`data/maintainer-tool/frontend/src/worklist/`):
- `VerificationWorklist.tsx` (+ the batch/overlay components) — read the 2.2 `already_acted`
  classification from the `/save/reverify-batch` preview response; render an already-acted row in the
  "no further action" state (out of the actionable block, no checkbox); ensure an actionable batch
  excludes already-acted rows (no no-op write).
- The close→needs-action surfacing: after a close-intervals confirm that orphans an entity, the worklist
  reflects that the entity is now needs-action (it surfaces in the s09 view on next visit; the s08 side
  is the "Orphaned by a close" state — the close stays one atomic transaction).

Does NOT change the resolver/preview logic (1.1/2.2 own it) or the Fix-flow (3.4).

## Test bar

Vitest component tests: a re-imported report with an already-acted row (the mocked preview marks it
`already_acted`) renders that row in "no further action" (no checkbox, not in any batch); the actionable
block shows only the still-actionable rows; confirming a batch never includes an already-acted row.
A close-intervals confirm that orphans an entity surfaces the "Orphaned by a close" state (the close
itself stays atomic). **FALSIFIABLE:** an already-acted row rendered actionable (or included in a batch)
fails; a close that orphans an entity but shows no needs-action signal fails. Gate: `npm run build` exit 0
+ `vitest run` green. Runnable AT this step (2.2's classification + the s08 worklist exist).

## Dependencies

- **2.2** — the preview's `already_acted` classification this display reads.
- **1.1** — the resolver's already-done skip (the classification rests on it).
- The existing s08 worklist (6.1/6.3, `FE:6e7f3b1`/`FE:d8771ff`).

## Reference

[`../plan-spec.md`](../plan-spec.md) — E3, E4.

## Design authority

`data/maintainer-tool/ui/screens/s08-verification-worklist.md` §"Report-vs-DB reconciliation" + §"Close →
needs-action" + §"States & variants" the "Already acted on" + "Orphaned by a close" entries. Build to THIS
screen spec, not this doc's summary.

## UX

Carried from the s08 screen spec (`.claude/rules/ux-first-class.md`):
- **Already acted on** — the row renders in a "no further action" state (surfaced, auditable, a marker
  *"interval already closed"* / *"already current"*, no checkbox, not in any batch); the actionable
  blocks show only rows still needing the action — a partly-acted re-import shows true remaining work.
- **Orphaned by a close** — a close that leaves an entity uncovered/not-deprecated/not-superseded flags
  it needs-action (it appears in the s09 view); the close completes atomically, the orphan resolved
  separately, never silent (law 4).
- **No no-op confirm** — an already-acted row is never in a batch, so confirming never produces an
  empty-delta no-op write.

## Disassembler-test / author-burden

None — a maintainer-tool FE screen; no author-facing plugin input, no game-function target.
