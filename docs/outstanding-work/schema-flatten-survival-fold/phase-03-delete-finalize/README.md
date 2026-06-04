# Phase 3 — delete the sibling + finalize

**Intent.** With the av columns proven equal to the survival table (Phase 1) and every consumer
migrated to read them (Phase 2), delete the `survival` sibling table + the `kind_form`
discriminator + the redundant `content_hash`/`length` dupes; sweep the survivors (tests/docs
referencing the table); populate 156/157's `vtable_slot`/`struct_offset` into the now-first-class
columns; and re-capture the rebuild-oracle baseline with `survival` gone. End state: one flat
table, the sibling deleted, all oracles green.

Shared spec: [`../plan-spec.md`](../plan-spec.md). Design:
[`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md) §11.2 (the
delete) + §11.5 (the migration checklist's final re-capture step).

## Step ledger

| Step | Status | Commit |
|---|---|---|
| 6 delete the survival table + kind_form + redundant dupes; deletion-hygiene sweep (tests/docs); populate 156/157 vtable_slot/struct_offset into the columns; re-capture the rebuild-oracle baseline with survival gone | DONE | 36d2682 |

## Step docs

6. [step-6-delete-survival-finalize.md](step-6-delete-survival-finalize.md)

## Verification gate (phase end)

- The `survival` table is gone from the SCHEMA + `USER_COLUMNS` + the DB; the `kind_form`
  discriminator is deleted; `survival_builder` is either removed or reduced to the
  `_KIND_TO_FORM` dispatch the av-row build now uses (no dead survival-table-write code).
- Deletion-hygiene (`.claude/rules/deletion-hygiene.md`): no prescriptive survivor references
  the `survival` table as a current structure — `test_survival_table.py` removed/repointed,
  any doc/comment naming the sibling swept; the design §11 already records the fold (the
  authority), so the references repoint to the av columns.
- 156/157's `vtable_slot`/`struct_offset` carry their RE-verified values (id156 slot 2/+0x10,
  id157 slot 4/+0x20) in the structured columns (now first-class), the prose keeping the
  narrative — the convention §11 establishes (a resolvable fact lives in its column).
- The full data-core suite is green; the rebuild-oracle baseline is re-captured with `survival`
  absent (deliberate + inspected + a BASELINE PROVENANCE entry documenting the table removal).
- Convergence holds: direct-write == seed-rebuild byte-identity after the delete.
- The whole-feature acceptance: a game launch confirms no resolve-path regression (the engine
  reads the av columns; the survival pass behavior is preserved) — the matrix stays green.
