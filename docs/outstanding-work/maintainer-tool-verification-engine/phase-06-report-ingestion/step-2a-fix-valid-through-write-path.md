# 6.2a-fix [BE/CORE] Complete the 6.2 write path — the `valid_through` interval-write affordance (D34/D35)

## What

Complete the under-scoped step 6.2: add the **interval-write affordance** (extend-window / close-window)
the batch write path needs to transact a `valid_through` edit. 6.2 shipped (`42ebd79`) with the audit
trio + `evidence_kind` editable but NOT `valid_through` — yet D34 (verify-all gap-pass: `valid_through`
→ report version) and D35 (close-intervals: `valid_through` → that row's `last_verified_at_version`)
both REQUIRE writing `valid_through`. The 6.2 step doc named `valid_through` among the batch edits, but
the build omitted it and no test caught it (`spec-conformance.md` — a plan summary named the edit, the
build dropped it, the green gate missed it). This step closes that gap at its source, BEFORE 6.2b's
resolver (6.2b emits edits this path must accept).

`valid_through` is a **stored cell** (`schema.py:188` — `("valid_through", "INTEGER")`, a nullable FK to
`game_versions.id`; NULL = open interval), NOT a derived/computed projection — so the fix is a write
affordance through the EXISTING `_apply_one_db` direct-write + validator + deferred-commit spine (D19 —
the write mechanism is reused, not bypassed; law 6 — the data-core stays the sole writer; D22 — a stored
typed cell written through the normal spine, NO schema change). The code already reserved this exact slot:
`import_to_sqlite.py:2307-2324` documents `valid_through` as written "only create-version (US-6) and
**re-verify open/close**" with `_UPDATE_PRESERVE_COLUMNS` machinery so an ordinary US-5 column edit never
clobbers the interval marker — the re-verify open/close path is D34/D35/D39, completed here.

A SECOND cross-step reconciliation lands in the same step: `validate_db_shape.py:197` asserts
"address_versions all baseline-open (`valid_through IS NULL`)" — the current DB has ZERO closed
intervals. A D35 close produces the DB's FIRST closed interval; this shape invariant (and any integrity
check the confirm spine runs — §7 "cheap integrity check") must be reconciled so a legitimately-closed
interval is VALID, not flagged as corruption.

## Scope

One commit in the kcdx tree (`data/refdata-extractor/python/seeds_shared/` + the shape validator):
- The interval-write affordance: teach the `_apply_one_db`-backed write path to accept a `valid_through`
  edit (as the tag form `valid_through_version` → resolved to the `game_versions.id` FK, mirroring how
  `valid_from_version` resolves) — added to `EDITABLE_VERSION_COLUMNS` (or a dedicated interval-edit op
  kept off the general editable set, the executor's settled-by-design choice between the two write-surface
  shapes — see Design authority), the CSV header / export round-trip if the editable surface carries it,
  and the validator gate (an interval edit validates: `valid_through >= valid_from`, the closed/open
  semantics, the partial-UNIQUE-index integrity). The write reuses the deferred-commit txn + D21 rollback.
- The `validate_db_shape.py` "all-intervals-open" invariant reconciliation: a closed interval
  (`valid_through IS NOT NULL`) is valid when it satisfies the interval semantics (`valid_through >=
  valid_from`, no overlap with the entity's other intervals), not flagged as a baseline violation.

Does NOT build the resolver (6.2b) or the FE (6.3) — this is the WRITE-PATH capability they depend on. Does
NOT change `/confirm/batch`'s transaction shape (it already takes `{kcdx_id, valid_from_version, edits}`;
this makes `valid_through` a valid `edits` key).

## Test bar

- **data-core pytest** (`data/refdata-extractor/tests/`, extending `test_db_editor_batch.py` or a new
  `test_db_editor_interval.py`): a `valid_through` extend (open row → `valid_through` set to a later
  version) AND a close (an open row → `valid_through` set to its `last_verified_at_version`) transacted
  end-to-end through the batch path — the row's `valid_through` holds the new value after commit; the
  interval validator accepts the legal edit and REJECTS an illegal one (`valid_through < valid_from`).
  Emit the canonical `ACCEPT-RESULT`/`ACCEPT-SUITE`. FALSIFIABLE: the write path STILL rejecting a
  `valid_through` edit (the pre-fix `DbEditError`) fails the row; an illegal interval (`valid_through <
  valid_from`) that the validator ACCEPTS fails the row.
- **shape-validator test**: `validate_db_shape.py` accepts a DB with a legitimately-closed interval (the
  first closed interval the reconciliation permits) + still rejects a genuinely-malformed one. FALSIFIABLE:
  a closed interval that the reconciled validator flags as a baseline violation fails the row.

Runnable AT this step (the `_apply_one_db` path, the batch path, `schema.py`, the shape validator all
exist; this adds the interval-write capability + the invariant reconciliation). Per
`.claude/rules/test-discipline.md`, `.claude/rules/spec-conformance.md`, `.claude/rules/headless-testable.md`.

## Dependencies

- **6.2** — the `/confirm/batch` endpoint + `update_version_rows_batch` + `EDITABLE_VERSION_COLUMNS` + the
  per-row edit gate (the write path this step COMPLETES — it adds `valid_through` to the editable surface
  that 6.2 shipped without). This step is the 6.2 under-scoping fix; on its landing, 6.2's ledger row
  returns from `NEEDS REWORK` to `DONE`.

## Reference

[`../plan-spec.md`](../plan-spec.md) — Group E + cross-step invariants 5 (all-or-nothing batch rollback) +
6 (data-core sole writer).

## Design authority

`data/maintainer-tool/design.md` **D34** (the gap-pass `valid_through` forward-extension) + **D35** (the
close-intervals `valid_through` retract) + **D19** (the write mechanism — `_apply_one_db` direct
INSERT/UPDATE + the deferred-commit txn; the interval write reuses it) + **D22** (the schema is flat +
final — `valid_through` is a stored typed cell, written through the normal spine, NO new column) + **§7**
("Batch mutation" + the "cheap integrity check" the shape reconciliation touches) + **§11.1** (the
`address_versions` interval columns — `valid_from`/`valid_through` as `game_versions.id` FKs). The
`import_to_sqlite.py:2307-2324` reserved "re-verify open/close" slot is the as-built anchor. Build to
these — the editable-surface shape (a general `valid_through_version` editable key vs a dedicated
interval-close op) is settled by which honors D22's "every item its own column / no polymorphic
relationships" + the validator's interval-integrity bar; if that choice is genuinely open, surface it.

## Disassembler-test / author-burden

None — a data-core write-path + validator completion; no author-facing input, no game-function target,
no AP18 addition (an interval edit is an UPDATE to an existing curated row, not a new entity/version row).
