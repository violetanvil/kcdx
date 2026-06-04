# schema-flatten-survival-fold

**Intent.** Fold the `survival` sibling table into `address_versions` and delete it —
implement the settled D22/§11 flat-schema migration so the reference DB is **one flat table
per concern, one typed column per fact, no polymorphic structure**. The `survival` table's
genuinely-survival-only columns move onto `address_versions` as nullable typed columns;
`content_hash`/`length` are dropped as proven-redundant; the `kind_form` discriminator is
deleted; every consumer (importer, exporter, engine, read seam) reads the folded columns.

The migration's safety spine: **dual-write → prove equal → delete** — add the columns
additively, populate them on the av row while the `survival` table is still written in
parallel, PROVE each av row's folded cells match the survival row (the 157/157 equivalence),
migrate every consumer to the av columns, and only then delete the sibling.

Shared spec: [`plan-spec.md`](plan-spec.md). Settled design:
[`data/maintainer-tool/design.md`](../../../data/maintainer-tool/design.md) D22 + §11
(`8c87b2f`); the §11.5 migration checklist is the step spine.

## Status ledger (phase-grain)

| Step | Status | Commit |
|---|---|---|
| Phase 1 — the data-core fold (schema + populate-on-av + dual-write + export/round-trip; oracle-verified each step) | DONE | 11a9c09 |
| Phase 2 — engine + consumers read the folded columns (engine SELECT/decode/ResolveResult + read seam/backend) | DONE | 428c7cd |
| Phase 3 — delete the sibling + finalize (drop survival table + kind_form, deletion-hygiene sweep, 156/157 slot/offset into columns, baseline re-capture) | DONE | 36d2682 |

The per-step ledgers live in each `phase-NN-*/README.md`. A top row flips to `DONE` only when
every step in the phase is `DONE`.

## Phases

- **[Phase 1 — the data-core fold](phase-01-data-core-fold/README.md)** — add the folded
  columns to `address_versions` (additive, oracle green), populate them on the av row from the
  per-kind dispatch WHILE dual-writing the `survival` table (the equivalence proof), and carry
  them through the exporter + round-trip. End state: the av row carries every survival fact,
  proven equal to the still-present survival table.
- **[Phase 2 — engine + consumers](phase-02-engine-consumers/README.md)** — the engine SELECT
  + `DecodeVersionRow` + the `ResolveResult` fields (the comprehensiveness contract wired), and
  the read seam (`read_api`) + the maintainer-tool backend passthrough. End state: every
  consumer reads the av columns, not the sibling.
- **[Phase 3 — delete + finalize](phase-03-delete-finalize/README.md)** — delete the `survival`
  table + the `kind_form` discriminator + the redundant content_hash/length dupes, sweep the
  survivors (tests/docs referencing the table), populate 156/157's vtable_slot/struct_offset
  into the now-first-class columns, and re-capture the rebuild-oracle baseline with `survival`
  gone. End state: one flat table, the sibling deleted, all oracles green.
