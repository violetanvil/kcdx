# Reference-data — where the full datasource + the dev DB live (MAINTAINER, LOCAL)

> **Note (2026-05-28):** Paths referenced below have moved. `tools/refdata-extractor/`
> is now `data/refdata-extractor/`, `data/address-library/` is now `data/seeds/`
> (split into `module_seed.csv` + `address_names_seed.csv` + `address_versions_seed.csv` +
> `policy.md`), and the schema READMEs are now `data/reference.md` + `data/reference-dev.md`
> (siblings to the gitignored DBs at `data/reference.sqlite` + `data/reference-dev.sqlite`).
> The Ghidra-extracted dump dirs no longer live at the absolute path
> `C:\kcdx-refdata\refdata-full-<stamp>\` — they now live under the repo at
> `data/refdata-extractor/dump/refdata-<game-version>/` (one dir per game version,
> resolved relative to the script so the pipeline runs on any maintainer's clone).
> This file is a historical record and is not maintained against the new layout —
> consult the current schema docs and policy.md for the authoritative shape.

PRIVATE / gitignored (`_research/`). Holds the local paths + maintainer detail
that must NOT reach the public repo. The public-facing description of the user DB
is `data/reference/README.md` (which deliberately names none of this).

## The artifacts (current, on this machine)

All under `C:\kcdx-refdata\`:

```
C:\kcdx-refdata\
  refdata-full-20260527-105617\        the raw CSV dump (the source of truth)
    functions/  statements/  referenced_vars/  call_edges/  signatures/  caller_reg_args/
    _MANIFEST.md
  db\
    reference.sqlite        (~0.1 MB)  the USER production DB (curated-only) -> ships as a release asset
    reference-dev.sqlite     (~1.13 GB) the DEV bulk discovery DB             -> on-demand author download
```

**Schema: the FLATTENED 5-user-table model** (`docs/outstanding-work/
parallel-ghidra-research.md` §11.9), with the streamlined three-track scope
applied (§11.8): the USER production DB carries CURATED ENTITIES ONLY (the
~140 from the seed); the DEV bulk discovery DB carries the curated set PLUS the
binary's full ~321K function table for `kcdx.find` discovery. Both built by
`import_to_sqlite.py`; gated by `validate_db_shape.py` (25/25 against the real
321K-function dump). `integrity_check=ok` on both; the name→address+verified-ABI
resolution path verified against real rows.

- **USER `reference.sqlite`** (~0.1 MB, curated-only) — `modules` +
  `game_versions` + `address_names` (143 curated; `id` IS the kcdx_id,
  autoincrement starting at 1) + `address_versions` (143 rows, all curated,
  `kcdx_id NOT NULL` — minus the dev-only `auto_name`/`decompile_quality`
  columns) + `meta`. Ships with every kcdx release (a release asset, NOT
  committed to git). The user-facing README is `data/reference/README.md`. The
  bulk function table is NOT here — Track-2 plugins targeting uncurated
  functions declare them themselves via `kcdx.declare(module, name, versions)`.
- **DEV `reference-dev.sqlite`** (~1.3 GB, bulk superset) — everything in USER
  in full + the bulk ~321K function entries (as `address_versions` rows with
  `kcdx_id NULL`) + `statements` + `referenced_vars` + `call_edges` + the
  dev-only columns. The dev-only tables carry TWO FK columns each
  (`address_version_id` always-set + `kcdx_id` nullable). The
  discovery/inspection dataset (`kcdx.find` / `kcdx_dev_inspect`); the
  maintainer's source-of-truth and the author's on-demand download for finding
  uncurated targets to declare. NOT shipped to users, NOT committed to git.

**Handoff (2026-05-27):** both DBs at `C:\kcdx-refdata\db\` are in the locked
schema the generator + engine consumer read. This UNBLOCKS that work. Remaining
import-side functionality (the two-mode CLI + `whdlversions.json` version
detector) is additive and does not change the DB shape the generator reads.
- **The raw CSV dump** — the per-table sharded output the import reads. Keep it;
  it is what `import_to_sqlite.py` re-imports, and the basis for the next
  game-version diff.

## How they were produced

The toolchain is `tools/refdata-extractor/` (tracked, public). Inputs are local:
the analyzed Ghidra project of WHGame.dll under `third-party-ghidra/`, the
function-enumeration CSV under `_research/parallel-ghidra-research/inventory/`, and
the live WHGame.dll.

```
# 1. the 8-way parallel dump (~20-30 min on a 16-core box):
pwsh tools/refdata-extractor/run-parallel.ps1 `
    -OutDir C:\kcdx-refdata\refdata-full-<stamp> -Workers 8 `
    -VersionTag release_1_5_1164953_841

# 2. import -> both DBs:
python tools/refdata-extractor/python/import_to_sqlite.py `
    C:\kcdx-refdata\refdata-full-<stamp> C:\kcdx-refdata\db
```

Verified `release_1_5_1164953_841`: 321,120 functions, 5.24M statements, 10.88M
referenced_vars, 1.52M call_edges; the merge was collision-clean; the output
validation harness passes 26/26 against independent anchors; the BLAKE3 primitive
passes the 35 official vectors.

## Distribution (the decision)

- USER DB → release asset (bundled in the kcdx release archive). NOT in git
  (a 50 MB binary would churn git history every version refresh).
- DEV DB → a separate on-demand download for mod authors (a GitHub Release asset
  or equivalent). NOT in git.
- Neither DB is committed; both are generated from the raw dump by
  `import_to_sqlite.py`, which IS tracked.

## Not yet done

- **Stable-ID assignment** (`functions.id` matched across game versions by
  name+signature+caller-graph fingerprint). A single-version import has no prior
  IDs to match against; this arrives mechanically with the 2nd KCD2 version (a
  `--assign-ids` step on the import). The current DBs key on `rva`, correct for
  one version.
- **The engine consumer** that reads `reference.sqlite` at launch (the `hash_at`
  survival-check primitive) is separate, unbuilt work — the DB exists but nothing
  loads it yet.
- **Secondary DLLs** (BugSplat64, BugSplatRc64, Quatmosphere, WhGdk) are not yet
  imported into the Ghidra project; dump them after they're added.

The full effort record is `docs/outstanding-work/parallel-ghidra-research.md`
(§4f the user/dev split + sizing, §8 the build ledger).
