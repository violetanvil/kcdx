# s01 — Entity navigator (search · filter · list)

**Phase & fidelity:** v1, high.

## Purpose / when shown
How the maintainer finds one entity among the ~143 curated ones, sees each entity's
lifecycle status at a glance, and starts a new entity. On wide screens it is the persistent
left pane; on phone it is the **home view** (the screen you land on). The entry point —
initial focus lands on its search field.

## Region & position
The navigator of the app shell. **Wide:** the left pane,
fixed minimum legible width; below that width the divider stops and the detail pane scrolls
(not the navigator collapsing — the navigation shell persists, only an overlay layer covers
the content area). **Phone:** the full-screen home view. Vertically:
search + filters fixed at top, the list scrolls beneath.

## Contents
| Element | Component (Mantine) | Data bound | Intent emitted |
|---|---|---|---|
| Search box | `search field` (`TextInput`) | filters the list by name OR `kcdx_id` substring, as-you-type | `filter_entities(query)` (local, no write) |
| Status filter | `filter control` (`Select`/`SegmentedControl`) | all / active / deprecated / superseded (status is derived, never an authored column) | `set_status_filter(value)` |
| Kind filter | `filter control` (`Select`) | all / the nine `kind` values (the entity's current-row kind) | `set_kind_filter(value)` |
| Entity row | `entity list row` (`NavLink`/row) | `name` · `kcdx_id` (mono) · `status chip` | `select_entity(kcdx_id)` → s02 |
| `+ New entity` | `button` (primary) | — | `open_new_entity()` → s05 (overlay) |
| `[Needs action ▸ N]` | `button` (default) + `Badge` count | the needs-action entity count (the lifecycle-completeness detection query's total; un-badged at `0`) | `open_needs_action()` → s09 (the standing lifecycle-completeness surface) |

The status chip (`Badge`) derives from the entity's lifecycle flags + current-version
verification (`status_active` / `status_deprecated` / `status_superseded` /
`status_unverified`), conveyed by glyph + text, never color alone. The list is fetched from
the backend API (the data-core's curated set); the frontend holds no SQL/rule logic — the
shared validator is the single gate and the frontend never reimplements a rule.

## States & variants
- **Populated** — the filtered, scrollable list; the selected row uses `surface_selected` +
  `body_strong`; hover uses `surface_hover`.
- **Empty (no DB resolved)** — the API reported the backend could not resolve the
  reference DB at its configured checkout path. Copy:
  *"No reference DB found. The server couldn't load the Address Library from its configured
  checkout. Check the mounted volume / the configured path."* (system-caused — copy
  disambiguates by cause; the maintainer-facing string names the operator-side cause without
  exposing a path the browser can't act on.)
- **Empty (no search match)** — the list area shows *"No entity matches '`<query>`'."* with
  a clear-search affordance (user-caused — different copy from the no-DB case).
- **Loading** — the initial API fetch in flight: a brief progress indication
  (`Loader`/skeleton rows) in the list area. The search/filters render disabled until loaded.
- **Error (load failed)** — the API/backend raised: *"Couldn't load the reference DB:
  `<reason>`."* + a retry affordance. The backend's reason is shown, not a raw trace.
- **Edge content** — a long entity name truncates with an ellipsis (full name on hover /
  in s02); the `kcdx_id` column is fixed-width mono so ids align regardless of name length;
  the list virtualizes/scrolls smoothly at ~143+ rows (and on a phone viewport).

## Links in / out
- **In:** the app's initial state (the phone home view); returning from any overlay or
  (phone) drilling back from s02.
- **Out:** `select_entity` → s02 detail (drill-down on phone); `+ New entity` → s05 create
  (overlay).

## Applicable interaction laws
- **Persistent shell** — the navigator persists as the navigation shell; only the overlay layer
  covers content.
- **User-driven navigation** — selecting/tapping a row is a user action that drives navigation; a
  background event (load completing, a status re-derivation) updates the list/chips in place, never
  auto-selects a row.
- **Non-color affordance** — status chips + any read-only treatment convey by more than color.
- **Semantic tokens only** — all color/size/spacing via tokens.

## Responsive behavior
- **Wide:** the left pane; selecting an entity fills the right detail pane (the navigator
  stays visible, selection persists).
- **Phone:** the full-screen home view; tapping an entity **drills** to s02 full-screen (a
  `‹ back` returns here, selection + scroll position preserved — layout stays stable and the
  return is user-driven). `+ New entity` opens s05 as a full-screen sheet.
