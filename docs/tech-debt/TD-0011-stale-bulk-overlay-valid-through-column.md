---
id: TD-0011
opened: 2026-06-11
status: Open
area: data/refdata-extractor reference-data pipeline (the D38 bulk derived overlay)
closure_gate: the next expert-regenerate of the bulk corpus from the full Ghidra dump (the D38 `--rebuild-from-dump` path / `run_rebuild_from_dump`), which re-exports the overlay without `valid_through`
owner: continuous (whoever next runs the expert from-dump rebuild + bulk re-export)
commit_at_filing: 3ef6a207cfae63309d019e103b27e10803b6e6a4
related:
  - TD-0006 (statement layer DEV-only — a different reference-data-pipeline gap; this TD is the bulk-overlay column-staleness left by the D40 valid_through move)
affected_sites:
  - data/db-export-bulk/address_versions_derived.csv  (the committed LFS bulk overlay — still carries a `valid_through` column the new exporter no longer writes)
  - data/refdata-extractor/python/seeds_shared/bulk_exporter.py  (the D40 move: `valid_through` reclassified `_AV_DERIVED_COLS` → `_AV_AUTHORED_COLS`, so `export_bulk` no longer emits it to the overlay)
---

# TD-0011 — stale `valid_through` column in the committed bulk derived overlay (post-D40)

## Context

D40 (the 6.2a-fix step) made `valid_through` an **authored** column: it moved off
the bulk derived overlay (`_AV_DERIVED_COLS`) and onto the curated seed CSV as
`valid_through_version` (`_AV_AUTHORED_COLS`), so an interval edit round-trips via
the human-reviewable authored surface rather than the dump-derived overlay. The
code change (`data/refdata-extractor/python/seeds_shared/bulk_exporter.py`) is
landed: `export_bulk` no longer writes `valid_through` to the overlay payload.

The **committed** bulk overlay artifact — `data/db-export-bulk/address_versions_derived.csv`
(Git LFS) — was exported by the OLD code, so it still carries a `valid_through`
column. The committed overlay is now stale with respect to the new exporter's
column set: it has a column the new code will not write on the next export.

This is a deliberate, user-approved deferral (the bulk-overlay re-export was not
in 6.2a-fix's scope; the concurrency boundary explicitly left
`data/db-export-bulk/` untouched). It is **bucket-2 test/data debt**
(`.claude/rules/test-discipline.md`): the artifact becomes current the moment a
specific named future thing lands (the expert bulk re-export below).

## Why it is safe to defer

The stale overlay feeds NOTHING routine:

- The committed overlay is consumed ONLY by a **from-LFS full rebuild** — the
  expert path (`--rebuild-from-dump` / `run_rebuild_from_dump`, the D38-demoted
  expert-only mode that regenerates the bulk corpus). No routine flow reads the
  committed overlay's `valid_through` column.
- The **per-commit gate** and **every routine data-core test** regenerate their
  OWN bulk overlay via `export_bulk` from a freshly-built DEV DB — they never read
  the committed stale overlay. So nothing routine sees the stale column, and the
  bulk-exporter tests assert the NEW (no-`valid_through`) column set.
- `valid_through` now **round-trips via the authored curated seed**
  (`data/db-export/address_versions_seed.csv`'s `valid_through_version`) — the
  interval value is preserved on the authored surface, not the overlay. Dropping
  the overlay's stale column loses no information.

So the staleness is invisible to every routine consumer and lossless; it surfaces
only on the rare expert from-dump rebuild, which is exactly the path that fixes it.

## Closure blocker

The next **expert-regenerate of the bulk corpus from the full Ghidra dump** — the
D38 `--rebuild-from-dump` path (`run_rebuild_from_dump`) followed by the bulk
re-export (`export_bulk`) — re-exports `data/db-export-bulk/address_versions_derived.csv`
WITHOUT the `valid_through` column (the new `_AV_DERIVED_COLS` set). Committing
that regenerated overlay closes this entry. This is a named source-mechanism (the
expert from-dump rebuild + bulk re-export), not a vague "later".

## Activity log

- **2026-06-11** — Initial filing. D40 (6.2a-fix) moved `valid_through` overlay →
  authored curated seed; the code no longer writes it to the overlay, but the
  committed LFS overlay still carries the old column. Re-export deferred with user
  approval (out of 6.2a-fix's scope; the bulk overlay was explicitly outside the
  step's concurrency boundary).

## What this entry does NOT do

- Does not double as a bug report — the staleness is a deliberate data-artifact
  lag, not a runtime defect; no routine consumer reads the stale column.
- Does not block any current capability — `valid_through` round-trips via the
  authored seed; the per-commit gate + routine tests regenerate their own overlay.
- Closure is appended by whoever runs the expert from-dump rebuild + bulk
  re-export, who then moves this file to `closed/` + reindexes per
  `doc-organization.md` — never at filing.

## Design authority

D40 (`valid_through` → authored, the overlay→curated-seed move) + D38 (the
authored-vs-derived seam + the expert-only from-dump rebuild this overlay belongs
to) + the 6.2a-fix step (`docs/outstanding-work/maintainer-tool-verification-engine/phase-06-report-ingestion/step-2a-fix-valid-through-write-path.md`),
whose approved brief deferred this bulk re-export.
