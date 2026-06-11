# refdata-extractor

The production **reference-data extractor** — a headless toolchain that
mechanically extracts, per WHGame.dll function, the reference data that backs the
author surface (`kcdx.find`, `kcdx.hook`, `kcdx.statement.*`). It emits **five
RVA-sharded CSV-per-table directories** (the bulk dump), which the import turns
into the shipped `reference.sqlite` baseline.

> **Source-of-truth note (2026-06-02 — see
> [`../maintainer-tool/design.md`](../maintainer-tool/design.md)).** Two distinct
> CSV roles must not be conflated: (1) the **bulk dump** this extractor emits (the
> `functions/`/`statements/`/… sharded tables — the per-version baseline input to
> `--rebuild`); and (2) the **curated CSV export** under `data/db-export/`. Under the
> settled maintainer-tool design the curated layer is authored **DB-first** — the
> maintainer tool edits `reference.sqlite` directly and **auto-exports** the curated
> CSVs as a derived, git-tracked diff layer (no longer hand-edited). This
> extractor's bulk dump is unchanged; the curated CSV↔DB direction inverted.

It runs against **a local Ghidra install + an analyzed Ghidra project of
WHGame.dll**, plus a function-enumeration CSV — all produced locally and kept out
of the repo (heavy, reproducible, machine-specific inputs). The extractor
*consumes* those inputs; it does not contain them.

## Layout

```
data/refdata-extractor/
  ghidra/                         the Java/Ghidra passes (run via Ghidra -scriptPath)
    ProduceReferenceData.java     entry point -- drives the 3 Java passes
    produce-reference-data.ps1    launcher (deadlock defenses + range args)
    refdata/
      FunctionPass.java           functions/ table
      StatementPass.java          statements/ + referenced_vars/ tables
      CallEdgePass.java           call_edges/ table (the call-graph backbone)
      ShardWriter.java            RVA-sharded CSV writer (autoflush + UTF-8)
      ContentHash.java            BLAKE3 content_hash helper (the contract)
      Csv.java                    RFC-4180 cell quoting
      RvaRange.java               half-open [start,end) range filter (resume/parallel)
    blake3/
      org/apache/commons/codec/digest/Blake3.java   vendored Apache Commons Codec 1.16.0
      Blake3SelfTest.java         35 official BLAKE3 vectors (the canonicality gate)
      Blake3Hex.java              stdin->hex filter (the harness's independent oracle)
      test_vectors.json           official BLAKE3 vectors (vendored)
      PROVENANCE.md
    BLAKE3-HASH-CONTRACT.md       the producer<->engine content_hash wire format
  python/                         the Python passes + the validation harness
    produce_signatures.py         signatures/ table (abi_walker honest width-typed floor)
    produce_caller_reg_args.py    caller_reg_args/ table (register-arg estimate)
    size_abi_walker_cost.py       compute-sizing probe (cost measurement)
    probe_caller_arity.py         caller-arity feasibility probe
    validate_extractor_output.py  THE GATE -- 26 checks vs independent anchors (dump CSVs)
    VALIDATE-EXTRACTOR-README.md  the harness runbook + its falsifiability record
    import_to_sqlite.py           dump + seed -> the two SQLite DBs (entity/version schema; rebuild + update modes)
    validate_db_shape.py          THE DB-SHAPE GATE -- 25 checks on the built DB schema
  README.md                       (this file)
```

The two gates are layered: `validate_extractor_output.py` checks the extractor's
**dump CSV** output (upstream); `validate_db_shape.py` checks that
`import_to_sqlite.py` builds the **DB** in the locked schema (downstream).

## The five output tables (→ reference.sqlite)

| Table | Pass | What |
|---|---|---|
| `functions/` | FunctionPass (Java) | per-function: rva, length, auto_name, content_hash, ghidra-signature, decompile_quality |
| `statements/` | StatementPass (Java) | per-statement: kind, pseudo_text, byte_range, content_hash, callee, string_ref |
| `referenced_vars/` | StatementPass (Java) | per-statement referenced-variable storage (approximation; NOT the live-in set) |
| `call_edges/` | CallEdgePass (Java) | the binary-wide caller↔callee graph (the discovery backbone) |
| `signatures/` | produce_signatures.py | abi_walker honest width-typed signature floor (merges over functions/ by rva) |
| `caller_reg_args/` | produce_caller_reg_args.py | caller-side register-arg estimate (a non-authoritative tighter floor) |

Every table is RVA-sharded on the SAME `shardOf(rva)=rva//0x100000` mapping, so all
tables' `*_<startRva>.csv` cover the identical RVA window — the maintainer imports
shard-by-shard.

## Run it

```powershell
# 1. The Java side (functions/ statements/ referenced_vars/ call_edges/) over the full binary:
pwsh data/refdata-extractor/ghidra/produce-reference-data.ps1 `
    -ProjectDir <ghidra-project-dir> -ProjectName KCD2 `
    -OutDir <out>/refdata-full -Module WHGame.dll -VersionTag release_1_5_1164953_841

# 2. The Python side (signatures/ caller_reg_args/), same out dir:
python data/refdata-extractor/python/produce_signatures.py \
    <WHGame.dll> <enum.csv> <out>/refdata-full
python data/refdata-extractor/python/produce_caller_reg_args.py \
    <WHGame.dll> <enum.csv> <out>/refdata-full
```

The full WHGame.dll run is ~2-2.5 hr single-threaded (decompile-bound; the output
flushes incrementally + prints a heartbeat every 10000 functions, so it is
observable + crash-survivable). Re-runs (new game versions) can be sharded with the
`-RvaStart/-RvaEnd` range filter across per-worker project COPIES — Ghidra locks a
project exclusively, so concurrent workers each need their own copy of the project.
The 8-way parallel runner wraps all of the above:

```powershell
pwsh data/refdata-extractor/run-parallel.ps1 -VersionTag release_1_5_1164953_841 -Workers 8
```

The runner derives the output directory from `-VersionTag` automatically —
the dump lands at `data/refdata-extractor/dump/refdata-<short-version>/`
(e.g. `dump/refdata-1.5.1164953/`). `-OutDir` is an optional override for
ad-hoc / synthetic runs. If a dir for the target game version already
exists, the runner stops without overwriting; delete the dir manually to
re-extract that same version.

See `dump/README.md` for the dump-dir naming convention + how downstream
tools resolve "which dump to use" by default.

## Import → the two shipped DBs

The import has **two modes**:

```bash
# REBUILD (--rebuild <dump_dir> <out_dir>): from-scratch baseline build.
# Reads from the dump_dir; writes BOTH DBs into the out_dir verbatim:
#   <out_dir>/reference.sqlite       (USER production, curated-only)
#   <out_dir>/reference-dev.sqlite   (DEV bulk discovery superset)
python data/refdata-extractor/python/import_to_sqlite.py \
    --rebuild data/refdata-extractor/dump/refdata-<version> data/

# UPDATE (default; takes <out_dir> <game_dir>): the per-version incremental path.
# Reads the existing DBs in <out_dir>, detects whether the game on disk is newer,
# decides whether the maintainer needs to re-verify. Reads the on-disk version
# from the game's whdlversions.json (the shipped MasterMasterPGO config's build
# number; the importer uses this JSON because it is a simpler parse than the
# DLL's own .rdata string -- the DLL carries no PE VS_VERSIONINFO resource,
# but it does intern the version twice as a .rdata string, which other tools
# may scan directly).
python data/refdata-extractor/python/import_to_sqlite.py data/ <game-dir>
#  exit 0 = DB already current; exit 3 = newer game version (maintainer
#  re-verifies the curated set against the new dump); exit 2 = usage error.
```

The import produces **two** DBs with **disjoint purposes** (per the streamlined
three-track model — see `data/reference.md` for the full author-facing
explanation):

| DB | Carries | Size | Who ships / fetches it |
|---|---|---|---|
| **USER** `reference.sqlite` (production) | **curated entities only** (~140) + their per-version verified facts: `address_names`, `address_versions`, `modules`, `game_versions`, `meta` | ~0.1 MB | every kcdx release — the engine resolves curated targets (`target = "IsInCombat"`) against this at launch |
| **DEV** `reference-dev.sqlite` (discovery) | **bulk superset** — the curated set PLUS the binary's full ~321K function table (as bulk `address_versions` rows with `kcdx_id NULL`), per-statement metadata, variable storage, call graph, abi_walker floor (`+ statements + referenced_vars + call_edges` + the dev-only columns) | ~1.3 GB | on-demand author download — `kcdx.find` discovery of uncurated targets, which the author then declares in their own plugin via `kcdx.declare(module, name, versions)` |

The schema is documented in full at `data/reference.md` (user) and
`data/reference-dev.md` (dev). In brief:

- **`address_names`** is the curated entity registry; `id` IS the `kcdx_id` — canonical from `data/db-export/address_names_seed.csv` (no autoincrement; null/duplicate id is a hard error). The kcdx_id is the stable cross-version handle plugins reference. **`address_versions`** holds per-version validity intervals: one open row per entity (`valid_through IS NULL` = current), carrying the content_hash, address, kind, abi_walker argument-width floor, the verified signature when curated, AND the per-version verification audit trail (`last_verified_at_version`, `verified_by`, `verified_date`, `evidence_kind`). **`address_versions.kcdx_id` is NULLABLE** — set when the row is a curated entity (FK to `address_names.id`), NULL when the row is a bulk uncurated DEV function. The dev-only `statements`/`referenced_vars`/`call_edges` carry TWO FK columns each: `address_version_id` (always set, FK to `address_versions.id`, the universal "which function row" handle that `kcdx.find` walks) + `kcdx_id` (nullable, FK to `address_names.id`, set only when the owning function is curated). **`modules`** / **`game_versions`** are registries; **`meta`** is the one-row header.
- **Why the user/dev split:** a mod user's runtime needs only the survival hashes + the marshalling ABI for the entities their plugins hook — the small USER DB (curated rows only). The per-statement metadata + the call graph + the bulk address_versions rows exist only for the author discovery/inspection surface → the larger DEV DB. (`call_edges` is dev-only: it powers `kcdx.find`'s caller-graph ranking + cross-version re-identification.)
- **Encoding (lossless):** content_hash → 32-byte BLOB; low-cardinality repetitive text → INTEGER FK into `_dict_*` lookup tables; address/count cols → INTEGER.
- **Append-only, updated in place:** the DB is NOT rebuilt per game version — the default update mode appends the new version's intervals to the existing DB.
- **The DBs ship as release assets** — out-dir, NOT in git (the user DB in the release; the dev DB as a separate download). Note (per [`../maintainer-tool/design.md`](../maintainer-tool/design.md)): for the CURATED layer the DB is the authoring surface and the `data/db-export/` CSVs are its git-tracked export — so the curated facts ARE version-controlled (via the exported CSVs), even though the built `.sqlite` files are not. The BULK dev superset (the ~321K dump rows) remains a generated-from-dump artifact.
- **NOT YET DONE:** the cross-version matcher that re-identifies an entity after its bytes change (so update mode can append a new game version's intervals). Update mode currently detects a newer version and reports "matcher required" (exit 3) without mutating the DB.

## Verify it

```bash
python data/refdata-extractor/python/validate_extractor_output.py   # -> 26/26 PASS
```

The harness is the **falsifiable regression net**. It runs the full toolchain over
a 256-function fixture and asserts 26 checks against INDEPENDENT anchors (the
function-enumeration CSV produced by a different tool; an independent BLAKE3
recompute; cross-corroborated call edges; ground truth from the probes) — each with
a nameable extractor-broken state that would flip it to FAIL. See
`python/VALIDATE-EXTRACTOR-README.md`.

The **DB-shape gate** verifies the import (downstream of the dump):

```bash
python data/refdata-extractor/python/validate_db_shape.py   # -> all PASS
```

It builds both DBs from the dump and asserts a suite of checks on the built schema
(currently 45+; the count grows as the schema does): table presence per DB, the
USER/DEV column projection, the one-open-interval-per-curated-entity invariant
(partial-unique on `address_versions.kcdx_id` where not null), the `address_names`
count == `address_names_seed.csv` count, the canonical-id authority (address_names.id
matches the seed's id set exactly), the bulk-vs-curated row split on
`address_versions` (bulk rows have `kcdx_id NULL`), the `content_hash` BLOB
round-trip (USER + DEV), end-to-end name resolution, FK closure on both
`address_version_id` (universal) and `kcdx_id` (curated), modules-table contents
match `module_seed.csv`, supersession + deprecation pair-integrity + cycle
detection, verification audit-trio integrity, and the version-ordering invariant
(`last_verified_at_version >= valid_from`) — each falsifiable.

The harness also drives `make_sandbox.py` indirectly via shared imports; the
sandbox itself (`sandbox/make_sandbox.py`) is the in-flight cross-version
matcher's fixture builder — separate work-in-progress, not on the live gate.

The BLAKE3 primitive has its own gate:

```bash
cd data/refdata-extractor/ghidra/blake3
javac org/apache/commons/codec/digest/Blake3.java Blake3SelfTest.java && java -cp . Blake3SelfTest
# -> 35/35 official vectors PASS
```

## Design

The content_hash wire format between this producer and the engine consumer is
`ghidra/BLAKE3-HASH-CONTRACT.md`: BLAKE3 256-bit, lowercase-hex / 32-byte BLOB, over
the raw on-disk function-body bytes `[rva, rva+length)`, no normalization — the
engine reads the same span from the on-disk module file so the static-dump hash and
the runtime hash compare directly.

## Python dependencies

The Python passes need `pefile` 2024.8.26 + `capstone` 5.0.7 (PE section mapping +
disassembly). The `seeds_shared/survival_checker.py` per-kind survival checker adds:

| Package | Version | License (elected) | Purpose |
|---|---|---|---|
| `blake3` | 1.0.8 | **Apache-2.0** (the package is `CC0-1.0 OR Apache-2.0`; the Apache-2.0 branch is elected, matching the vendored Java BLAKE3 at `ghidra/blake3/`, which is Apache Commons Codec, Apache-2.0) | Canonical BLAKE3 for the survival checker's function body-hash kind. stdlib `hashlib` has no blake3; the body-hash re-hashes `[rva, rva+length)` and compares to the stored `content_hash`. Agrees with the production content_hash and the Java oracle via the 35 official BLAKE3 vectors (the canonicality gate; `ghidra/BLAKE3-HASH-CONTRACT.md` §1). |
