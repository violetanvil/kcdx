# maintainer-tool-db-direct

**Intent.** Build the maintainer tool: DB-direct authoring of the Address Library
with CSV auto-export. MVP = Job 2 (re-verify one curated entity at the current
game version) end-to-end — edit the reference DB directly, validate, auto-export
the three seed CSVs (byte-identity round-trip), show the CSV diff, commit on
confirm. No CSV hand-edit at any point.

Shared spec: [`plan-spec.md`](plan-spec.md). Settled design:
[`data/maintainer-tool/design.md`](../../../data/maintainer-tool/design.md) (v1,
`4300027`).

## Status ledger (phase-grain)

| Step | Status | Commit |
|---|---|---|
| Phase 1 — data-core (csv_exporter + round-trip oracle + db_editor) | NOT STARTED | — |
| Phase 2 — GUI shell (PySide6, over the proven core) | NOT STARTED | — |
| Phase 3 — distribution (PyInstaller single-.exe) | NOT STARTED | — |

The per-step ledgers live in each `phase-NN-*/README.md`. A top row flips to
`DONE` only when every step in the phase is `DONE`.

## Phases

- **[Phase 1 — data-core](phase-01-data-core/README.md)** — the headless,
  Qt-free authoring logic in `seeds_shared/`: `csv_exporter.py` (DB→3 CSVs,
  diff-preserved), the bidirectional byte-identity round-trip oracle, and
  `db_editor.py` (the validated atomic audit-trio UPDATE). Each step ships a
  `data/refdata-extractor/tests/test_*.py` oracle; the whole authoring path is
  proven with zero Qt before any GUI exists.
- **[Phase 2 — GUI shell](phase-02-gui-shell/README.md)** — the PySide6 thin
  shell over the proven data-core: load the curated set, browse + pick an entity
  (current-row-first + full-history, read-only triple), edit the audit trio with
  inline validation, the save chain (validate→write→export→round-trip→diff), and
  commit-on-Confirm under the git-concurrency discipline. Every UX state.
- **[Phase 3 — distribution](phase-03-distribution/README.md)** — the PyInstaller
  single-`.exe` bundle + `<exe-dir>/../seeds/` resolution. Nothing to bundle until
  the GUI works, so it lands last.
