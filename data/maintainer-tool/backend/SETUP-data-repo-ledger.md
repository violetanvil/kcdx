# Maintainer-tool dedicated data repo — setup ledger

**Goal.** Provision the maintainer-tool's own git repo for the derived Address-Library
record, point the backend's confirm checkout at it (env `KCDX_CHECKOUT`), so a save
commits the `data/db-export/*.csv` to THAT repo — never the kcdx tree. Settles defect (b)
from the create-entity save-failure investigation; the user chose "dedicated data repo".

**Decision (user-settled, 2026-06-04).** The maintainer-tool data is its own repo, not the
kcdx tree. The DB is the originator (gitignored, the data-core amends it locally); only the
derived `db-export/*.csv` is the git-tracked record. FIX A (`41b0438`) already stopped the
confirm from staging the gitignored DB; this ledger relocates the CHECKOUT so the tracked
record lands in a dedicated repo.

**Checkout layout the confirm requires (from `config.py`).** Relative to the checkout root:
- `data/seeds/` — the 3 frozen bootstrap CSVs (read by the load endpoint; the DB's genesis).
- `data/reference.sqlite` + `data/reference-dev.sqlite` — the two DBs the data-core amends
  USER-first-then-DEV on every save (BOTH required; `reference-dev.sqlite` is ~1.26 GB).
  Gitignored — the originator, never committed.
- `data/db-export/` — the derived CSV record the confirm exports + `git add`s + commits.

**Repo location.** `<kcdx parent>/kcdx-maintainer-data/` — a sibling of the kcdx tree,
fully outside it (so nothing lands in kcdx history).

## Ledger

| Step | What | Status | Notes |
|---|---|---|---|
| 1 | Create the data repo dir + `data/{seeds,db-export}/` layout; copy the 3 seed CSVs + both reference DBs | DONE | seeds tiny; dev DB ~1.26 GB (one-time copy) |
| 2 | `git init` + `.gitignore` (ignore `data/*.sqlite`) + MIT LICENSE + README; commit the seeds + db-export placeholder | DONE | DBs gitignored; only seeds + db-export tracked |
| 3 | Point the backend at the new checkout via `KCDX_CHECKOUT`; restart the backend; verify `/health` resolves against it | DONE | health `state:resolved`, checkout_source `env` |
| 4 | Live-probe a real save through the running backend; confirm the db-export CSVs commit to the DATA repo, kcdx tree untouched | DONE | save→commit lands in data repo; kcdx `git status` shows no data/db-export change |
| 5 | Reset the stale `AD data/db-export/*.csv` index leftover in the kcdx tree (never-committed, from the pre-fix failed saves) | DONE | kcdx index clean of the maintainer-tool's db-export |

## Verification gate

A create-entity save from the browser commits to `kcdx-maintainer-data`, the kcdx tree's
`git status` shows zero `data/db-export/` change, and `/health` reports the data repo as the
resolved checkout. Then re-run the full Phase-4 create/save acceptance the user was blocked on.

**Headless verification (DONE).** A live `edit-notes` save (kcdx_id=1, the smallest non-AP18
UPDATE) through the running backend returned `status:saved` and committed to the data repo
(`519c2ba`); the kcdx tree showed no `data/db-export/` change. Both defects resolved: the DB is
no longer git-staged (FIX A `41b0438`), and the derived record lands in `kcdx-maintainer-data`,
not kcdx. The probe left one artifact — a test note on `lua_pcall` in the data repo's DB +
`519c2ba` — harmless (a notes-field edit, overwritten by any real edit); flag to the user.

**Backend run command (for restarts).** `KCDX_CHECKOUT=<kcdx-maintainer-data>` then
`python -m uvicorn app.main:app --port 8000 --host 127.0.0.1` from the backend dir. The env var
is what points the confirm checkout at the data repo (vs the dev-default kcdx-tree-root).
