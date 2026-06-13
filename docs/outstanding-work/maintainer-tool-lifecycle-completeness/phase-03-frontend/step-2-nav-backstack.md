# 3.2 [FE] The in-app content back-stack + state-carrying frames + ‹ back + unsaved-changes guard (D42/D43/D44)

## What

Replace `App.tsx`'s flat mutually-exclusive content-screen model (the `selectedKcdxId` /
`worklistOpen` / `needsActionOpen` booleans, where `selectEntity` CLOSES the open content screen and
`onBack` clears everything to the navigator root) with the **in-app content back-stack** the redesign
settled (TRD D42, law 10). This is the cross-cutting navigation primitive every later Phase-3 step
wires onto — it lands FIRST, on its own, independently testable, before 3.3/3.4/3.5 consume it.

The primitive (law 10):
- **A back-stack of state-carrying frames** for the content area (s02 / s08 / s09), within the
  persistent shell (law 2), NO URL router. Each frame stores the screen AND its full in-memory state.
- **A resolve-action PUSHES a frame; a top-level destination RESETS the stack.** A context-carrying
  resolve-action (an s09 row's resolve, an s08 `[Fix ▸]`) pushes onto the stack (building s09→s02→s04 /
  s08→s02→s04 chains). A top-level destination (a navigator entity-select, `[Needs action]`,
  `[Import report]`, `[+ New entity]`) resets the stack to a fresh root there.
- **`‹ back` restores a frame's full state exactly** — never a fresh re-mount. The back affordance
  (the Layer-1 `back affordance` silhouette) sits top-left of the content pane at stack depth > 1,
  labeled with its destination ("‹ back to Needs action", "‹ back to the report"), in reserved space
  (no reflow, law 1); absent at the stack root.
- **The unsaved-changes guard (D44):** navigation away from a dirty editor (s04 field editor / s02
  lifecycle with pending edits not yet through the s06 confirm) is intercepted by a Save / Discard /
  Cancel confirm (the `overlay surface`) BEFORE navigating — nothing saved or lost without an explicit
  choice.
- **The version-new selector (D43):** the version dropdown sources the server-known `game_versions`
  AND the linked-Bin-resolved version — when the linked Bin resolves to a version the DB does not know,
  it appears under a "From your linked Bin" group, labeled "`<version>` · new" (glyph+text, law 7),
  selectable like any version, ephemeral until an AP18-confirmed row commits. (The dropdown source +
  marked entry; the s02 wiring that drives authoring from it is part of this primitive's seam, consumed
  by the create flow.)

In the SEPARATE gitignored frontend repo (`data/maintainer-tool/frontend/src/`).

## Scope

One commit in the frontend repo:
- A new navigation store (e.g. `src/shell/navStack.ts` or `src/nav/`) — the frame type (screen kind +
  its carried state), push / reset / pop (`‹ back`) operations, and the current-frame + depth selectors.
- `App.tsx` — replace the three flat booleans + `selectEntity` / `openWorklist` / `openNeedsAction` /
  `onBack` with the store: a navigator entity-select / top-level entry RESETS; the content slot renders
  the current frame's screen from its carried state; `‹ back` pops and restores.
- `AppShell.tsx` / a new `BackAffordance` component — the `‹ back` control (top-left of content pane,
  depth-gated, destination-labeled, reserved space).
- The unsaved-changes guard overlay — a dirty-editor navigate-away interception (Save/Discard/Cancel)
  wired into every stack mutation (push / reset / pop) out of a dirty s04/s02.
- The version dropdown's second source (the linked-Bin-resolved-new entry, D43) — the dropdown options
  model + the marked "From your linked Bin · new" entry.

Does NOT wire the s09 per-row resolution actions (3.3), the s08 reconciliation display (3.4), or the
s08 Fix-flow context-carry/applied-value (3.5) — those CONSUME this primitive. Builds the primitive +
its own tests only.

## Test bar

Vitest unit + component tests on the nav store + the shell wiring:
- **Push/reset:** a resolve-action push adds a frame (depth grows); a top-level destination (navigator
  select / `[Needs action]` / `[Import report]`) resets to a fresh root (depth 1). **FALSIFIABLE:** a
  navigator select that PUSHES instead of resetting fails; a resolve-action that resets instead of
  pushing fails.
- **State restore:** a frame carries its screen's state; `‹ back` restores it exactly (assert a frame's
  carried state survives a push + pop — the canonical case: an s08 frame's parsed-report stand-in state
  is restored on `‹ back`, not re-mounted empty). **FALSIFIABLE:** a `‹ back` that re-mounts a fresh
  screen (loses the carried state) fails.
- **Back affordance:** present + destination-labeled at depth > 1, absent at root, in reserved space
  (law 1 — no reflow on its appearance). **FALSIFIABLE:** a bare-arrow (unlabeled) back, or a back shown
  at root, fails.
- **Unsaved-changes guard:** a navigate-away from a dirty editor surfaces Save/Discard/Cancel first;
  Cancel stays put, Discard drops + navigates, Save runs the save spine then navigates; a clean editor
  navigates with no guard. **FALSIFIABLE:** a dirty-editor navigate-away that proceeds with no guard
  (silent loss) fails; a clean-editor navigate that shows the guard (spurious) fails.
- **Version-new selector (D43):** the dropdown lists a linked-Bin-resolved-new version under the "From
  your linked Bin" group marked "`<v>` · new"; selecting it is the same gesture as a known version.
  **FALSIFIABLE:** a new Bin version absent from the dropdown (forcing a hand-type) fails.

Gate: `npm run build` exit 0 + `vitest run` green. Runnable AT this step (the store + shell + the
existing s02/s08/s09 screens it routes among all exist; 3.1 landed the s09 view shell).

## Dependencies

- **3.1** — the s09 view shell (one of the content screens the stack routes among) exists.
- The existing s02 / s08 content screens + the s05 create flow (the stack's other frames + the
  version-new consumer) — all already in the FE.

## Reference

[`../plan-spec.md`](../plan-spec.md) — the D41 frontend; this primitive is the D42/D43/D44 navigation
layer the later steps build on.

## Design authority

`data/maintainer-tool/ui/design.md` **law 10** (the back-stack contract — push/reset, full-state
restore, the back affordance, the dirty-editor guard) + **law 2** (the content area is a back-stack on
both breakpoints) + the **`back affordance`** and **`select / dropdown`** (the version-new entry, D43)
silhouettes + the **App shell** §"the content area … is governed by an in-app back-stack". Cross-screen:
`s08-verification-worklist.md` §"The Fix flow carries context and returns" + §"Returned from a [Fix ▸]"
(what the s08 frame must carry), `s09-needs-action.md` §"Region & position" (the s09 frame's carried
state), `s02-entity-detail.md` §"States & variants" (the version-new selector + the dirty-editor guard).
Build to THESE specs, not this doc's summary.

## UX

Carried from the screen specs (`.claude/rules/ux-first-class.md`):
- **The back affordance** — top-left of the content pane at depth > 1, destination-labeled, reserved
  space (no reflow, law 1); absent at root; one affordance on both breakpoints (replaces the prior
  phone-only drill-down back).
- **State restore on `‹ back`** — the screen the maintainer returns to is exactly as they left it
  (toggles, scroll, the s08 ingested report), never a fresh empty re-mount.
- **The unsaved-changes guard** — Save / Discard / Cancel before a dirty-editor navigate-away; the
  maintainer is never surprised by a silent save OR a silent loss (D44 — the user's explicit "we can't
  let them think something was going to be saved but wasn't").
- **The version-new selector** — a new linked-Bin version is selectable from the dropdown, marked "from
  your linked Bin · new", never hand-typed (the maintainer-side disassembler-test, D43).
- **States** — empty (stack at root: the navigator is the way out, no back); the guard overlay
  (Save/Discard/Cancel); edge (a deep chain s09→s02→s04 — each `‹ back` walks one frame).

## Disassembler-test / author-burden

None on the plugin-author surface — a maintainer-tool FE screen, no author-facing plugin input, no
game-function target. The **maintainer-side** disassembler-test applies to D43: the tool resolves the
version from the linked Bin (the maintainer never hand-types it).
