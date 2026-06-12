# 1.1 [DATA-CORE] Close-intervals already-done skip + the reconciliation classification (E1, E2)

## What

The close-intervals path in `reverify_resolver.py` (the D39 resolver) currently emits a `valid_through`
edit unconditionally — even when the target row's interval is ALREADY closed to its
`last_verified_at_version` (`valid_through == last_verified`, non-NULL), producing a no-op edit with an
empty field-delta (the silent no-op confirm the live acceptance hit). Add the symmetric already-done
skip that verify-all already has (`last_verified >= swept → return None`): when the close target's
`valid_through` already equals `last_verified_at_version`, produce NO edit-spec — there is nothing to
retract. This is the data-core half of the report-vs-DB reconciliation: the resolver derives "already
acted on" from `(report version + current DB state)` per row (it already reads the row's current state
via `_saved_cells`), and a row whose recommended action already landed produces no edit-spec.

## Scope

One commit in the kcdx tree:
- `data/refdata-extractor/python/seeds_shared/reverify_resolver.py` — the close-intervals path
  (~lines 340–377): guard the `specs.append` with the already-done check (`valid_through == lvv_tag`
  → skip, no spec), mirroring verify-all's `return None` skip (~lines 207–210). The classification
  (already-acted vs actionable) is the resolver returning no spec for an already-done row; the
  caller (and the 2.2 preview surface) reads "no spec → already acted, no action".

Does NOT change the write path (`_apply_one_db`), the verify-all skip, or the FE.

## Test bar

- **data-core pytest** (`data/refdata-extractor/tests/test_reverify_resolver.py`): extend the close-intervals
  tests — an ALREADY-CLOSED row (`valid_through == last_verified`, non-NULL) → NO edit-spec (the skip);
  an OPEN row (`valid_through IS NULL`) → the real `valid_through: '' → <last_verified>` delta (the skip
  did not break the working path). **FALSIFIABLE:** removing the skip makes the already-closed case emit
  a no-op spec (the test goes red); the open-row case proves the skip is conditional, not blanket. Emits
  the canonical `ACCEPT-RESULT` / `ACCEPT-SUITE`. Runnable AT this step (the resolver + its fixture DB exist).

## Dependencies

None new — extends the existing `reverify_resolver.py` (6.2b, committed `201e646`) + the authored
`valid_through` column (6.2a-fix, `69f54d2`). The verify-all skip (the mirror) already exists.

## Reference

[`../plan-spec.md`](../plan-spec.md) — E1, E2 + the cross-step invariant "no report-schema change".

## Design authority

`data/maintainer-tool/design.md` **D41** fact (2) (the already-acted-on derivation + the close-intervals
symmetric skip) — `| D41 |` in the decisions table. Build to D41's settled reconciliation, not this
doc's summary.

## Disassembler-test / author-burden

None — a data-core resolve seam; no author-facing input, no game-function target, no AP18 row addition.
