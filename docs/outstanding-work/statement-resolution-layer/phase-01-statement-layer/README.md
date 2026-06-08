# Phase 1 — statement data ships + refdb resolves + docs reconciled

The single phase of the statement-resolution-layer prerequisite. Ships the curated
statement subset into `reference.sqlite` (step 1, DB lane), exposes it through a
refdb statement-resolution API (step 2, engine lane), and reconciles the doc drift
(step 3). On completion, Phase 9.3 steps 1 / 2 / 5 are unblocked.

Shared spec + decisions + coverage map: [`../plan-spec.md`](../plan-spec.md).

## Step ledger

| Step | Status | Commit |
|---|---|---|
| [1 — ship curated statement subset into reference.sqlite (DB lane)](step-1-ship-curated-statements.md) | DONE | (landed) — USER projection ships statements (2,385) + referenced_vars (5,595), pinned columns, bulk+call_edges DEV-only; DB-shape ACCEPT 17/17. DBs are gitignored regenerated artifacts (code committed; rebuild locally). |
| [2 — refdb statement-resolution API, eager-load (engine lane)](step-2-refdb-statement-api.md) | NOT STARTED | — |
| [3 — reconcile the DEV-only doc drift (both lanes)](step-3-doc-reconcile.md) | NOT STARTED | — |

## Verification gate

The phase is done when:

1. **Step 1** — the rebuilt `data/reference.sqlite` carries the curated `statements` +
   `referenced_vars` subset with exactly the pinned columns; the DB-shape acceptance
   test emits `ACCEPT-SUITE: N/N passing` (the bulk absent, `call_edges` absent, the
   curated row counts match). Headless — extractor + sqlite assertion, no game launch.
2. **Step 2** — the `cap-NN-stmt-resolve` regression plugin resolves a representative
   locator against a curated function and reads its `byte_range_len` + captures; the
   suite stays green (`suite: X/Y passing` → the canonical `ACCEPT-*` lines in
   `kcdx-dev.log`). Confirmed by the user's one launch + the agent's log read.
3. **Step 3** — no surviving doc claims the curated statement subset is DEV-only, and
   no Phase 9.3 step doc references the non-existent `statements.captures` column
   (deletion-hygiene survivor sweep clean). Docs-only.

Non-UI prerequisite — no user-facing UX surface; the verification gate is build +
the headless DB assertion + the one in-game suite confirmation.
