# s02 — Entity detail (header · version & verify surface · lifecycle · version area)

**Phase & fidelity:** v1, high.

## Purpose / when shown
Shown when an entity is selected/tapped in s01 (the right pane on wide screens; a
full-screen drill-down on phone). Presents the whole entity the maintainer is managing: its
identity, the **version & verify surface** (which game version an edit targets — the
dissolved desktop status bar's content now lives here), its editable lifecycle (supersede /
deprecate — Jobs 4/5), and the entry point to its version rows (current row, history,
compare, new version). Single-row field editing lives in s04; history/compare in s03.

## Region & position
The detail region of the app shell. Vertically stacked sections: **header** (identity +
the version & verify surface, fixed top) → **lifecycle** (editable flags) → **version area**
(current row + the history / compare / new-version actions). The version area's field editor
(s04) renders inline below the version table. The region scrolls if content exceeds height;
section positions are stable (law 1). On phone the header carries the `‹ back` affordance to
s01.

## Contents
| Element | Component (Mantine) | Data bound | Intent emitted |
|---|---|---|---|
| Entity title | `title` text | `name` · `kcdx_id` (mono) | — (read-only, law 7) |
| Identity block | `field row (read-only)` ×2 | `kcdx_id`, `name` | — (never editable, law 7) |
| **Version & verify surface** | `version & verify surface` (`Select` version dropdown + the per-module DLL link table) | the targeted game version (a `game_versions` tag); the per-module linked DLL + resolved version; the advisory verify state | `pick_version(tag)` / `link_dll(module)` / `repick_dll(module)` (client-side, D15/D24–D30) |
| Per-module link rows | `per-module link row` ×M (one per module) | each module's linked DLL filename + resolved version + version-match indicator | `link_dll(module)` / `repick_dll(module)` |
| Link-to-create prompt | `warning banner` (advisory, info) | shown when a linked DLL's version is uncovered by the entity's rows | `open_new_version(source=selected_row, version=<dll_version>)` → s05 |
| **Lifecycle — supersede** | `field row (editable)` (`Select` + version) | `superseded_by` (target entity) + `superseded_at_version` | `edit_lifecycle(...)` → s06 |
| **Lifecycle — deprecate** | `field row (editable)` (`Checkbox` + version + `Select`) | `is_deprecated` + `deprecated_at_version` + `deprecation_replacement` | `edit_lifecycle(...)` → s06 |
| `notes` | `text well` (`TextInput`/`Textarea`) | `notes` (entity-level) | `edit_lifecycle(...)` → s06 |
| Version table | `version table row` ×N (newest first) | the entity's `address_versions` rows | `select_version(valid_from)` → s04 |
| `[Show history]` | `button` (subtle) | toggles full-history vs current-row-only | `toggle_history()` → s03 |
| `[Compare versions]` | `button` (subtle) | enters compare multi-select | `open_compare()` → s03 |
| `[+ New version]` | `button` (default) | — | `open_new_version(source=selected_row)` → s05 |
| `‹ back` (phone) | `drill-down back` | — | `back_to_list()` → s01 |
| Selected-row editor | `s04 field editor` (inline) | the selected version row | (see s04) |

**The version & verify surface (the relocated s07 + the verification engine, TRD D24–D31).**
A **version dropdown** (`Select`, populated from the server-known `game_versions` tags) is the
default way the maintainer states which version an edit targets; the picked version
default-selects/marks the matching row.

Beneath it, a **per-module DLL link table** (`per-module link row` ×M — one per module the
entity's rows reference; today only `WHGame.dll`). Each row lets the maintainer **link** a
local DLL for that module (browser File API — the DLL is read in-page, **never uploaded**, TRD
D15/D26) and shows the linked DLL's filename + its **resolved version** (the `.rdata` version
scan) + a **version-match indicator** (✓ matches the selected row's version / ≠ no match —
glyph+text, law 7). The link is **re-picked each session** (no persisted path — TRD D30); a
`[re-pick]` swaps the DLL.

Linking a version-matching DLL is what enables the **per-author static verification check**
(the per-kind survival check against the DLL's bytes) — whose verdict renders inline in **s04**
(the field editor, where the `rva` / `signature` being checked are authored), NOT here. The
link table is the verification *context*; the per-row verdict lives where the row is authored.

**Link-to-create (TRD D30).** When a linked DLL resolves to a version **not covered** by any of
the entity's `address_versions` rows (a build newer than the DB knows), the surface shows an
inline advisory prompt (`warning banner`): *"`<dll>` is `<version>` — this entity has no row for
it. [Add a version row at `<version>`]"*. The maintainer clicks (a user action, law 3 — never
auto-opened) → the s05 create-version overlay, **prefilled at the DLL's version**
(`valid_from_version = <dll version>`) → author `rva`/`signature` → the check runs against the
linked DLL → on a passing check the audit trio auto-fills (TRD D29) → save (AP18 confirm, law 8).

**Advisory, never required** (law 4): with just a picked version (or none → newest-row default —
TRD D10), every flow proceeds; an unresolved/unverified/Changed/Ambiguous state warns and is
overridable downstream (s06); no matching DLL linked → the check is unavailable + noted (a
degraded state — TRD D30), authoring still proceeds.

**Lifecycle pair-integrity** (`policy.md`, enforced by the shared validator — law 6):
supersede sets `superseded_by` AND `superseded_at_version` together (both-or-neither, no
self-supersede, no cycle); deprecate sets `is_deprecated` AND `deprecated_at_version`
together; `deprecation_replacement` allowed only when deprecated. The UI renders the paired
fields together and the validator's verdict inline; a partial pair is a validation error
(s04 pattern), never a write.

## States & variants
- **Populated** — header + version&verify surface + lifecycle + the version table with the
  default row selected and its s04 editor inline below.
- **Empty (no entity selected)** — wide screens before a selection: the detail pane shows a
  neutral prompt *"Select an entity from the list to view and edit it."* (not a blank pane).
  (On phone there is no empty detail — you arrive only by drilling into an entity.)
- **Loading** — an entity's version rows loading (a brief indication in the version area).
- **Error (entity load failed)** — *"Couldn't load entity `<name>`: `<reason>`."* + retry.
- **Verify states** (the version&verify surface): a **picked version** (the default); a
  **module not linked** — *"`<module>` not linked — link a DLL to verify"* (degraded, never a
  block, TRD D30); a **linked DLL resolving** — the per-module link row shows a brief
  loading indication while the browser reads the DLL's bytes (an 86 MB ArrayBuffer + the
  `.rdata` version scan — law 1, the row reserves its space); a **linked + version match** —
  *"`<dll>`: `<version>` ✓ matches"* (the per-author check is now available in s04); a **linked
  + version mismatch** — *"`<dll>`: `<version>` ≠ no row at this version"* (which surfaces the
  link-to-create prompt); a **resolve failure** — *"couldn't resolve a version from that DLL
  (interns disagree)"* (advisory + override; system-caused copy naming the cause); a **non-PE /
  unreadable pick** — *"that file isn't a readable DLL (`<reason>`)"* (system-caused). All
  advisory (law 4) — none blocks authoring.
- **Disabled** — lifecycle edits on an already-superseded entity follow `policy.md` (the
  successor chain); the validator gates an illegal transition with an inline error; the
  control is not silently hidden.
- **Edge content** — a long `signature` / `notes` wraps within its field without pushing
  siblings (law 1); an entity with many version rows scrolls the version table, header stays
  fixed; a deprecated/superseded entity shows its state prominently in the header.

## Links in / out
- **In:** s01 `select_entity` (drill-down on phone).
- **Out:** `select_version` → s04; `toggle_history` / `open_compare` → s03; `+ New version`
  → s05; any lifecycle edit → s06 (save-confirm); a new entity arrives here after s05;
  `back_to_list` (phone) → s01.

## Applicable laws
- **Law 1** — sections + the inline editor never reflow on selection/dirty/error.
- **Law 2** — the navigation shell persists; edits confirm via the s06 overlay, not a shell
  swap.
- **Law 3** — selecting a version row / opening a mode is user-driven; a verify result
  updates the "current"/"checks out" markers in place, never re-selects for the user.
- **Law 4** — the version&verify surface is advisory; an unverified state never blocks; the
  override lives downstream (s06).
- **Law 6** — lifecycle pair-integrity is the validator's (via the API), rendered inline.
- **Law 7** — identity fields read-only, non-color affordance.
- **Law 8** — lifecycle edits are UPDATEs (not approval-gated); only new entity/version are.
- **Law 9** — tokens only.

## Responsive behavior
- **Wide:** the right pane; the version&verify surface sits in the header alongside the
  identity block.
- **Phone:** the full-screen drill-down; the header stacks the identity + the version&verify
  surface vertically and carries `‹ back`; the version table + the inline editor get the
  full viewport width (the table scrolls horizontally if needed). The "check against a local
  DLL" control uses the device's file picker (only meaningful where the device has the
  game — otherwise the maintainer uses the version dropdown, D15/D10).
