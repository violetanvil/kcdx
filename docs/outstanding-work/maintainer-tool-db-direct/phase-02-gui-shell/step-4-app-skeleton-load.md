# Step 4 — app skeleton + load curated set (US-1)

**What.** Stand up the PySide6/Qt6 application skeleton in `data/maintainer-tool/`
(a new GUI package — design §5) and implement US-1: on launch the tool loads the
curated entity set by calling the Phase-1 data-core (no Ghidra / `WHGame.dll` /
dump prerequisite — R2). The GUI is a thin shell: it calls down into
`seeds_shared/` to read the curated tables; it holds no SQL, no validation, no
export logic. This step delivers a launchable window that shows the loaded curated
set (or the empty/loading state), the foundation every later GUI step builds on.

**Scope.** The app entry point + the main window shell + the load path (data-core
call → in-memory model the later steps render). No editing, no picking detail yet
(step 5), no save (step 7). Resolves the DB/seeds location (the full
`<exe-dir>/../seeds/` convention is finalized in step 9; this step uses a working
dev path so the skeleton loads).

**Test bar.** A headless data-core test already covers the load logic (it lives in
`seeds_shared/`, exercised by a `tests/test_*.py`); the GUI shell's own bar is the
**user-facing acceptance** at the phase gate — the tool launches and shows the
curated set / the empty state. Per `.claude/rules/headless-testable.md` the
load-bearing logic is reached headless; the Qt window is the thin shell over it.

**Dependencies.** Phase 1 (the data-core reads the curated set — steps 1–3 give a
populated, validated DB to load). The GUI calls the data-core's read path.

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§6 US-1 (load) + §5 (thin-shell seam — GUI calls the data-core, holds no logic) +
§7 (the populated / empty / loading states this step renders).

**UX** (`.claude/rules/ux-first-class.md`, from design §7):
- **Populated** — the curated entity set shown (the list surface step 5 fills out;
  here it renders the loaded set).
- **Empty** — no DB/seeds resolved (wrong working dir, missing files): a message
  naming WHERE the tool looked (`<exe-dir>/../seeds/`, the DB path) and what to do
  — not a blank window.
- **Loading** — the data-core load in flight: a brief progress indication (a
  one-shot load, not a hot path).
- **Flow + feedback:** Launch → (load) → see the curated set or the empty state.
  No silent failure — a load that finds nothing says why.
- **Accessibility + consistency:** standard Qt6 widgets, keyboard-reachable; one
  consistent Qt layout idiom (the screen's base, extended by later steps).

**Disassembler-test / author-burden.** N/A — no author-facing game-function input.
