# db-updator Phase 1 — the DB updator

**Intent.** Build the incremental `apply` mode for
`data/refdata-extractor/python/import_to_sqlite.py`: land hand-edited seed-CSV
deltas into both reference DBs without a full rebuild, sharing one row-builder
with the rebuild path. Phase 1 of the maintainer-tool flow (Phase 2 = the GUI,
out of scope).

Shared spec: [`context.md`](context.md). Authoritative design:
[`data/maintainer-tool/plan.md`](../../../data/maintainer-tool/plan.md).

## Status ledger (phase-grain)

| Step | Status | Commit |
|---|---|---|
| Phase 1 — the db updator | NOT STARTED | — |

The per-step ledger lives in
[`phase-01-db-updator/README.md`](phase-01-db-updator/README.md). This top-level
row flips to `DONE` only when every step in the phase is `DONE`.

## Phases

- **[Phase 1 — the db updator](phase-01-db-updator/README.md)** — extract the
  shared module, add the `.rdata` resolver, build `apply` across its SQL
  families (re-verify → add/promote → deprecate/supersede). Ends in a state
  where a maintainer hand-edits the seeds and runs `apply` to update both DBs
  incrementally, with `--rebuild` available as the reconciliation oracle.
