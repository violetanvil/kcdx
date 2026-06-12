# 1.2 [DATA-CORE] The lifecycle-completeness detection query (E8)

## What

A new data-core module computing the **needs-action set** at the current game version V — the
version-relative incomplete-lifecycle entities the write-time HARD-ERROR checks cannot catch (D41
fact 1). It computes the three kinds, read-only:

- **Uncovered-at-V orphan** — an entity with NO `address_versions` interval covering V
  (`valid_from <= V <= valid_through` OR `valid_through IS NULL`) AND `address_names.is_deprecated = 0`
  AND `superseded_by IS NULL`. (No row authoritative for V — UNVERIFIED at V, the incomplete-lifecycle
  state needing a decision; per D41 + policy.md §"Status is derived".)
- **Never verified** — an `address_versions` row with `last_verified_at_version IS NULL`.
- **Broken reference** — `deprecation_replacement` / `superseded_by` pointing at a nonexistent or
  itself-incomplete entity.

The module returns the set grouped by kind (each entity + its specific gap), ready for the 2.1 backend
endpoint to serve. It builds on the existing `validators.py:check_every_entity_covered` (coverage
logic) + `validate_db_shape.py:interval_check_results` (interval validity).

## Scope

One commit in the kcdx tree:
- A new data-core module under `data/refdata-extractor/python/seeds_shared/` (e.g. `lifecycle_audit.py`)
  — the detection query over the current DB state, returning the three kinds. Read-only DB access (no
  write — law 6; the maintainer resolves via the canonical write path).
- Reuse the existing coverage/interval helpers; do not duplicate the interval-containment logic.

Does NOT add a backend endpoint (2.1) or any FE.

## Test bar

- **data-core pytest** (`data/refdata-extractor/tests/`, a new `test_lifecycle_audit.py` mirroring the
  existing data-core fixture-DB pattern): over a fixture DB — an orphaned entity (closed interval, no
  successor at V, not deprecated/superseded) is flagged in the Uncovered set; a `last_verified IS NULL`
  row is flagged Never-verified; a dangling `deprecation_replacement` is flagged Broken-reference; a
  HEALTHY entity (covered at V, verified, no dangling ref) is in NONE of the sets. **FALSIFIABLE:** an
  orphan whose interval IS open (covers V) must NOT be flagged; a deprecated/superseded orphan must NOT
  be flagged (the deprecation/supersession is the completed lifecycle). Emits the canonical
  `ACCEPT-RESULT` / `ACCEPT-SUITE`. Runnable AT this step (the DB + the coverage helpers exist).

## Dependencies

- **1.1** (not strictly — they are independent data-core changes, but landing 1.1 first keeps the
  resolver reconciliation + the detection query coherent). The existing `check_every_entity_covered` /
  `interval_check_results` helpers.

## Reference

[`../plan-spec.md`](../plan-spec.md) — E8 + §"The detection conditions" (the data model the query rests on).

## Design authority

`data/maintainer-tool/design.md` **D41** fact (1) (the standing needs-action set — the version-relative
incomplete-lifecycle shapes) + `data/maintainer-tool/policy.md` §"Status is derived" (the derived-status
rules) + `data/maintainer-tool/ui/screens/s09-needs-action.md` §Contents (the three kinds' conditions).
Build to the D41/policy.md detection conditions, not this doc's summary.

## Disassembler-test / author-burden

None — a read-only data-core query; no author-facing input, no game-function target, no AP18 row addition.
