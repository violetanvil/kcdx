# TD-0004 — rebuild-oracle baseline is stale; needs a deliberate, inspected re-capture

**Reported:** 2026-06-02
**Status:** Open
**Closure trigger (named):** a deliberate `--capture` of
`data/refdata-extractor/tests/oracle_baseline.json` from the corrected full-dump
rebuild, with each drift source inspected and a provenance note appended to
`test_rebuild_oracle.py`'s docstring — landing in its own commit, not folded into an
unrelated change.

## What it is

`data/refdata-extractor/tests/test_rebuild_oracle.py` is the project's
behaviour-preserving gate: it rebuilds `reference.sqlite` + `reference-dev.sqlite` from
the full local dump (`dump/refdata-1.5.1164953`) and compares every table (content hash +
row count) against the recorded golden snapshot `oracle_baseline.json`. The baseline was
last captured at commit `78f7f27` (2026-05-31). **It is now stale** — the rebuild drifts
from it, so the oracle is RED on `main`.

## Why the baseline is stale (the drift sources — ALL must be inspected before re-capture)

Six+ commits to the seed/import path landed AFTER the baseline capture (`78f7f27..HEAD`),
each legitimately changing the rebuild output:

1. **Curated entity growth (143 → 151 rows).** `e37b5af` curated IConsole::PrintLine
   (id 150) + PrintLinePlus (id 151); other commits curated save/load hooks + vtable_index
   slots. The baseline records 143 curated rows; the rebuild now produces 151. (USER+DEV
   `address_names` / `address_versions` / `survival` row counts + hashes.)
2. **The `importer-no-prose-derivation` Phase-3 rewrite.** Commits `91f60ec`, `a72736b`,
   etc. rewrote the import logic (deleted the prose-derivation machinery; authored the
   per-kind datum columns). This drifts the bulk DEV tables (`call_edges`, `statements`,
   `referenced_vars`, `sqlite_sequence`).
3. **The signature-NULL fix (this surfacing change — step 1b of `maintainer-tool-db-direct`).**
   `row_builder.build_curated_row` now persists NULL for a blank authored `signature` on
   curated `function_no_sig` / `function_variadic` rows instead of inheriting the bulk
   `abi_walker` floor (`? (...)`). This drifts the `address_versions` content hash on the
   12 affected rows (USER+DEV). This is the ONLY drift source covered by an isolated,
   green per-step oracle (`test_importer_blank_signature.py`).

## Why deferred (not fixed at the surfacing change)

The re-capture cannot be folded into step 1b's commit: doing so would bake drift sources
1 + 2 — eight new entities and an entire import rewrite that step 1b did not author and has
not inspected — into a "golden" baseline under step 1b's commit. That is the
laundering-uninspected-output failure the oracle's own docstring forbids ("Re-capture …
ONLY for a deliberate, reviewed output change … never to paper over an unexplained
drift"). Each drift source must be inspected by whoever can vouch for it before the
snapshot is recorded. Step 1b's slice (source 3) is isolated-proven by
`test_importer_blank_signature.py`; sources 1 + 2 are owed an inspection this entry tracks.

The oracle was ALREADY red before step 1b (sources 1 + 2 predate it); step 1b adds source
3 and does not regress the gate.

## Closure (when the trigger fires)

1. Inspect each drift source against its intent: source 1 — the 8 new curated entities are
   correct (row counts == curated entity count, correct per-kind survival datum); source 2
   — the no-prose rewrite output matches its own design; source 3 — the 12 signature cells
   are NULL where the seed is blank (already proven by `test_importer_blank_signature.py`).
2. Re-capture `oracle_baseline.json` from the corrected rebuild (`--capture`).
3. Append a provenance entry to `test_rebuild_oracle.py`'s `BASELINE PROVENANCE` docstring
   naming all three drift sources (mirroring the existing step-4 / step-5.1 / step-5.2
   entries).
4. Confirm `test_rebuild_oracle.py` is green, commit, close this entry (move to `closed/`,
   reindex).
