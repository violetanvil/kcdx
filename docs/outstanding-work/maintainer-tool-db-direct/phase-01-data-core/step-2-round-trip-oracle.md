# Step 2 — round-trip oracle (bidirectional byte-identity)

**What.** Add the bidirectional byte-identity round-trip oracle that binds the
exporter (step 1) and the existing importer together as the correctness contract
(design §4, D2). It asserts both directions: `import(export(DB)) == DB` (export
the DB to CSVs, re-import them, the resulting DB rows are byte-identical to the
original) AND `export(import(CSVs)) == CSVs` (import the committed seed CSVs,
re-export, the CSVs are byte-identical — diff-preserved). A divergence is a tool
bug, caught here. This is the oracle the GUI save chain (Phase 2 step 7) calls
after every write.

**Scope.** A new test oracle in `data/refdata-extractor/tests/test_round_trip.py`,
plus any thin shared helper in `seeds_shared/` the two directions need (a
`round_trip` entry point that the GUI can also call — design §5 names the
round-trip as a data-core concern, so its callable form lives in `seeds_shared/`,
the test drives it). Reuses `csv_exporter` (step 1) + `import_to_sqlite.py`'s read
side. No GUI, no Qt.

**Test bar.** `test_round_trip.py` (new). On the mini-dump fixture: both
directions byte-identical. The DB-comparison direction compares the curated table
rows (the authored surface), not the bulk dev-only tables. Runs headless.

**Dependencies.** Step 1 (`csv_exporter` — the export half). The import half is
the existing `import_to_sqlite.py`. Both must exist for the round-trip to run —
step 1 lands first, so this step is verifiable when it lands.

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§4 (the round-trip contract, verbatim: `import(export(DB)) == DB` AND
`export(import(CSVs)) == CSVs`) + §10 D2. Mirrors the existing apply==rebuild
oracle discipline (`test_rebuild_oracle.py`).

**Disassembler-test / author-burden.** N/A — no author-facing game-function input.
