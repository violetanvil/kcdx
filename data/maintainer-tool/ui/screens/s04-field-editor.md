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
group tooltip). Tooltip content is SOURCED from `../../policy.md` +
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
| `verified_by` | `field row (editable)` → `text well`, **prefilled from the resolved identity, overrideable** (TRD D17a) | audit trio — the signer; on Confirm it is SENT as the request `author_name` (the git commit author) | `edit_field(...)` |
| `verified_date` | `field row (read-only)`, **shown only when the row is verified** (`last_verified_at_version` non-empty); system-set to today on verify (TRD D17b) | audit trio — a SYSTEM fact (when verification happened), never hand-typed | — (read-only, system-set, law 7) |
| `evidence_kind` | `field row (editable)` → `Select` | audit trio (5-value ranked enum) | `edit_field(...)` |
| six survival columns | `field row (editable)` ×6 | `survival_aob` … `survival_expect_unique` | `edit_field(...)` |
| **Check-vs-DLL verdict** | `verdict badge` (+ reserved detail line) | the per-kind static check result against the linked, version-matching DLL | — (advisory result; the override is `accept_unverified()` → s06) |
| `[show matches]` (Ambiguous only) | `button` (subtle) | the callsite AOB's N `.text` match locations | `show_match_locations()` |
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
re-find columns collectively. Content is sourced from `../../policy.md` (§"Address kinds",
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
data-core, via the API), rendered inline in the reserved error line: an out-of-enum
`evidence_kind`/`kind`, a partial audit trio (the trio is all-set-or-all-null — `policy.md`),
`last_verified_at_version < valid_from_version`, an unresolvable `module` FK. The error names
what's wrong; `[Review changes]` stays disabled while any dirty field is invalid. No rule is
reimplemented in the frontend. (The validator remains the authority on `verified_date`'s shape —
law 6 — but the FE never surfaces a malformed-date entry path: `verified_date` is system-set,
read-only, never hand-typed (TRD D17b), so the maintainer cannot author a bad date.)

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

**The per-author static check (TRD D24–D27, D31).** When a **version-matching** DLL is linked
(the s02 link table), the editor runs the row's **per-kind static survival check** against the
DLL's bytes IN THE BROWSER (no upload) and renders the `verdict badge` inline, in the row's
verification region — **directly below the kind-relevant fields the check is about** (`rva` /
`signature` for a `function`; `survival_aob` for a `callsite` / `instruction_anchor`;
`survival_anchor_string` for a `string_anchor`; `survival_slot_count` for a `vtable_base`;
`survival_rule` for a `data_slot`). The check re-runs as those fields change (a dirty
re-check). The badge's space is **always reserved** (law 1 — no reflow when the verdict
appears/changes). The four verdicts (glyph+text, law 7):

- **Unchanged** ✓ — *"matches the binary at `<resolved site>`"* (the authored value resolves
  uniquely / hashes equal in the linked DLL).
- **Changed** — *"does NOT match the binary (`<what was observed>`)"* (the body hash differs /
  the AOB has zero hits / the anchor literal is gone). Advisory; the override (`accept_unverified`)
  carries to s06.
- **Ambiguous** (callsite/anchor multiple hits) — the **warn-and-steer** verdict (TRD D31):
  *"pattern matches `<N>` sites in `.text` — add context bytes to make it unique"* + a
  `[show matches]` affordance listing the `<N>` match locations (a nudge to extend the
  `survival_aob` / `signature` field right here). Advisory — never refuses; the maintainer
  extends the pattern and the check re-runs, or overrides.
- **CannotCheck** — *"can't check this kind against the DLL (`<reason>`)"* (a `vtable_index`,
  whose survival is deferred; or a dependent kind whose anchor is itself Changed — the
  transitive case, TRD `fingerprint-per-kind.md` anchor-dependency).

**No matching DLL linked → no badge** — the check is **unavailable**, the existing "Not verified
against a game DLL" advisory stands (the degraded state, TRD D30); authoring proceeds.

**The function-kind check reads a verify-only `content_hash` (read-contract addendum).** A
`function` row's check re-hashes the on-disk body and compares it to the row's **recorded**
`content_hash` (the engine-computed BLAKE3, TRD `fingerprint-per-kind.md` §function). That hash is
the engine fingerprint and is **never shown or edited** on s02/s03 (the read contract excludes it
from the display/edit column set). To make the function badge work, the read API returns
`content_hash` as a **verify-only field** — it crosses the wire for the in-browser check ONLY, never
enters the display/edit surfaces (s02/s03 still never render it). A function row whose verify-only
`content_hash` is absent (a row never fingerprinted) renders **CannotCheck** *"no recorded body
fingerprint to compare against"* — honest, advisory. The AOB/string/vtable/derivation kinds need no
such addendum (their survival data is already in the display set). The `text_range` a `vtable_base`
check needs is **derived from the linked DLL's `.text` section bounds** (computed in-browser from the
parsed PE), not a stored column.

**evidence_kind from the check (TRD D29).** A passing static check (Unchanged) refines the
audit trio's `evidence_kind` from its auto-filled default (`maintainer_ghidra`, set when
`last_verified_at_version` is filled) to the tier the check establishes — a browser
AOB-uniqueness pass → `pattern_scan`. The maintainer can still edit `evidence_kind`; the check
sets the strongest tier it proves, never over-claims. (A `live_production` tier comes only from
the in-game live check, ingested via s08 — not from a browser static check.)

## Arriving from a failing report row — the per-field divergence diff (TRD D45)

When s04 is reached via an s08 `[Fix ▸]` on a `failed` report row, the editor surfaces **WHICH
recorded field diverged from the running build, recorded-vs-actual, inline at the field to edit** —
re-derived IN-BROWSER, with no engine or report-schema change. The in-game sweep produces only a
whole-row `failed` verdict + a prose `detail` (it computes one whole-body hash per entity, it cannot
isolate a field — TRD D45); the per-field diff is the TOOL's, computed by re-running the per-author
static check above against the divergent build.

**The "What diverged" banner (E5, extended).** At the top of the editor, an advisory banner
(`Alert`, law 4 — never blocks the edit) titled **"What diverged"** names that the maintainer
arrived from a failing worklist row and carries the engine's prose `detail` (e.g. *"on-disk body
hash mismatch: build diverged from the recorded version"*) so the reason is visible without
re-checking the worklist. When the per-field diff has computed (a DLL is linked, below), the banner
also **names the diverged field(s)** at a glance (*"signature, rva diverge from the linked build"*) —
the glance-level overview; the field-level detail is inline (below).

**'Actual' derives from the REPORT's DLL — the divergent build (TRD D45).** The diff derives the
"actual" column from the SAME running-game DLL the in-game sweep ran against — the build that
diverged — NOT a version-matching DLL. The report's `game_version` prefills the DLL-link prompt; the
maintainer links/picks that running build. (A `failed` row failed because the build diverged from the
recorded version, so the divergent build is where "what's actually there now" lives — the honest
source for recorded-vs-actual.)

**Per-field recorded-vs-actual (law 1 / law 7).** For each KIND-RELEVANT field the check covers
(§"Field relevance by kind": `function` → `rva` + `signature`; `callsite` / `instruction_anchor` →
`survival_aob`; `string_anchor` → `survival_anchor_string`; `vtable_base` → `survival_slot_count`),
a field that diverged shows, inline in its reserved gutter (the same space the dirty marker + "was:"
line + verdict badge already reserve — no reflow): the **recorded** value, the derived **actual**
(what the linked build shows — the resolved site / body-hash result / `.text` match count), and a
**diverged marker** (glyph + text, never color-alone — law 7). This puts the diff directly where the
maintainer edits, answering "the actual field is clear showing what is different". A kind-relevant
field that did NOT diverge shows no marker (it matches the build). (Per-field ATTRIBUTION is the
build's thin new layer over `runVerdictCheck`, which returns one row-level verdict per row — TRD D45
marks the attribution `unverified, probe before building`; the probe settles which kind-relevant
field a row-level divergence maps to before the inline diff lands.)

**The diverged-field box — red-boxed emphasis so the eye lands on the break (TRD D47, law 1 / law 7
/ law 9).** A diverged kind-relevant field is wrapped in a **diverged-field box**: an `error`-token
border (1–2px) around the whole field row — the input plus its already-reserved gutter / "was:" /
verdict-badge space — plus a faint `error`-tinted row background. The box reuses the field row's
EXISTING reserved box, so it adds NO layout and shifts nothing (law 1 — no reflow). It NEVER stands
alone as the divergence signal: the diverged glyph+text marker (above) is retained, so the cue is
glyph + text + box, never color-alone (law 7); the border + tint resolve to the `error` semantic
token, never a raw value (law 9). **Every diverged field is boxed equally** — when more than one
kind-relevant field diverges (e.g. a `function` row where both `rva` and `signature` diverge), each
gets its own identical box (no priority ordering); the "What diverged" banner continues to name the
full diverged set at a glance. A kind-relevant field that did NOT diverge shows no box (it matches
the build). The box is keyed to the per-field divergence RESULT, not to a raw color — it is the same
attribution layer the diverged marker uses.

**The box persists through the edit until the fix is saved (law 1 / law 3).** The diverged-field box
marks the DIVERGENCE FACT — that this field diverged from the linked build — which holds true until
the correction is committed. So when the maintainer edits a boxed field, the box PERSISTS while the
field is dirty (the existing dirty marker layers ON TOP, in its own reserved gutter — both visible,
no reflow); the box clears only after the save lands and the row is no longer divergent. The box
never clears on the first keystroke — a half-typed or wrong correction must keep the "this was
broken" signal until ground truth (a saved, re-checked row) says otherwise. (This is the static
divergence result driving the box; the dirty marker is the orthogonal edit-in-progress signal — the
two compose, neither replaces the other.)

**The no-DLL-yet state (law 4).** On `[Fix ▸]` arrival before a suitable DLL is linked — the common
case, since a divergent build is new and no version-matching DLL exists — the editor shows every
RECORDED field value immediately + the "What diverged" banner's prose `detail` + a **non-blocking
prompt** (*"Link the running-game DLL to see what diverged"*, prefilled from the report's
`game_version`). The per-field "actual" fills in once the DLL is picked. The editor is NEVER blocked
on the DLL (a maintainer hand-correcting a known value proceeds without a file pick — the existing
"no DLL → advisory, authoring proceeds" contract, law 4).

## Field relevance by kind
Each `kind` populates only certain columns; the rest stay NULL (`../../policy.md`
§"Address kinds" + §"Survival columns" — the AUTHORITY; the "Used by kind(s)" column is the
authoritative per-survival-column map). The editor shows the fields the current kind uses, hides
the empty-irrelevant ones, and ALWAYS shows an irrelevant field that carries a non-NULL value
(flagged "not used by this kind", so the maintainer can clear it).

**Always shown (every kind):** `kind`, `module`, and the audit-trio editable cells
`last_verified_at_version`, `verified_by`, `evidence_kind`. `valid_from_version` is always shown
read-only (identity key, law 7). **`verified_date` is NOT in the always-shown set** — it renders
ONLY when the row is verified (`last_verified_at_version` non-empty), read-only and system-set
(TRD D17b); an unverified row shows no `verified_date` cell at all.

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
- **Navigate-away while dirty — the unsaved-changes guard (law 10 / TRD D44)** — s04 is THE
  dirty-editor whose pending edits the guard protects. While ≥1 field is dirty, any navigation that
  would leave s04 — a `‹ back` (e.g. back to the s08 report a `[Fix ▸]` came from), a navigator
  entity-switch, or a top-level entry — is **intercepted by a confirm** (the `overlay surface`:
  **Save** primary → runs the save spine (validate → s06 confirm → atomic txn) then navigates /
  **Discard** outline-danger → drops the edits then navigates / **Cancel** subtle → stays in s04).
  Nothing is saved OR lost without the explicit choice. (A clean s04 — no dirty field — navigates
  away with no guard.)
- **Validation error** — inline on the offending field; `[Review changes]` disabled; no
  write attempted; the error names the cause (user-caused copy).
- **Disabled (read-only context)** — viewing a row in a context where editing isn't
  offered (e.g. a non-selected compare column before `[Edit]`): fields render read-only.
- **Unverified / resolver-failure** — the advisory `warning banner` (`Alert`) + the override
  carried to save (law 4, D15).
- **Audit trio locked while unverified (KI-0024)** — while the prospective
  `last_verified_at_version` is empty, the two manual audit-trio inputs (`verified_by` +
  `evidence_kind`) are DISABLED, each carrying the hint "Set Last verified at version first — the
  verification fields fill in together." (text, not color-alone — law 7). This makes the all-or-null
  trio orphan unreachable by construction: a lone trio cell cannot be written (manually OR by the D29
  evidence_kind auto-suggest, which is suppressed on an unverified row). Setting the version runs the
  FIX-1 coupling (auto-fills the empty trio) and re-enables the inputs. `verified_date` is already
  read-only + hidden-when-unverified, so it is not in the locked set.
- **Check verdict states** (the `verdict badge`, when a version-matching DLL is linked — TRD
  D24–D31): **no badge** (no matching DLL linked → degraded, the unverified advisory stands);
  **checking** (a brief loading state in the reserved badge region while the per-kind check runs
  over the DLL bytes — law 1, the region is reserved); **Unchanged** ✓ / **Changed** /
  **Ambiguous** (+ `[show matches]` steer) / **CannotCheck** — each with its reserved detail
  line; the badge re-evaluates as the kind-relevant fields change (a dirty re-check). All
  advisory (law 4) — `[Review changes]` is NOT gated on the verdict (only on validator
  validity, law 6); a Changed/Ambiguous verdict warns + carries the "I accept — save anyway"
  override to s06, it never disables save.
- **Arrived from a failing `[Fix ▸]` — no DLL linked (TRD D45)** — the "What diverged" banner
  (engine prose `detail`) + every recorded field value + the non-blocking "Link the running-game
  DLL to see what diverged" prompt (prefilled from the report's `game_version`). No per-field
  "actual" yet; the editor is fully usable (law 4 — never blocked on the DLL).
- **Arrived from a failing `[Fix ▸]` — DLL linked, diff computed (TRD D45 / D47)** — each diverged
  kind-relevant field shows recorded-vs-actual inline (diverged marker, glyph+text not
  color-alone, law 7) in its reserved gutter (no reflow, law 1) AND is wrapped in the
  **diverged-field box** (`error`-token border + faint tint around the field row, TRD D47) so the
  eye lands on the break; the marker is retained alongside the box (glyph + text + box, never
  color-alone, law 7), the box reuses the row's reserved space (no reflow, law 1), and the
  border/tint resolve to the `error` token (law 9). Every diverged field is boxed equally; the box
  PERSISTS through editing (with the dirty marker layered on) and clears only when the saved
  correction makes the row non-divergent. The "What diverged" banner names the diverged field(s) at
  a glance. A field that matches the build shows no marker and no box.
- **Arrived from a failing `[Fix ▸]` — no divergence found / cannot check (TRD D45)** — the
  per-kind check PASSES against the linked build → surfaced honestly (*"no field diverged against
  the linked build"*), never a silent empty; OR the check cannot run for this kind (a
  `vtable_index` deferral, a function row with no recorded `content_hash`) → an honest CannotCheck
  reason in the banner, advisory, never a faked pass.
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
- **Law 10** — navigating away from s04 while a field is dirty surfaces the unsaved-changes guard
  (Save/Discard/Cancel) before the navigation; nothing is saved or lost silently (TRD D44). s04 is
  the dirty-editor the back-stack's guard clause protects.
- **Law 9** — tokens only (the tooltip affordance, the stray flag, and the kind-conditional grid
  all use theme tokens — no raw hex/px). The `?` help affordance is a focusable button (keyboard +
  screen-reader reachable); the Mantine `Tooltip` wires the aria description.

## Responsive behavior
- **Wide:** renders inline below the version table in the detail pane.
- **Phone:** when reached from s02's selected row it renders inline within the detail
  drill-down (scrolls); when reached from a compare column (s03) it opens as a full-screen
  sheet (the edit-existing confirmation precedes it). The dirty-marker gutter + "was:" line +
  error line keep their reserved space at every width (law 1).
