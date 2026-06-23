# s05 — Create new entity / new version

**Phase & fidelity:** v1, high.

## Purpose / when shown
Two related authoring flows that GROW the Address Library, both approval-gated (creating a new
DB row — entity or version — requires explicit maintainer approval in the confirm step):
- **New entity** — from s01's `+ New entity`: claim the next free `kcdx_id`,
  author a `name`, and author its first `address_versions` row.
- **New version** — from s02/s03's `+ New version`: author a new game-version row
  for an existing entity, prefilled from a chosen source row.

## Region & position
New entity: an **overlay surface** (`Modal` centered on wide, full-screen sheet on phone) —
a fresh entity has no detail view yet. New version: opens the s04 field-editor pattern in
the detail region (an inline prefilled form on wide; a full-screen sheet on phone), with a
clear "NEW VERSION (from `<source>`)" banner. Both dim/overlay as the overlay layer over the
persistent navigation shell; on save they land and the new row appears selected in s02.

## Contents

### New entity
| Element | Component (Mantine) | Data bound | Intent emitted |
|---|---|---|---|
| `kcdx_id` | `field row (read-only)` | next free integer (auto-assigned) | — (tool-assigned, not hand-typed) |
| `name` | `text well` (`TextInput`) | `address_names.name` | `edit_new_entity('name', v)` |
| first version row | `s04 field editor` (blank) | a new `address_versions` row | `edit_field(...)` |
| `[Review changes]` | `button` (primary) | enabled when required fields valid | `review_changes()` → s06 |
| `[Cancel]` | `button` (subtle) | — | `close_create()` |

`kcdx_id` is **tool-assigned** (next free integer, no autoincrement, append-only) — the
maintainer never types it (the tool supplies identity, the author declares intent). Required
first-row columns: `valid_from_version`, `module`, `kind`. The audit trio may be set or left
all-null (a brand-new unverified row is all-null).

### New version
| Element | Component (Mantine) | Data bound | Intent emitted |
|---|---|---|---|
| Source banner | `warning banner` (`Alert`, info) | "NEW VERSION of `<name>` (from `<source v>`)" | — |
| `valid_from_version` | `text well` / `Select` (the version dropdown) | the new row's identity key (the game version this row is for) | `edit_field('valid_from_version', v)` |
| all other columns | `s04 field editor` (prefilled) | copied from the source row, including the audit trio | `edit_field(...)` |
| `[Review changes]` | `button` (primary) | enabled when valid AND ≥1 change vs source | `review_changes()` → s06 |
| `[Cancel]` | `button` (subtle) | — | `close_create()` |

**Prefill (the decided rule):** a new version prefills **all columns from the source row,
including the audit trio**. `valid_from_version` is the field the maintainer sets — the one
column that defines the new game version (prefilled from the version&verify surface's pick
or the client-DLL-resolved version when checked, see s02; editable regardless — verification
is advisory, the maintainer is final authority). When this flow is reached via the
**linked-Bin-resolved-new version** (the s02 link-to-create path), `valid_from_version`
prefills from **that resolved version** — the maintainer never hand-types it (the version was
read from the linked folder, so the tool supplies it). The new version becomes a persisted
`game_versions` tag only when THIS row commits (the approval-gated confirm) — it is ephemeral
until then.

**Nothing-changed guard (the decided warning):** if the maintainer tries to save a new
version with **no field changed from the source** (other than `valid_from_version`), a
`warning banner` blocks the save with steering copy — *"Nothing changed from version
`<source>`. If this row is identical, don't create a duplicate version — instead re-verify
the existing row for the game version you're targeting (edit its `last_verified_at_version`
in the field editor)."* This routes a "new version" mistake to the correct action
(re-verify, in s04). The maintainer can still proceed past it only by changing a
field; the guard names the right alternative rather than silently allowing a duplicate.

## States & variants
- **Populated** — the create form (modal for entity, prefilled right-pane for version).
- **Validation error** — inline per the s04 pattern (the shared validator is the single gate):
  a missing required column, a malformed `valid_from_version`, a partial audit trio, an
  unresolvable `module`, a duplicate `(kcdx_id, valid_from_version)` tuple (HARD ERROR).
- **Nothing-changed (new version)** — the steering `warning banner` (above); save blocked
  until a field differs.
- **Unverified / resolver-failure** — `valid_from_version` can't be auto-resolved (no
  client DLL check / interns disagree): the advisory warning + the "I accept — save anyway"
  override (verification is advisory, the maintainer is final authority); the maintainer
  picks/types the version manually.
- **Disabled** — `[Review changes]` disabled until required fields validate.
- **Edge content** — a long first-version `signature` wraps; the overlay scrolls internally
  (the modal/sheet body scrolls) rather than growing past the viewport.

## Links in / out
- **In:** s01 `+ New entity`; s02/s03 `+ New version`.
- **Out:** `review_changes` → s06 (save-confirm, with the new-row approval gate); `Cancel` →
  back to s01 (entity) or s02 (version), nothing lands; on save, the new row appears
  selected in s02.

## Applicable interaction laws
- **Stable layout** — prefilled fields + dirty markers + the nothing-changed banner reserve
  space; no reflow.
- **Persistent shell** — new-entity + new-version are the overlay layer (modal/sheet); the
  navigation shell persists beneath.
- **Advisory verification** — `valid_from_version` resolution is advisory + overridable.
- **Single validator** — required-column + tuple-uniqueness + trio integrity are the validator's
  (via the API).
- **Read-only identity** — `kcdx_id` (assigned) and `valid_from_version` (once set, the row's
  key) render with the read-only treatment after entry.
- **New-row approval gate** — a new entity / new version requires explicit maintainer approval
  in s06 before it lands.
- **Semantic tokens only** — tokens only.

## Responsive behavior
- **Wide:** new-entity is a centered `Modal`; new-version renders inline (prefilled) in the
  detail region.
- **Phone:** both open as full-screen sheets (slide up, fill the viewport, a header with the
  flow name + a close ✕); the sheet body scrolls; `[Review changes]` is pinned reachable at
  the bottom of the sheet. On save the new row appears selected in s02 (the detail
  drill-down).
