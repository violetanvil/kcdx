# Maintainer Tool — UI design system (master)

**How to use.** This file + the per-screen docs in `screens/` are the **implementation
spec** for the maintainer tool's PySide6/Qt6 GUI. It is the visual + interaction layer
that sits ON the functional design in **[`../design.md`](../design.md)** (the TRD — what
the tool DOES). This doc fixes what the tool LOOKS like and how it BEHAVES; it never
re-litigates a TRD decision. Every screen references the tokens + laws below by name — a
value or law duplicated into a screen is drift.

**Status:** v1 (Draft). Date: 2026-06-02. The TRD it builds on is v1-revised (the
six-job scope — see `../design.md` §9 + `../changelog.md`).

---

## Product framing

- A **private maintainer tool** — one operator (the Address Library maintainer), one
  Windows desktop window. Not a shipped end-user app.
- **Function over form.** Ease of use and comprehensive capability are paramount.
  Organization, scannable lists, searchable/filterable navigation, editable forms,
  dropdowns that gate values, and **a stable layout that never jumps on state change**
  are the design's whole job. No marketing polish, no decorative motion.
- **The complete six-job tool.** v1 manages the entire reference DB: create entities
  (Job 1), re-verify (Job 2), supersede (Job 4), deprecate (Job 5), create versions
  (Job 6), and edit any existing version's full columns — plus browse, view, history,
  and side-by-side version comparison. (`../design.md` §9.)
- Target stack: **PySide6 / Qt6**, standard widgets. The data-core (`seeds_shared/`) is
  headless and Qt-free; this UI is a thin presentation shell that CALLS it
  (`../design.md` §5).

---

## Window skeleton (every screen shares this)

Two persistent panes + an overlay layer:

```
┌──────────────────┬──────────────────────────────────────────────────┐
│  NAVIGATOR       │   ENTITY DETAIL                                   │   ◄─ status bar
│  (entity list)   │   header · version area · field editor           │      (bottom,
│                  │                                                   │      persistent)
│  s01             │   s02 / s03 / s04 / s05                           │
├──────────────────┴──────────────────────────────────────────────────┤
│  STATUS BAR  —  DLL link state · save/commit result · global notices │   s07
└──────────────────────────────────────────────────────────────────────┘
       ◄─ modal layer: confirm-save (s06), new-entity (s05), dialogs — float above,
          dim the panes, never displace them
```

- **NAVIGATOR** (`s01`) — the left pane: search + filters + the scrollable entity list.
  Always present. The maintainer finds an item here. `+ New entity` lives here.
- **DETAIL** (`s02`–`s04`) — the right pane: the selected entity's header (identity +
  lifecycle), its version area (current row + history + compare), and the field editor.
  Replaced in content by selection; the pane itself never moves.
- **STATUS BAR** (`s07`) — a persistent bottom strip: the DLL-link state (linked version
  or "no DLL linked"), the last save/commit result, and transient notices. Always
  present so verification context + the result of the last action are always visible.
- **OVERLAY / MODAL** — the confirm-save delta (`s06`), the new-entity form (`s05` modal
  variant), and dialogs float above the panes and dim them; they never resize or
  displace a pane.

### Window sizing & responsiveness
The window is resizable. **Interface size** (`scale_base`) sets the lower bound on
element size — elements never shrink below it. Within that bound the layout
**responsively compresses** as the window narrows: paddings/gaps tighten toward their
`xs` step, the navigator holds a minimum legible width, and **content scrolls rather than
panes collapsing**. The two-pane split has a draggable divider; below the navigator's
minimum width the divider stops and the detail pane scrolls. **No element jumps position
on a state change** (law 1) — reserved space for conditional affordances (the "was:"
line, the dirty marker) is always allocated, rendered empty when inactive.

---

## Global interaction laws (non-negotiable)

Numbered, enforceable. A screen cites "law N". Each states what it FORBIDS.

1. **Layout is stable across state changes.** A state change (a field becomes dirty, a
   validation error appears, a row is selected, the DLL link changes) updates content
   IN PLACE — it never moves, resizes, or reflows a sibling element. Conditional
   affordances reserve their space always (the dirty marker gutter, the "was: <old>"
   helper line, the validation-error line) and render empty when inactive. **Forbids:**
   any element shifting position because another element appeared/changed.

2. **The two panes persist; only modals overlay.** The navigator and detail panes
   render in every state. No surface covers a pane except a modal (confirm-save,
   new-entity, a dialog), which dims the panes without displacing them and is dismissed
   back to the same pane state. **Forbids:** a full-screen view that hides the navigator;
   a pane that disappears to make room.

3. **Navigation is user-action-driven.** The detail pane changes only when the user
   selects an entity, selects a version row, or opens a mode (history / compare / new).
   A background event (a DLL link resolving, a validation result, a save completing)
   updates STATUS or CONTENT in place — it never switches the selected entity/row or
   opens a mode for the user. **Forbids:** auto-navigating, auto-selecting a different
   row, or auto-opening a mode on a non-user event.

4. **Verification is advisory; the maintainer is final authority.** The tool's
   resolve/verify checks (the DLL `.rdata` version resolver) WARN when they cannot
   confirm a fact — they never block an action. Every "cannot resolve/verify" state
   carries an explicit **"I accept — save anyway"** override (guards against a tool
   error). **Forbids:** a hard block on an unlinked DLL or a resolver failure; a silent
   bypass with no explicit maintainer acknowledgment.

5. **A mutation is one atomic, confirmed transaction.** Every save runs
   validate → write DB → export CSVs → round-trip → commit as ONE transaction, gated by
   a confirm step that shows the plain-language field delta. A failure at any stage rolls
   back fully (the DB + CSVs return to their pre-action state). **Forbids:** a partial
   write; a save that lands without the confirm delta; surfacing the git mechanics to the
   maintainer (the commit is invisible plumbing — the maintainer sees "Saved").

6. **The shared validator is the single gate.** Every field/row/entity invariant is the
   data-core's shared validator (`../design.md` §8, R3); the UI binds it and renders its
   verdict — it never reimplements a rule. **Forbids:** a validation rule written in the
   GUI; a value accepted that the validator would reject.

7. **Identity fields are read-only; the read-only state is conveyed by more than color.**
   `kcdx_id`, `name` (entity identity), and `valid_from_version` (version-row identity
   key) never change once authored (`policy.md`) — they render visibly non-editable
   (a distinct surface treatment + a lock affordance, not merely a greyed field).
   **Forbids:** an editable-looking identity field; read-only state signaled by color
   alone.

8. **A new-DB-row action is approval-gated.** Creating a new entity (Job 1) or a new
   version row (Job 6) grows the Address Library — it requires explicit maintainer
   approval in the confirm step before it lands (AP18, `policy.md` §"DB additions require
   explicit approval"). An UPDATE to an existing row is not gated. **Forbids:** a new
   entity/version landing without an explicit approval acknowledgment in the confirm
   modal.

9. **No raw values at a call site.** Every color/size/spacing/font/radius in a screen
   resolves to a semantic token or scale factor below — never a hex, px, or literal.
   **Forbids:** a raw value in a screen spec or at a widget call site.

## Keyboard & focus (whole-window)
- **Initial focus** lands on the navigator search field (the entry point to finding an
  item); never on a destructive or mutating control.
- **Tab order** is navigator (search → filters → list) → detail (header → version area →
  field editor → primary action) → (open modal last). Within the entity list and the
  version table, Up/Down move row to row; Enter selects.
- **Esc** closes the topmost modal only (law 2); it never quits the app or changes the
  selection.
- **Enter** in the field editor with a valid dirty form triggers the primary action
  (Review changes / Save); never a destructive action.
- Every field, dropdown, list row, and action is keyboard-reachable and labelled
  (law 7's non-color affordance applies to read-only state).

---

## Design-system contract

Function-first, calm, legible. Dark + light, one accent. Every value is a semantic token
or a scale factor — a theme swap is a role→value remap with zero call-site change (law 9).
Raw hex/px live ONLY in the theme module the GUI implements; every screen reads a role.

### Color — semantic roles (dark + light)

| role | purpose | dark | light |
|---|---|---|---|
| `accent` | primary action / selection | `#3b82f6` | `#2563eb` |
| `accent_hover` | accent hover/active | `#5b9bff` | `#1d4ed8` |
| `accent_text` | text/glyph ON an accent fill | `#06080d` | `#ffffff` |
| `text_primary` | body + headings | `#e7eaf0` | `#161a21` |
| `text_secondary` | sub-text / captions / "was:" | `#98a1b2` | `#5a6373` |
| `text_disabled` | disabled / hint | `#586273` | `#a7afbd` |
| `surface` | window background | `#0c0e13` | `#f3f5f8` |
| `surface_raised` | pane / card / modal | `#14171e` | `#ffffff` |
| `surface_sunken` | input wells / list / table | `#090b0f` | `#e9edf2` |
| `surface_hover` | hovered row / tile | `#1a1e27` | `#eef1f6` |
| `surface_selected` | selected list/version row | `#1d2533` | `#dde6f5` |
| `border` | hairline divider / field edge | `#222732` | `#d9dee6` |
| `border_strong` | emphasized border (hover/active) | `#313847` | `#c2c9d4` |
| `focus_ring` | keyboard focus ring | `#3b82f6` | `#2563eb` |
| `dirty` | changed-field marker + "was:" accent | `#fbbf24` | `#d97706` |
| `diff_band` | differing-field row band (compare) | `#2a2410` | `#fdf6e3` |
| `readonly_field` | read-only identity field fill | `#101319` | `#eceff4` |
| `success` | saved / verified | `#34d399` | `#059669` |
| `warning` | can't-resolve / unlinked / nothing-changed | `#fbbf24` | `#d97706` |
| `error` | validation error / write failure / blocked | `#f87171` | `#dc2626` |
| `info` | neutral notice | `#60a5fa` | `#2563eb` |
| `status_active` | entity active | `#34d399` | `#059669` |
| `status_deprecated` | entity deprecated | `#98a1b2` | `#5a6373` |
| `status_superseded` | entity superseded | `#a78bfa` | `#7c3aed` |
| `status_unverified` | row unverified at current version | `#fbbf24` | `#d97706` |

### Type — named scale (factor × `font_size_base`; no raw font px)
One base size (`font_size_base`, default 14 — a dense maintainer tool, not a marketing
page); every step is a factor of it, so one knob rescales all type.

| name | factor | use |
|---|---|---|
| `title` | 1.5 | window / entity title |
| `heading` | 1.15 | section headings (Versions, Lifecycle, Fields) |
| `body` | 1.0 | body text, field labels, list rows |
| `body_strong` | 1.0 | body at weight 600 (selected row, dirty field) |
| `label` | 0.85 | field labels, filter pills, status chips |
| `caption` | 0.8 | "was:" helper, sub-text, validation text |
| `mono` | 1.0 | tabular data — rva, signature, version tags, ids (monospace) |

**Fonts:** UI = the platform default sans (system UI font); data = a bundled monospace
(`mono` scale — every rva / signature / version tag / id / CSV-ish value). The mono font
makes columns align and hex scannable. Font family is a token (swaps without touching
call sites).

### Space — rem rhythm (factor × base × density)
| step | factor | | step | factor |
|---|---|---|---|---|
| `xs` | 0.25 | | `lg` | 1.0 |
| `sm` | 0.5 | | `xl` | 1.5 |
| `md` | 0.75 | | `2xl` | 2.25 |

Pane padding, field gaps, table cell padding, the navigator width all expressed in these.
**Density** = 1.0 but biased compact (small base, `sm`/`md` gaps the norm) — a maintainer
scans dense data. The dirty-marker gutter, the "was:" line, and the validation-error line
each reserve a fixed step of space always (law 1).

### Icon sizing — rem, off the same knob
`icon_sm` 0.875 · `icon_md` 1.0 · `icon_lg` 1.25. Glyph boxes are rem constants — the
interface-size knob rescales icons in lockstep with type + spacing.

### Shape & elevation
Corner-radius steps: `radius_sm` 3px · `radius_md` 5px (the narrow px exception — radii
don't scale with the type ramp). Elevation is flat: hierarchy from `surface` /
`surface_raised` / `surface_sunken` + spacing + hairline `border` — not drop-shadows.
The modal layer gets one soft scrim (`surface` at reduced alpha) + a `surface_raised`
card.

### Motion
Minimal and functional only: a row-selection highlight cross-fade, a history/compare
expander slide, a modal fade-in, a transient status-bar notice fade. **No motion that
moves a resting element** (law 1). Subtle, fast, skippable.

---

## Component silhouettes (each rendered once)

- **search field** — the navigator's filter-as-you-type box.
- **filter control** — a status filter (all / active / deprecated / superseded) + a kind
  filter (all / the nine kinds), as compact dropdowns or pill toggles.
- **entity list row** — name · `kcdx_id` (mono) · status chip; selected / hovered states.
- **status chip** — a small labelled+colored chip (active / deprecated / superseded /
  unverified), color + glyph (never color-alone, law 7).
- **section header** — a heading bar (Versions / Lifecycle / Fields) with an optional
  action affordance.
- **version table row** — version tag (mono) · kind · verified-date · evidence; current /
  selected / differing (compare) states.
- **field row (read-only)** — label + value, the read-only identity treatment (law 7).
- **field row (editable)** — label + input (text well / dropdown) + dirty marker gutter +
  reserved "was: <old>" line + reserved validation-error line.
- **dropdown** — a gated value picker (the `evidence_kind` enum, the `kind` enum, the
  module picker, the supersede-target picker).
- **text well** — a single-line editable field (rva, signature, dates, free text).
- **primary / secondary / ghost / destructive button** — Review changes, Save, Cancel,
  Revert-field, Deprecate.
- **dirty marker** — the changed-field accent gutter mark (`dirty` role + glyph).
- **diff cell** — a compare-table cell whose value differs across the set (`diff_band`
  row + a marker glyph).
- **modal card** — confirm-save delta, new-entity form, dialogs (+ scrim).
- **field-delta list** — the confirm modal's `field: old → new` rows.
- **warning banner** — an inline banner (unlinked DLL, resolver failure, nothing-changed)
  with its override / acknowledge affordance.
- **status-bar segment** — DLL-link state, last-save result, transient notice.

---

## Screen index

| Doc | Screen | Region | Fidelity |
|---|---|---|---|
| [`screens/s01-navigator.md`](screens/s01-navigator.md) | Entity navigator (search/filter/list) | left pane | v1 |
| [`screens/s02-entity-detail.md`](screens/s02-entity-detail.md) | Entity detail (header + lifecycle + version area) | right pane | v1 |
| [`screens/s03-version-history-compare.md`](screens/s03-version-history-compare.md) | Version history + side-by-side compare | right pane | v1 |
| [`screens/s04-field-editor.md`](screens/s04-field-editor.md) | Field editor (view/edit a version row) | right pane | v1 |
| [`screens/s05-create.md`](screens/s05-create.md) | Create new entity / new version | modal + right pane | v1 |
| [`screens/s06-save-confirm.md`](screens/s06-save-confirm.md) | Save confirm — field delta + approval | modal | v1 |
| [`screens/s07-status-dll-link.md`](screens/s07-status-dll-link.md) | Status bar + DLL link / verification context | bottom bar | v1 |

---

## Navigation map

```
s01 navigator ── always present; search + filters + list; [+ New entity ▸ s05]
 │  select an entity ──▶ s02 entity detail (right pane)
 │
 └─ s02 entity detail ── header (identity read-only) + lifecycle (editable: supersede/
        │                 deprecate) + version area
        │  default-select newest row (or DLL-resolved row, s07) ──▶ s04 field editor
        │  [Show history] ──▶ s03 history list
        │  [Compare versions] ──▶ s03 compare (multi-select ▸ N columns side-by-side)
        │  [+ New version] ──▶ s05 (prefilled from a source row)
        │
        ├─ s03 history/compare ── view past versions; a column is editable in place ──▶ s04
        │
        └─ s04 field editor ── view a row; edit (audit trio / full columns); dirty markers
              │  edit an EXISTING version ──▶ confirmation it's an edit, not a new version
              │  [Review changes] ──▶ s06 save-confirm
              │
              └─ s06 save-confirm ── field delta (old → new); approval (s if new row, law 8);
                    [Save] ──▶ one atomic commit ──▶ s07 status "Saved" (or "blocked — Retry")
                    [Cancel] ──▶ back to s04, nothing lands

s07 status bar ── always present; DLL-link state + last result + notices; floats nothing,
     navigates nothing (law 3); [Link DLL…] sets the verification context (s07)
```

---

## Per-screen doc template

Each `screens/sNN-*.md` uses this skeleton so the set stays comparable:

1. **Phase & fidelity** — v1/high, or sketch.
2. **Purpose / when shown.**
3. **Region & position.**
4. **Contents** — table: *Element → Component → Data bound → Intent emitted*.
5. **States & variants** — populated / empty / loading / error / disabled / edge.
6. **Links in / out** — the local slice of the nav map.
7. **Applicable laws** — which numbered laws bite here.
