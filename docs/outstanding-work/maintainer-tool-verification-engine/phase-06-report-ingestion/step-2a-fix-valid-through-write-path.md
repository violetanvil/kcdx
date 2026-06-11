# 6.2a-fix [BE/CORE] `valid_through` becomes an authored + auto-filled column — the write affordance + the seam move (D40)

## What

Complete the under-scoped step 6.2 per the settled **D40**: make `valid_through` (the interval-window
column) an **authored + tool-auto-filled column** the maintainer write path can transact. 6.2 shipped
(`42ebd79`) with the audit trio + `evidence_kind` editable but NOT `valid_through` — yet D34 (verify-all
gap-pass: `valid_through` → report version) and D35 (close-intervals: `valid_through` → that row's
`last_verified_at_version`) both REQUIRE writing `valid_through`. The 6.2 step doc named `valid_through`
among the batch edits, but the build omitted it and no test caught it (`spec-conformance.md` — a plan
summary named the edit, the build dropped it, the green gate missed it). This step closes that gap at its
source, BEFORE 6.2b's resolver (6.2b emits edits this path must accept).

D40 settles `valid_through` follows the **audit-trio pattern** (D17a/D17b/D29): authored AND tool-auto-filled,
hand-overrideable like `verified_by` (D17a — NOT system-only like `verified_date`/D17b). Three settled facts
this step implements:

1. **AUTHORED — on the seed surface + the SEAM MOVE.** `valid_through` is added to the authored surface
   (`EDITABLE_VERSION_COLUMNS` + `ADDRESS_VERSIONS_CSV_HEADER` as the tag form `valid_through_version` →
   resolved to the `game_versions.id` FK exactly as `valid_from_version` resolves + the seed reader
   `read_address_versions_seed` + the curated exporter SELECT), AND **MOVED OUT of the bulk derived-overlay
   `_AV_DERIVED_COLS`** (`bulk_exporter.py`) — a deliberate edit to the D38 authored-vs-derived seam (the
   value was already versioned + round-tripped via the overlay; this re-homes it to the authored side where
   a maintainer-owned curated fact belongs). It stays in the curated→git half (D38); only its
   sub-classification (derived → authored) changes.
2. **AUTO-FILLED — the common path.** The re-verify (6.2b's `reverify_resolver`) computes the value (D34
   extend / D35 retract); the maintainer confirms it — never hand-types an interval on the common path.
3. **HAND-OVERRIDEABLE — the s04 single-version editor.** A maintainer can hand-correct a mis-attributed
   interval (or close one the sweep didn't cover) in the s04 field editor — the `verified_by`/D17a model.

**The write path is NOT a no-op reclassification (the load-bearing work, per the D40 §C.4 correction).**
`valid_through` is a stored cell (`schema.py:188` — `("valid_through", "INTEGER")`, nullable FK to
`game_versions.id`; NULL = open), so the SCHEMA is unchanged (D22, no new column). But adding it to the
editable surface alone does NOT make it write: today `_seed_action_rows` (`import_to_sqlite.py`) carries no
`valid_through` slot, the trio-only re-verify UPDATE (`import_to_sqlite.py:2456-2461`) writes only the 4 trio
columns, and the full-column US-5 UPDATE **deliberately EXCLUDES** `valid_through` via
`_UPDATE_PRESERVE_COLUMNS` (`import_to_sqlite.py:2324`) — an exclusion that guards against a full-column
UPDATE CLOBBERING (NULLing) an already-closed `valid_through` back to open (`build_curated_row` mints
`valid_through=None` → an unguarded UPDATE re-opens a closed interval → two open rows → the
open-interval-uniqueness index `ix_av_open_unique` trips). So this step must: (a) reclassify the column
(seed surface + `EDITABLE_VERSION_COLUMNS`, remove from `_AV_DERIVED_COLS`); AND (b) **extend the data-core
write path to actually emit `valid_through` on a re-verify/interval UPDATE** — add the `valid_through` slot
to `_seed_action_rows`, emit it in the `_apply_one_db` UPDATE for the interval edit, and **reconcile that
emission with `_UPDATE_PRESERVE_COLUMNS`** (the re-verify write sets a non-null ordinal on an existing row —
the OPPOSITE of the NULLing-collision the exclusion guards; the two coexist by making the interval edit a
distinct edit shape the preserve-set does not strip, e.g. the re-verify/interval UPDATE branch emits
`valid_through` while the US-5 full-column UPDATE still preserves it). The write runs through the EXISTING
`_apply_one_db` direct-write + deferred-commit txn + D21 rollback (D19 reused, law 6 — data-core sole
writer); what is NEW is the `valid_through`-emitting UPDATE branch, not a parallel writer.

**The interval validator** (gates the auto-filled AND the hand edit): `valid_through >= valid_from` (the
close is not before the open), no overlap with the entity's other intervals, the open-row uniqueness (one
`valid_through IS NULL` per kcdx_id — `ix_av_open_unique` is the DB guard; the validator fronts it with a
clean error). A `valid_through_version` tag that resolves to no `game_versions` row rejects.

**Two reconciliations land with the seam move:** the **round-trip oracle re-baselines** (deliberate +
inspected — the only delta is `valid_through` moving overlay→authored-seed + any closed-interval values, the
same kind of deliberate re-capture as the `char`→`i8` drift), and the **`validate_db_shape.py:197`
"all baseline-open (`valid_through IS NULL`)" invariant** is reconciled so a legitimately-closed interval
(satisfying the interval validator) is VALID, not flagged as a baseline violation (a D35 close produces the
DB's first closed interval; the open-row-uniqueness sub-check at `validate_db_shape.py:201-206` is kept).

## Scope

One commit in the kcdx tree (`data/refdata-extractor/python/seeds_shared/` + the shape validator):
- **Reclassify `valid_through` to authored:** add `valid_through_version` to `ADDRESS_VERSIONS_CSV_HEADER`
  + the seed reader (`validators.read_address_versions_seed`) + the curated exporter SELECT (`csv_exporter`)
  + `EDITABLE_VERSION_COLUMNS` (`db_editor.py`); REMOVE `valid_through` from `_AV_DERIVED_COLS`
  (`bulk_exporter.py`) — re-homing it across the D38 authored-vs-derived seam (update the explicit
  authored/derived/key partition + its coverage/disjoint asserts).
- **Extend the write path:** the `valid_through` slot in `_seed_action_rows` + the `_apply_one_db`
  re-verify/interval UPDATE branch that emits `valid_through` (tag→FK resolved), reconciled with
  `_UPDATE_PRESERVE_COLUMNS` so the interval edit writes it while the US-5 full-column UPDATE still
  preserves it. Reuse the deferred-commit txn + D21 rollback.
- **The interval validator** (`validators` / the shared gate): `valid_through >= valid_from`, no overlap,
  open-row uniqueness, FK-resolvable tag.
- **The `validate_db_shape.py` reconciliation:** a closed interval is valid when it satisfies the interval
  validator; keep the open-row-uniqueness sub-check.

Does NOT build the resolver (6.2b) or the FE (6.3) — this is the WRITE-PATH + authored-column capability they
depend on. Does NOT change `/confirm/batch`'s transaction shape (it already takes `{kcdx_id,
valid_from_version, edits}`; this makes `valid_through_version` a valid `edits` key). The s04 field-editor
hand-edit UI is the FE's surface — this step delivers the BE write affordance + validator the s04 hand-edit
(and the 6.2b auto-fill) both drive; the s04 editor wiring rides the FE work where the editable surface is
rendered (6.2b/6.3 consume the now-authored column).

## Test bar

- **data-core pytest** (`data/refdata-extractor/tests/`, extending `test_db_editor_batch.py` or a new
  `test_db_editor_interval.py`): a `valid_through` EXTEND (open row → set to a later version) AND a CLOSE
  (open row → set to its `last_verified_at_version`) transacted end-to-end through the batch path — the row's
  `valid_through` holds the new value after commit; the US-5 full-column UPDATE still PRESERVES a closed
  `valid_through` (the `_UPDATE_PRESERVE_COLUMNS` reconciliation — an ordinary column edit does NOT re-open a
  closed interval); the interval validator ACCEPTS the legal edit and REJECTS an illegal one (`valid_through
  < valid_from`; a tag resolving to no game_versions row). Emit the canonical `ACCEPT-RESULT`/`ACCEPT-SUITE`.
  FALSIFIABLE: the write path STILL rejecting a `valid_through` edit (the pre-fix `DbEditError`) fails the
  row; an illegal interval the validator ACCEPTS fails the row; a US-5 column edit that NULLs a closed
  interval (the `_UPDATE_PRESERVE_COLUMNS` regression) fails the row.
- **round-trip oracle (re-baselined):** the widened oracle stays green with `valid_through` on the authored
  seed (re-baseline is deliberate — the only delta is the overlay→authored move + closed-interval values).
  FALSIFIABLE: a round-trip that is NOT byte-identical after the re-baseline fails.
- **shape-validator test**: `validate_db_shape.py` ACCEPTS a DB with a legitimately-closed interval + STILL
  rejects a genuinely-malformed one (overlap / two open rows). FALSIFIABLE: a legal closed interval flagged
  as a baseline violation fails the row.

Runnable AT this step (the `_apply_one_db` path, the batch path, `schema.py`, the shape validator, the
round-trip oracle all exist; this adds the authored-column + write-path capability + the reconciliations).
Per `.claude/rules/test-discipline.md`, `.claude/rules/spec-conformance.md`, `.claude/rules/headless-testable.md`.

## Dependencies

- **6.2** — the `/confirm/batch` endpoint + `update_version_rows_batch` + `EDITABLE_VERSION_COLUMNS` + the
  per-row edit gate (the write path this step COMPLETES — it adds the authored `valid_through` column 6.2
  shipped without). This step is the 6.2 under-scoping fix; on its landing, 6.2's ledger row returns from
  `NEEDS REWORK` to `DONE`.
- **Phase 1 of the seeds migration** (the round-trip oracle + the `_AV_DERIVED_COLS` partition + the
  derived-overlay) — this step edits that authored-vs-derived seam (moves `valid_through` out of the
  overlay) and re-baselines the oracle it established.

## Reference

[`../plan-spec.md`](../plan-spec.md) — Group E + cross-step invariants 5 (all-or-nothing batch rollback) +
6 (data-core sole writer).

## Design authority

`data/maintainer-tool/design.md` **D40** (the GOVERNING decision — `valid_through` is an authored +
tool-auto-filled column, the audit-trio pattern; hand-overrideable like `verified_by` (D17a); the
overlay→authored-seed move; the write-path extension reconciled with `_UPDATE_PRESERVE_COLUMNS`; the oracle
re-baseline + the `validate_db_shape.py:197` reconciliation; schema unchanged per D22) + **D34** (the gap-pass
extend) + **D35** (the close retract) + **D17a** (the `verified_by` hand-overrideable model `valid_through`
follows) + **D19** (the `_apply_one_db` write mechanism reused) + **D22** (flat-final schema — a
reclassification, not a new column) + **D38** (the authored-vs-derived seam this step edits) + **§7** ("Batch
mutation" + the integrity check). The `import_to_sqlite.py:2307-2324` reserved "re-verify open/close" slot +
`_UPDATE_PRESERVE_COLUMNS` (`:2324`) are the as-built anchors. Build to D40 — its write-path-extension clause
settles the editable-surface shape (an authored editable column, NOT a dedicated off-surface op) + names the
real write-path work; do NOT re-open that as a fork.

## Disassembler-test / author-burden

None — a data-core write-path + authored-column + validator completion; no author-facing hex/ABI input, no
game-function target, no AP18 addition (an interval edit is an UPDATE to an existing curated row, not a new
entity/version row). `valid_through_version` is a version TAG the maintainer/tool supplies by name (resolved
to the FK by the engine), not a hand-written address — it passes the disassembler test (declare the version,
the engine resolves the id).
