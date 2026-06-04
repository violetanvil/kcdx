# Step 3 — exporter + round-trip carry the folded columns

**What.** The exporter emits and reads the folded `address_versions` columns to/from the seed
CSV, and the round-trip oracle asserts byte-identity with them present. The folded survival data
now lives in the av-row's seed columns (the seed already carried `survival_*` columns from the
step-3c/5.2 work — confirm whether they stay `survival_`-prefixed in the CSV or merge into the av
column set; the CSV column naming is the export contract). After this step the full
`export(DB)==CSV` and `import(CSV)==DB` round-trip holds with the folded columns.

**Scope.** `csv_exporter.py` — export the folded av columns to the `address_versions_seed.csv`
(QUOTE_MINIMAL, diff-preserved, the canonical export form); read them back on import. `round_trip.py`
— the bidirectional byte-identity assertion covers the folded columns. Confirm the seed CSV column
header for the folded columns (the seed's existing `survival_aob`/`survival_rule`/etc. columns are
the natural home — they map to the av columns now; decide whether the CSV keeps the `survival_`
prefix or renames, and keep it consistent with the importer's read in step 2). One commit.

**Test bar.** `tests/test_round_trip.py` green with the folded columns present (both directions
byte-identical). `tests/test_csv_exporter.py` green (the export is byte-identical to the committed
seed, folded columns included). Runnable now — the columns are populated (step 2); this proves
they survive the CSV round-trip.

**Dependencies.** Step 2 (the folded columns are populated on the av row, so there is data to
export + round-trip).

**Reference.** [`../plan-spec.md`](../plan-spec.md) §"Cross-step invariants" (D2 round-trip
byte-identity).

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§4 (the round-trip contract — D2 bidirectional byte-identity) + §11.2 (the folded columns).

**Disassembler-test / author-burden.** N/A — CSV export/import of already-authored columns.
