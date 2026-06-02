# Step 1 — csv_exporter.py (DB → 3 seed CSVs, deterministic + diff-preserved)

**What.** Add `data/refdata-extractor/python/seeds_shared/csv_exporter.py` — a
headless, Qt-free function that reads the curated tables from a reference DB
(`module`, `address_names`, `address_versions`) and writes the three seed CSVs
(`module_seed.csv`, `address_names_seed.csv`, `address_versions_seed.csv`)
deterministically and diff-preserved: row order stable, `#`-comment lines
preserved verbatim in position, `QUOTE_MINIMAL` per cell, trailing-newline
convention preserved (design §4, §5; R11 diff-preservation). The exporter is the
DB→CSV half of the round-trip; it invents no column the CSV schema does not carry
and emits every authored column the DB holds (DB↔CSV information-equivalence,
DE4). The importer (`import_to_sqlite.py`) reuses it for any DB→CSV need.

**Scope.** One new module in `seeds_shared/`. Reads via `schema.py`'s column
projection + `dict_codec.py` for dict-encoded columns (decode back to the CSV's
text form). No DB write, no GUI, no Qt. Reuses the existing `schema.py` /
`dict_codec.py`; does not duplicate column knowledge.

**Test bar.** `data/refdata-extractor/tests/test_csv_exporter.py` (new, joins the
existing oracle tree). Asserts against the mini-dump fixture
(`tests/fixtures/mini-dump/`): export the curated DB → the three CSVs match the
committed seed CSVs cell-for-cell (diff-preserved — same row order, comments,
quoting, trailing newline). Runs headless; no Qt.

**Dependencies.** None within this plan — first step. Rests only on the existing
`seeds_shared/{schema,dict_codec}.py` + the mini-dump fixture (both present).

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§4 (round-trip contract — the diff-preservation half) + §5 (`csv_exporter.py`
responsibility: "given a DB, produce the three seed CSVs deterministically,
preserving the diff"). Column shape + format: `data/seeds/policy.md`
§"File-format details" + §"Required columns".

**Disassembler-test / author-burden.** N/A — no author-facing game-function
input; this step reads DB columns and writes CSV cells.
