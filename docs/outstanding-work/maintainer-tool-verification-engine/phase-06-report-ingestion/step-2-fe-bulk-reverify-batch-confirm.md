# 6.2 [FE] The two batch actions — verify-all (proof-rank `evidence_kind` + `valid_through` extend) + close-intervals → the s06 batch confirm (D32/D34/D35)

## What

Wire the s08 **two batch actions** (D35), each its own s06 batch confirm (the `batch field-delta
list`), each ONE atomic transaction through the existing save spine with **all-or-nothing rollback**
(D32/D21). **(1) Verify-all** (verified block, `[Verify N rows]`) — per selected verified-block row:
`last_verified_at_version → <report version>`, `verified_date → today`, `verified_by → <injected
identity>`, **`evidence_kind → <by the row's D36 proof rank>`** (D29-revised — a rank-1
`verified_working` row → `live_production`; a ranks-2-5 `passed_not_verified` row → `live_test_plugin`,
the in-game-test-plugin static tier, NOT `live_production`), AND — when the report version sat in a
GAP or beyond the matched row's interval — `valid_through → <report version>` on the **matched
`address_version` row** (the `matched_address_version_id` from the report — D34). **(2)
Close-intervals** (failing block, `[Close intervals · N rows]`) — per selected failing row:
`valid_through → <that row's last_verified_at_version>` (retract the over-claimed interval to the
last version it passed — D35). Both are **all-UPDATE** (a passing check found the bytes unchanged; a
failing one only closes an interval) → the new-row approval gate (law 8/AP18) does NOT apply; a
genuine new/variant row is authored individually via `[Fix ▸]` (AP18 per-row). This completes the
report-ingestion loop: the data-core remains the sole writer (law 6); nothing lands silently.

## Scope

One commit in the frontend repo: the `bulk_verify(selected)` and `bulk_close_intervals(selected)`
→ s06 batch confirm wiring (two batched confirms, each its `batch field-delta list` under one
confirm), the single-transaction all-or-nothing routing through the existing save-spine API for
each, and the in-place result update (law 3). Verify-all's delta includes the **proof-rank-keyed
`evidence_kind`** (rank-1 → `live_production`, ranks-2-5 → `live_test_plugin` — D29-revised) AND the
D34 `valid_through` forward-extension on a gap-pass (driven by the report's
`matched_address_version_id`); close-intervals' delta is the `valid_through` retraction. Built to the
reconciled s08 + §7 (D32/D35) spec. The save spine + its batch-transaction support (D32) is the
maintainer-tool backend's; this step drives it from s08.

## Test bar

Vitest unit/component tests in the frontend repo: **verify-all** — `[Verify N rows]` opens the s06
batch confirm showing the per-row `old → new` audit-trio deltas, the **proof-rank-keyed
`evidence_kind`** delta (a `verified_working` row → `evidence_kind → live_production`; a
`passed_not_verified` row → `evidence_kind → live_test_plugin` — NOT `live_production` — D29-revised),
AND a `valid_through → <report version>` delta on a gap-pass row (the matched row from
`matched_address_version_id` — D34) under one confirm; **close-intervals** —
`[Close intervals · N rows]` opens the s06 batch confirm showing each failed row's `valid_through →
<its last_verified_at_version>` delta under one confirm; each confirm routes ONE atomic batch
transaction; a simulated one-row failure rolls back the WHOLE batch (nothing partial lands —
D32/D21); the actioned rows update in place, never re-navigating (law 3); both batches are
all-UPDATE so no AP18 gate fires (law 8 N/A). Runnable at this step (the 6.1 three-block s08
worklist + the save-spine batch support exist) — `.claude/rules/test-discipline.md`,
`.claude/rules/incremental-delivery.md`.

## Dependencies

- **6.1** — the three-block s08 worklist (the per-block selected rows + the `[Verify N rows]` /
  `[Close intervals · N rows]` buttons it renders).
- The existing maintainer-tool save spine + its D32 batch-transaction support (the backend the
  batch confirms drive). The `valid_through` UPDATE on the matched row is within the existing
  full-column UPDATE the spine already supports.

## Reference

[`../plan-spec.md`](../plan-spec.md) — Group E (bulk re-verify → s06 batch confirm, confirm-spine
routing) + Group G (`evidence_kind` keyed by the in-game check's proof rank) + cross-step
invariants 5 (all-or-nothing batch rollback) + 6 (data-core sole writer).

## Design authority

`data/maintainer-tool/ui/screens/s08-verification-worklist.md` §"Two batched bulk actions, each ONE
batched confirm (law 5 at batch scale, TRD D32/D35)" (the two actions verify-all +
close-intervals, the per-row deltas incl. the proof-rank-keyed `evidence_kind` + the `valid_through`
extend/retract, each one atomic transaction, all-UPDATE → no law-8 gate) + `data/maintainer-tool/
design.md` **D29** (rev — `evidence_kind` keyed by the row's D36 proof rank: `verified_working` →
`live_production`, `passed_not_verified` → `live_test_plugin`) + **D32** (the save spine's BATCH
mutation, TWO batch actions, all-or-nothing rollback — D21) + **D34** (verify-all extends the matched
row's `valid_through` on a gap-pass) + **D35** (close-intervals retracts `valid_through` to
`last_verified_at_version`; a failure needs no "failed" field — UNVERIFIED by derivation) + **§7**
"Batch mutation". Plus `ui/design.md` law 5 + the `batch field-delta list` silhouette. Build to these
sections (the reconciled s08/§7 spec + D29-revised), not to this doc's summary.

## UX

Carried from the reconciled s08 + §7 spec (`.claude/rules/ux-first-class.md` — not invented):
- **Populated (the two batch actions)** — selecting verified-block rows enables `[Verify N rows]`;
  selecting failing-block rows enables `[Close intervals · N rows]`; each opens its own s06 batch
  confirm.
- **The s06 batch confirm (the acceptance moment)** — the `batch field-delta list`: every selected
  row's change as `field: old → new` (one group per row), under ONE confirm action. **Verify-all**
  shows the audit-trio deltas + the proof-rank-keyed `evidence_kind` delta (the maintainer sees a
  `verified_working` row record `live_production` vs a `passed_not_verified` row record
  `live_test_plugin` — the proof tier is visible before commit) + the `valid_through → <report
  version>` extend on a gap-pass row; **close-intervals** shows the `valid_through →
  <last_verified_at_version>` retract. The maintainer reviews the whole batch delta and confirms
  once. No silent bulk write — the delta is always shown before anything lands (law 5).
- **Save result (success / failure)** — on confirm → one atomic txn → toast "Verified `N` rows" /
  "Closed intervals on `N` rows", and the actioned rows update IN PLACE (law 3 — never
  re-navigates); on failure → "blocked — Retry", the WHOLE batch rolled back, nothing partial
  landed (D32/D21).
- **Disabled (per action block)** — `[Verify N rows]` disabled when zero verified rows selected;
  `[Close intervals · N rows]` disabled when zero failing rows selected (more than color, law 7).
- **Edge** — a large selection (157+ rows) scrolls the batch field-delta list while the confirm
  action pins (law 1); on phone the s06 batch confirm is a full-screen sheet. Keyboard-reachable
  confirm/cancel; the git commit/push stays invisible plumbing (the toast, never a hash).

## Disassembler-test / author-burden

None — both batch actions apply the engine's verdicts through the confirm spine; the maintainer
reviews + confirms, the engine did the verification. It removes per-row manual re-verify burden
(the disassembler-test direction), adds no hand-hex input.
