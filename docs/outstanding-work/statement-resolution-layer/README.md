# statement-resolution-layer — Phase 9.3 prerequisite

Ship the curated-function statement metadata into `data/reference.sqlite` and expose
it through a refdb statement-resolution API, so the Phase 9.3 `kcdx.locator.*` /
`kcdx.op.*` / `kcdx.statement.*` author surface can resolve a locator to a statement
at runtime. Unblocks Phase 9.3 steps 1 / 2 / 5.

Shared spec + the settled decisions + the coverage map: [`plan-spec.md`](plan-spec.md).
The step-1 DB requirements hand-off (a separate lane's work):
[`HANDOFF-db-curated-statements.md`](HANDOFF-db-curated-statements.md).

## Phase ledger

| Phase | Status | Commit |
|---|---|---|
| [1 — statement data ships + refdb resolves + docs reconciled](phase-01-statement-layer/README.md) | NOT STARTED | — |

## Lane split

- **Step 1** — DB / extractor lane (works from the hand-off).
- **Steps 2 + 3** — engine / consumer lane.
- The two meet at the column contract in [`plan-spec.md`](plan-spec.md); both build in
  parallel, the engine's runtime acceptance gating on step 1's deploy.
