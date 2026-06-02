# s02 — Entity detail (header · lifecycle · version area)

**Phase & fidelity:** v1, high.

## Purpose / when shown
The right pane, shown when an entity is selected in s01. Presents the whole entity the
maintainer is managing: its identity, its editable lifecycle (supersede / deprecate —
Jobs 4/5), and the entry point to its version rows (current row, history, compare, new
version). The single-row field editing lives in s04; history/compare in s03.

## Region & position
The right pane of the skeleton. Vertically stacked sections: **header** (identity, fixed
top) → **lifecycle** (editable flags) → **version area** (current row + the history /
compare / new-version actions). The version area's field editor (s04) renders inline
below the version table. The whole pane scrolls if content exceeds height; section
positions are stable (law 1).

## Contents
| Element | Component | Data bound | Intent emitted |
|---|---|---|---|
| Entity title | `title` text | `name` · `kcdx_id` (mono) | — (read-only, law 7) |
| Identity block | `field row (read-only)` ×2 | `kcdx_id`, `name` | — (never editable, law 7) |
| **Lifecycle — supersede** | `field row (editable)` | `superseded_by` (`dropdown`, target entity) + `superseded_at_version` | `edit_lifecycle(...)` → s06 |
| **Lifecycle — deprecate** | `field row (editable)` | `is_deprecated` (toggle) + `deprecated_at_version` + `deprecation_replacement` (`dropdown`) | `edit_lifecycle(...)` → s06 |
| `notes` | `text well` | `notes` (entity-level) | `edit_lifecycle(...)` → s06 |
| Version table | `version table row` ×N (newest first) | the entity's `address_versions` rows | `select_version(valid_from)` → s04 |
| `[Show history]` | `ghost button` | toggles full-history vs current-row-only | `toggle_history()` → s03 |
| `[Compare versions]` | `ghost button` | enters compare multi-select | `open_compare()` → s03 |
| `[+ New version]` | `secondary button` | — | `open_new_version(source=selected_row)` → s05 |
| Selected-row editor | `s04 field editor` (inline) | the selected version row | (see s04) |

The version table defaults to the **current/default row selected**: the DLL-resolver-
matched row when a DLL is linked (s07), else the newest authored row (highest
`valid_from_version`). The current row carries a "current" marker; with a linked DLL the
resolver-matched row also carries a "matches linked DLL `<version>`" marker.

**Lifecycle pair-integrity** (`policy.md`, enforced by the shared validator — law 6):
supersede sets `superseded_by` AND `superseded_at_version` together (both-or-neither, no
self-supersede, no cycle); deprecate sets `is_deprecated` AND `deprecated_at_version`
together; `deprecation_replacement` allowed only when deprecated. The UI renders the
paired fields together and the validator's verdict inline; a partial pair is a validation
error (s04 pattern), never a write.

## States & variants
- **Populated** — header + lifecycle + the version table with the default row selected and
  its s04 editor inline below.
- **Empty (no entity selected)** — before a selection: the pane shows a neutral prompt
  *"Select an entity from the list to view and edit it."* (not a blank pane).
- **Loading** — an entity's version rows loading (fast; brief indication in the version
  area).
- **Error (entity load failed)** — *"Couldn't load entity `<name>`: `<reason>`."* + retry.
- **Disabled** — lifecycle edits on an already-superseded entity follow `policy.md` (the
  successor chain); the validator gates an illegal transition with an inline error, the
  control is not silently hidden.
- **Edge content** — a long `signature` / `notes` wraps within its field without pushing
  siblings (law 1); an entity with many version rows scrolls the version table, header
  stays fixed; a deprecated/superseded entity shows its state prominently in the header.

## Links in / out
- **In:** s01 `select_entity`.
- **Out:** `select_version` → s04; `toggle_history` / `open_compare` → s03; `+ New version`
  → s05; any lifecycle edit → s06 (save-confirm); a new entity arrives here after s05.

## Applicable laws
- **Law 1** — sections + the inline editor never reflow on selection/dirty/error.
- **Law 2** — the pane persists; edits confirm via the s06 modal, not a pane swap.
- **Law 3** — selecting a version row / opening a mode is user-driven; a resolver result
  (s07) updates the "current"/"matches DLL" markers in place, never re-selects for the user.
- **Law 6** — lifecycle pair-integrity is the validator's, rendered inline.
- **Law 7** — identity fields read-only, non-color affordance.
- **Law 8** — lifecycle edits are UPDATEs (not approval-gated); only new entity/version are.
- **Law 9** — tokens only.
