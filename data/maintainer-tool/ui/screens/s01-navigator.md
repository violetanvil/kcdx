# s01 — Entity navigator (search · filter · list)

**Phase & fidelity:** v1, high.

## Purpose / when shown
The left pane, always present. How the maintainer finds one entity among the ~143 curated
ones, sees each entity's lifecycle status at a glance, and starts a new entity. The entry
point — initial focus lands on its search field.

## Region & position
The left pane of the window skeleton (`../design.md` §"Window skeleton"). Fixed minimum
legible width; below it the divider stops and the detail pane scrolls (not the navigator
collapsing — law 2). Vertically: search + filters fixed at top, the list scrolls beneath.

## Contents
| Element | Component | Data bound | Intent emitted |
|---|---|---|---|
| Search box | `search field` | filters the list by name OR `kcdx_id` substring, as-you-type | `filter_entities(query)` (local, no write) |
| Status filter | `filter control` | all / active / deprecated / superseded (status derived per `policy.md` §"Status is NOT an authored column") | `set_status_filter(value)` |
| Kind filter | `filter control` | all / the nine `kind` values (the entity's current-row kind) | `set_kind_filter(value)` |
| Entity row | `entity list row` | `name` · `kcdx_id` (mono) · `status chip` | `select_entity(kcdx_id)` → s02 |
| `+ New entity` | `primary button` | — | `open_new_entity()` → s05 (modal) |

The status chip derives from the entity's lifecycle flags + current-version verification
(`status_active` / `status_deprecated` / `status_superseded` / `status_unverified`),
color + glyph (law 7 — never color-alone).

## States & variants
- **Populated** — the filtered, scrollable list; the selected row uses
  `surface_selected` + `body_strong`; hover uses `surface_hover`.
- **Empty (no DB/seeds resolved)** — the navigator shows a message naming WHERE the tool
  looked (`<exe-dir>/../seeds/` + the DB path) and what to do — not a blank list. Copy:
  *"No reference DB found. Looked in `<exe-dir>/../seeds/`. Place the seed files there, or
  check the working directory."* (system-caused — law: copy disambiguates by cause.)
- **Empty (no search match)** — the list area shows *"No entity matches '`<query>`'."*
  with a clear-search affordance (user-caused — different copy from the no-DB case).
- **Loading** — the one-shot data-core load in flight: a brief progress indication in the
  list area (the load is not a hot path). The search/filters render disabled until loaded.
- **Error (load failed)** — the data-core raised: *"Couldn't load the reference DB:
  `<reason>`."* + a retry affordance. The validator/loader's reason is shown, not a raw
  trace.
- **Edge content** — a long entity name truncates with an ellipsis (full name on hover/in
  s02); the `kcdx_id` column is fixed-width mono so ids align regardless of name length;
  the list virtualizes/scrolls smoothly at ~143+ rows.

## Links in / out
- **In:** the app's initial state; returning from any modal.
- **Out:** `select_entity` → s02 detail; `+ New entity` → s05 create (modal).

## Applicable laws
- **Law 2** — the navigator persists in every state; only modals overlay.
- **Law 3** — selecting a row is a user action that drives navigation; a background event
  (load completing, a status re-derivation) updates the list/chips in place, never
  auto-selects a row.
- **Law 7** — status chips + any read-only treatment convey by more than color.
- **Law 9** — all color/size/spacing via tokens.
