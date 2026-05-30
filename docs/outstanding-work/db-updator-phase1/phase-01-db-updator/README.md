# Phase 1 — the db updator

**Intent.** Stand up the incremental `apply` mode end-to-end: first extract the
shared row-builder/validator/schema/codec so the new writer cannot drift from
rebuild, then the version resolver it needs, then `apply` itself built up one SQL
family at a time (each family shippable + oracle-tested before the next).

Shared spec: [`../context.md`](../context.md).

## Step ledger

| Step | Status | Commit |
|---|---|---|
| 1 extract `seeds_shared/` (schema + validators + dict_codec + row_builder); rebuild = thin caller | DONE | 8f2922d |
| 2 `.rdata` `version_resolver` (scan + hard intern-agreement) | NOT STARTED | — |
| 3 `apply` scaffold + re-verify path (resolve → validate → delta → audit-trio UPDATE) | NOT STARTED | — |
| 4 `apply` add-entity + add-versions-row (kind-class branch + baseline-present gate) | NOT STARTED | — |
| 5 `apply` deprecate + supersede (names-side UPDATE + acyclicity gate) | NOT STARTED | — |

## Step docs

1. [step-1-extract-seeds-shared.md](step-1-extract-seeds-shared.md)
2. [step-2-version-resolver.md](step-2-version-resolver.md)
3. [step-3-apply-reverify.md](step-3-apply-reverify.md)
4. [step-4-apply-add-entity.md](step-4-apply-add-entity.md)
5. [step-5-apply-deprecate-supersede.md](step-5-apply-deprecate-supersede.md)

## Verification gate (phase end)

The phase is done when, on the same hand-edited seeds:

- An `apply` sequence covering all five action types produces a DB row-set
  identical to a `--rebuild` from the same seeds (the oracle test;
  `address_versions.id` autoincrement is the only permitted difference).
- `apply` refuses a function-kind add at a non-baselined version with the
  rebuild-directing message (`../context.md` decision 4), instead of minting a
  NULL-fingerprint row.
- A re-run of the same `apply` is a clean set of no-ops (idempotence on the
  already-applied delta).
- `--rebuild` output is unchanged from before step 1 (the extraction is
  behaviour-preserving).
