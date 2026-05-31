# importer-no-prose-derivation

**Intent.** Make the seed CSV the single, versioned source of every field the
refdata importer needs — eliminate all prose-derivation, regex-scraping, and
inference from `import_to_sqlite.py` + `seeds_shared/`. Each address-kind's datum
becomes its own explicit authored column; `notes` is human commentary only.

Shared spec: [`context.md`](context.md).

## Status ledger (phase-grain)

| Step | Status | Commit |
|---|---|---|
| Phase 1 — audit + column design | DONE | (landed) |
| Phase 2 — schema: explicit per-kind columns | DONE | (landed) |
| Phase 3 — author values + rewire readers + delete prose machinery | NOT STARTED | — |

The per-step ledgers live in each `phase-NN-*/README.md`. This top row flips to
`DONE` only when every step in the phase is `DONE`.

## Phases

- **[Phase 1 — audit + column design](phase-01-audit/README.md)** — exhaustive
  value-provenance audit: enumerate EVERY value the importer writes, prove each
  comes from an authored column or a legitimate non-prose source, and produce the
  per-kind column plan. No code change — the proof step that guarantees nothing is
  missed.
- **[Phase 2 — schema: explicit per-kind columns](phase-02-schema/README.md)** —
  add the explicit authored per-kind columns to the versions seed + schema,
  collapse the `value`/`offset`/`vtable_slot` sprawl, validators enforce each.
  Columns exist (still NULL); no reader rewiring yet.
- **[Phase 3 — author + rewire + delete prose](phase-03-author-rewire/README.md)**
  — hand-author the 6 vtable_index slots + any data_slot offsets into the new
  columns (user verifies), rewire both writers to read the authored columns,
  delete `kind_offset_and_slot()` + dead `infer_kind()`, re-capture the oracle
  baseline.
