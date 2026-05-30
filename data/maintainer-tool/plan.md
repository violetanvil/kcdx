# Maintainer tool — design plan

The architectural plan for the seed→DB maintainer flow. Derived from
[requirements.md](requirements.md) (R1–R12) plus the decisions worked through
in the design sessions.

**The flow is built in two phases, with a hard seam between them:**

- **Phase 1 — the `apply` script (headless, no UI).** Humans keep hand-editing
  the three seed CSVs under `data/seeds/` (as they do today). A script reads
  those CSVs and performs the DB actions: a full **rebuild** (already exists) and
  a new **incremental add** that lands CSV rows not yet in the DB into BOTH the
  user DB and the dev DB, without dropping and rebuilding. **The script writes
  no CSV** — the CSV is human-authored input; the script is the CSV→DB applier.
- **Phase 2 — the tool (GUI).** Built later, on top of the proven Phase-1
  applier. The GUI is what eventually replaces CSV hand-editing; until it stands
  on its own, the CSV stays the authoring surface.

This doc commits the Phase-1 architecture (the incremental SQL shapes, the
shared-module extraction, the version resolver). Phase 2's surface — screens,
forms, dense/nested lists, the CSV-writing path, atomic multi-file CSV
transactions — is deliberately out of scope here and is specced when Phase 2
starts.

> **Requirements note.** R1/R6 as written scope the tool to seed-CSV editing
> and explicitly exclude building the DBs. That scope has been superseded: the
> flow writes the DBs (user + dev) and the human-edits-CSV / script-applies-DB
> split is the agreed Phase-1 shape. R1/R6 are to be amended to match this two-
> phase architecture in the same change that lands Phase 1.

## Contents

1. [The fundamental insight — incremental, not rebuild](#1-fundamental-insight)
2. [Where each address-versions field comes from](#2-where-each-field-comes-from)
3. [Per-action SQL shape (Phase 1)](#3-per-action-sql-shape)
4. [The dev-DB prereq matrix](#4-the-dev-db-prereq-matrix)
5. [Shared module — one row-builder, validator, schema, codec](#5-shared-module)
6. [Transaction model (Phase 1)](#6-transaction-model)
7. [Version resolution from the linked DLL](#7-version-resolution-from-the-linked-dll)
8. [Phase 1 deliverable on disk](#8-phase-1-deliverable)
9. [Phase 2 — out of scope for this plan](#9-phase-2-out-of-scope)

---

## 1. The fundamental insight — incremental, not rebuild <a name="1-fundamental-insight"></a>

Today the only path that gets a curated edit into `reference.sqlite` /
`reference-dev.sqlite` is `import_to_sqlite.py --rebuild`. `build_rows()` does
six distinct things, of which **only two** are needed for an edit that adds,
modifies, or re-verifies a curated entity:

| Step | Importer `build_rows()` does | Needed for a curated edit? |
|---|---|---|
| 1 | Read `functions/` dump (~321K rows), sort by RVA, assign `address_versions.id` 1..N | NO |
| 2 | Merge `signatures/` + `caller_reg_args/` per RVA | NO |
| 3 | INSERT ~321K bulk `address_versions` rows with `kcdx_id IS NULL` | NO |
| 4 | Build dev-only tables (`statements`, `referenced_vars`, `call_edges`) | NO |
| 5 | Read `address_names_seed.csv` → INSERT `address_names` rows | YES |
| 6 | Read `address_versions_seed.csv` → INSERT or PROMOTE `address_versions` rows | YES |

Steps 1–4 are bulk dev-DB construction, independent of which curated entities
exist and unchanged from one curated edit to the next. The rebuild re-runs them
only because `write_db` starts with `os.remove(db_path)` — the whole DB is
dropped, so everything is re-inserted.

**The incremental `apply` path skips steps 1–4 entirely.** It runs the
equivalent of steps 5 and 6 against the existing DB. Re-verifying a row is a
single `UPDATE`; adding a new curated entity is `INSERT` + (`UPDATE` or
`INSERT`); neither needs the dump, the bulk function table, or the dev-only
tables.

The full rebuild is preserved for two cases:
- **Baseline build.** First time a game version is onboarded.
- **Schema migration.** When the schema itself changes.

Day-to-day curated edits use incremental `apply`, not rebuild.

## 2. Where each address-versions field comes from <a name="2-where-each-field-comes-from"></a>

The USER projection of `address_versions` is **20 columns**
([import_to_sqlite.py](../refdata-extractor/python/import_to_sqlite.py) `USER_COLUMNS["address_versions"]`);
the dev projection adds two more (`auto_name`, `decompile_quality`).
Knowing where each column ORIGINATES decides which actions can run without the
dev DB and which can't.

| Column | Source | Re-verify | Add entity |
|---|---|---|---|
| `id` | autoincrement | n/a | n/a |
| `kcdx_id` | seed (canonical) | unchanged | new from seed |
| `kind` | derived from seed `notes` + RVA pattern | unchanged | derived |
| `module_id` | seed `module` column (FK) | unchanged | new from seed |
| `rva` | seed | unchanged | new from seed |
| `length` | **dump `functions/` table** | unchanged | **dev DB (function kinds only)** |
| `content_hash` | **dump `functions/` table** | unchanged | **dev DB (function kinds only)** |
| `value` | derived (vtable slot int for vtable_index kind) | unchanged | derived |
| `signature` | seed (or abi_walker floor when seed is empty) | unchanged | new from seed |
| `observed_arg_slots` | **dump `signatures/` table** | unchanged | **dev DB (function kinds only)** |
| `caller_reg_arg_count` | **dump `caller_reg_args/` table** | unchanged | **dev DB (function kinds only)** |
| `caller_arg_agreement` | **dump `caller_reg_args/` table** | unchanged | **dev DB (function kinds only)** |
| `offset` | derived from seed `notes` for callsite kind | unchanged | derived |
| `vtable_slot` | derived from seed `notes` for vtable_index kind | unchanged | derived |
| `last_verified_at_version` | seed (audit trio) | **changed** | new from seed |
| `verified_by` | seed (audit trio) | **changed** | new from seed |
| `verified_date` | seed (audit trio) | **changed** | new from seed |
| `evidence_kind` | seed (audit trio, dict-encoded) | **changed** | new from seed |
| `valid_from` | seed (FK to game_versions) | unchanged | new from seed |
| `valid_through` | derived (next-row's valid_from, or NULL) | unchanged | NULL |

**Re-verify mutates only the audit-trio columns.** Every other column is
already correct in the existing row — it came from the baseline build and
re-verification doesn't change it. **Re-verify does NOT need the dev DB.**

**Add-entity needs `length`, `content_hash`, `observed_arg_slots`,
`caller_reg_arg_count`, `caller_arg_agreement` — but ONLY for function kinds,
and ONLY those columns live in the dev DB.** The fingerprint-vs-NULL decision
keys on the row's **kind-class**, not on whether an RVA happens to match a bulk
row (see §3 — this is the corrected branch).

## 3. Per-action SQL shape (Phase 1) <a name="3-per-action-sql-shape"></a>

Phase 1 covers three actions against both DBs: **re-verify**, **add new
entity**, and **add a new versions row** (a moved/renamed function at a new
game version). The shape is identical across the user DB and the dev DB; only
the column projection differs (some columns are dev-only). Every INSERT/PROMOTE
goes through the SINGLE shared row-builder (§5) — the column lists below are the
rows that builder emits, not hand-maintained SQL parallel to the importer's.

### Re-verify

Mutates one existing `address_versions` row's audit trio.

```sql
UPDATE address_versions
   SET last_verified_at_version = :lvv_id,
       verified_by              = :verified_by,
       verified_date            = :verified_date,
       evidence_kind            = :evidence_kind_id
 WHERE kcdx_id    = :kcdx_id
   AND valid_from = :valid_from_id;
```

One row per DB; two statements total. `evidence_kind` is dict-encoded via the
shared codec (§5). **Idempotent** — running it twice equals running it once.

### Add new curated entity

Two-table edit. **The fingerprint branch keys on kind-class, NOT on RVA match.**

**Step 1 — names row (both DBs):**

```sql
INSERT INTO address_names (id, name, superseded_by, superseded_at_version,
                           is_deprecated, deprecated_at_version,
                           deprecation_replacement, notes)
VALUES (:kcdx_id, :name, NULL, NULL, 0, NULL, NULL, :notes);
```

**Step 2 — versions row.** The three-way kind-class partition
(from [seed-to-db-migration-mapping.md](../../docs/outstanding-work/seed-to-db-migration-mapping.md)
§"Row-kind taxonomy" — 108 plain functions + 31 non-plain rows):

- **(a) Function kinds** (`function`, `function_no_sig`, `function_variadic`) —
  these have a real RVA that matches a bulk dev row. PROMOTE the bulk row,
  KEEPING its fingerprint columns (`length`, `content_hash`,
  `observed_arg_slots`, `caller_reg_arg_count`, `caller_arg_agreement`):

  ```sql
  SELECT id FROM address_versions WHERE rva = :rva AND kcdx_id IS NULL;   -- dev DB
  ```
  If found → PROMOTE (set `kcdx_id`, `kind`, `signature` via COALESCE, the
  audit trio, `offset`, `vtable_slot`, `value`); leave the five fingerprint
  columns as the bulk row had them. The user-DB row is built from the same
  shared row-builder output.

- **(b) RVA-bearing non-function kinds** (`callsite`, `data_slot`,
  `string_anchor`, `instruction_anchor`, `vtable_base` — 25 of the 31 non-plain
  rows). These ALSO carry a real RVA and may match a bulk dev row, but a
  fingerprint is meaningless for them. MINT (or, if promoting a matched bulk
  row, FORCE the five fingerprint columns to NULL regardless of the match):

  ```sql
  INSERT INTO address_versions (kcdx_id, kind, module_id, rva, length,
                                content_hash, value, signature,
                                observed_arg_slots, caller_reg_arg_count,
                                caller_arg_agreement, offset, vtable_slot,
                                last_verified_at_version, verified_by,
                                verified_date, evidence_kind,
                                valid_from, valid_through)
  VALUES (:kcdx_id, :kind_id, :module_id, :rva, NULL, NULL, :value, :signature,
          NULL, NULL, NULL, :offset, :vtable_slot,
          :lvv_id, :verified_by, :verified_date, :evidence_kind_id,
          :valid_from_id, NULL);
  ```

- **(c) RVA-less kind** (`vtable_index` — exactly the 6 rows ids 3000–3005).
  `rva` is empty; the row carries an integer slot. MINT with `rva` NULL and all
  five fingerprint columns NULL (same INSERT shape as (b), with `:rva` NULL).

**Mismatch check.** When a function-kind row (a) matches a bulk row whose
`valid_from` does not match the seed row's `valid_from_version`, the action is
REFUSED — "your dev DB is from a different game version than your seed expects."

### Add a new versions row (existing entity, moved/renamed at a new version)

Same Step-2 partition as add-entity (the new RVA's kind decides
fingerprint-vs-NULL), PLUS closing the previous open interval BEFORE the new
INSERT:

```sql
UPDATE address_versions
   SET valid_through = :prev_version_id
 WHERE kcdx_id       = :kcdx_id
   AND valid_through IS NULL;
```

The closing UPDATE runs first so the partial-unique index
`address_versions(kcdx_id) WHERE kcdx_id IS NOT NULL AND valid_through IS NULL`
([import_to_sqlite.py](../refdata-extractor/python/import_to_sqlite.py)
`ix_av_open_unique`) is never violated. **Note the `kcdx_id IS NOT NULL` term**
— it is what lets the ~321K `kcdx_id IS NULL` bulk rows (all with open
`valid_through`) coexist without tripping the uniqueness constraint; the
incremental INSERT must respect it.

### Deprecate / supersede (names-side, both DBs)

Deprecate is a single names UPDATE (`is_deprecated = 1`,
`deprecated_at_version`, optional `deprecation_replacement`). Supersede sets
`superseded_by` + `superseded_at_version` on the predecessor and adds the
successor via the add-entity path. The shared validator runs the acyclicity
check before either writes.

## 4. The dev-DB prereq matrix <a name="4-the-dev-db-prereq-matrix"></a>

**Dev DB is required for actions that promote a function-kind row** (they read
the bulk row's fingerprint); optional for actions that only touch metadata or
mint non-function kinds. The script refuses an action whose prereq isn't met
with a clear message.

| Action | Dev DB required? | Reason |
|---|---|---|
| Re-verify | NO | UPDATE on existing row; audit-trio columns are seed-sourced |
| Add entity — function kind | **YES** | Promotes the bulk row for its fingerprint columns |
| Add entity — non-function kind | NO | Minted with fingerprint columns NULL; no bulk read |
| Add versions row — function kind | YES | New row promotes the bulk row at the new RVA |
| Add versions row — non-function kind | NO | Minted NULL-fingerprint |
| Deprecate | NO | Names UPDATE only |
| Supersede | mixed | NO for the edge; the successor follows add-entity's rule |

The dev DB is treated as a linkable resource alongside the module DLLs (R12's
per-module link table).

## 5. Shared module — one row-builder, validator, schema, codec <a name="5-shared-module"></a>

Per R3 (extract, don't duplicate) and the Option-A decision: the rebuild path
and the new incremental `apply` path **share one definition of a row**, so they
cannot drift. The extraction happens in the SAME work that adds incremental
`apply` — `import_to_sqlite.py` is already ~1490 lines (past the one-file/one-
concern bar), so the extraction is overdue independently.

**Layout** — `data/refdata-extractor/python/seeds_shared/` (private, inside the
already-private `refdata-extractor/` tree):

- `schema.py` — `SCHEMA`, `USER_COLUMNS`, `DEV_TABLES`, `USER_TABLES`,
  `DICT_COLS`, `EVIDENCE_KIND_ENUM`, `ADDRESS_KINDS`. The canonical declaration
  of every table, column, dict-encoded column, and enum.
- `validators.py` — `read_module_seed`, `read_address_names_seed`,
  `read_address_versions_seed`, plus the post-loop cross-row checks
  (supersession acyclicity, deprecation/supersession pair integrity, FK
  closure, audit-trio integrity, `(kcdx_id, valid_from_version)` uniqueness).
- `row_builder.py` — **the single function that maps a validated seed row →
  the `address_versions` column dict** both writers INSERT. The importer's
  step-6 promote/mint and the incremental `apply` both call it. This is the
  highest-consequence shared piece: it makes "the incremental path wrote a
  different row shape than rebuild would have" structurally impossible.
- `dict_codec.py` — the `Dicts` encode/decode logic (currently inline in the
  importer).
- `version_resolver.py` — the `.rdata` scanner (§7).

**The importer becomes a thin caller.** `build_rows` calls
`validators.read_*`, `schema.SCHEMA`, `row_builder.build`, `dict_codec`. Its
full-rebuild orchestration (the dump reads, the bulk insert, the dev tables)
stays in the importer — that's the baseline/migration path.

**The incremental `apply` mode is a sibling entry point in the same importer
module**, sharing all of `seeds_shared/`. It does step-5/step-6-equivalent work
incrementally against the existing DBs.

## 6. Transaction model (Phase 1) <a name="6-transaction-model"></a>

The CSV is read-only input in Phase 1 (humans own it); the script writes only
the two DBs. So Phase 1's atomicity is per-DB SQLite transactions, not the
multi-file CSV transaction R11 describes (that's a Phase-2 concern when the GUI
writes CSVs).

**Re-verify** is idempotent (the UPDATE's predicate selects the same row
regardless of prior partial state; the columns set derive purely from the CSV).
A failed second-DB write is recovered by re-running `apply`.

**Add-entity / add-versions-row** are multi-row INSERTs, NOT idempotent under
naive retry. The script wraps each action in `BEGIN; ...; COMMIT;` per DB and
applies in user→dev order. A crash between the two DB commits leaves the user DB
applied and the dev DB not; the script detects this on next run by checking
whether the entity's row already exists in each DB, and completes the missing
side (or the operator re-runs a full rebuild, which is always the ground-truth
reconciliation). A journal file is reserved if the per-DB-existence check proves
insufficient in practice; not built for Phase 1.

**Validation gate.** No DB write begins until the shared validator (§5) accepts
the full CSV state. Validation failure → abort, no writes.

## 7. Version resolution from the linked DLL <a name="7-version-resolution-from-the-linked-dll"></a>

Per R12, `V_current` per module is resolved from a linked DLL by scanning
`.rdata` for the `release_M_N_BUILD_SUB` form, with a hard intern-agreement
check. The resolver lives in `seeds_shared/version_resolver.py`.

Algorithm:

1. Open the DLL with `pefile` (already an importer dependency).
2. For each `.rdata` section, scan raw bytes with
   `rb"release_(\d{1,3})_(\d{1,3})_(\d{4,8})_(\d{1,4})"`.
3. Collect all matches with their `(M, N, BUILD, SUB)` tuples and `.rdata` VAs.
4. Fewer than 2 matches → REFUSE ("expected ≥2 interns; found N").
5. Matches disagree → REFUSE; surface each match's VA and value.
6. Matches agree → return `tag = "{M}.{N}.{BUILD}"`, `ordinal = BUILD`.

Two interns verified in 1.5.1164953 at va=0x183c3edef and va=0x183dba258
([_research/init-cycle-recon/_version_strings.txt](../../_research/init-cycle-recon/_version_strings.txt)).

The importer's existing `whdlversions.json` path
([import_to_sqlite.py](../refdata-extractor/python/import_to_sqlite.py)
`read_game_version`) stays valid; this `.rdata` resolver is an alternative the
incremental path uses, not a replacement.

## 8. Phase 1 deliverable on disk <a name="8-phase-1-deliverable"></a>

```
data/refdata-extractor/python/
├── import_to_sqlite.py            (thin caller: --rebuild + new incremental apply mode)
└── seeds_shared/                  (the shared module — §5)
    ├── schema.py
    ├── validators.py
    ├── row_builder.py             (THE single row constructor — §5)
    ├── dict_codec.py
    └── version_resolver.py        (.rdata scan — §7)
```

`data/refdata-extractor/` is already private and already in
`publish-public.ps1`'s `$PrivateSubpaths`; `data/maintainer-tool/` is likewise
already carved out (R10 is already implemented — no publish-script change is
owed by Phase 1).

Phase 1 has no UI and writes no CSV. It is exercised by running the script
against the hand-edited seeds and asserting the resulting DB rows match what a
full `--rebuild` would have produced for the same seeds (the rebuild path is the
oracle — incremental and rebuild must agree, which is exactly what the shared
row-builder guarantees and what the Phase-1 test checks).

## 9. Phase 2 — out of scope for this plan <a name="9-phase-2-out-of-scope"></a>

Phase 2 is the GUI tool, built on the Phase-1 applier once it stands on its own.
Specced when Phase 2 starts; named here only to mark the seam:

- **The GUI surface.** Basic but capable of dense lists, form fills, nested
  lists (the entity list, the version-history view, the add/deprecate forms).
  Framework, layout, screen flow — Phase 2.
- **CSV writing by the tool.** When the GUI replaces hand-editing, it gains the
  CSV-write path (R11 atomic temp-file+rename, diff-preservation, multi-file
  transactions). None of that exists in Phase 1.
- **Driven evidence flows (R5).** The `pattern_scan` AOB-uniqueness scan and the
  `live_test_plugin` coverage convention. Phase 2+.
- **The new-game-version campaign UI.** The unchanged/moved/gone/renamed delta
  report against a new dump dir — the largest single UI component. Phase 2+;
  needs a dump dir to compare hashes.

Phase 1 commits the headless CSV→DB core (incremental + rebuild sharing one
row-builder). The GUI is layered on top, screen by screen, once that core is
proven.
