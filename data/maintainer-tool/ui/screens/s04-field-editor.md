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
The detail region, below the version area. A responsive grid of fields, sized to content:
short fields (version tag, kind, dates, the survival integers, evidence_kind) are narrow and
placed 2–3 per row; long fields (signature, aob, anchor_string) span the full width. Grouped
under the existing sub-headings (identity/location, the audit trio, Survival). Collapses to
one column on phone. Within each grid cell, each editable field reserves its dirty-marker
gutter + "was:" line + validation-error line always (law 1). The grid scrolls if tall; field
positions are fixed across dirty/error transitions.

**The shown field set is KIND-CONDITIONAL** (see §"Field relevance by kind"): the grid renders
only the fields the current `kind` uses, plus any populated stray (an irrelevant field that
carries a value), plus the always-shown set; an empty-irrelevant field is hidden entirely (its
grid cell is removed — law 1 holds per SHOWN field, a hidden field reserves nothing). **Every
shown field carries a plain-language tooltip** explaining what it IS and what it is FOR (a
keyboard-focusable `?` help affordance beside the field label; the Survival sub-heading carries a
group tooltip). Tooltip content is SOURCED from `../../seeds/policy.md` +
`../../refdata-extractor/python/seeds_shared/schema.py` (the column comments) — domain facts, not
invented.

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
`module` is a dropdown over `module_seed` rows. **Which of the editable set RENDERS is
kind-conditional** — see §"Field relevance by kind".

**Per-field tooltips:** every field (editable AND the read-only identity key) carries a
plain-language tooltip on a keyboard-focusable `?` info affordance beside its label — a 1–2
sentence explanation a maintainer who does not know the internal vocabulary understands (what the
field IS, what it is FOR). The Survival sub-heading carries a GROUP tooltip explaining the six
re-find columns collectively. Content is sourced from `../../seeds/policy.md` (§"Address kinds",
§"Survival columns", the audit-trio + evidence_kind sections) +
`../../refdata-extractor/python/seeds_shared/schema.py` (the column comments) — not invented. The
affordance is keyboard-reachable and the content available to a screen reader (Mantine `Tooltip`
aria; the trigger is a focusable button), law 9 (theme tokens).

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

## Field relevance by kind
Each `kind` populates only certain columns; the rest stay NULL (`../../seeds/policy.md`
§"Address kinds" + §"Survival columns" — the AUTHORITY; the "Used by kind(s)" column is the
authoritative per-survival-column map). The editor shows the fields the current kind uses, hides
the empty-irrelevant ones, and ALWAYS shows an irrelevant field that carries a non-NULL value
(flagged "not used by this kind", so the maintainer can clear it).

**Always shown (every kind):** `kind`, `module`, the audit trio (`last_verified_at_version`,
`verified_by`, `verified_date`, `evidence_kind`). `valid_from_version` is always shown read-only
(identity key, law 7).

**Per-kind used set** (the kind-specific fields shown; grounded in policy.md + schema.py — the map
the build encodes in `frontend/src/editor/fieldModel.ts` `KIND_FIELD_RELEVANCE`):

| Kind | Fields shown (beyond always-shown) | Grounding |
|---|---|---|
| `function` / `function_variadic` / `function_no_sig` | `rva`, `signature` | a function entry has a verified signature; its survival datum is its body fingerprint, so NONE of the six survival columns apply (policy.md §"function kinds need no survival authoring") |
| `callsite` | `rva`, `offset`, `survival_aob`, `survival_expect_unique` | a call instruction, not a function entry → no signature; survival table: aob + expect_unique → callsite; offset = the consumer offset (schema.py) |
| `instruction_anchor` | `rva`, `survival_aob`, `survival_expect_unique`, `survival_derives_from` | survival table: aob + expect_unique + derives_from (→ its string_anchor) → instruction_anchor |
| `string_anchor` | `rva`, `survival_anchor_string`, `survival_expect_unique` | survival table: anchor_string + expect_unique → string_anchor |
| `vtable_index` | `vtable_slot`, `survival_derives_from` | policy.md: "no RVA; slot-only" → no rva, no signature; survival table: derives_from (→ its vtable_base) → vtable_index (its survival is DEFERRED) |
| `vtable_base` | `rva`, `survival_slot_count` | the vtable base pointer table; survival table: slot_count → vtable_base (derives_from is NOT vtable_base's — it is vtable_index's) |
| `data_slot` | `offset`, `survival_rule`, `survival_derives_from`, `value` | a `.data` slot resolved by a derivation → no rva; survival table: rule + derives_from → data_slot; value = the authored datum (schema.py) |

`struct_offset` is named by no kind in the sources — it surfaces only as a populated stray. On
genuine ambiguity, the rule ERRS TOWARD SHOWING (a stray-flagged populated field is never hidden).

**The visibility rule:**
1. **Shown if:** in the always-shown set, OR in the current kind's used set, OR it carries a
   non-NULL value (a populated stray — shown flagged "not used by this kind").
2. **Hidden if:** irrelevant for the current kind AND empty (NULL/empty string).
3. **Live:** changing the `kind` Select updates the visible set immediately (the prospective `kind`
   drives relevance). A field that becomes irrelevant after a kind change but now carries a (dirty)
   value stays shown (it has a value → a stray).
4. **The stray flag:** a small muted note ("not used by this kind") on the field, so the maintainer
   knows it is atypical for the kind and can clear it.

Save/validate/dirty-tracking behavior is UNCHANGED — only which fields render. A hidden empty field
sends nothing (it was NULL anyway); a shown stray with a value is editable as normal.

## States & variants
- **View (no edits)** — all fields at saved values; `[Review changes]` disabled; read-only
  fields in the law-7 treatment. Only the current kind's fields + populated strays render
  (kind-conditional, see §"Field relevance by kind").
- **Kind changed** — changing the `kind` Select re-computes the visible field set live: fields the
  new kind uses appear, empty fields the new kind doesn't use disappear, populated fields the new
  kind doesn't use stay (flagged "not used by this kind").
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
  trio + resolve facts aren't buried. A `kind` with few used fields renders a short grid
  (the empty-irrelevant columns are hidden); a row with many populated strays renders them
  all (flagged), never hiding data.

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
- **Law 9** — tokens only (the tooltip affordance, the stray flag, and the kind-conditional grid
  all use theme tokens — no raw hex/px). The `?` help affordance is a focusable button (keyboard +
  screen-reader reachable); the Mantine `Tooltip` wires the aria description.

## Responsive behavior
- **Wide:** renders inline below the version table in the detail pane.
- **Phone:** when reached from s02's selected row it renders inline within the detail
  drill-down (scrolls); when reached from a compare column (s03) it opens as a full-screen
  sheet (the edit-existing confirmation precedes it). The dirty-marker gutter + "was:" line +
  error line keep their reserved space at every width (law 1).
