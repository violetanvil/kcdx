# Maintainer Tool — UI design system (master)

**How to use.** This file + the per-screen docs in `screens/` are the **implementation
spec** for the maintainer tool's **React + Mantine web frontend**. It is the visual +
interaction layer that sits ON the functional design in **[`../design.md`](../design.md)**
(the TRD — what the tool DOES). This doc fixes what the tool LOOKS like and how it BEHAVES;
it never re-litigates a TRD decision. Every screen references the tokens + laws below by
name — a value or law duplicated into a screen is drift.

**Status:** v1 (Draft). Date: 2026-06-02. The TRD it builds on is v1-revised + web-pivoted
(the six-job scope §9; the web app D14–D18 — see `../design.md` + `../changelog.md`). This
UI layer was re-expressed PySide6-desktop → React-web in the web-pivot pass (`changelog.md`).

---

## Product framing

- A **private maintainer web app** — a small set of trusted maintainers (auth is the
  operator's, TRD D17), reachable from any browser **including a phone** (the "edit on the
  go" goal). Not a shipped end-user app.
- **Function over form.** Ease of use and comprehensive capability are paramount.
  Organization, scannable lists, searchable/filterable navigation, editable forms,
  dropdowns that gate values, and **a stable layout that never jumps on state change** are
  the design's whole job. No marketing polish, no decorative motion.
- **The complete six-job tool.** v1 manages the entire reference DB: create entities
  (Job 1), re-verify (Job 2), supersede (Job 4), deprecate (Job 5), create versions
  (Job 6), and edit any existing version's full columns — plus browse, view, history, and
  side-by-side version comparison. (`../design.md` §9.)
- Target stack: **React + Mantine** (a React component library strong on forms, tables,
  selects, and modals — TRD D14). The data-core (`seeds_shared/`) is headless; the Python
  backend wraps it as an API; this frontend is a thin presentation shell that CALLS the API
  (`../design.md` §5). The frontend holds NO authoring logic — every rule is the
  data-core's, surfaced through the API (law 6).

---

## App shell (every screen shares this) — responsive

A navigation shell + a content area + an overlay layer. The shell is **responsive**: a
two-pane split on wide screens, a master-detail drill-down on narrow (phone) screens.

```
DESKTOP / WIDE (≥ the two-pane breakpoint)        PHONE / NARROW (< the breakpoint)
┌──────────────────┬───────────────────────┐      ┌───────────────────────┐
│  NAVIGATOR       │   ENTITY DETAIL        │      │  NAVIGATOR  (s01)     │   list fills
│  (entity list)   │   header · versions ·  │      │  search · filters ·   │   the screen
│  s01             │   field editor         │      │  list                 │
│                  │   s02 / s03 / s04       │      └───────────────────────┘
└──────────────────┴───────────────────────┘            │ tap an entity
       ◄─ overlay layer: confirm-save (s06),             ▼
          create (s05), dialogs · toasts (top)    ┌───────────────────────┐
                                                  │ ‹ back   ENTITY DETAIL │   detail fills
                                                  │  header · versions ·   │   the screen;
                                                  │  field editor          │   ‹back → list
                                                  └───────────────────────┘
       toast (top-anchored) floats above both layouts; overlays dim, never displace
```

- **NAVIGATOR** (`s01`) — search + filters + the scrollable entity list. The maintainer
  finds an item here. `+ New entity` lives here. On wide screens it is the persistent left
  pane; on phone it is the **default/home view** (the screen you land on).
- **DETAIL** (`s02`–`s04`) — the selected entity's header (identity + lifecycle + the
  **version-pick & verification surface**, formerly the status bar), its version area
  (current row + history + compare), and the field editor. On wide screens it is the right
  pane; on phone it is a **full-screen drill-down** reached by tapping an entity. The content
  area (DETAIL and its peer content screens s08/s09) is governed by an **in-app back-stack**
  (law 10): a **‹ back** affordance (top-left of the content pane on wide; the drill-down back
  on phone) returns to **the screen that pushed the current one**, restoring its full state —
  not always the list (e.g. `‹ back` from a fix returns to the s08 report it was launched
  from, intact). At the stack root there is no back (the navigator is the way out).
- **OVERLAY LAYER** — the confirm-save delta (`s06`), the create form (`s05`), and dialogs
  dim the content and float above it (centered modals on desktop, **full-screen sheets on
  phone**). **Toasts** (`s06` overlay concern) are **top-anchored**, transient, and float
  above every layout — the last save result + non-blocking notices live here (there is NO
  persistent status bar — the desktop `s07` bar is dissolved; D14 web pivot).

### Responsiveness & sizing
**Interface size** (`scale_base`) sets the type/space scale; the layout reflows by
breakpoint, never below a legible floor. **Wide:** the two-pane split has a draggable
divider; the navigator holds a minimum legible width, content scrolls within a pane rather
than the pane collapsing. **Narrow (phone):** one view at a time (list ⇄ detail
drill-down); each view gets the full viewport; the compare table scrolls horizontally
within the detail view. **No element jumps position on a state change** (law 1) — reserved
space for conditional affordances (the "was:" line, the dirty marker, the validation-error
line) is always allocated, rendered empty when inactive, at every breakpoint.

**The detail pane (s02) leads with the work surface.** A detail screen with heavy secondary
machinery (the version & verify surface, lifecycle) does NOT stack every section at natural
height — that lets the header eat the pane and starves the work surface. Instead: a **compact
pinned summary** (identity + the version `Select` + the one-line verify summary) stays visible
at the top, the **heavy/secondary sections** (the DLL link table, lifecycle) are **collapsible
sections** (collapsed by default — the `collapsible section` silhouette), and the **work surface
(the version table + the inline field editor) is expanded by default and takes the majority of
the pane**, scrolling within it. The screen leads with what the maintainer works on; the
verify/lifecycle machinery is one disclosure away, never consuming the viewport. The same
compact-header + collapse model holds on phone (within the full-viewport drill-down).

---

## Global interaction laws (non-negotiable, responsive-aware)

Numbered, enforceable. A screen cites "law N". Each states what it FORBIDS.

1. **Layout is stable across state changes.** A state change (a field becomes dirty, a
   validation error appears, a row is selected, the version/verify state changes) updates
   content IN PLACE — it never moves, resizes, or reflows a sibling element. Conditional
   affordances reserve their space always (the dirty marker gutter, the "was: <old>" helper
   line, the validation-error line) and render empty when inactive. (Breakpoint changes are
   not state changes — a viewport resize MAY reflow; a data/interaction state change MAY
   NOT.) **Forbids:** any element shifting position because another element appeared/changed
   within a layout.

2. **The navigation shell persists; the content area is a back-stack; only the overlay layer
   covers content.** The shell (on wide: both panes; on narrow: the current view + its back
   affordance) renders in every data state. The **content area is governed by an in-app
   back-stack on BOTH breakpoints** (the mechanics are law 10) — content changes only via
   navigation (a content screen is pushed/popped/reset on the stack, never auto-swapped on a
   background event, law 3); nothing covers the content except the overlay layer — a
   confirm/create/dialog (which dims, never permanently displaces, and dismisses back to the
   same state) or a transient toast. On wide the navigator stays persistently visible (a
   navigator entity-selection is never a back-stack frame — it resets the content stack, law
   10); on narrow the back-stack drives the drill-down. **Forbids:** a surface that
   permanently hides the navigation affordance; a data-state change that swaps the whole
   shell; a URL router (the back-stack is in-app, no routes — TRD D42).

3. **Navigation is user-action-driven.** The detail content changes only when the user
   selects an entity, selects a version row, drills in/back, or opens a mode (history /
   compare / new). A background event (a verify result, a validation result, a save
   completing) updates a STATUS surface or content IN PLACE — it never navigates, drills, or
   re-selects for the maintainer. **Forbids:** auto-navigating, auto-drilling, auto-selecting
   a different row, or auto-opening a mode on a non-user event.

4. **Verification is advisory; the maintainer is final authority.** The version an edit
   targets comes from a **version dropdown** (the default — server-known game versions) or
   from a **client-side check against a local DLL** (the browser reads a locally-picked DLL
   and runs the `.rdata` scan in-page, sending only the resolved version tag — TRD D15). The
   same client-side, no-upload posture extends to the **per-author verification checks** (the
   per-kind static survival check against the linked DLL's bytes — TRD D24–D27) and to the
   **ingested live-verification report** (the in-game plugin's verdicts — TRD D28): every
   verification result — version-resolve, a per-kind static verdict (Unchanged / Changed /
   Ambiguous / CannotCheck), an imported live verdict — is **advisory**. An
   unresolved/unverified/Changed/Ambiguous state WARNS, never blocks, and carries an explicit
   **"I accept — save anyway"** override; an Ambiguous callsite STEERS the maintainer to
   extend the pattern (TRD D31), it does not refuse. A check runs only against a linked DLL
   whose resolved version **matches** the row's version; no matching DLL → the check is
   **unavailable + noted**, never a block (degraded — TRD D30). **Forbids:** a hard block on a
   missing/unverified version OR a Changed/Ambiguous check verdict; a silent bypass with no
   explicit maintainer acknowledgment; uploading a DLL (or a report whose verdicts re-verify
   rows) without the per-mutation confirm (law 5); auto-acting on a verification result
   (law 3).

5. **A mutation is one atomic, confirmed transaction.** Every save runs validate → write DB
   → export CSVs → round-trip → commit + push (server-side, TRD D16) as ONE transaction,
   gated by a confirm step that shows the plain-language field delta. A failure at any stage
   rolls back fully. **Forbids:** a partial write; a save that lands without the confirm
   delta; surfacing the git mechanics to the maintainer (the commit/push is invisible
   plumbing — the maintainer sees "Saved").

6. **The shared validator is the single gate.** Every field/row/entity invariant is the
   data-core's shared validator, reached through the API (`../design.md` §8, R3); the
   frontend binds it and renders its verdict — it never reimplements a rule. **Forbids:** a
   validation rule written in the frontend; a value accepted that the validator would
   reject. (The one exception: the client-side `.rdata` scan, D15 — a verification aid whose
   Python counterpart is the test-of-record, not a data-integrity rule.)

7. **Identity fields are read-only; the read-only state is conveyed by more than color.**
   `kcdx_id`, `name` (entity identity), and `valid_from_version` (version-row identity key)
   never change once authored (`policy.md`) — they render visibly non-editable (a distinct
   surface treatment + a lock affordance, not merely a greyed field). **Forbids:** an
   editable-looking identity field; read-only state signaled by color alone.

8. **A new-DB-row action is approval-gated.** Creating a new entity (Job 1) or a new version
   row (Job 6) grows the Address Library — it requires explicit maintainer approval in the
   confirm step before it lands (AP18, `policy.md` §"DB additions require explicit
   approval"). An UPDATE to an existing row is not gated. **Forbids:** a new entity/version
   landing without an explicit approval acknowledgment in the confirm surface.

9. **No raw values at a call site.** Every color/size/spacing/font/radius resolves to a
   semantic token (the Mantine `theme` + CSS variables below) — never a hex, px, or literal
   in a component. **Forbids:** a raw value in a screen spec or at a component call site.

10. **The content area is an in-app back-stack; `‹ back` restores full state; navigation
    never silently discards unsaved edits (TRD D42/D44).** Navigation between content screens
    (s02 / s08 / s09) is governed by an **in-app back-stack** (no URL router), on both
    breakpoints, within the persistent shell (law 2). Its contract:
    - **A resolve-action PUSHES a state-carrying frame; a top-level destination RESETS the
      stack.** A context-carrying resolve-action (an s09 row's resolve, an s08 `[Fix ▸]`)
      pushes a frame, building the fix-flow chain (s09→s02→s04). A deliberate **top-level
      destination** — a navigator entity-selection, or a top-level entry (`[Needs action]`,
      `[Import report]`, `[+ New entity]`) — **resets** the stack to a fresh root there. So
      `‹ back` always walks the way the maintainer reached the CURRENT screen, never a detour
      through an abandoned task.
    - **Each frame carries the screen's FULL in-memory state — `‹ back` restores it exactly.**
      A frame stores the screen AND its live state: s08's ingested report (the parsed
      worklist + block split + scroll), s09's section toggles + scroll, s02's selected version
      + expanded collapsible-section toggles + scroll. `‹ back` restores that state — it never
      re-mounts a fresh copy (the s08 ingested report survives a `[Fix ▸]` excursion with no
      re-import; the report is client-side only — TRD D31).
    - **The back affordance** — at stack depth > 1, a **`‹ back` control** (the silhouette
      below) sits at the **top-left of the content pane** (above the screen's own header),
      labeled with its destination ("‹ back to Needs action", "‹ back to the report"), in
      reserved space so its appearance/disappearance never reflows the content (law 1). At
      stack root there is no back control (the navigator is the way out on wide; the navigator
      home on phone).
    - **Navigation away from a dirty editor surfaces the unsaved-changes guard FIRST** — a
      `‹ back`, a stack-reset, or an entity-switch out of a dirty editor (the s04 field editor
      or s02 lifecycle with pending edits not yet through the s06 confirm) is **intercepted by
      a confirm** (the `overlay surface` silhouette: **Save** primary → runs the save spine
      then navigates / **Discard** outline-danger → drops the edits then navigates / **Cancel**
      subtle → stays put). Nothing is saved OR lost without an explicit choice (TRD D44).
    **Forbids:** a URL router or browser-history routing; an identity-only frame that re-mounts
    a screen fresh on `‹ back` (it loses the s08 report); a `‹ back` that navigates past an
    abandoned task it didn't push; navigating away from a dirty editor without the guard;
    auto-saving or silently discarding on navigate-away.

## Keyboard, focus & touch (whole-app)
- **Initial focus** lands on the navigator search field (the entry point to finding an
  item); never on a destructive or mutating control. On phone the search field is the
  home-view focus.
- **Tab order** is navigator (search → filters → list) → detail (header → version surface →
  version area → field editor → primary action) → (open overlay last). Within lists/tables,
  Up/Down move row to row; Enter selects.
- **Esc** closes the topmost overlay only (law 2); it never quits or changes the selection.
  On phone a sheet has an explicit close (‹/✕) in addition to Esc.
- **Enter** in the field editor with a valid dirty form triggers the primary action (Review
  changes / Save); never a destructive action.
- **Touch:** primary affordances meet a comfortable touch-target minimum; the drill-down
  ‹back and the sheet close are thumb-reachable; the save toast is top-anchored (out of the
  thumb zone of the primary action). Every field, dropdown, list row, and action is
  keyboard-reachable AND touch-operable, and labelled (law 7's non-color affordance applies
  to read-only state).

---

## Design-system contract (Mantine theme + CSS variables)

Function-first, calm, legible. Dark + light, one accent. Every value is a semantic token —
raw hex/px live ONLY in the Mantine `theme` object (mapped to CSS variables); every
component reads a role (law 9). The token tables below ARE the `theme` the frontend
implements.

### Color — semantic roles (dark + light)

| role | purpose | dark | light |
|---|---|---|---|
| `accent` | primary action / selection | `#3b82f6` | `#2563eb` |
| `accent_hover` | accent hover/active | `#5b9bff` | `#1d4ed8` |
| `accent_text` | text/glyph ON an accent fill | `#06080d` | `#ffffff` |
| `text_primary` | body + headings | `#e7eaf0` | `#161a21` |
| `text_secondary` | sub-text / captions / "was:" | `#98a1b2` | `#5a6373` |
| `text_disabled` | disabled / hint | `#586273` | `#a7afbd` |
| `surface` | app background | `#0c0e13` | `#f3f5f8` |
| `surface_raised` | pane / card / modal / sheet | `#14171e` | `#ffffff` |
| `surface_sunken` | input wells / list / table | `#090b0f` | `#e9edf2` |
| `surface_hover` | hovered row / item | `#1a1e27` | `#eef1f6` |
| `surface_selected` | selected list/version row | `#1d2533` | `#dde6f5` |
| `border` | hairline divider / field edge | `#222732` | `#d9dee6` |
| `border_strong` | emphasized border (hover/active) | `#313847` | `#c2c9d4` |
| `focus_ring` | keyboard focus ring | `#3b82f6` | `#2563eb` |
| `dirty` | changed-field marker + "was:" accent | `#fbbf24` | `#d97706` |
| `diff_band` | differing-field row band (compare) | `#2a2410` | `#fdf6e3` |
| `readonly_field` | read-only identity field fill | `#101319` | `#eceff4` |
| `success` | saved / verified | `#34d399` | `#059669` |
| `warning` | can't-resolve / unverified / nothing-changed | `#fbbf24` | `#d97706` |
| `error` | validation error / write failure / blocked | `#f87171` | `#dc2626` |
| `info` | neutral notice | `#60a5fa` | `#2563eb` |
| `status_active` | entity active | `#34d399` | `#059669` |
| `status_deprecated` | entity deprecated | `#98a1b2` | `#5a6373` |
| `status_superseded` | entity superseded | `#a78bfa` | `#7c3aed` |
| `status_unverified` | row unverified at the targeted version | `#fbbf24` | `#d97706` |

### Type — named scale (factor × `font_size_base`; no raw font px)
One base size (`font_size_base`, default 14 — a dense maintainer tool); every step is a
factor, so one knob rescales all type. (Maps to Mantine's `fontSizes` + a `mono` family.)

| name | factor | use |
|---|---|---|
| `title` | 1.5 | screen / entity title |
| `heading` | 1.15 | section headings (Versions, Lifecycle, Fields) |
| `body` | 1.0 | body text, field labels, list rows |
| `body_strong` | 1.0 | body at weight 600 (selected row, dirty field) |
| `label` | 0.85 | field labels, filter pills, status chips |
| `caption` | 0.8 | "was:" helper, sub-text, validation text |
| `mono` | 1.0 | tabular data — rva, signature, version tags, ids (monospace) |

**Fonts:** UI = the system UI sans stack; data = a bundled/`ui-monospace` monospace
(`mono` scale — every rva / signature / version tag / id). The mono font makes columns
align and hex scannable. The family is a theme token (swaps without touching call sites).

### Space — rem rhythm (factor × base × density)
| step | factor | | step | factor |
|---|---|---|---|---|
| `xs` | 0.25 | | `lg` | 1.0 |
| `sm` | 0.5 | | `xl` | 1.5 |
| `md` | 0.75 | | `2xl` | 2.25 |

Pane padding, field gaps, table cell padding, the navigator width all expressed in these
(Mantine `spacing`). **Density** = 1.0 but biased compact. The dirty-marker gutter, the
"was:" line, and the validation-error line each reserve a fixed step of space always
(law 1).

### Breakpoints
- `bp_two_pane` — at/above this viewport width the two-pane split renders; below it, the
  master-detail drill-down. (One breakpoint drives the responsive shell; Mantine
  `breakpoints` carries it.) Tables/compare add an internal horizontal-scroll affordance
  below their content width regardless of breakpoint.

### Icon sizing — rem, off the same knob
`icon_sm` 0.875 · `icon_md` 1.0 · `icon_lg` 1.25. Glyph boxes are rem constants — the
interface-size knob rescales icons in lockstep with type + spacing.

### Shape & elevation
Corner-radius steps: `radius_sm` 3px · `radius_md` 5px (the narrow px exception — radii
don't scale with the type ramp). Elevation is flat: hierarchy from `surface` /
`surface_raised` / `surface_sunken` + spacing + hairline `border` — not drop-shadows. The
overlay layer gets one soft scrim (`surface` at reduced alpha) + a `surface_raised`
card/sheet.

### Motion
Minimal and functional only: a row-selection highlight cross-fade, a history/compare
expander, a modal/sheet transition (a phone sheet slides up; a desktop modal fades), a
drill-down slide (list ⇄ detail on phone), a toast in/out. **No motion that moves a resting
element** (law 1). Subtle, fast, respects `prefers-reduced-motion`.

---

## Component silhouettes (Mantine primitives; each rendered once)

Named to the Mantine component each maps to. Composed patterns (dirty marker, "was:" line,
diff cell) are app components built FROM these.

- **search field** → `TextInput` — the navigator's filter-as-you-type box.
- **filter control** → `Select`/`SegmentedControl` — status filter (all / active /
  deprecated / superseded) + kind filter (all / the nine kinds).
- **entity list row** → a list `NavLink`/row — name · `kcdx_id` (mono) · status chip;
  selected / hovered states.
- **status chip** → `Badge` — labelled+colored (active / deprecated / superseded /
  unverified), color + glyph (never color-alone, law 7).
- **section header** → a heading bar (`Group` + `Title`) with an optional action affordance.
- **version table row** → a `Table` row — version tag (mono) · kind · verified-date ·
  evidence; current / selected / differing (compare) states.
- **field row (read-only)** → label + value with the read-only identity treatment (law 7).
- **field row (editable)** → label + `TextInput`/`Select` + dirty marker gutter + reserved
  "was: <old>" line + reserved validation-error line (the layout-stability composite).
- **select / dropdown** → `Select` — a gated value picker (`evidence_kind` enum, `kind`
  enum, module picker, supersede-target picker, the **version dropdown**). The **version
  dropdown** sources TWO origins (TRD D43): the server-known `game_versions` tags AND the
  **linked-Bin-resolved version** — when a linked Bin resolves to a version the DB does not
  know, it appears as a marked entry under a **"From your linked Bin"** group header, labeled
  **"`<version>` · new"** (glyph + text, law 7 — never color-alone); selecting it is the same
  gesture as any version. The maintainer never hand-types a version (the version is read from
  the linked folder, not typed — the maintainer-side disassembler-test).
- **text well** → `TextInput` — a single-line editable field (rva, signature, dates).
- **button** → `Button` variants (primary / default / subtle / outline-danger) — Review
  changes, Save, Cancel, Revert-field, Deprecate.
- **dirty marker** → the changed-field accent gutter mark (`dirty` role + glyph).
- **diff cell** → a compare-`Table` cell whose value differs across the set (`diff_band` +
  a marker glyph).
- **overlay surface** → `Modal` (centered, desktop) / full-screen `Drawer` or `Modal
  fullScreen` (phone sheet) — confirm-save delta, create form, dialogs (+ scrim).
- **field-delta list** → the confirm overlay's `field: old → new` rows.
- **warning banner** → `Alert` — an inline banner (unverified, resolver failure,
  nothing-changed) with its override / acknowledge affordance.
- **toast** → `Notification`/`notifications` — top-anchored, transient: last-save result +
  non-blocking notices (replaces the dissolved status bar).
- **collapsible section** → a `Group`/`UnstyledButton` section header (a label + a state
  chevron `›`/`⌄`) over a collapsible body (`Collapse`). Clicking/Enter/Space toggles; the
  **header row never moves** (the body expands IN PLACE below it, pushing the sections below it
  down — a user-toggled disclosure, not a state-change reflow, so law 1's no-reflow-on-state-
  change is honored). Collapsed/expanded state is glyph+text (the chevron + an aria-expanded),
  never color-alone (law 7); the header is keyboard-reachable and a button role. The grouping
  primitive for a heavy/secondary s02 section (the DLL link table, lifecycle) so the work
  surface keeps the room.
- **version & verify surface** → the s02-header composite, split into a **compact pinned
  summary** (always visible: the version `Select` + a **one-line verify summary** — e.g. "Bin
  folder linked — `<version>` ✓" / "no folder linked") and a **collapsible "Verify against a
  DLL" section** (the install-set link surface: the **Bin-folder pick** + the per-module link
  rows + the link-to-create prompt). The maintainer links the game's **Bin FOLDER once** (a
  single in-session `<input webkitdirectory>` directory pick — portable, the DLLs read in-page,
  never uploaded, D15/D26; the install-set, D30); the tool resolves the install version from
  WHGame.dll and finds each referenced module's DLL by filename in that folder. The full link
  table collapses (collapsed by default — D30/the s02 layout); the one-line summary keeps the
  verify state glanceable without it.
- **per-module link row** → a row per module with a **stable top line that never moves** (the
  module name (mono) · the DLL's status in the linked folder — found / "not found in the linked
  folder" · a **version-match indicator** glyph+text — "✓ matches row" / "≠ no match", never
  color-alone, law 7) AND a **verify message in reserved multi-line space BELOW** the top line
  (a long system-caused message — "that folder has no WHGame.dll", "couldn't resolve a version"
  — wraps DOWNWARD into the reserved space, never pushing the top-line affordance sideways off
  the row; law 1, the reserved-space reflow-safe structure). A **non-WHGame module inherits the
  install's version from WHGame.dll** (the CryEngine DLLs carry no KCD2 version string — D30).
  The folder link is re-picked each session (D30); no persisted handle.
- **verdict badge** → a `Badge`+text composite rendering the s04 author-time STATIC per-kind
  check verdict — one of **Unchanged** / **Changed** / **Ambiguous** / **CannotCheck** — as
  glyph + label (never color-alone, law 7), with a reserved detail line beneath (law 1). The
  advisory carrier for the static check result in s04. (s08's live-report verdict is the
  separate **live verdict badge** below — the static and live vocabularies diverged at TRD D36,
  so each surface carries only the states it can actually produce.)
- **live verdict badge** → a `Badge`+text composite rendering the s08 live-report verdict — one
  of the **7 D36 states** `verified_working` / `passed_not_verified` / `failed` /
  `not_applicable` / `cannot_check` / `skipped` / `error` — as glyph + label (never color-alone,
  law 7), with a reserved detail line beneath (law 1). The verdict is the CEILING of the
  strongest verification method that ran (TRD D36); the badge's role colour follows the verdict's
  worklist block — `success` role for the verified block (`verified_working` / `passed_not_verified`),
  `error` role for the failing block (`failed`), `warning`/`info` role for the no-action verdicts
  (`not_applicable` / `cannot_check` / `skipped` / `error`). s08-only; the s04 static check uses
  the 4-state **verdict badge** above. Composed beside a **proof-rank chip** that carries the
  proof-strength.
- **proof-rank chip** → a compact `Badge`-family chip rendering a live-report row's
  `method_rank` (1–5, TRD D36's proof ladder) as **"rank N · &lt;method&gt;"** — `1 · observed` /
  `2 · safe-read` / `3 · reachability` / `4 · on-disk hash` / `5 · resolution` — glyph + text,
  never color-alone (law 7). It carries the "how strong is this proof" signal the verdict alone
  cannot: a `passed_not_verified` at rank 4 (on-disk hash) reads distinctly from one at rank 2
  (safe-read), and `verified_working` is always rank 1. s08 composes it beside the **live verdict
  badge**; the rank→method labels live here once (no screen restates them).
- **ingest progress bar** → a determinate `Progress` + count label ("Parsing report… N / M
  rows") shown while s08 parses an imported report; resolves in place to the worklist (law 1
  — it occupies the worklist's region).
- **batch field-delta list** → the s06 confirm composite at batch scale: a scrollable list of
  per-row `field: old → new` deltas (one group per affected row) under one confirm action —
  the bulk-re-verify surface (law 5 at batch scale; the single-row `field-delta list` is its
  one-row case).
- **back affordance** → a `‹ back` control (`Button` subtle, with a `‹` glyph + a destination
  label) governed by the content back-stack (law 10). Shown at the **top-left of the content
  pane** when the stack has depth > 1, **labeled with where it returns** ("‹ back to Needs
  action", "‹ back to the report") — never a bare arrow; in reserved space so its
  presence/absence never reflows the content (law 1). At stack root it is absent (the navigator
  is the way out on wide; on phone, at the stack root `‹ back` returns to the navigator home
  view). One affordance, both breakpoints (it replaces the prior phone-only drill-down back).

---

## Screen index

| Doc | Screen | Region | Fidelity |
|---|---|---|---|
| [`screens/s01-navigator.md`](screens/s01-navigator.md) | Entity navigator (search/filter/list) | left pane / phone home | v1 |
| [`screens/s02-entity-detail.md`](screens/s02-entity-detail.md) | Entity detail (header + version&verify surface + lifecycle + version area) | right pane / phone drill-down | v1 |
| [`screens/s03-version-history-compare.md`](screens/s03-version-history-compare.md) | Version history + side-by-side compare | detail | v1 |
| [`screens/s04-field-editor.md`](screens/s04-field-editor.md) | Field editor (view/edit a version row) | detail | v1 |
| [`screens/s05-create.md`](screens/s05-create.md) | Create new entity / new version | overlay (modal/sheet) + detail | v1 |
| [`screens/s06-save-confirm.md`](screens/s06-save-confirm.md) | Save confirm (field delta + approval) + the toast/overlay concern | overlay | v1 |
| [`screens/s08-verification-worklist.md`](screens/s08-verification-worklist.md) | Verification report worklist (import the in-game plugin's report → pass/fail → batched bulk re-verify) | right pane / phone drill-down | v1 |
| [`screens/s09-needs-action.md`](screens/s09-needs-action.md) | Needs-action view (incomplete-lifecycle entities grouped by kind → route each to its canonical resolve flow, TRD D41) | right pane / phone drill-down | v1 |

*(s07 — the desktop status bar — is dissolved in the web pivot: its version/verify content
moved into s02's header; its save-result + notices became the top-anchored toast, specified
in s06's overlay concern.)*

*(s08 is the verification engine's review surface — TRD D28/US-11. The per-module DLL link
table + the link-to-create prompt live in s02's verify surface; the per-author static check
verdict lives inline in s04; s08 is the BULK live-report review screen. The version&verify
surface, the s04 check verdict, and s08 together cover the verification engine's UI — TRD
D24–D31.)*

---

## Navigation map

```
s01 navigator ── the home view (phone) / left pane (wide); search + filters + list;
 │                [+ New entity ▸ s05]   [Import verification report ▸ s08]
 │                [Needs action ▸ s09]  (count badge; the standing lifecycle-completeness surface)
 │  select/tap an entity ──▶ s02 entity detail (right pane / drill-down)
 │  s09 needs-action ── incomplete-lifecycle entities by kind; a row's resolution ──▶ s02/s04/s05, returns to s09
 │
 └─ s02 entity detail ── header (identity read-only + the version & verify surface) +
        │                 lifecycle (editable: supersede/deprecate) + version area
        │  default-select newest row (or the verified/picked version) ──▶ s04 field editor
        │  [Show history] ──▶ s03 history list
        │  [Compare versions] ──▶ s03 compare (multi-select ▸ N columns side-by-side)
        │  [+ New version] ──▶ s05 (prefilled from a source row)
        │  ‹ back (content-pane, depth>1) ──▶ the screen that pushed s02 (law 10); at root, navigator
        │
        ├─ s03 history/compare ── view past versions; a column is editable in place ──▶ s04
        │
        └─ s04 field editor ── view a row; edit (audit trio / full columns); dirty markers
              │  edit an EXISTING version ──▶ confirmation it's an edit, not a new version
              │  [Review changes] ──▶ s06 save-confirm (overlay)
              │
              └─ s06 save-confirm ── field delta (old → new); approval (if a new row, law 8);
                    [Save] ──▶ one atomic commit+push ──▶ toast "Saved" (or "blocked — Retry")
                    [Cancel] ──▶ back to s04, nothing lands

s08 verification worklist ── [Import verification report] (s01) ──▶ pick report.json (File API)
 │   ──▶ ingest progress (N/M rows) ──▶ the worklist (pass/fail split, every row + verdict)
 │   select passing rows ▸ [Re-verify] ──▶ s06 BATCH confirm (per-row delta list, one txn)
 │   a failing row [Fix ▸] ──▶ PUSHES s02/s04 (law 10, state-carrying); ‹ back ──▶ this worklist, report intact
 │   ‹ back (content-pane, depth>1) ──▶ the screen that pushed s08 (law 10); at root, navigator

s02 verify surface (the install-set link table, a collapsible section) ── link the game Bin
     FOLDER once (D30; the per-module rows resolve from that folder). The resolved version
     becomes a selectable in the top version dropdown — even when NEW (not in game_versions),
     marked "from your linked Bin · new" (D43); a linked install newer than the entity's rows
     ──▶ inline "[Add a version row at <v>]" ──▶ s05 create-version, prefilled at the install's
     version (user-action, law 3; AP18 confirm, law 8). A per-author STATIC check verdict renders
     inline in s04 (the row being authored).

overlay layer ── confirm/create/dialogs (centered modal on wide, full-screen sheet on phone);
     the unsaved-changes guard (Save/Discard/Cancel, law 10) on navigate-away from a dirty editor;
     toast (top-anchored, transient: save result + notices). Floats above; never navigates
     (law 3). The version & verify surface (the compact summary: version dropdown + the one-line
     verify state; the collapsible install-set link table + the client checks, D15/D24–D30) lives
     in s02's header, NOT a global bar.
```

---

## Per-screen doc template

Each `screens/sNN-*.md` uses this skeleton so the set stays comparable:

1. **Phase & fidelity** — v1/high, or sketch.
2. **Purpose / when shown.**
3. **Region & position** — incl. the wide-vs-phone placement.
4. **Contents** — table: *Element → Component (Mantine) → Data bound → Intent emitted*.
5. **States & variants** — populated / empty / loading / error / disabled / edge.
6. **Links in / out** — the local slice of the nav map.
7. **Applicable laws** — which numbered laws bite here.
8. **Responsive behavior** — what changes between the two-pane and the drill-down/phone
   layout (where it differs from the default).
