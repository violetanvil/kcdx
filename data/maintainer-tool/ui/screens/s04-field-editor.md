# s04 — Field editor (view / edit a version row)

**Phase & fidelity:** v1, high.

## Purpose / when shown
The inline editor for one `address_versions` row, shown in the detail region below the
version table (s02) or from a compare column / history row (s03); a full-screen sheet on
phone when entered from compare. The maintainer views the row's full columns and edits the
editable ones with live, layout-stable dirty feedback and inline validation. The audit-trio
edit (Job 2) and the full-column edit (general correction) are the same surface — what's
editable is the full row minus the identity key.

## Region & position
The detail region, below the version area. A vertical field list; each editable field
reserves its dirty-marker gutter + "was:" line + validation-error line always (law 1). The
list scrolls if tall; field positions are fixed across dirty/error transitions.

## Contents
| Element | Component (Mantine) | Data bound | Intent emitted |
|---|---|---|---|
| `valid_from_version` | `field row (read-only)` | identity key | — (never editable, law 7) |
| `module` | `field row (editable)` → `Select` | `module` (FK to `module_seed`) | `edit_field('module', v)` |
| `kind` | `field row (editable)` → `Select` | `kind` (the nine-kind enum) | `edit_field('kind', v)` |
| `rva` | `field row (editable)` → `text well` (`TextInput`, mono) | `rva` | `edit_field('rva', v)` |
| `signature` | `field row (editable)` → `text well` (`TextInput`, mono) | `signature` | `edit_field('signature', v)` |
| `last_verified_at_version` | `field row (editable)` → `text well` | audit trio | `edit_field(...)` |
| `verified_by` | `field row (editable)` → `text well` | audit trio | `edit_field(...)` |
| `verified_date` | `field row (editable)` → `text well` (`YYYY-MM-DD`) | audit trio | `edit_field(...)` |
| `evidence_kind` | `field row (editable)` → `Select` | audit trio (5-value ranked enum) | `edit_field(...)` |
| six survival columns | `field row (editable)` ×6 | `survival_aob` … `survival_expect_unique` | `edit_field(...)` |
| Revert-field | `button` (subtle) per dirty field | — | `revert_field(name)` |
| `[Review changes (N)]` | `button` (primary) | enabled when ≥1 dirty AND valid | `review_changes()` → s06 |

**Editable set** (`../design.md` §"editable"): everything on the row EXCEPT
`valid_from_version` (identity key, read-only law 7) and the entity identity (`kcdx_id`,
`name`, on s02). `evidence_kind` and `kind` are dropdowns (gated enums, never free text);
`module` is a dropdown over `module_seed` rows.

**Dirty feedback (law 1):** a changed field gets the `dirty` accent marker in its gutter
AND a `caption` "was: `<old>`" line directly below it (`text_secondary` + `dirty` accent).
The "was:" line's vertical space is ALWAYS reserved (rendered empty when unchanged) — no
reflow when it appears. An unchanged field shows "(unchanged)" muted or nothing in the
reserved line.

**Validation (law 6):** each field's validity comes from the shared validator (the
data-core, via the API), rendered inline in the reserved error line: a malformed
`verified_date` shape, an out-of-enum `evidence_kind`/`kind`, a partial audit trio (the trio
is all-set-or-all-null — `policy.md`), `last_verified_at_version < valid_from_version`, an
unresolvable `module` FK. The error names what's wrong; `[Review changes]` stays disabled
while any dirty field is invalid. No rule is reimplemented in the frontend.

**Edit-existing confirmation (law 5):** opening an already-decided version for editing
raises a one-time confirmation — *"Editing existing version `<v>` of `<name>`. Changes
update this version in place. To author a different game version instead, use + New
version."* — before fields unlock. This prevents mistaking an edit for a new-version
authoring.

**Verification context (law 4):** when the targeted version is not verified against a DLL
(the version&verify surface in s02's header — the dropdown pick with no client DLL check, or
a resolver failure), a `warning banner` (`Alert`) notes *"Not verified against a game DLL."*
The edit still proceeds; the warning is advisory. A resolver failure surfaces the same way,
with the "I accept — save anyway" override carried into s06 (D15).

## States & variants
- **View (no edits)** — all fields at saved values; `[Review changes]` disabled; read-only
  fields in the law-7 treatment.
- **Editing (dirty)** — ≥1 field changed: dirty markers + "was:" lines; `[Review changes
  (N)]` enabled once all dirty fields validate.
- **Validation error** — inline on the offending field; `[Review changes]` disabled; no
  write attempted; the error names the cause (user-caused copy).
- **Disabled (read-only context)** — viewing a row in a context where editing isn't
  offered (e.g. a non-selected compare column before `[Edit]`): fields render read-only.
- **Unverified / resolver-failure** — the advisory `warning banner` (`Alert`) + the override
  carried to save (law 4, D15).
- **Edge content** — a long `signature` / survival AOB wraps within its well; the field
  list scrolls; many survival columns stay grouped under a "Survival" sub-heading so the
  trio + resolve facts aren't buried.

## Links in / out
- **In:** s02 `select_version`; s03 `select_version` / `edit_version` (with the
  edit-existing confirmation).
- **Out:** `review_changes` → s06 (save-confirm); `revert_field` stays in s04.

## Applicable laws
- **Law 1** — dirty marker, "was:" line, and error line reserve space always; no reflow.
- **Law 4** — unverified/resolver-failure is advisory + overridable, never a block.
- **Law 5** — editing an existing version crosses the confirm boundary; save is one
  transaction via s06.
- **Law 6** — every field's validity is the shared validator's verdict (via the API),
  rendered inline.
- **Law 7** — `valid_from_version` read-only, non-color affordance.
- **Law 9** — tokens only.

## Responsive behavior
- **Wide:** renders inline below the version table in the detail pane.
- **Phone:** when reached from s02's selected row it renders inline within the detail
  drill-down (scrolls); when reached from a compare column (s03) it opens as a full-screen
  sheet (the edit-existing confirmation precedes it). The dirty-marker gutter + "was:" line +
  error line keep their reserved space at every width (law 1).
