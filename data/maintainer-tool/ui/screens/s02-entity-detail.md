# s02 — Entity detail (header · version & verify surface · lifecycle · version area)

**Phase & fidelity:** v1, high.

## Purpose / when shown
Shown when an entity is selected/tapped in s01 (the right pane on wide screens; a
full-screen drill-down on phone). Presents the whole entity the maintainer is managing: its
identity, the **version & verify surface** (which game version an edit targets — the
dissolved desktop status bar's content now lives here), its editable lifecycle (supersede /
deprecate), and the entry point to its version rows (current row, history,
compare, new version). Single-row field editing lives in s04; history/compare in s03.

## Region & position
The detail region of the app shell. The layout **leads with the work surface** (the
detail-pane responsiveness/sizing model — the heavy verify/lifecycle machinery must not eat
the pane):

- **Compact pinned summary** (always visible, top): the entity identity (read-only) + the
  version `Select` + a **one-line verify summary** ("Bin folder linked — `<version>` ✓" / "no
  folder linked"). It carries the `‹ back` affordance (content-pane on wide / drill-down on phone)
  when the stack has depth > 1 — returning to the screen that pushed s02 (the content back-stack),
  not always s01.
- **Collapsible "Verify against a DLL" section** (`collapsible section`, **collapsed by
  default**): the install-set link surface — the Bin-folder pick + the per-module link rows +
  the link-to-create prompt.
- **Collapsible "Lifecycle" section** (`collapsible section`, **collapsed by default**):
  supersede / deprecate / notes.
- **Work surface** (**expanded by default**, takes the majority of the pane, scrolls within
  it): the version table + the inline field editor (s04, rendered below the selected row).

A collapsible section's header row never moves; expanding pushes the sections BELOW it down,
never reflowing the work surface above (a user-toggled disclosure reserves its space, not a
state-change reflow). The compact summary stays pinned; the work surface scrolls.

## Contents
| Element | Component (Mantine) | Data bound | Intent emitted |
|---|---|---|---|
| **Compact pinned summary** (always visible, top) | | | |
| Entity title + identity | `title` text + `field row (read-only)` ×2 | `name` · `kcdx_id` (mono) | — (read-only, never editable; identity is read-only) |
| Version `Select` | `select / dropdown` | the targeted game version — sourced from `game_versions` tags AND the linked-Bin-resolved version | `pick_version(tag)` |
| `‹ back` (content-pane depth > 1; drill-down back on phone) | `back affordance` | the content back-stack | `nav_back()` → the screen that pushed s02; at root → navigator (wide) / navigator home (phone) |
| One-line verify summary | text + glyph (status role) | the install link state ("Bin folder linked — `<version>` ✓" / "no folder linked") | — (status, read-only; glyph+text, never color-alone) |
| **Verify against a DLL** (`collapsible section`, collapsed by default) | | | |
| Section header | `collapsible section` header | collapsed/expanded state (chevron + aria-expanded) | `toggle_verify_section()` (user action; reserves its space, no reflow) |
| Link the Bin folder | folder-pick affordance (`<input webkitdirectory>`) | the picked install's directory (read in-page, never uploaded) | `link_bin_folder()` / `relink_bin_folder()` |
| No-upload reassurance | static note (caption, `🔒` glyph + text) directly beneath the link affordance | — (fixed copy) | — (informational) |
| Per-module link rows | `per-module link row` ×M (one per module the entity references) | each module's DLL status in the linked folder (found / not-found) + the version-match indicator (the install version inherited for non-WHGame) | — (the row reflects the install link; the affordance is the folder pick above) |
| Link-to-create prompt | `warning banner` (advisory, info) | shown when the linked install's version is uncovered by the entity's rows | `open_new_version(source=selected_row, version=<install_version>)` → s05 |
| **Lifecycle** (`collapsible section`, collapsed by default) | | | |
| Section header | `collapsible section` header | collapsed/expanded state | `toggle_lifecycle_section()` (user action; reserves its space, no reflow) |
| Lifecycle — supersede | `field row (editable)` (`Select` + version) | `superseded_by` (target entity) + `superseded_at_version` | `edit_lifecycle(...)` → s06 |
| Lifecycle — deprecate | `field row (editable)` (`Checkbox` + version + `Select`) | `is_deprecated` + `deprecated_at_version` + `deprecation_replacement` | `edit_lifecycle(...)` → s06 |
| `notes` | `text well` (`TextInput`/`Textarea`) | `notes` (entity-level) | `edit_lifecycle(...)` → s06 |
| **Work surface** (expanded by default, scrolls) | | | |
| Version table | `version table row` ×N (newest first) | the entity's `address_versions` rows | `select_version(valid_from)` → s04 |
| `[Show history]` | `button` (subtle) | toggles full-history vs current-row-only | `toggle_history()` → s03 |
| `[Compare versions]` | `button` (subtle) | enters compare multi-select | `open_compare()` → s03 |
| `[+ New version]` | `button` (default) | — | `open_new_version(source=selected_row)` → s05 |
| Selected-row editor | `s04 field editor` (inline) | the selected version row | (see s04) |

A **new module** (a CryEngine module not yet in the `module` table) is registered as a
surfaced step when the maintainer authors the first address for it (the deliberate-addition
posture — creating a new DB row requires explicit maintainer approval); the link table then
shows its `per-module link row`, found by name in the linked folder.

**The version & verify surface (the relocated s07 + the verification engine).**
A **version dropdown** (`Select`) is the default way the maintainer states which version an edit
targets; the picked version default-selects/marks the matching row. The dropdown sources **two
origins**: the server-known `game_versions` tags, AND the **linked-Bin-resolved
version**. When a linked Bin folder resolves (via WHGame.dll's `.rdata` scan) to a version
the DB does NOT know — a build newer than any `game_versions` tag, with no row covering it — that
resolved version appears as a **marked entry under a "From your linked Bin" group header** in the
dropdown, labeled **"`<version>` · new"** (a glyph + text, never color-alone), and
**auto-becomes the selected version** (the maintainer never hand-types it — the tool reads the
version, the maintainer declares intent). Selecting it is the same gesture as any version: the
compact summary's one-line verify state then reads *"from your linked Bin: `<version>` · new"*. The
Bin-resolved-new version is a **first-class selectable that drives authoring** — it is the version a
new-row action targets, feeding the link-to-create flow below; it is **ephemeral until a row is
authored at it** (it does not enter `game_versions` until an approval-confirmed row commits there).
On a phone / no-install host there is no Bin to link → the `game_versions` dropdown +
the newest-row default is the path, unchanged.

Beneath it, the **DLL link surface** — the maintainer **links the game's Bin folder once** (a
single in-session `<input webkitdirectory>` directory pick — the DLLs read in-page, **never
uploaded**; the **install-set** model). The tool reads **WHGame.dll** from
the folder to resolve the install's game version (the `.rdata` `release_M_N_BUILD` scan) and finds
**every referenced module's DLL by its filename** in that same folder. The surface then shows a
**`per-module link row` ×M** (one per module the entity's rows reference; today `WHGame.dll`, but
multi-module once the DB carries CryEngine entities): each row shows the module name + the DLL's
status in the linked folder (found / **not found in the linked folder**) + a **version-match
indicator** (✓ the install's version matches the selected row's version / ≠ no match — glyph+text,
never color-alone). A **non-WHGame module inherits the install's version from WHGame.dll** (the
CryEngine DLLs carry no KCD2 version string of their own). The folder link is **re-picked each
session** (no persisted handle — only File-System-Access *persistence* is rejected, not the
in-session pick); a `[re-pick folder]` swaps the linked install. A **new module** (a CryEngine
module not yet in the `module` table) is **registered as a surfaced step** when the maintainer
authors the first address for it (the deliberate-addition posture). **Multi-store is
out of scope**: the install-set keys on the game version only.

**No-upload reassurance (a static caption directly beneath the link affordance).** The browser's
OWN native directory-picker dialog labels the action **"Upload"** and warns the site can **"read
all files in this folder"** — Chromium/WebKit hardcode that copy for *any* `<input
webkitdirectory>` and a page cannot relabel or suppress it. It is misleading here: nothing is
uploaded (the DLL bytes are read in-page via `File.arrayBuffer` and never leave the browser — the
no-upload invariant above). To keep the maintainer from reading the browser's generic wording as
data exfiltration, the surface renders a **fixed caption beneath the link button** — `🔒 Your DLLs
are read in your browser and never uploaded. Your browser's folder dialog may say "upload" —
that's its generic wording; nothing is sent.` (glyph + text, never color-alone). The note is
informational, always shown (not state-dependent), and addresses the confusion at the exact point
it occurs.

Linking a version-matching DLL is what enables the **per-author static verification check**
(the per-kind survival check against the DLL's bytes) — whose verdict renders inline in **s04**
(the field editor, where the `rva` / `signature` being checked are authored), NOT here. The
link table is the verification *context*; the per-row verdict lives where the row is authored.

**Link-to-create.** When a linked DLL resolves to a version **not covered** by any
of the entity's `address_versions` rows (a build newer than the DB knows), two things happen: the
resolved version **auto-becomes the selected version in the top dropdown** (the "From your linked
Bin · new" entry above), AND the surface shows an inline advisory prompt (`warning banner`):
*"`<dll>` is `<version>` — this entity has no row for it. [Add a version row at `<version>`]"*. The
maintainer clicks (a user action — never auto-opened) → the s05 create-version overlay,
**prefilled at the DLL's resolved version** (`valid_from_version = <dll version>`) → author
`rva`/`signature` → the check runs against the linked DLL → on a passing check the audit trio
auto-fills → save (approval-gated confirm) — and only THEN does the new version become a
persisted `game_versions` tag (ephemeral until the row commits).

**Advisory, never required** (verification is advisory, the maintainer is final authority): with
just a picked version (or none → newest-row default), every flow proceeds; an
unresolved/unverified/Changed/Ambiguous state warns and is
overridable downstream (s06); no matching DLL linked → the check is unavailable + noted (a
degraded state), authoring still proceeds.

**Lifecycle pair-integrity** (enforced by the shared validator — the single gate):
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
- **Verify states** (the version&verify surface — the folder-pick install-set): a
  **picked version** (the default, no folder linked); a **no folder linked** — *"link the game
  Bin folder to verify"* (degraded, never a block); a **folder resolving** — a brief
  loading indication while the browser reads the folder's DLLs (the WHGame.dll `.rdata` version
  scan over the ~86 MB ArrayBuffer — the surface reserves its space, no reflow); a
  **not a KCD2 Bin folder** — *"that folder has no WHGame.dll — pick the game Bin folder"*
  (WHGame.dll absent, the install version can't resolve; system-caused); a **resolve failure** —
  *"couldn't resolve a version from WHGame.dll (interns disagree)"* (the folder has WHGame.dll but
  its `.rdata` gave no agreed version; advisory + override). Then **per module** (the
  `per-module link row`): a **module DLL not found** — *"`<module>` not found in the linked
  folder"* (that module's check unavailable, the others + authoring proceed; advisory); a
  **version match** — *"`<module>` (`<install-version>`) ✓ matches"* (the per-author check is now
  available in s04 for that module); a **version mismatch** — *"`<module>` (`<install-version>`)
  ≠ no row at this version"* (surfaces the link-to-create prompt). All advisory (verification is
  advisory) — none blocks authoring; every indicator is glyph+text, never color-alone.
- **Section collapsed / expanded** (the `collapsible section` — "Verify against a DLL",
  "Lifecycle"): on open, both are **collapsed by default** (the work surface gets the room); the
  one-line verify summary in the compact header still shows the verify state. The section header
  shows its state (chevron `›` collapsed / `⌄` expanded + `aria-expanded`, glyph+text); toggling
  expands the body IN PLACE below the header (the header row never moves, the work surface above is
  never reflowed — a user-toggled disclosure reserves its space). Keyboard: the header is
  focusable, Enter/Space toggles.
- **Per-module row — reflow-safe** (within the expanded Verify section): the row's **top line**
  (module name + the link/re-pick affordance + the match glyph) holds a **fixed position**; a
  long verify message (the not-found / not-a-Bin-folder / resolve-failure copy) **wraps in the
  reserved multi-line space below the top line**, growing downward — the affordance never shifts
  sideways off the row regardless of message length (the reserved-space structure keeps layout
  stable; this is the fix for the prior reflow defect).
- **Version selector — linked-Bin-new** — when a linked Bin resolves to a version not in
  `game_versions`, the version `Select` shows a **"From your linked Bin"** group with a **"`<version>`
  · new"** entry (glyph + text), auto-selected; the compact summary's verify line reads "from
  your linked Bin: `<version>` · new". The maintainer never types the version. Selecting it drives
  the link-to-create flow (above); the version is ephemeral until an approval-confirmed row commits at it.
  When the linked Bin's version IS already known (`game_versions` has it / a row covers it), no "new"
  marking — it selects like any known version.
- **Returned via `‹ back`** — when the maintainer `‹ back`s into s02 (it was pushed by s08
  `[Fix ▸]` or an s09 resolve), s02 restores its FULL prior state: the selected version, the expanded
  collapsible-section toggles (Verify / Lifecycle — exactly as left, not re-collapsed to default),
  the selected version row, and scroll. The `‹ back` control sits top-left of the content pane,
  labeled with its destination ("‹ back to the report" / "‹ back to Needs action"), in reserved space
  (no reflow); absent when s02 is at the stack root (reached by a navigator selection).
- **Unsaved-changes guard** — navigating away from s02 (a `‹ back`, a navigator
  entity-switch, a top-level entry) while the s04 inline editor OR the Lifecycle section holds pending
  edits surfaces the Save / Discard / Cancel confirm (the `overlay surface`) FIRST; nothing saves or
  is lost without an explicit choice.
- **Disabled** — lifecycle edits on an already-superseded entity follow the successor-chain
  rules; the validator gates an illegal transition with an inline error; the
  control is not silently hidden.
- **Edge content** — a long `signature` / `notes` wraps within its field without pushing
  siblings (stable layout); an entity with many version rows scrolls the work surface (the version
  table), the compact header + the collapsible-section headers stay fixed; a long DLL filename or a
  multi-line verify message wraps within its `per-module link row` without pushing siblings (stable
  layout); a deprecated/superseded entity shows its state prominently in the compact header.

## Links in / out
- **In:** s01 `select_entity` (RESETS the stack to a fresh s02 root); OR pushed onto the
  stack by an s08 `[Fix ▸]` / an s09 resolve-action (`‹ back` returns there with state).
- **Out:** `select_version` → s04; `toggle_history` / `open_compare` → s03; `+ New version`
  → s05; any lifecycle edit → s06 (save-confirm); a new entity arrives here after s05;
  `nav_back` → the screen that pushed s02; at s02's root, `‹ back` → the navigator (wide)
  / navigator home (phone).

## Applicable interaction laws
- **Stable layout** — sections + the inline editor never reflow on selection/dirty/error.
- **Persistent shell** — the navigation shell persists; edits confirm via the s06 overlay, not a
  shell swap.
- **User-driven navigation** — selecting a version row / opening a mode is user-driven; a verify
  result updates the "current"/"checks out" markers in place, never re-selects for the user.
- **Advisory verification** — the version&verify surface is advisory; an unverified state never
  blocks; the override lives downstream (s06).
- **Single validator** — lifecycle pair-integrity is the validator's (via the API), rendered inline.
- **Read-only identity / non-color affordance** — identity fields read-only, conveyed by more than
  color.
- **New-row approval gate** — lifecycle edits are UPDATEs (not approval-gated); only new
  entity/version are.
- **Semantic tokens only** — tokens only.
- **Content back-stack** — a navigator selection RESETS to a fresh s02 root; an s08
  `[Fix ▸]` / s09 resolve PUSHES s02 (state-carrying); `‹ back` restores s02's full state (selected
  version, expanded section toggles, version-row selection, scroll). Navigating away from a dirty
  s04 inline editor or Lifecycle section surfaces the unsaved-changes guard (Save/Discard/Cancel)
  first. The `‹ back` affordance is top-left of the content pane (depth > 1), labeled with
  its destination; absent at root.

## Responsive behavior
- **Wide:** the right pane. The compact pinned summary (identity + version `Select` + the
  one-line verify summary) sits at the top; the collapsible Verify + Lifecycle sections + the
  work surface (version table + inline editor) stack below, the work surface taking the majority
  of the pane height and scrolling within it (the compact summary + the section headers stay
  pinned).
- **Phone:** the full-screen drill-down. The same compact summary pins at the top (and carries
  the `‹ back`, which the back-stack drives: returns to the screen that pushed s02, or the
  navigator home at root, restoring full state); the collapsible sections + the work surface get the full viewport width; the work
  surface scrolls (the version table scrolls horizontally if needed). The compact-header + collapse
  model holds — the work surface gets the room, Verify/Lifecycle collapsed by default. The folder
  pick (`<input webkitdirectory>`) uses the device's native directory picker (only meaningful where
  the device has the game install — otherwise the maintainer uses the version dropdown).
