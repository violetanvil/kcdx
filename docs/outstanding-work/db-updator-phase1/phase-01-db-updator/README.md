# Phase 1 — the db updator

**Intent.** Stand up the incremental `apply` mode end-to-end: first extract the
shared row-builder/validator/schema/codec so the new writer cannot drift from
rebuild, then the version resolver it needs, then `apply` itself built up one SQL
family at a time (each family shippable + oracle-tested before the next), then
the per-kind survival datum the DB must carry, then the names-side edges.

Shared spec: [`../context.md`](../context.md).

## Step ledger

| Step | Status | Commit |
|---|---|---|
| 1 extract `seeds_shared/` (schema + validators + dict_codec + row_builder); rebuild = thin caller | DONE | 8f2922d |
| 2 `.rdata` `version_resolver` (scan + hard intern-agreement) | DONE | eb1aa2c |
| 3 `apply` scaffold + re-verify path (resolve → validate → delta → audit-trio UPDATE) | DONE | a6e956e |
| 4 `apply` add-entity + add-versions-row (kind-class branch + baseline-present gate) | DONE | 6bfd634 |
| 5 per-kind survival fingerprint: `survival` table + populate (DB-side; engine consumer separate) | NOT STARTED | — |
| 6 `apply` deprecate + supersede (names-side UPDATE + acyclicity gate) | NOT STARTED | — |

## Step docs

1. [step-1-extract-seeds-shared.md](step-1-extract-seeds-shared.md)
2. [step-2-version-resolver.md](step-2-version-resolver.md)
3. [step-3-apply-reverify.md](step-3-apply-reverify.md)
4. [step-4-apply-add-entity.md](step-4-apply-add-entity.md)
5. [step-5-survival-fingerprint.md](step-5-survival-fingerprint.md)
6. [step-6-apply-deprecate-supersede.md](step-6-apply-deprecate-supersede.md)

## Verification gate (phase end)

The phase is done when, on the same hand-edited seeds:

- An `apply` sequence covering all action types (re-verify, add-entity,
  add-versions-row, the survival-datum write, deprecate, supersede) produces a
  DB row-set identical to a `--rebuild` from the same seeds (the oracle test;
  `address_versions.id` autoincrement is the only permitted difference).
- `apply` refuses a function-kind add at a non-baselined version with the
  rebuild-directing message (`../context.md` decision 4), instead of minting a
  NULL-fingerprint row.
- The `survival` table carries the correct per-kind datum for every curated
  entity (function hash; callsite AOB; data_slot/instruction_anchor derivation
  + `derives_from`; vtable_base slot-shape; vtable_index row present, datum
  population deferred) — apply and rebuild agree on it.
- A re-run of the same `apply` is a clean set of no-ops (idempotence on the
  already-applied delta).
- `--rebuild` output matches the recorded oracle baseline (behaviour preserved
  except deliberate, documented baseline re-captures).

## Note on the survival fingerprint split

Step 5 is the **DB-side** half of the per-kind survival fingerprint system: the
`survival` table + populating its per-kind datum. The **engine consumer** half
(per-kind `SurvivalCheck` dispatch, the production binder feed, the
dependency-DAG walk at check time, apply-time `on_changed` enforcement) is
engine work, NOT db-updator scope — it is tracked in the restructure plan's
survival lineage. Design for both halves:
[`data/maintainer-tool/fingerprint-per-kind.md`](../../../../data/maintainer-tool/fingerprint-per-kind.md).
