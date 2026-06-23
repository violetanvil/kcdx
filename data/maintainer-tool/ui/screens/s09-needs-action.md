# s09 — Needs-action view (the lifecycle-completeness surface · incomplete entities grouped by kind)

## Phase & fidelity
v1 / high.

## Purpose / when shown
A standing, tool-wide surface listing every entity whose lifecycle is INCOMPLETE at the current
game version — the version-relative integrity gaps the write-time structural checks cannot catch.
It exists so no entity is ever left silently incomplete: an entity uncovered at the
current version (a closed interval with no successor, not deprecated/superseded), a row never
signed off (`last_verified_at_version` NULL), or a dangling deprecation/supersession reference.
The maintainer opens it deliberately from the s01 navigator to see "what needs a decision" and
resolves each via its existing canonical flow. The screen does NOT resolve anything itself — it
DETECTS incompleteness (a data-core query over the current DB state) and ROUTES each gap to the
canonical editor (s02 / s04 / s05); the resolution lands through the existing validated write
path (the shared validator is the single gate).

## Region & position
The main content area (right pane on wide; a drilled-in full view on phone), peer to s02/s08 —
NOT an overlay (a maintenance-review session is a working surface, not a dim-and-dismiss overlay;
the navigation shell persists, only an overlay layer covers content). Reached from s01's
`[Needs action ▸ N]` affordance (a count badge showing the number of
incomplete entities) — a **top-level destination that RESETS the content back-stack** to a fresh
root at s09. The app shell persists around it. A row's resolve-action **PUSHES**
the resolve screen (s02/s04/s05) onto the stack, so `‹ back` from there returns to THIS
s09 view with its state intact (section toggles + scroll preserved). At s09's own stack root there
is no `‹ back` (the navigator is the way out on wide; on phone `‹ back` returns to the navigator
home).

## Contents
| Element | Component (Mantine) | Data bound | Intent emitted |
|---|---|---|---|
| `[Needs action ▸ N]` (entry) | `button` (default) + `Badge` count — lives in s01 | the needs-action entity count (the detection query's total) | `open_needs_action()` → s09 |
| Header | `section header` | `Needs action · N entities` + `at version <V>` (the current game version the incompleteness is measured against) | — (status, read-only) |
| Kind section: **Uncovered at current version** | `collapsible section` (the s08 no-action-block primitive) | the orphan set (`N` entities: a closed interval, no successor covering `V`, `is_deprecated=0`, `superseded_by` NULL) | `toggle_section('uncovered')` |
| Kind section: **Never verified** | `collapsible section` | the never-verified set (`N` rows: `last_verified_at_version` NULL) | `toggle_section('never_verified')` |
| Kind section: **Broken references** | `collapsible section` | the dangling-reference set (`N` entities: `deprecation_replacement` / `superseded_by` pointing at a nonexistent or itself-incomplete entity) | `toggle_section('broken_refs')` |
| Entity row (uncovered) | `entity row` | `kcdx_id` · `name` (mono) · the gap (`closed · no successor at <V>`) | `select_entity(kcdx_id)` (the row body → s02) |
| Row action `[Author successor ▸]` | `button` (primary, subtle) | the uncovered row | `author_successor(kcdx_id)` → s05 create-new-version (prefilled from the closed row) |
| Row action `[Deprecate ▸]` | `button` (subtle) | the uncovered row | `deprecate_entity(kcdx_id)` → s02 lifecycle editor |
| Row action `[Supersede ▸]` | `button` (subtle) | the uncovered row | `supersede_entity(kcdx_id)` → s02 lifecycle editor |
| Entity row (never-verified) | `entity row` | `kcdx_id` · `name` (mono) · `never signed off` | `select_entity(kcdx_id)` → s02 |
| Row action `[Verify ▸]` | `button` (primary, subtle) | the never-verified row | `verify_row(kcdx_id)` → s04 field editor (the verify surface) |
| Entity row (broken-ref) | `entity row` | `kcdx_id` · `name` (mono) · the dangling pointer (`<field> → <missing/incomplete target>`) | `select_entity(kcdx_id)` → s02 |
| Row action `[Fix reference ▸]` | `button` (primary, subtle) | the broken-ref row | `fix_reference(kcdx_id)` → s02 lifecycle editor |

Every per-row action **PUSHES** the EXISTING canonical resolve flow (s02 / s04 / s05) onto the
content back-stack — s09 reimplements no editing surface (the single validator/editor is the gate).
A `‹ back` returns from the resolve flow to s09 with its full state restored (the section toggles +
scroll preserved). A resolved entity drops off the s09 list IN PLACE on return — the view never
auto-navigates on a resolution.

## States & variants
- **Populated** — the header (`Needs action · N entities · at version <V>`) + the three kind
  sections, each with its count `(N)`. A section with `N > 0` is expanded by default (it is the
  work surface); a section with `N = 0` renders collapsed with a `0` count + a muted
  *"none"* — surfaced, never hidden (never a silent loss). Sections order:
  Uncovered first (the most consequential — an entity that resolves to nothing at the current
  version), then Never verified, then Broken references.
- **Empty (all-clear — the GOAL state, success-framed)** — when `N = 0` across every kind, the
  view shows a positive all-clear, NOT a neutral empty pane: *"Every entity's lifecycle is
  complete at version `<V>` — nothing needs action."* rendered in the `success` token (it is an
  accomplishment, the data is fully reconciled — distinct from a "no data" empty). The s01
  navigator's `[Needs action]` affordance shows `0` / is un-badged.
- **Loading** — the detection query in flight: a brief progress indication in the content region
  (the same fetch-in-flight indicator the s01 navigator uses), resolving in place to the populated
  or all-clear state (stable layout).
- **Error (detection query failed)** — the API/backend raised computing the needs-action set:
  *"Couldn't compute the needs-action set: `<reason>`."* (system-caused copy naming the cause —
  the backend's reason, not a raw trace) + a retry affordance. The shell persists; nothing is
  silently dropped.
- **Disabled** — a per-row action is disabled (not hidden, conveyed by more than color)
  when the resolution flow it routes to is unavailable in the current degraded state (e.g. the DB
  read seam is down). A degraded `[Author successor]` is unavailable + noted, never silently
  removed.
- **Back affordance** — when s09 sits at stack depth > 1 (rare for s09, which is usually a
  reset-to-root top-level destination, but possible if a flow pushed it), a `‹ back to <destination>`
  control shows top-left of the content pane in reserved space (no reflow); at s09's root
  (the common case) there is no `‹ back` (the navigator is the way out). After a resolve-action
  pushes s02/s04/s05 and the maintainer returns, s09's section toggles + scroll are restored exactly.
- **Edge content** — a long entity `name` / a long dangling-pointer detail wraps within its cell
  without pushing siblings (stable layout); a very long needs-action list scrolls the content region
  while the header pins (stable layout); on phone the view is a full-screen drill-in (a kind section
  is a full-width collapsible).

## Links in / out
- **In:** s01 navigator `[Needs action ▸ N]` → s09 (the only entry; the standing surface is
  reached deliberately from home).
- **Out:** a row body `select_entity` → s02 entity detail; `[Author successor]` → s05
  create-new-version (prefilled); `[Deprecate]` / `[Supersede]` / `[Fix reference]` → s02
  lifecycle editor; `[Verify]` → s04 field editor. Each out-link **PUSHES** the resolve screen
  onto the back-stack, so `‹ back` returns to s09 with its state intact (the resolved
  entity drops off on return). At s09's stack root, `‹ back` → the navigator (the
  reset-to-s09 entry came from the navigator).

## Applicable interaction laws
- **Stable layout** — layout stable across state changes: a resolved entity dropping off the list
  reflows content in place; the header pins; no element jumps on a section toggle or a resolution.
- **Persistent shell** — the shell persists; s09 is a peer CONTENT screen (not an overlay); a
  resolve flow it routes to (s02/s04/s05) replaces the content area via the back-stack.
- **Content back-stack** — opening s09 from s01 RESETS the stack to a fresh s09 root;
  a row's resolve-action PUSHES the resolve screen (state-carrying frame); `‹ back` returns to s09
  with its full state restored (section toggles + scroll). The `‹ back` affordance sits top-left of
  the content pane at stack depth > 1, labeled with its destination; absent at s09's root.
- **User-driven navigation** — navigation is user-action-driven: the view changes on a maintainer
  action (open / toggle / resolve); a completed resolution updates the list IN PLACE on return — it
  never auto-navigates or drills for the user.
- **Advisory verification** — verification is advisory; the maintainer is final authority: s09
  SURFACES the incompleteness (it never blocks or auto-resolves); a `0`-count kind section is shown
  (none silently dropped).
- **Single validator** — the shared validator/editor is the single gate: s09 routes every
  resolution to the canonical s02/s04/s05 editor + its validated write path — it reimplements no
  editing surface and no validation.
- **Non-color affordance** — a disabled per-row action is conveyed by more than color (an
  icon/label affordance).
- **Semantic tokens only** — no raw values at a call site: every color/size/spacing/font resolves
  to a semantic token (the `success` all-clear, the `warning`-toned gap markers, the
  `collapsible section` primitive) — never a hex, px, or literal.

## Responsive behavior
Wide: the right content pane (peer to s02/s08), the s01 navigator persisting at left. Phone: a
full-screen drill-in reached from the navigator's `[Needs action ▸ N]`, each kind section a
full-width collapsible; `‹ back` (when present) returns to the screen that pushed s09, or
to the navigator home at s09's root. The header (`Needs action · N · at version
<V>`) pins; the kind sections scroll beneath it (stable layout).
