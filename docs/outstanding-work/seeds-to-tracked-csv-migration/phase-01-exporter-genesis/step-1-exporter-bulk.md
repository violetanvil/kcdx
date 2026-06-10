# 1.1 [CORE] Extend the exporter to capture the bulk half losslessly → data/db-export-bulk/

## What

Extend `csv_exporter` to export the DEV DB's **bulk half** — the DEV-only tables `statements`,
`referenced_vars`, `call_edges`, plus the `kcdx_id`-NULL bulk `address_versions` discovery rows — to
a new `data/db-export-bulk/` location, alongside the existing curated `data/db-export/` export. Today
the exporter filters to the curated subset (`WHERE kcdx_id IS NOT NULL`); this adds the complementary
bulk projection so the full corpus is captured. The export must be LOSSLESS (every bulk row, every
column the rebuild needs) — the P0 probe confirmed the DB tables hold the full bulk; this step makes
the production exporter write it.

## Scope

One commit in `data/refdata-extractor/python/seeds_shared/csv_exporter.py` (+ any shared schema
constant naming the bulk tables/columns): the bulk export pass (the three DEV-only tables + the
`kcdx_id`-NULL `address_versions` rows → CSV under `data/db-export-bulk/`), reusing the existing
dict-decode / row-encode logic. The curated `data/db-export/` export is unchanged. Does NOT touch
`run_rebuild` (1.3) or the round-trip oracle (1.4) or Git LFS (1.2) — this step only produces the
bulk CSVs.

## Test bar

Data-core pytest (`data/refdata-extractor/tests/`): exporting a known DEV DB produces the bulk CSVs
with (1) every bulk table present (`statements`/`referenced_vars`/`call_edges`); (2) row counts
matching the DEV DB's bulk row counts (the ~321k `kcdx_id`-NULL `address_versions` + the
multi-million `statements`); (3) every column captured (a per-column completeness assert — a dropped
column is a contract-drift FAIL). Emits the canonical `ACCEPT-RESULT`/`ACCEPT-SUITE` to the
DB-pipeline test sink (`.claude/rules/acceptance-signal.md`). FALSIFIABLE: a bulk CSV missing a
table, short on rows, or dropping a column fails the row. Per `.claude/rules/test-discipline.md`.

## Dependencies

- **Phase 0 (0.1)** — the probe proved the bulk round-trips CSV-losslessly (the DB tables hold the
  full bulk). This step builds the production exporter to that proven completeness.

## Reference

[`../plan-spec.md`](../plan-spec.md) — the coverage map (the exporter-over-bulk element) + cross-step
invariant 4 (a layer round-trips byte-identical).

## Design authority

`data/maintainer-tool/design.md` **D38** §"the BULK half (the `kcdx_id`-NULL discovery rows + the
DEV-only `statements`/`referenced_vars`/`call_edges`) → a CSV bundle... at `data/db-export-bulk/`" +
the curated/bulk seam (`kcdx_id IS NOT NULL`). Build the bulk projection to D38's seam + the
lossless bar. The CSV column shape mirrors the DEV DB table schemas (`schema.py`).

## Disassembler-test / author-burden

None — a data-export extension; no author-facing input, no game-function target, no AP18 addition
(it exports existing DEV rows, authors nothing).
