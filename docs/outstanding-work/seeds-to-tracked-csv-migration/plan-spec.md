# Seeds → tracked-CSV migration — plan spec (the shared authority every step leans on)

## Goal

Migrate the maintainer-tool data layer off the retired `data/seeds/` to **D38's model**: both
reference DBs (`reference.sqlite` USER + `reference-dev.sqlite` DEV) rebuild from the **tracked CSV
export** (curated → `data/db-export/` in git, bulk → `data/db-export-bulk/` under Git LFS) instead
of the ~1.3 GB Ghidra dump; the dump retires to an expert-only bulk-regeneration tool. The DB stays
the authoring source-of-truth, OUT of git (D1 holds).

## The settled design — the authority (read this; build to it; never re-decide)

This plan decomposes the ALREADY-SETTLED **D38** (`data/maintainer-tool/design.md` §10, committed
`f5023f5`, supersedes D20). Read D38 + the revised D18/D19 + the §body rebuild/authoring dataflow
diagram + the §2 glossary verbatim — build to those, not this spec's summary.

**D38 (verbatim intent):**
- `data/seeds/` is fully deprecated/retired; the data is archived at `data/seeds - deprecated/`
  pending deletion. Nothing reads `data/seeds/`.
- The DB stays the authoring source-of-truth, OUT of git (D1 holds — both `.sqlite` files stay
  git-ignored derived artifacts).
- The CSV export splits by the existing `kcdx_id IS NOT NULL` curated/bulk seam:
  - **curated half** (the `kcdx_id`-bearing rows, ~157) → `data/db-export/*.csv`, plain git-tracked
    text (the D1/D20 diff/review layer; already exported on every save).
  - **bulk half** (the `kcdx_id`-NULL discovery rows + the DEV-only `statements` [~5.24M rows],
    `referenced_vars`, `call_edges` tables) → a CSV bundle under **Git LFS** at
    `data/db-export-bulk/`.
- `run_rebuild`'s genesis changes from `Ghidra dump + data/seeds/*.csv → both DBs` to
  `data/db-export/ + data/db-export-bulk/ → both DBs`. The dump retires to an **expert-only one-off**
  (regenerate the bulk CSVs when the dump itself changes — a new game version's fresh disassembly),
  never a routine rebuild input.
- The exporter extends to capture BOTH DBs losslessly; the **completeness bar** is the existing
  curated round-trip oracle **widened over the bulk**: rebuild-from-CSV → DB → re-export →
  byte-identical for both the curated and bulk halves.

## Cross-step invariants (every step holds these — not re-decided per step)

1. **The DB stays out of git (D1).** No step commits `reference.sqlite` or `reference-dev.sqlite`;
   they remain git-ignored derived artifacts. Only the CSV export is tracked.
2. **The dump is the sole source of the bulk corpus TODAY** (soundness gate, verified from source:
   `import_to_sqlite.build_rows` reads `functions`/`statements`/`referenced_vars`/`call_edges`/the
   `kcdx_id`-NULL rows via `iter_table(dump_dir)`; the seeds supply only the curated overlay; the
   current exporter + round-trip oracle cover only the curated ~157 rows). Therefore: **the
   dump-retirement is the POST-WORK end-state, gated on the exporter-extension + the widened oracle
   landing green first.** No step treats the dump as retired before P1 lands and round-trips green
   (`.claude/rules/incremental-delivery.md`).
3. **Bulk-rebuild-from-CSV completeness is a VERIFIED PROBE OUTCOME, not an assumed mechanism**
   (`.claude/rules/results-driven.md`). P0 probes it before the migration builds on it; the P1.4
   widened oracle is the standing proof thereafter.
4. **All-or-nothing per the round-trip bar.** A migration step is not done until its layer
   round-trips byte-identical (curated unchanged + the bulk it added).
5. **The data-core remains the sole writer (D13/law 6).** The migration repoints the genesis +
   export paths; it does not change the direct-write mechanism (D19 holds — only the genesis source
   moved).

## Scope — core data-layer migration only (user-decided 2026-06-10)

IN: the exporter-over-bulk, `data/db-export-bulk/` + Git LFS, the widened round-trip oracle, the
`run_rebuild` genesis repoint, the backend migration (config / csv_integrity / routes_confirm +
tests), the `import_to_sqlite` SEED_DIR repoint, the test repoints (the paused 6.2 batch tests + the
pre-existing rebuild tests), the `data/seeds - deprecated/` deletion.

OUT — **DEFERRED (user-decided, a separate later sweep):** the governance/design prose sweep —
`address-library.md` (incl. its `paths: data/seeds/**` glob + the "seed CSVs under data/seeds/"
prose), `CLAUDE.md`, `policy.md`'s relocation + every `data/seeds/policy.md` link, the
public-private rule's seed carve-out, and the publish-public allowlist's Git-LFS interaction. Noted
as the follow-up; this tree leaves those references as the known-interim gap.

## Coverage map — every D38 element → its step (or DEFERRED)

| Design element (D38 / the revised §s) | Covered by | Notes |
|---|---|---|
| The bulk round-trips CSV-losslessly (the gating completeness unknown — soundness carry-forward) | P0 step 1 | Probe (results-driven): export bulk → rebuild-from-CSV → diff. Byte-identical → proceed; divergence → STOP+surface (the dump provides data the tables don't capture) |
| The exporter extends to capture the BULK half losslessly | P1 step 1 | `statements`/`referenced_vars`/`call_edges` + kcdx_id-NULL rows → `data/db-export-bulk/`; today it filters to curated |
| `data/db-export-bulk/` location + Git LFS tracking | P1 step 2 | `.gitattributes` LFS patterns + LFS init (D38 — bulk under LFS) |
| `run_rebuild` genesis repoint (seeds/dump → tracked CSVs; dump → expert-only) | P1 step 3 | `data/db-export/` + `data/db-export-bulk/` → both DBs; the from-dump path becomes expert-only bulk regeneration |
| The round-trip oracle widened over the bulk (the completeness bar) | P1 step 4 | `csv_integrity` rebuild-from-CSV → DB → re-export → byte-identical, both halves |
| `import_to_sqlite.py` SEED_DIR / *_SEED_CSV constants → db-export | P2 step 1 | The toolchain's seed-path constants repoint |
| Backend migration: config.py (`seed_dir`) / csv_integrity.py / routes_confirm.py + the ~6 backend tests asserting `seed_dir must stay at data/seeds` | P2 step 2 | The backend + its tests move off `data/seeds/` |
| Repoint the broken rebuild tests + the paused 6.2 batch tests (data/seeds → db-export) | P2 step 3 | `test_db_editor_batch`/`test_confirm_batch_endpoint`/`test_deferred_commit`/`test_confirm_endpoint`/… ; unblocks 6.2 + the pre-existing breakage |
| Delete `data/seeds - deprecated/` (the deletion-hygiene close) | P3 step 1 | Once no in-scope (toolchain/backend) reference remains |
| The governance/design prose sweep (address-library.md + paths:-glob, CLAUDE.md, policy.md relocation + links, public-private carve-out, publish-allowlist LFS) | **DEFERRED** | User-decided (2026-06-10): a separate later sweep; this tree is core-data-layer only |
| policy.md's new home + its links | **DEFERRED** | Rides the governance sweep above (user-decided) |
