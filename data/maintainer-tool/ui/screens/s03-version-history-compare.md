# s03 — Version history + side-by-side compare

**Phase & fidelity:** v1, high.

## Purpose / when shown
Shown in s02's version area when the maintainer opens **Show history** or **Compare
versions** (the detail region on wide screens; within the full-screen drill-down on phone).
Lets them see all of an entity's past game-version rows, and compare two or more of them
side-by-side with the differing fields clearly marked — and edit any version directly from
there. "Version" here = a game-version `address_versions` row (a data row), NOT a git
revision; the compare diffs the DB rows dynamically, never commit history.

## Region & position
The detail region's version area (below the s02 header/lifecycle). History is an expanded
version table (`Table`); compare replaces the single-row s04 editor with an N-column
`Table`. The region scrolls vertically; the compare table scrolls **horizontally** within a
`ScrollArea` when the column count × content width exceeds the available width (dynamic —
fits until it doesn't; the stable layout keeps non-compare elements fixed). On phone the
compare table's horizontal scroll is the primary way 2–3 version columns fit a narrow viewport.

## Contents

### History (Show history)
| Element | Component (Mantine) | Data bound | Intent emitted |
|---|---|---|---|
| Version row | `version table row` (`Table` row) ×N (newest first) | each `address_versions` row: version tag (mono) · `kind` · `verified_date` · `evidence_kind` | `select_version(valid_from)` → s04 |
| `[Hide history]` | `button` (subtle) | collapses to current-row-only | `toggle_history()` |
| `[Compare versions]` | `button` (subtle) | enters multi-select | `open_compare()` |

### Compare (Compare versions)
| Element | Component (Mantine) | Data bound | Intent emitted |
|---|---|---|---|
| Row select checkbox | `Checkbox` per `version table row` | the set to compare (≥2) | `toggle_compare_select(valid_from)` |
| `[Compare (N)]` | `button` (primary) | the selected set | `show_compare(set)` |
| Field-name gutter | `field row (read-only)` labels (`Table` left column) | the union of version-row columns | — |
| Version column | `diff cell` ×fields, per selected version (`Table` column) | each version's full column set | `select_version_column(valid_from)` → enters edit (s04) |
| Diff marker | marker glyph + `diff_band` row | a field whose value differs across the compared set | — |
| `[Edit <version>]` | `button` (default) per column | — | `edit_version(valid_from)` → s04 (with the edit-existing confirmation) |
| `[Exit compare]` | `button` (subtle) | back to history/current | `close_compare()` |

**Diff marking:** every field is listed for every compared version (full records visible);
a field whose values DIFFER across the set gets a marker glyph + a `diff_band` row band;
identical fields render plain (band is a semantic token, marker is the non-color signal —
the diff is conveyed by more than color). The full record stays visible so the eye jumps to
changes without losing context.

**Edit from compare:** a column's `[Edit]` opens that version in the s04 editor. Because
it is an EXISTING decided version, s04 raises its **edit-existing confirmation** ("You are
editing an existing version `<v>`, not creating a new one") before fields become editable
(the confirm discipline applies on the edit boundary, not only at save).

## States & variants
- **Populated (history)** — the full version list, newest first, current row marked.
- **Populated (compare)** — the N-column diff table; ≥2 columns; differing fields banded.
- **Empty (one version)** — most entities have a single `address_versions` row today:
  history shows the one row with a note *"This entity has one version."*; Compare is
  disabled with a tooltip *"Need at least two versions to compare."* (edge: zero/one/many
  handled).
- **Loading** — version rows loading (brief).
- **Error** — a row failed to load: the row shows *"Couldn't load version `<v>`."*; the
  others still render.
- **Disabled** — `[Compare]` disabled until ≥2 rows are selected; copy names why.
- **Edge content** — many versions → horizontal scroll engages dynamically; a long
  `signature` wraps within its cell, the column width holds (stable layout); 3+ compared
  columns scroll rather than shrinking below legibility.

## Links in / out
- **In:** s02 `toggle_history` / `open_compare`.
- **Out:** `select_version` / `edit_version` → s04; an edit → s06 (save-confirm);
  `[+ New version]` (carried from s02) → s05.

## Applicable interaction laws
- **Stable layout** — entering compare expands the version area in place; non-compare sections
  don't move; horizontal scroll, never column collapse that reflows.
- **User-driven navigation** — entering history/compare and selecting columns are user actions.
- **Confirmed edit boundary** — editing an existing version from compare crosses the confirm
  boundary.
- **Non-color affordance** — diff marker is a glyph, not color-alone; read-only identity columns
  hold the read-only treatment.
- **Semantic tokens only** — `diff_band` + all dims via tokens.

## Responsive behavior
- **Wide:** the compare table fits 2–3 columns inline; more scroll horizontally within the
  detail pane.
- **Phone:** within the full-screen drill-down; the compare table's `ScrollArea` horizontal
  scroll is the primary fit mechanism (the field-name gutter stays sticky-left so the
  maintainer keeps the field labels while scrolling version columns). `[Edit <version>]`
  opens s04 as a full-screen sheet (the edit-existing confirmation precedes it).
