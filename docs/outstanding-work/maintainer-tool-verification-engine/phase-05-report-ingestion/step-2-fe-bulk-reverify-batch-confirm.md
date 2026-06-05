# 5.2 [FE] Bulk re-verify → the s06 batch field-delta confirm (D32) + per-verdict confirm-spine routing

## What

Wire the s08 **bulk re-verify** path: selecting passing rows + `[Re-verify N rows]` opens an **s06
batch confirm** (the `batch field-delta list`) — every selected row's audit-trio change shown as
`field: old → new` (per row: `last_verified_at_version → <report version>`, `evidence_kind →
live_production` per D29, `verified_date → today`, `verified_by → <injected identity>`), under ONE
confirm action. The maintainer reviews the whole batch delta, confirms ONCE → ONE atomic
transaction through the existing save spine (validate → write → export → commit + push), with
**all-or-nothing rollback** (one row failing rolls back the WHOLE batch — D32/D21). Re-verify is
all-UPDATE → the new-row approval gate (law 8) does NOT apply. This completes the report-ingestion
loop: the data-core remains the sole writer (law 6); nothing lands silently.

## Scope

One commit in the frontend repo: the `bulk_reverify(selected)` → s06 batch confirm wiring, the
`batch field-delta list` (the per-row delta groups under one confirm), the single-transaction
all-or-nothing routing through the existing save-spine API, and the per-verdict confirm-spine
routing (each selected row's audit-trio UPDATE validated, the batch committed atomically, the
result updating the rows IN PLACE — law 3). Built to the s08 + §7 (D32) spec. The save spine + its
batch-transaction support (D32) is the maintainer-tool backend's; this step drives it from s08.

## Test bar

Vitest unit/component tests in the frontend repo: `[Re-verify N rows]` opens the s06 batch confirm
showing the per-row `old → new` audit-trio deltas (incl. `evidence_kind → live_production`, D29)
under one confirm; confirming routes ONE atomic batch transaction; a simulated one-row failure
rolls back the WHOLE batch (nothing partial lands — D32/D21); the re-verified rows update in
place, never re-navigating (law 3); the batch is all-UPDATE so no AP18 approval gate fires (law 8
N/A). Runnable at this step (s08 worklist + the save-spine batch support exist) —
`.claude/rules/test-discipline.md`, `.claude/rules/incremental-delivery.md`.

## Dependencies

- **5.1** — the s08 worklist (the selected passing rows + the `[Re-verify N rows]` button it
  renders).
- The existing maintainer-tool save spine + its D32 batch-transaction support (the backend the
  batch confirm drives).

## Reference

[`../plan-spec.md`](../plan-spec.md) — Group E (bulk re-verify → s06 batch confirm, confirm-spine
routing) + Group G (evidence_kind `live_production` from the in-game check) + cross-step
invariants 5 (all-or-nothing batch rollback) + 6 (data-core sole writer).

## Design authority

`data/maintainer-tool/ui/screens/s08-verification-worklist.md` §"Bulk re-verify = ONE batched
confirm (law 5 at batch scale, TRD D28)" (the s06 batch confirm, the per-row delta, the one atomic
transaction, all-UPDATE → no law-8 gate) + `data/maintainer-tool/design.md` **D32** (the save
spine's BATCH mutation: N audit-trio UPDATEs as ONE atomic transaction, one batched field-delta
confirm, all-or-nothing rollback — D21) + **§7** "Batch mutation (bulk re-verify — US-11/D32)".
The `evidence_kind → live_production` is **D29**. Plus `ui/design.md` law 5 + the `batch
field-delta list` silhouette. Build to these sections, not to this doc's summary.

## UX

Carried from the s08 + §7 spec (`.claude/rules/ux-first-class.md` — not invented):
- **Populated (the batch action)** — selecting passing rows enables `[Re-verify N rows]`; clicking
  opens the s06 batch confirm.
- **The s06 batch confirm (the acceptance moment)** — the `batch field-delta list`: every selected
  row's audit-trio change as `field: old → new` (one group per row), under ONE confirm action; the
  maintainer reviews the whole batch delta and confirms once. No silent bulk write — the delta is
  always shown before anything lands (law 5).
- **Save result (success / failure)** — on confirm → one atomic txn → toast "Re-verified `N`
  rows", and the re-verified rows update IN PLACE (law 3 — never re-navigates); on failure →
  "blocked — Retry", the WHOLE batch rolled back, nothing partial landed (D32/D21).
- **Disabled** — `[Re-verify N rows]` disabled when zero passing rows selected (more than color,
  law 7).
- **Edge** — a large selection (157+ rows) scrolls the batch field-delta list while the confirm
  action pins (law 1); on phone the s06 batch confirm is a full-screen sheet. Keyboard-reachable
  confirm/cancel; the git commit/push stays invisible plumbing ("Re-verified N rows", never a hash).

## Disassembler-test / author-burden

None — bulk re-verify applies the engine's verdicts through the confirm spine; the maintainer
reviews + confirms, the engine did the verification. It removes per-row manual re-verify burden
(the disassembler-test direction), adds no hand-hex input.
