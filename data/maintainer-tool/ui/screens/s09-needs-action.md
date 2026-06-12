# s09 — Needs-action view (the lifecycle-completeness surface · incomplete entities grouped by kind)

## Phase & fidelity
v1 / high. TRD authority: [`../../design.md`](../../design.md) **D41** (entity-lifecycle
completeness + report-vs-DB reconciliation — the standing needs-action view) + policy.md
§"Status is derived" (the derived-status rules the incompleteness is measured against).

## Purpose / when shown
A standing, tool-wide surface listing every entity whose lifecycle is INCOMPLETE at the current
game version — the version-relative integrity gaps the write-time structural checks cannot catch
(TRD D41). It exists so no entity is ever left silently incomplete: an entity uncovered at the
current version (a closed interval with no successor, not deprecated/superseded), a row never
signed off (`last_verified_at_version` NULL), or a dangling deprecation/supersession reference.
The maintainer opens it deliberately from the s01 navigator to see "what needs a decision" and
resolves each via its existing canonical flow. The screen does NOT resolve anything itself — it
DETECTS incompleteness (a data-core query over the current DB state) and ROUTES each gap to the
canonical editor (s02 / s04 / s05); the resolution lands through the existing validated write
path (law 6).

## Region & position
The main content area (right pane on wide; a drilled-in full view on phone), peer to s02/s08 —
NOT an overlay (a maintenance-review session is a working surface, not a dim-and-dismiss overlay,
law 2). Reached from s01's `[Needs action ▸ N]` affordance (a count badge showing the number of
incomplete entities). The app shell persists around it (law 2); `‹ back` (phone) returns to s01.

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

Every per-row action NAVIGATES to the EXISTING canonical resolve flow (s02 / s04 / s05) — s09
reimplements no editing surface (law 6, the single validator/editor). A `‹ back` returns from the
resolve flow to s09 (law 2/3, the report/list state preserved). A resolved entity drops off the
s09 list IN PLACE on return — the view never auto-navigates on a resolution (law 3).

## States & variants
- **Populated** — the header (`Needs action · N entities · at version <V>`) + the three kind
  sections, each with its count `(N)`. A section with `N > 0` is expanded by default (it is the
  work surface); a section with `N = 0` renders collapsed with a `0` count + a muted
  *"none"* — surfaced, never hidden (law 4 / no silent-success). Sections order:
  Uncovered first (the most consequential — an entity that resolves to nothing at the current
  version), then Never verified, then Broken references.
- **Empty (all-clear — the GOAL state, success-framed)** — when `N = 0` across every kind, the
  view shows a positive all-clear, NOT a neutral empty pane: *"Every entity's lifecycle is
  complete at version `<V>` — nothing needs action."* rendered in the `success` token (it is an
  accomplishment, the data is fully reconciled — distinct from a "no data" empty). The s01
  navigator's `[Needs action]` affordance shows `0` / is un-badged.
- **Loading** — the detection query in flight: a brief progress indication in the content region
  (the same fetch-in-flight indicator the s01 navigator uses), resolving in place to the populated
  or all-clear state (law 1).
- **Error (detection query failed)** — the API/backend raised computing the needs-action set:
  *"Couldn't compute the needs-action set: `<reason>`."* (system-caused copy naming the cause —
  the backend's reason, not a raw trace) + a retry affordance. The shell persists; nothing is
  silently dropped (law 4).
- **Disabled** — a per-row action is disabled (not hidden, conveyed by more than color — law 7)
  when the resolution flow it routes to is unavailable in the current degraded state (e.g. the DB
  read seam is down). A degraded `[Author successor]` is unavailable + noted, never silently
  removed.
- **Edge content** — a long entity `name` / a long dangling-pointer detail wraps within its cell
  without pushing siblings (law 1); a very long needs-action list scrolls the content region while
  the header pins (law 1); on phone the view is a full-screen drill-in (a kind section is a
  full-width collapsible).

## Links in / out
- **In:** s01 navigator `[Needs action ▸ N]` → s09 (the only entry; the standing surface is
  reached deliberately from home, law 3).
- **Out:** a row body `select_entity` → s02 entity detail; `[Author successor]` → s05
  create-new-version (prefilled); `[Deprecate]` / `[Supersede]` / `[Fix reference]` → s02
  lifecycle editor; `[Verify]` → s04 field editor. Each out-link carries a return path back to
  s09 (the resolved entity drops off on return — law 2/3); `‹ back` (phone) → s01.

## Applicable laws
- **Law 1** — layout stable across state changes: a resolved entity dropping off the list reflows
  content in place; the header pins; no element jumps on a section toggle or a resolution.
- **Law 2** — the shell persists; s09 is a peer CONTENT screen (not an overlay); a resolve flow
  it routes to (s02/s04/s05) replaces the content area, and `‹ back` returns to s09.
- **Law 3** — navigation is user-action-driven: the view changes on a maintainer action
  (open / toggle / resolve); a completed resolution updates the list IN PLACE on return — it never
  auto-navigates or drills for the user.
- **Law 4** — verification is advisory; the maintainer is final authority: s09 SURFACES the
  incompleteness (it never blocks or auto-resolves); a `0`-count kind section is shown (none
  silently dropped).
- **Law 6** — the shared validator/editor is the single gate: s09 routes every resolution to the
  canonical s02/s04/s05 editor + its validated write path — it reimplements no editing surface and
  no validation.
- **Law 7** — a disabled per-row action is conveyed by more than color (an icon/label affordance).
- **Law 9** — no raw values at a call site: every color/size/spacing/font resolves to a semantic
  token (the `success` all-clear, the `warning`-toned gap markers, the `collapsible section`
  primitive) — never a hex, px, or literal.

## Responsive behavior
Wide: the right content pane (peer to s02/s08), the s01 navigator persisting at left. Phone: a
full-screen drill-in reached from the navigator's `[Needs action ▸ N]`, each kind section a
full-width collapsible, `‹ back` returning to s01. The header (`Needs action · N · at version
<V>`) pins; the kind sections scroll beneath it (law 1).
