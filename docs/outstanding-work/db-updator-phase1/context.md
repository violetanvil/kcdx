# db-updator Phase 1 — shared context

The spec every step in this plan leans on. Steps cross-link here rather than
restating it.

## Goal

Build the **incremental DB-updator** — an `apply` mode for
`data/refdata-extractor/python/import_to_sqlite.py` that lands hand-edited
seed-CSV deltas into BOTH reference DBs (user + dev) without a full rebuild,
sharing one row-builder with the existing rebuild path so the two writers cannot
drift.

This is Phase 1 of the maintainer-tool flow. Humans keep hand-editing the seed
CSVs; the script is the CSV→DB applier. Phase 2 (the GUI) is out of scope.

## Authoritative design doc

[`data/maintainer-tool/plan.md`](../../../data/maintainer-tool/plan.md) is the
full architectural plan (SQL shapes, prereq matrix, transaction model, operator
procedure). This plan tree is the build decomposition of that doc's Phase 1.
When a step needs a detail, it cites a `plan.md` section.

## Settled decisions (folded in — do not re-litigate)

1. **Option A — `apply` lives in `import_to_sqlite.py`, sharing `seeds_shared/`
   with rebuild.** One row-builder, validator, schema, dict-codec; both writers
   call it so they emit byte-identical rows (R3 extract-don't-duplicate). Source:
   senior-architect-consult, this conversation; `plan.md` §5.
2. **The script writes DBs only — never a CSV.** The CSV is human-authored
   input in Phase 1. CSV-writing is Phase 2. Source: user, this conversation;
   `plan.md` §1 lead.
3. **Fingerprint branch keys on kind-class, NOT RVA-match.** Three classes:
   (a) function kinds (`function`, `function_no_sig`, `function_variadic`) →
   promote bulk row, KEEP fingerprint; (b) RVA-bearing non-function kinds
   (`callsite`, `data_slot`, `string_anchor`, `instruction_anchor`,
   `vtable_base`) → fingerprint columns forced NULL; (c) RVA-less
   (`vtable_index`, the 6 rows ids 3000–3005) → mint all-NULL. Source:
   [seed-to-db-migration-mapping.md](../seed-to-db-migration-mapping.md)
   §"Row-kind taxonomy"; `plan.md` §3.
4. **`apply` skips rebuild steps 1–4 only when the target version's bulk
   baseline is present.** A function-kind add at a version never baselined →
   REFUSE, direct to `--rebuild`; never mint a NULL-fingerprint function row.
   Source: this conversation; `plan.md` §1, §3 baseline-present check.
5. **`.rdata` version resolver is an alternative to `whdlversions.json`, not a
   replacement.** The importer's existing JSON path stays valid. Source: R12;
   `plan.md` §7.

## Cross-step invariants

- **The rebuild path is the oracle.** Every `apply` step's correctness test is:
  an `apply` sequence produces a DB byte-identical (or row-set-identical, modulo
  `address_versions.id` autoincrement) to what `--rebuild` from the same seeds
  produces. The shared row-builder (step 1) is what makes this hold.
- **Both DBs, every action.** User DB and dev DB are written in user→dev order,
  each action wrapped `BEGIN; …; COMMIT;` (`plan.md` §6).
- **Validation gate before any write.** No DB write begins until the shared
  validator accepts the FULL seed CSV state (not just the changed rows).
- **Append-only schema.** `seeds_shared/schema.py` is the single declaration;
  no step adds a column without it flowing through the row-builder to both DBs.

## Test bar — note on `test-suite.md`

This is Python tooling under `data/refdata-extractor/`, not engine code, so the
`test-suite.md` "permanent `test-plugins/` plugin + in-game matrix row" bar does
not map. The Phase-1 test bar is the **apply-equals-rebuild oracle**: a Python
test (under `data/refdata-extractor/`) asserting `apply` and `--rebuild` agree on
the same seeds. Each step states its slice of that bar.

## Current state (what exists today)

- `import_to_sqlite.py --rebuild` — full baseline build; all of `SCHEMA`,
  `read_*_seed` validators, the `Dicts` codec, and the row construction are
  inline in this ~1490-line file.
- Game-version resolution via `whdlversions.json` (`read_game_version`).
- The three hand-edited seed CSVs under `data/seeds/`.
- `data/refdata-extractor/` and `data/maintainer-tool/` already private
  (`publish-public.ps1` `$PrivateSubpaths`).

Nothing of `apply` / `seeds_shared/` / the `.rdata` resolver exists yet — this
plan builds them.
