# Step 7 — app skeleton + token/theme layer + load curated set

**What.** Stand up the PySide6/Qt6 application in `data/maintainer-tool/`: the window
skeleton (two-pane split + persistent status bar + modal layer — `ui/design.md`
§"Window skeleton"), the **token/theme layer** (the semantic color/type/space/icon/
shape tokens from `ui/design.md` §"Design-system contract" implemented as the one
module raw hex/px live in — law 9), and the load path (call the Phase-1 data-core to
read the curated set into an in-memory model the later steps render). No Ghidra /
`WHGame.dll` / dump prerequisite (R2). The GUI is a thin shell: it calls down into
`seeds_shared/`; it holds no SQL, no validation, no export logic.

**Scope.** The app entry point + the main window shell (the two empty panes + status
bar) + the token module + the load path. No navigator list yet (step 8), no detail
(step 9), no editing/save. Resolves the DB/seeds location with a working dev path (the
`<exe-dir>/../seeds/` convention is finalized in step 17).

**Test bar.** The load logic is headless data-core (already covered by
`seeds_shared/` tests). This step's own bar is the **user-facing acceptance** at the
phase gate — the tool launches, shows the two-pane shell + status bar, and loads the
curated set (or the empty state). The token layer is verified by the screens that
consume it (no token has a value outside this module — law 9). Per
`.claude/rules/headless-testable.md` the load-bearing logic is reached headless; the
Qt window is the thin shell over it.

**Test bar runnable now?** The headless load test runs now; the GUI shell is verified
at the phase's user-facing acceptance gate (it is the foundation later steps render
into — its own behavior is "launches + loads + shows shell/empty", an eyeball gate).

**Dependencies.** Phase 1 (the data-core read path — a populated, validated DB to
load). This step is the GUI foundation every later GUI step builds on; it lands first
in Phase 2.

**Design authority.** [`data/maintainer-tool/ui/design.md`](../../../../data/maintainer-tool/ui/design.md)
§"Window skeleton" (the two panes + status bar + modal layer) + §"Design-system
contract" (the token system this step implements) + §"Global interaction laws"
(law 1 layout stability, law 2 pane persistence, law 9 no-raw-values).
[`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md) §5
(thin-shell seam) + §6 US-1 (load) + R2 (no dump prerequisite).

**UX** (`.claude/rules/ux-first-class.md`, from the screen specs):
- **Populated** — the two-pane shell + status bar; the curated set loaded (the
  navigator list fills in step 8; here the model is loaded and the shell renders).
- **Empty (no DB/seeds resolved)** — a message naming WHERE the tool looked
  (`<exe-dir>/../seeds/`, the DB path) and what to do — not a blank window
  (s01 empty state).
- **Loading** — the data-core load in flight: a brief progress indication (one-shot,
  not a hot path).
- **Flow + feedback:** Launch → (load) → see the shell with the set loaded, or the
  empty state. No silent failure — a load that finds nothing says why.
- **Accessibility + consistency:** initial focus on the navigator search field (law
  keyboard&focus); standard Qt6 widgets; the token layer is the one consistent styling
  source.

**Disassembler-test / author-burden.** N/A — no author-facing game-function input.
