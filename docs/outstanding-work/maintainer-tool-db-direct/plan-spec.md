# plan-spec — maintainer-tool-db-direct

The shared spec every step in this plan leans on. Steps cross-link here rather
than restating context.

## Goal

Build the maintainer tool: DB-direct authoring of the Address Library with CSV
auto-export. The MVP is **Job 2 — re-verify one curated entity at the current
game version** end-to-end: edit the reference DB directly, validate, auto-export
the three seed CSVs (byte-identity round-trip), show the maintainer the CSV diff,
commit on confirm — with no hand-edit of any CSV.

## The settled design — the authority every step builds to

The design artifact is **[`data/maintainer-tool/design.md`](../../../data/maintainer-tool/design.md)**
(v1, committed `4300027`). Every step that builds a surface dereferences to the
named §section there — the step doc is a pointer, not a replacement
(`.claude/rules/spec-conformance.md`). Supporting authority:
`data/seeds/policy.md` (the column-level invariants the shared validator
enforces), `requirements.md` R2–R5/R7–R12.

### Settled decisions (verbatim, from design.md §10)

- **D1 — Source of truth:** DB authoritative; CSVs auto-exported as the
  git-tracked diff layer (derived, never hand-edited).
- **D2 — Round-trip contract:** bidirectional byte-identity —
  `import(export(DB)) == DB` AND `export(import(CSVs)) == CSVs` (diff-preserved).
- **D3 — Sequencing:** MVP is DB-direct from day one (Job 2 on the DB). No
  CSV-editor surface ever built.
- **D4 — Data-layer seam:** headless data-core in `seeds_shared/`
  (`csv_exporter` + `db_editor`); the PySide6 GUI is a thin shell over it.
- **D5 — Save/commit UX:** validate → write DB → auto-export → round-trip → show
  the CSV diff for confirm/revert. The exported diff IS the acceptance signal.
- **D6 — Commit boundary:** the tool commits on Confirm — exact-path staging
  (only the DB + the 3 CSVs, never `-A`/`.`/`-u`), respect a live `index.lock`
  (block-and-retry, never reap), self-authored message. The existing committer
  discipline (`.claude/rules/concurrency-git.md`) applied to the tool.

## Cross-step invariants

- **The data-core is headless + Qt-free** (`.claude/rules/headless-testable.md`,
  design §5). All authoring logic (`csv_exporter`, `db_editor`, the round-trip
  check) is exercisable with zero Qt; the GUI calls down, never the reverse. Every
  Phase-1 step ships a `data/refdata-extractor/tests/test_*.py` oracle (the
  existing test convention: `test_rebuild_oracle.py`, `test_apply_*.py`,
  `test_version_resolver.py`, run with the mini-dump fixture at
  `tests/fixtures/mini-dump/`).
- **DB↔CSV information-equivalence** (design §4): no field lives only in the DB or
  only in the CSV. The exporter invents no column; the importer drops none. A
  derived/cache column the CSV does not mirror is forbidden on the authored
  surface.
- **The shared validator (R3) is the single gate** — every invariant runs through
  `seeds_shared/validators.py`; the GUI and the importer both consume it; no rule
  is reimplemented in the tool. A DB write does not begin until the validator
  accepts the prospective post-action state.
- **`policy.md` column invariants bind on the DB-write path** — AP18 new-entity
  approval, the audit trio (all-set-or-all-null), `evidence_kind` enum,
  `verified_date` shape, the read-only triple (`kcdx_id` / `name` /
  `valid_from_version`).
- **Incremental order** (`.claude/rules/incremental-delivery.md`): the data-core
  lands + is oracle-tested before any GUI exists; each GUI step rests on a proven
  lower step. No step builds a surface whose dependency is not yet built.

## Reuse — what already exists (do not rebuild)

- `seeds_shared/`: `schema.py`, `validators.py`, `row_builder.py`,
  `dict_codec.py`, `version_resolver.py` (the `.rdata` scan + intern-agreement —
  R12; the MVP CONSUMES it, does not rebuild it).
- `import_to_sqlite.py`: `--rebuild` + the incremental `apply` path (Phase-1
  db-updator, done). The exporter reuses `import_to_sqlite.py`'s read side for the
  round-trip oracle.
- `tests/`: the mini-dump fixture + `oracle_baseline.json` + the existing
  `test_*.py` oracles — the new oracles join this tree.
- Privacy: `data/maintainer-tool/` is already in `publish-public.ps1`
  `$PrivateSubpaths` (R10 — done, out of scope here).

## Coverage map — every design element → its step (or deferral)

| Design element | Covered by | Notes |
|---|---|---|
| DE1 — `csv_exporter.py` (DB→3 CSVs, deterministic, diff-preserved) | P1 step 1 | design §5; enforces DB↔CSV equivalence (DE4) |
| DE2 — `db_editor.py` (validated atomic audit-trio UPDATE) | P1 step 3 | design §5, §6 US-4 |
| DE3 — bidirectional byte-identity round-trip oracle | P1 step 2 | design §4; re-asserted in the GUI save path (P2 step 7) |
| DE4 — DB↔CSV information-equivalence | P1 step 1 (exporter enforces) + step 2 (round-trip proves) | design §4 |
| DE5 — `.rdata` version resolver + interval current-row filter + degraded mode | P2 step 5 | CONSUMES existing `version_resolver.py`; resolver already built (R12) |
| DE6 — GUI load curated set (US-1) | P2 step 4 | design §6 US-1, §7 |
| DE7 — browse/pick + current-row-first/full-history + read-only triple | P2 step 5 | design §6 US-2, §7, R8 |
| DE8 — audit-trio edit + inline validation (US-3) | P2 step 6 | design §6 US-3, §7 |
| DE9 — save chain → diff-confirm (US-4, D5) | P2 step 7 | design §6 US-4, §7 |
| DE10 — commit-on-Confirm, concurrency-safe (D6) | P2 step 8 | design §8, `.claude/rules/concurrency-git.md` |
| DE11 — UX states (empty/loading/val-err/write-fail/diff-confirm/commit-result/edge) | distributed: empty+loading→step4, val-err→step6, write-fail+diff-confirm→step7, commit-result→step8, edge(multi-row history)→step5 | design §7 |
| DE12 — PyInstaller single-`.exe` (R9) | P3 step 9 | design §8 |
| DE13 — privacy carve-out (R10) | OUT-OF-SCOPE | already done — `data/maintainer-tool/` in `$PrivateSubpaths` |
| DE14 — seed resolution `<exe-dir>/../seeds/` (R9) | P3 step 9 | design §8 |
| Jobs 1/3/4/5/6 (R7) | DEFERRED | post-MVP phases per R7 order; design §9 |
| driven evidence flows (R5) | DEFERRED | post-MVP per R7; design §9 |
| new-game-version campaign UI | DEFERRED | post-MVP per R7; design §9 |
| multi-file rename journal (R11) | DEFERRED | reserved, not built until it bites; design §9 |
