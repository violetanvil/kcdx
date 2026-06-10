# 1.4 [CORE] Widen the round-trip oracle over the bulk (the completeness bar)

## What

Widen the existing curated round-trip oracle (`csv_integrity` / `round_trip.py` — today scoped to the
curated ~157 rows, explicitly excluding the bulk DEV tables) to cover the FULL corpus: rebuild-from-CSV
→ DB → re-export → byte-identical for BOTH the curated half AND the bulk half (`statements`/
`referenced_vars`/`call_edges` + the `kcdx_id`-NULL rows). This is D38's stated completeness bar made
a standing gate — the durable proof that the export captures all the data necessary and the
dump-retirement is safe. After this is green, the dump is genuinely retired as a routine input.

## Scope

One commit extending `data/maintainer-tool/backend/app/csv_integrity.py` (+ `round_trip.py` /the
oracle): the round-trip compare's scope widens from the curated authored surface to include the bulk
DEV tables (drop the existing "DEV-only bulk tables the export does not touch" exclusion now that 1.1
exports them); the import half reads the tracked CSVs (no dump, per 1.3). Does NOT change the exporter
(1.1) or run_rebuild (1.3) — it asserts their round-trip.

## Test bar

Data-core pytest: the widened oracle runs rebuild-from-CSV → DB → re-export over the full corpus and
asserts byte-identical for both halves (curated unchanged + the bulk now covered). A divergence in
ANY bulk table/column fails. Emits the canonical `ACCEPT-RESULT`/`ACCEPT-SUITE`. FALSIFIABLE: a
widened oracle that passes while a bulk column does NOT round-trip (e.g. the exporter drops a
`statements` field the rebuild can't reconstruct) fails the row — the oracle asserts full-corpus
byte-identity, not just the curated subset. This is the completeness bar; build-green elsewhere is
necessary but THIS is the proof the export is lossless (`.claude/rules/skeptical-expert.md`). Per
`.claude/rules/test-discipline.md`.

## Dependencies

- **1.1** — the exporter writes the bulk (the oracle round-trips it).
- **1.3** — `run_rebuild` reads the CSVs as genesis (the oracle's import half).

## Reference

[`../plan-spec.md`](../plan-spec.md) — cross-step invariants 3 (bulk-rebuild completeness is a
VERIFIED outcome — this oracle is the standing proof) + 4 (the round-trip byte-identity bar).

## Design authority

`data/maintainer-tool/design.md` **D38** §"the completeness bar is the existing curated round-trip
oracle **widened over the bulk**: rebuild-from-CSV → DB → re-export → byte-identical for both the
curated and bulk halves." Build the oracle to exactly that bar.

## Disassembler-test / author-burden

None — a round-trip verification widening; no author-facing input, no game-function target, no AP18
addition.
