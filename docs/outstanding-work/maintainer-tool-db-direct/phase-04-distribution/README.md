# Phase 4 — distribution

**Intent.** Package the working tool as a single self-contained Windows `.exe`
(PyInstaller bundles Python + PySide6 + the data-core), living in
`data/maintainer-tool/`, resolving the seeds via `<exe-dir>/../seeds/` (R9). Lands
last — nothing to bundle until the full GUI works (Phases 2–3).

Shared spec: [`../plan-spec.md`](../plan-spec.md). Design:
[`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md) §8.

## Step ledger

| Step | Status | Commit |
|---|---|---|
| 17 PyInstaller single-`.exe` + `<exe-dir>/../seeds/` resolution (R9) | NOT STARTED | — |

## Step docs

17. [step-17-pyinstaller-exe.md](step-17-pyinstaller-exe.md)

## Verification gate (phase end)

- A PyInstaller build config produces a single `.exe` in `data/maintainer-tool/`
  bundling Python + PySide6 + the data-core; no Python install / `pip` / venv
  needed to run it.
- The `.exe` resolves its seeds via `<exe-dir>/../seeds/` (relative to its own
  location — not a hard-coded absolute path, not `%APPDATA%`, not a first-launch
  prompt).
- **User-facing acceptance:** the maintainer runs the `.exe` (dropped into
  `data/maintainer-tool/`) and it launches + finds the seeds + runs the full
  six-job flow — the same flows Phases 2–3 verified, now from the bundled binary.
- The `.exe` is gitignored (`*.exe`); it is a release artifact, not tracked
  (R9) — the build config + the resolution logic are what land in the repo.
