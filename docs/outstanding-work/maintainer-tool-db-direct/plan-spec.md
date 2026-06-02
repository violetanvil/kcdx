# plan-spec — maintainer-tool-db-direct

The shared spec every step in this plan leans on. Steps cross-link here rather
than restating context.

## Goal

Build the maintainer tool: DB-direct authoring of the Address Library with CSV
auto-export — **the complete six-job tool**. A maintainer manages the entire
reference DB through one function-first GUI: browse/search/filter the curated set,
view any entity's full record and all its game-version rows, compare versions
side-by-side, and author any of the six jobs (create entity, re-verify, supersede,
deprecate, create version, plus correcting any existing version's full columns).
Every mutation: validate → write DB → auto-export the 3 seed CSVs (byte-identity
round-trip) → show a plain-language field delta → commit as one atomic transaction
— no hand-edit of any CSV, git invisible to the maintainer.

## The settled design — the authority every step builds to

Two design artifacts, both settled:

- **[`data/maintainer-tool/design.md`](../../../data/maintainer-tool/design.md)**
  (v1-revised, committed `2c03145`) — the TRD: WHAT the tool does. §6 (US-1…US-10),
  §9 (scope), §10 (D1–D12).
- **[`data/maintainer-tool/ui/design.md`](../../../data/maintainer-tool/ui/design.md)**
  + **[`ui/screens/`](../../../data/maintainer-tool/ui/screens/)** — the UI design
  layer: what it looks like + how it behaves (window skeleton, 9 interaction laws,
  token system, 7 screen specs s01–s07).

Every step that builds a surface dereferences to the named §section / screen spec —
the step doc is a pointer, not a replacement (`.claude/rules/spec-conformance.md`).
Supporting authority: `data/seeds/policy.md` (the column-level invariants the shared
validator enforces), `requirements.md` R2–R5/R7–R12.

### Settled decisions (verbatim, from design.md §10)

- **D1 — Source of truth:** DB authoritative; CSVs auto-exported as the git-tracked
  diff layer (derived, never hand-edited).
- **D2 — Round-trip contract:** bidirectional byte-identity —
  `import(export(DB)) == DB` AND `export(import(CSVs)) == CSVs` (diff-preserved).
- **D3 — Sequencing:** DB-direct from day one; no CSV-editor surface ever built.
- **D4 — Data-layer seam:** headless data-core in `seeds_shared/`
  (`csv_exporter` + `db_editor`); the PySide6 GUI is a thin shell over it.
- **D5 — Save/commit UX:** validate → write DB → auto-export → round-trip → confirm
  → commit.
- **D6 — Commit boundary:** the tool commits on Confirm — exact-path staging (only
  the DB + the 3 CSVs, never `-A`/`.`/`-u`), respect a live `index.lock`
  (block-and-retry, never reap), self-authored message. One atomic commit per save.
- **D7 — v1 scope:** v1 is the COMPLETE six-job tool (Jobs 1/2/4/5/6 +
  edit-any-version + compare) — built in steps, nothing deferred.
- **D8 — Confirm surface:** a plain-language field delta (`field: old → new`, only
  changed fields) is the human's acceptance signal; the literal CSV diff is
  oracle-verified + lands in the commit but is not shown to the maintainer.
- **D9 — DLL link / verification:** advisory, never required. Any action proceeds
  unlinked with a "can't verify" warning; a resolver failure or the unlinked state
  is overridable by an explicit "I accept — save anyway" (the maintainer is final
  authority over a tool error).
- **D10 — Default row (unlinked):** newest authored row (highest
  `valid_from_version`) is default-selected when no DLL is linked.
- **D11 — New-row approval (AP18):** creating a new entity (Job 1) or new version
  (Job 6) is approval-gated in the confirm step; an UPDATE is not gated.
- **D12 — New-version "nothing changed":** saving a new version identical to its
  source is blocked with steering copy routing the maintainer to re-verify the
  existing row instead of creating a duplicate.

## Cross-step invariants

- **The data-core is headless + Qt-free** (`.claude/rules/headless-testable.md`,
  design §5). All authoring logic (`csv_exporter`, `db_editor`, the round-trip
  check, the field-delta computation) is exercisable with zero Qt; the GUI calls
  down, never the reverse. Every Phase-1 step ships a
  `data/refdata-extractor/tests/test_*.py` oracle (the existing convention:
  `test_rebuild_oracle.py`, `test_apply_*.py`, `test_version_resolver.py`, run with
  the mini-dump fixture at `tests/fixtures/mini-dump/`).
- **DB↔CSV information-equivalence** (design §4): no field lives only in the DB or
  only in the CSV. The exporter invents no column; the importer drops none.
- **The shared validator (R3) is the single gate** — every invariant runs through
  `seeds_shared/validators.py`; the GUI and the importer both consume it; no rule is
  reimplemented in the tool (UI design.md law 6). A DB write does not begin until the
  validator accepts the prospective post-action state.
- **`policy.md` column invariants bind on the DB-write path** — AP18 new-entity/
  version approval (D11), the audit trio (all-set-or-all-null), `evidence_kind` /
  `kind` enums, `verified_date` shape, the read-only triple (`kcdx_id` / `name` /
  `valid_from_version`), supersession/deprecation pair-integrity + acyclicity,
  `(kcdx_id, valid_from_version)` tuple-uniqueness, append-only id-assignment.
- **The 9 interaction laws bind on every GUI step**
  (`data/maintainer-tool/ui/design.md` §"Global interaction laws") — layout
  stability (no element jumps, law 1), two-pane persistence (law 2), user-driven
  navigation (law 3), advisory verification (law 4), atomic confirmed transaction
  (law 5), single-validator gate (law 6), read-only identity (law 7), approval-gated
  new rows (law 8), no raw values at a call site (law 9). Each GUI step cites the
  laws it obeys.
- **Incremental order** (`.claude/rules/incremental-delivery.md`): the data-core
  lands + is oracle-tested before any GUI exists; the GUI spine (read/edit/save an
  existing version) lands before the jobs built on it (create/compare/lifecycle);
  each step rests on a proven lower step. No step builds a surface whose dependency
  is not yet built.

## Reuse — what already exists (do not rebuild)

- `seeds_shared/`: `schema.py`, `validators.py`, `row_builder.py`,
  `dict_codec.py`, `version_resolver.py` (the `.rdata` scan + intern-agreement —
  R12; the GUI CONSUMES it for the DLL-link, does not rebuild it).
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
| DE-A — `csv_exporter.py` (DB→3 CSVs, deterministic, diff-preserved) | P1 step 1 | design §5; enforces DB↔CSV equivalence |
| DE-B — bidirectional byte-identity round-trip oracle | P1 step 2 | design §4; re-asserted in every GUI save |
| DE-C — `db_editor.py` version-row UPDATE (audit-trio + full-column; Job 2 / US-5) | P1 step 3 | design §5, §6 US-3/US-4/US-5 |
| DE-D — `db_editor.py` INSERT shapes (new version Job 6 + new entity Job 1) | P1 step 4 | design §6 US-6/US-7; `policy.md` id-assignment + tuple-uniqueness + AP18 |
| DE-E — `db_editor.py` lifecycle UPDATE (supersede/deprecate Jobs 4/5) | P1 step 5 | design §6 US-8; `policy.md` pair-integrity + acyclicity |
| DE-F — field-delta computation (`field: old → new`, D8) | P1 step 6 | design §10 D8; headless, feeds s06 |
| DE-G — `.rdata` version resolver + intern-agreement (R12) | P3 step 12 (consumes) | resolver already built; the DLL-link binds it |
| DE-H — s01 navigator (search + status/kind filters + list + chips) | P2 step 8 | `ui/screens/s01-navigator.md` |
| DE-I — s02 entity detail (header read-only + version table) | P2 step 9 (read) + P3 step 15 (lifecycle edit) | `ui/screens/s02-entity-detail.md` |
| DE-J — s03 version history + side-by-side compare | P3 step 16 | `ui/screens/s03-version-history-compare.md` |
| DE-K — s04 field editor (view/edit full row, dirty markers, validation) | P2 step 10 | `ui/screens/s04-field-editor.md` |
| DE-L — s05 create (new entity Job 1 + new version Job 6) | P3 step 13 (version) + step 14 (entity) | `ui/screens/s05-create.md` |
| DE-M — s06 save-confirm (field-delta + approval + override) | P2 step 11 | `ui/screens/s06-save-confirm.md` |
| DE-N — s07 status bar + DLL-link (verification context) | P3 step 12 | `ui/screens/s07-status-dll-link.md` |
| DE-O — atomic save→commit transaction (one commit/save, D6, law 5) | P2 step 11 | design §8, `.claude/rules/concurrency-git.md` |
| DE-P — UX states (empty/loading/val-err/write-fail/unlinked/resolver/compare-edge) | distributed: empty+loading→step7, val-err→step10, write-fail→step11, unlinked/resolver→step12, compare-edge→step16 | design §7, each screen spec |
| DE-Q — the 9 interaction laws | binding on every GUI step (P2–P3); token layer → step 7 | `ui/design.md` §"Global interaction laws" |
| DE-R — PyInstaller single-`.exe` + `<exe-dir>/../seeds/` resolution (R9) | P4 step 17 | design §8 |
| privacy carve-out (R10) | OUT-OF-SCOPE | already done — `data/maintainer-tool/` in `$PrivateSubpaths` |
| Job 3 — new-game-version campaign (bulk delta report) | OUT-OF-SCOPE (design §9) | a batch workflow over v1's per-entity primitives; not in v1 |
| driven evidence flows (R5) | OUT-OF-SCOPE (design §9) | `pattern_scan` / `live_test_plugin` automation; values authorable, automation not built |
| multi-file rename journal (R11) | OUT-OF-SCOPE (design §9) | reserved, not built until the atomic-rename window bites |
