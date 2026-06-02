# Step 9 — PyInstaller single-.exe + seed resolution (R9)

**What.** Package the working tool as a single self-contained Windows `.exe` via
PyInstaller (bundles the Python interpreter + PySide6/Qt6 + the `seeds_shared/`
data-core), placed in `data/maintainer-tool/`. Implement the seed-resolution
convention (R9): the running `.exe` resolves its seed CSV paths relative to its own
executable location — `<exe-dir>/../seeds/` — not a hard-coded absolute path, not
`%APPDATA%`, not a first-launch prompt. The maintainer downloads the `.exe`, drops
it into `data/maintainer-tool/`, runs it; the tool finds the seeds.

**Scope.** The PyInstaller build config/spec + the `<exe-dir>/../seeds/` resolution
logic (replacing the dev path step 4 used) + the sidecar-cache path resolution
(`data/maintainer-tool/.maintainer-tool-cache.json` next to the `.exe`, R12). The
`.exe` itself is gitignored (`*.exe` — a release artifact, R9); the build config +
the resolution code land in the repo. No new GUI behavior — packaging + path
resolution only.

**Test bar.** The seed-resolution logic (`<exe-dir>/../seeds/` + the sidecar-cache
path) is headless — a `tests/test_*.py` asserts the resolver computes the right
paths from a given exe-dir (no Qt, no actual PyInstaller run needed for the path
logic). The PyInstaller bundle producing a launchable `.exe` that finds the seeds
is confirmed at the phase's user-facing acceptance gate (run the built `.exe`).

**Dependencies.** Phase 2 (the working GUI is what gets bundled — nothing to
package until Job 2 runs end-to-end). Sequenced last.

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§8 (R9 distribution: single self-contained `.exe`, PyInstaller, the
`<exe-dir>/../seeds/` convention; R12 the sidecar cache next to the `.exe`,
gitignored). R10 (the dir is private) is already satisfied —
`data/maintainer-tool/` is in `publish-public.ps1` `$PrivateSubpaths`; no
publish-script change owed.

**Disassembler-test / author-burden.** N/A — packaging + path resolution; no
author-facing game-function input.
