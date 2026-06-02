# maintainer-tool-db-direct

**Intent.** Build the maintainer tool: DB-direct authoring of the Address Library
with CSV auto-export — **the complete six-job tool** (v1). A maintainer manages the
entire reference DB through one function-first GUI: browse/search/filter, view any
entity's full record + all game-version rows, compare versions side-by-side, and
author all six jobs (create entity, re-verify, supersede, deprecate, create version,
plus edit any existing version's full columns). Every mutation validates, writes the
DB, auto-exports the three seed CSVs (byte-identity round-trip), shows a
plain-language field delta, and commits as one atomic transaction — no CSV hand-edit,
git invisible.

Shared spec: [`plan-spec.md`](plan-spec.md). Settled design:
[`data/maintainer-tool/design.md`](../../../data/maintainer-tool/design.md) (TRD,
v1-revised, `2c03145`) +
[`data/maintainer-tool/ui/design.md`](../../../data/maintainer-tool/ui/design.md) +
[`ui/screens/`](../../../data/maintainer-tool/ui/screens/) (the UI design layer).

## Status ledger (phase-grain)

| Step | Status | Commit |
|---|---|---|
| Phase 1 — data-core (exporter + round-trip + editor shapes UPDATE/INSERT/lifecycle + field-delta) | NOT STARTED | — |
| Phase 2 — GUI spine (shell + navigator + detail + field editor + save-confirm + commit) | NOT STARTED | — |
| Phase 3 — full jobs (DLL-link + create entity/version + supersede/deprecate + compare) | NOT STARTED | — |
| Phase 4 — distribution (PyInstaller single-`.exe`) | NOT STARTED | — |

The per-step ledgers live in each `phase-NN-*/README.md`. A top row flips to
`DONE` only when every step in the phase is `DONE`.

## Phases

- **[Phase 1 — data-core](phase-01-data-core/README.md)** — the headless, Qt-free
  authoring logic in `seeds_shared/`: `csv_exporter.py` (DB→3 CSVs, diff-preserved),
  the bidirectional byte-identity round-trip oracle, `db_editor.py` (the validated
  atomic version-row UPDATE, the INSERT shapes for new version/entity, the lifecycle
  UPDATE for supersede/deprecate), and the field-delta computation. Each step ships a
  `data/refdata-extractor/tests/test_*.py` oracle; the whole authoring path is proven
  with zero Qt before any GUI exists.
- **[Phase 2 — GUI spine](phase-02-gui-spine/README.md)** — the PySide6 read/edit/save
  backbone over the proven data-core: the app shell + token layer, the entity
  navigator (s01), the entity detail (s02 read), the field editor (s04), and the
  save-confirm + atomic commit (s06). End state: re-verify/edit an existing version
  end-to-end works.
- **[Phase 3 — full jobs](phase-03-full-jobs/README.md)** — the capabilities built on
  the spine: the DLL-link verification context (s07), create new version (Job 6) +
  create new entity (Job 1) (s05), lifecycle editing supersede/deprecate (Jobs 4/5)
  on s02, and version history + side-by-side compare (s03).
- **[Phase 4 — distribution](phase-04-distribution/README.md)** — the PyInstaller
  single-`.exe` bundle + `<exe-dir>/../seeds/` resolution. Lands last — nothing to
  bundle until the GUI works.
