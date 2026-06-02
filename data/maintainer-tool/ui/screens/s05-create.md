# s05 — Create new entity / new version

**Phase & fidelity:** v1, high.

## Purpose / when shown
Two related authoring flows that GROW the Address Library, both approval-gated (law 8):
- **New entity** (Job 1) — from s01's `+ New entity`: claim the next free `kcdx_id`,
  author a `name`, and author its first `address_versions` row.
- **New version** (Job 6) — from s02/s03's `+ New version`: author a new game-version row
  for an existing entity, prefilled from a chosen source row.

## Region & position
New entity: a modal card over the panes (a fresh entity has no detail pane yet). New
version: opens the s04 field-editor pattern in the right pane, prefilled, with a clear
"NEW VERSION (from `<source>`)" banner. Both dim/overlay per law 2; on save they land and
the new row appears selected in s02.

## Contents

### New entity (Job 1)
| Element | Component | Data bound | Intent emitted |
|---|---|---|---|
| `kcdx_id` | `field row (read-only)` | next free integer (auto, `policy.md` §"ID assignment") | — (tool-assigned, not hand-typed) |
| `name` | `text well` | `address_names.name` | `edit_new_entity('name', v)` |
| first version row | `s04 field editor` (blank) | a new `address_versions` row | `edit_field(...)` |
| `[Review changes]` | `primary button` | enabled when required fields valid | `review_changes()` → s06 |
| `[Cancel]` | `secondary button` | — | `close_create()` |

`kcdx_id` is **tool-assigned** (next free integer, no autoincrement, append-only) — the
maintainer never types it (the disassembler-test principle: the tool supplies identity, the
author declares intent). Required first-row columns (`policy.md` §"Required columns"):
`valid_from_version`, `module`, `kind`. The audit trio may be set or left all-null (a
brand-new unverified row is all-null).

### New version (Job 6)
| Element | Component | Data bound | Intent emitted |
|---|---|---|---|
| Source banner | `warning banner` (info variant) | "NEW VERSION of `<name>` (from `<source v>`)" | — |
| `valid_from_version` | `text well` | the new row's identity key (the game version this row is for) | `edit_field('valid_from_version', v)` |
| all other columns | `s04 field editor` (prefilled) | copied from the source row, including the audit trio | `edit_field(...)` |
| `[Review changes]` | `primary button` | enabled when valid AND ≥1 change vs source | `review_changes()` → s06 |
| `[Cancel]` | `secondary button` | — | `close_create()` |

**Prefill (the decided rule):** a new version prefills **all columns from the source row,
including the audit trio**. `valid_from_version` is the field the maintainer sets — the one
column that defines the new game version (prefilled from the linked DLL's resolved version
when a DLL is linked, s07; editable regardless, law 4).

**Nothing-changed guard (the decided warning):** if the maintainer tries to save a new
version with **no field changed from the source** (other than `valid_from_version`), a
`warning banner` blocks the save with steering copy — *"Nothing changed from version
`<source>`. If this row is identical, don't create a duplicate version — instead re-verify
the existing row for the game version you're targeting (edit its `last_verified_at_version`
in the field editor)."* This routes a "new version" mistake to the correct action
(re-verify = Job 2 in s04). The maintainer can still proceed past it only by changing a
field; the guard names the right alternative rather than silently allowing a duplicate.

## States & variants
- **Populated** — the create form (modal for entity, prefilled right-pane for version).
- **Validation error** — inline per the s04 pattern (shared validator, law 6): a missing
  required column, a malformed `valid_from_version`, a partial audit trio, an unresolvable
  `module`, a duplicate `(kcdx_id, valid_from_version)` tuple (HARD ERROR — `policy.md`).
- **Nothing-changed (new version)** — the steering `warning banner` (above); save blocked
  until a field differs.
- **Unlinked / resolver-failure** — `valid_from_version` can't be auto-resolved: the
  advisory warning + the "I accept — save anyway" override (law 4); the maintainer types
  the version manually.
- **Disabled** — `[Review changes]` disabled until required fields validate.
- **Edge content** — a long first-version `signature` wraps; the modal scrolls internally
  rather than growing past the window.

## Links in / out
- **In:** s01 `+ New entity`; s02/s03 `+ New version`.
- **Out:** `review_changes` → s06 (save-confirm, with the law-8 approval gate); `Cancel` →
  back to s01 (entity) or s02 (version), nothing lands; on save, the new row appears
  selected in s02.

## Applicable laws
- **Law 1** — prefilled fields + dirty markers + the nothing-changed banner reserve space;
  no reflow.
- **Law 2** — new-entity is a modal; new-version overlays the right pane; panes persist.
- **Law 4** — `valid_from_version` resolution is advisory + overridable.
- **Law 6** — required-column + tuple-uniqueness + trio integrity are the validator's.
- **Law 7** — `kcdx_id` (assigned) and `valid_from_version` (once set, the row's key)
  render with the read-only treatment after entry.
- **Law 8** — a new entity / new version is approval-gated in s06 before it lands.
- **Law 9** — tokens only.
