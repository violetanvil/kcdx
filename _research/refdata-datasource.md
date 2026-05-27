# Reference-data — where the full datasource + the dev DB live (MAINTAINER, LOCAL)

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
    reference.sqlite        (~50 MB)   the USER DB  -> ships as a release asset
    reference.sqlite.zip    (~22 MB)   (the zipped form, for reference)
    reference-dev.sqlite     (~1.13 GB) the DEV DB   -> on-demand author download
    reference-dev.sqlite.zip (~397 MB)
```

- **USER `reference.sqlite`** — functions + signatures + caller_reg_args. Ships
  with every kcdx release (a release asset, NOT committed to git). The
  user-facing README is `data/reference/README.md`.
- **DEV `reference-dev.sqlite`** — the full six tables (+ statements +
  referenced_vars + call_edges). The discovery/inspection dataset
  (`kcdx.find` / `kcdx_dev_inspect`). Mod AUTHORS fetch it on demand; it is the
  maintainer's source-of-truth. NOT shipped to users, NOT committed to git.
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
