# P0.1 probe finding — the DEV bulk round-trips DB→CSV→DB losslessly (no dump)

**Verdict: PASS** (run 2026-06-10, `probe_bulk_csv_roundtrip.py` against the live
`data/reference-dev.sqlite`, 1.3 GB).

| Table | Rows | Result |
|---|---|---|
| `address_versions` | 321,144 (157 curated + 320,987 bulk `kcdx_id`-NULL) | all 29 columns identical |
| `statements` | 5,240,326 | all 11 columns identical (incl. BLOB `content_hash`) |
| `referenced_vars` | 10,883,443 | all 9 columns identical |
| `call_edges` | 1,293,022 | all 6 columns identical |

~17.7M rows round-tripped **byte/value-identical** — every column, the id/FK
integers (`address_version_id`, `caller/callee_address_version_id`, the `id` PKs)
preserved verbatim, the BLOB `content_hash` intact, NULL-vs-empty-string
distinguished (the `\N` sentinel), no AUTOINCREMENT renumber.

## Why it's lossless (the mechanism)

The dump is the sole source of the bulk corpus, BUT the derivation it does —
`build_rows` resolves `function_rva → address_version_id` via the `rva_to_av_id`
map built during the `functions/` pass (`import_to_sqlite.py:680-759`) — happens
ONCE at original-build time and is **baked into the stored FK integers**. The DEV
DB rows store the resolved `address_version_id`, not the raw `function_rva`. So a
DB→CSV→DB round-trip only needs to (a) export every column incl. the id/FK
integers + the BLOB, and (b) reinsert verbatim (the original CREATE TABLE sql +
explicit-id INSERT, no AUTOINCREMENT renumber). Both hold → lossless. No dump-only
derivation survives outside the DB tables.

## What this clears

The D38 / soundness-gate gating unknown: **the export CAN be lossless from the DB
alone, no dump.** Phase 1 (the production exporter-over-bulk + the widened
round-trip oracle) builds on a verified premise, not an assumption. The
production exporter (P1.1) must preserve the same properties the probe did:
all columns incl. id/FK + BLOB, NULL≠'', verbatim reinsert.

## Reusable wiring (for P1.1 + the widened oracle P1.4)

The probe's export/reimport recipe is the reference shape for the production
work: `cell_to_csv`/`csv_to_cell` (the `\N` NULL sentinel + `blob:`-hex BLOB
encoding + type-aware decode), the original-CREATE-TABLE-sql + explicit-id INSERT
(no renumber), and the ordered-by-PK streaming full-equality diff. The script is
`probe_bulk_csv_roundtrip.py` beside this file.
