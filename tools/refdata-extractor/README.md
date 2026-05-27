# refdata-extractor

The production **reference-data extractor** — a headless toolchain that
mechanically extracts, per WHGame.dll function, the reference data that backs the
author surface (`kcdx.find`, `kcdx.hook`, `kcdx.statement.*`). It emits **five
RVA-sharded CSV-per-table directories**, which a maintainer import turns into the
shipped `reference.sqlite`.

It runs against **a local Ghidra install + an analyzed Ghidra project of
WHGame.dll**, plus a function-enumeration CSV — all produced locally and kept out
of the repo (heavy, reproducible, machine-specific inputs). The extractor
*consumes* those inputs; it does not contain them.

## Layout

```
tools/refdata-extractor/
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
    validate_extractor_output.py  THE GATE -- 26 checks vs independent anchors
    VALIDATE-EXTRACTOR-README.md  the harness runbook + its falsifiability record
  README.md                       (this file)
```

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
pwsh tools/refdata-extractor/ghidra/produce-reference-data.ps1 `
    -ProjectDir <ghidra-project-dir> -ProjectName KCD2 `
    -OutDir <out>/refdata-full -Module WHGame.dll -VersionTag release_1_5_1164953_841

# 2. The Python side (signatures/ caller_reg_args/), same out dir:
python tools/refdata-extractor/python/produce_signatures.py \
    <WHGame.dll> <enum.csv> <out>/refdata-full
python tools/refdata-extractor/python/produce_caller_reg_args.py \
    <WHGame.dll> <enum.csv> <out>/refdata-full
```

The full WHGame.dll run is ~2-2.5 hr single-threaded (decompile-bound; the output
flushes incrementally + prints a heartbeat every 10000 functions, so it is
observable + crash-survivable). Re-runs (new game versions) can be sharded with the
`-RvaStart/-RvaEnd` range filter across per-worker project COPIES — Ghidra locks a
project exclusively, so concurrent workers each need their own copy of the project.
The 8-way parallel runner wraps all of the above:

```powershell
pwsh tools/refdata-extractor/run-parallel.ps1 -OutDir <out>/refdata-full -Workers 8 `
    -VersionTag release_1_5_1164953_841
```

## Import → the two shipped DBs

```bash
python tools/refdata-extractor/python/import_to_sqlite.py <out>/refdata-full <out>/db
#  -> <out>/db/reference.sqlite       (USER, ~48 MB)
#     <out>/db/reference-dev.sqlite   (DEV,  ~1.13 GB)
```

The import produces **two** DBs — the user-vs-dev split:

| DB | Tables | Size (disk / zipped) | Who ships / fetches it |
|---|---|---|---|
| **USER** `reference.sqlite` | functions + signatures + caller_reg_args | 48 MB / 22 MB | every kcdx release — the per-launch survival check (`functions.content_hash`) + the ABI a callback hook needs at install (`functions.signature`) |
| **DEV** `reference-dev.sqlite` | + statements + referenced_vars + call_edges | 1.13 GB / 397 MB | on-demand author download — `kcdx.find` / `kcdx_dev_inspect` discovery |

- **Why the split:** a mod user's runtime needs only the survival hashes + the
  marshalling ABI for the functions their plugins hook — that is the small USER
  DB. The per-statement metadata + the call graph exist only for the author
  discovery/inspection surface, so they go in the larger DEV DB an author fetches
  on demand. (`call_edges` is dev-only: it powers `kcdx.find`'s caller-graph
  ranking, an author feature.)
- **Encoding (lossless):** content_hash → 32-byte BLOB; low-cardinality repetitive
  text → INTEGER FK into `_dict_*` lookup tables; address/count cols → INTEGER.
- **Seamless delivery:** the user DB ships UNCOMPRESSED inside the release zip
  (the zip handles the download); the engine opens the plain `.sqlite` — no
  decompression step, no install-time assembly.
- **Launch cost is negligible** — the survival check is lazy + indexed (only the
  functions a user's plugins hooked are queried; ~12 ms for a heavy total
  conversion). The DB size does not affect launch speed (SQLite mmaps, reads only
  touched pages).
- **The DBs are generated artifacts** — they live at the out-dir, NOT in git; they
  ship as release assets (the user DB in the release; the dev DB as a separate
  download).
- **NOT YET DONE:** stable-ID assignment (`functions.id` matched across game
  versions). A single-version import has no prior IDs to match; a `--assign-ids`
  follow-up adds it when the 2nd game version arrives. The current DB keys on
  `rva` (correct for one version).

## Verify it

```bash
python tools/refdata-extractor/python/validate_extractor_output.py   # -> 26/26 PASS
```

The harness is the **falsifiable regression net**. It runs the full toolchain over
a 256-function fixture and asserts 26 checks against INDEPENDENT anchors (the
function-enumeration CSV produced by a different tool; an independent BLAKE3
recompute; cross-corroborated call edges; ground truth from the probes) — each with
a nameable extractor-broken state that would flip it to FAIL. See
`python/VALIDATE-EXTRACTOR-README.md`.

The BLAKE3 primitive has its own gate:

```bash
cd tools/refdata-extractor/ghidra/blake3
javac org/apache/commons/codec/digest/Blake3.java Blake3SelfTest.java && java -cp . Blake3SelfTest
# -> 35/35 official vectors PASS
```

## Design

The content_hash wire format between this producer and the engine consumer is
`ghidra/BLAKE3-HASH-CONTRACT.md`: BLAKE3 256-bit, lowercase-hex / 32-byte BLOB, over
the raw on-disk function-body bytes `[rva, rva+length)`, no normalization — the
engine reads the same span from the on-disk module file so the static-dump hash and
the runtime hash compare directly.
