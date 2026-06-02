# Step 17 — PyInstaller single-`.exe` + `<exe-dir>/../seeds/` resolution (R9)

**What.** Package the maintainer tool as a single self-contained Windows `.exe`
(PyInstaller bundles Python + PySide6 + the data-core `seeds_shared/`), living in
`data/maintainer-tool/`, resolving the seeds via `<exe-dir>/../seeds/` (relative to its
own location — R9). The build config + the seed-path resolution logic land in the repo;
the `.exe` itself is gitignored (a release artifact). This is the LAST step — there is
nothing to bundle until the full six-job GUI works (Phases 2–3).

**Scope.** The PyInstaller spec/build config + the `<exe-dir>/../seeds/` resolution
logic (replacing the dev path the app skeleton used in step 7). No new tool behavior —
it packages what Phases 1–3 built. The `.exe` is gitignored; the config + resolution
are tracked.

**Test bar.** The seed-path resolution (`<exe-dir>/../seeds/` derivation) is a small
testable function (a `tests/test_*.py` asserting the derived path given a known
exe-dir). The bundled `.exe` launching + finding the seeds + running the flows is the
**user-facing acceptance** at the phase gate (the maintainer runs the bundled binary).

**Test bar runnable now?** The path-resolution test runs when this step lands. The
`.exe` acceptance requires Phases 2–3 complete (there is no GUI to bundle before them) —
which is exactly why this step is ordered LAST (`.claude/rules/incremental-delivery.md`:
the dependency, a working GUI, lands before its consumer, the bundle).

**Dependencies.** Phases 1–3 complete (the full tool to bundle). This step packages the
finished tool; it builds nothing new.

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§8 (Distribution R9: single self-contained `.exe`, `<exe-dir>/../seeds/` resolution,
gitignored release artifact). `requirements.md` R9.

**UX** (`.claude/rules/ux-first-class.md`): no NEW user-facing surface — the `.exe`
runs the same flows Phases 2–3 verified. The acceptance is that the bundled binary
launches + resolves the seeds + runs the catalog identically to the dev run (an
eyeball/launch gate). A seed-resolution failure (wrong drop location) shows the same
empty state s01 defines (naming where it looked).

**Disassembler-test / author-burden.** N/A — packaging step; no author-facing input.
