# 6.3 [FE] The two batch actions — verify-all (proof-rank `evidence_kind` + `valid_through` extend) + close-intervals → the s06 batch confirm (D32/D34/D35)

## What

Wire the s08 **two batch actions** (D35), each its own s06 batch confirm (the `batch field-delta
list`), each ONE atomic transaction through the existing save spine with **all-or-nothing rollback**
(D32/D21). **Per D39, the FE does NOT compute the edit-specs** — it sends the report's actionable rows
to the **`/save/reverify-batch` preview endpoint (6.2b)**, which resolves + computes them server-side
(the data-core is the sole writer + owns interval logic — law 6) and returns the per-row field-deltas
the FE displays; the maintainer confirms; the FE POSTs the SAME returned edits to `/confirm/batch`
(6.2) to transact. **(1) Verify-all** (verified block, `[Verify N rows]`) — the preview returns, per
selected verified-block row, the trio (`last_verified_at_version → <report version>`, `verified_date →
today`, `verified_by → <injected identity>`), the **proof-rank-keyed `evidence_kind`** (D29-revised — a
rank-1 `verified_working` row → `live_production`; a ranks-2-5 `passed_not_verified` row →
`live_test_plugin`, NOT `live_production`), AND — on a gap-pass — `valid_through → <report version>` on
the matched `address_version` row (D34). **(2) Close-intervals** (failing block, `[Close intervals · N
rows]`) — the preview returns, per selected failing row, `valid_through → <that row's
last_verified_at_version>` (the D35 retract; the data-core resolves the target row deterministically by
`kcdx_id`+the resolved version). Both are **all-UPDATE** → the new-row approval gate (law 8/AP18) does
NOT apply; a genuine new/variant row is authored individually via `[Fix ▸]` (AP18 per-row). This
completes the report-ingestion loop: the data-core remains the sole writer (law 6); nothing lands
silently.

## Scope

One commit in the frontend repo: the `bulk_verify(selected)` and `bulk_close_intervals(selected)`
wiring. Each action POSTs the selected report rows + the action to **`/save/reverify-batch` (6.2b)**,
renders the returned per-row field-deltas as the s06 batch confirm (the `batch field-delta list` under
one confirm), and on confirm POSTs the SAME returned edits to `/confirm/batch` (6.2) — ONE atomic
transaction, all-or-nothing — then updates the actioned rows in place (law 3). **The FE computes NO
edit-specs** (D39): the trio + the proof-rank-keyed `evidence_kind` + the D34 gap-extension + the D35
retract are all computed by the data-core's `reverify_resolver` (6.2b) and arrive in the preview's
returned deltas — the FE displays them and relays the confirm. Built to the reconciled s08 + §7
(D32/D35) + D39 spec. The resolve+preview (6.2b) and the batch-transaction (6.2) are the maintainer-tool
backend's; this step drives them from s08.

## Test bar

Vitest unit/component tests in the frontend repo (the FE drives the seam; the deltas come from the
mocked `/save/reverify-batch` response — the FE does NOT compute them): **verify-all** — `[Verify N
rows]` POSTs the selected rows to `/save/reverify-batch` and opens the s06 batch confirm showing the
per-row `old → new` deltas the preview RETURNED (the audit-trio + the **proof-rank-keyed
`evidence_kind`** — a `verified_working` row → `live_production`; a `passed_not_verified` row →
`live_test_plugin`, NOT `live_production` — D29-revised — AND a `valid_through → <report version>` delta
on a gap-pass row — D34) under one confirm; **close-intervals** — `[Close intervals · N rows]` POSTs to
`/save/reverify-batch` and opens the s06 batch confirm showing each failed row's `valid_through → <its
last_verified_at_version>` delta the preview returned; on confirm each POSTs the SAME returned edits to
`/confirm/batch` (ONE call, the list body — assert the call shape) routing ONE atomic batch transaction;
a simulated one-row failure (the 200-body `failed` result) rolls back the WHOLE batch (nothing partial
lands, the rows do NOT update in place — D32/D21); the actioned rows update in place on success, never
re-navigating (law 3); both batches are all-UPDATE so no AP18 gate fires (law 8 N/A). FALSIFIABLE: the
FE relays the preview's returned deltas verbatim — a test asserting the FE re-derives or mutates a
delta (e.g. computes `evidence_kind` itself) is the D39-violation shape. Runnable at this step (6.1's
three-block s08 worklist + 6.2's `/confirm/batch` + `confirmBatch` client + **6.2b's
`/save/reverify-batch` preview endpoint** all exist) — `.claude/rules/test-discipline.md`,
`.claude/rules/incremental-delivery.md`.

## Dependencies

- **6.1** — the three-block s08 worklist (the per-block selected rows + the `[Verify N rows]` /
  `[Close intervals · N rows]` buttons it renders).
- **6.2** — the D32 `/confirm/batch` endpoint + the `confirmBatch` FE client method (the
  batch-transaction this step DRIVES on confirm). Built in 6.2 so this FE wiring drives a real
  endpoint, not a non-existent one.
- **6.2b** — the `/save/reverify-batch` preview endpoint + the `reverify_resolver` (D39): the seam this
  step POSTs the report rows to and whose returned field-deltas it displays. Built before this so the FE
  drives a real resolve+preview endpoint and never computes the edit-specs itself (D39 — the data-core
  is the sole writer + owns the gap/interval logic; the FE has no `valid_from_version`/`valid_through`/
  `last_verified_at_version` to compute with).

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
`last_verified_at_version`; a failure needs no "failed" field — UNVERIFIED by derivation) + **D39**
(the data-core resolves the edit-specs via `/save/reverify-batch`/`reverify_resolver`; the FE sends
report rows + displays the returned deltas, never computes edits) + **§7** "Batch mutation". Plus
`ui/design.md` law 5 + the `batch field-delta list` silhouette. Build to these sections (the reconciled
s08/§7 spec + D29-revised + D39), not to this doc's summary.

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
