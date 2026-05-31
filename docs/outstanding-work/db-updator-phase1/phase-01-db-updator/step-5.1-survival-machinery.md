# Step 5.1 — survival machinery: seed columns + `survival` table + populate + oracle

**What.** The CODE half of the per-kind survival fingerprint (DB-side). Add the
per-kind survival COLUMNS to `address_versions_seed.csv`, add the `survival`
table to the schema, have apply+rebuild populate the `survival` table FROM those
seed columns, and extend the oracle. This sub-step defines the seed column shape
that step 5.2 fills with verified data; it does NOT author the per-kind values
(an empty seed column → an empty/placeholder survival datum, never a guess).
Runs in PARALLEL with 5.2.

**This sub-step is DB-side ONLY.** The engine consumer (per-kind `SurvivalCheck`
dispatch, production binder feed, dependency-DAG walk at check time, apply-time
`on_changed` enforcement) is engine work tracked in the restructure survival
lineage — not here.

**Authoritative design:**
[`data/maintainer-tool/fingerprint-per-kind.md`](../../../../data/maintainer-tool/fingerprint-per-kind.md)
— the per-kind datum table + the sibling-`survival`-table schema decision (1:1
with `address_versions`, `kind_form` + `derives_from` FK + kind-typed payload)
are settled there.

**Scope (commit-grain).**

1. **Seed columns** (the shape 5.2 writes into). Add to
   `address_versions_seed.csv` the per-kind survival columns — survival is
   per-version data, so it lives alongside `rva`/`signature`. The exact column
   set realizes the design doc's payload (one open sub-decision: typed columns
   vs one small typed blob — pick, and document the chosen format in
   `data/seeds/policy.md` so 5.2 has an authoring spec). At minimum the columns
   must express, per kind: an AOB pattern+mask (callsite, instruction_anchor),
   an anchor string (string_anchor), a derivation rule + `derives_from` ref
   (data_slot, instruction_anchor), a slot count (vtable_base), and the
   base-ref+index (vtable_index). All NULL-valid (a row whose kind doesn't use a
   column leaves it empty; an unfilled row leaves it empty until 5.2).
   - Update `seeds_shared/validators.py` `read_address_versions_seed` to read +
     validate the new columns (format checks only — e.g. AOB is hex+`?`; a
     malformed value is a HARD ERROR, an EMPTY value is allowed).
   - Update `data/seeds/policy.md` (the authoring law) with the new columns:
     required/optional, format, which kind uses which.

2. **`survival` table** in `seeds_shared/schema.py` — 1:1 with
   `address_versions` (`address_version_id` FK), `kind_form`, `derives_from` FK
   (nullable; the cross-row DAG edge), and the kind-typed payload columns.
   Append-only to the schema. Confirm USER vs DEV projection while building (the
   curated survival datum ships to USER; decide + note).

3. **Populate (rebuild + apply, shared).** A single shared builder maps
   `(kind, seed survival columns, the function fingerprint)` → the `survival`
   row, called by BOTH `build_rows` (rebuild) and the incremental `apply` — same
   no-drift discipline as the step-1 row-builder. Per kind:
   - function kinds → `function_hash` form from the bulk row's
     `content_hash`+`length` (no seed authoring; reuses what's there).
   - callsite / instruction_anchor → `aob` form from the seed AOB column.
   - string_anchor → literal form from the seed string column.
   - data_slot → derivation form from the seed rule column + `derives_from`.
   - vtable_base → table-shape form from the seed slot-count column.
   - vtable_index → slot-target form (base-ref + index); population DEFERRED
     (emit the row shape, datum marked unpopulated — gated on the runtime-vtable
     verification path).
   - **A non-function row whose seed survival column is EMPTY (5.2 hasn't filled
     it yet) → emit the `survival` row with `kind_form` set and the payload
     empty.** Never derive a value by guessing or by parsing `notes`.

**Out of scope:** authoring the per-kind VALUES (that's 5.2); any engine
consumer; the `notes`-prose parse (deliberately not done — 5.2 authors verified
values into the structured columns instead).

**Disassembler test.** The maintainer authors a structured AOB / slot-count /
rule into a seed column (5.2); the machinery reads it. No hand-computed hash or
file offset is demanded by the machinery. Compliant — and 5.2's authoring is
itself the expert RE pass that the structured column exists to capture once.

**Test bar.** Oracle extended to the `survival` table: a rebuild emits a
`survival` row per curated entity with the right `kind_form`; function rows carry
the body hash; non-function rows carry their seed-column datum when present and
an empty payload when not; `derives_from` is set where the kind has a dependency.
An `apply` that adds/edits a curated entity writes a `survival` row
byte-identical to a rebuild's (apply==rebuild, now covering the new table).
`oracle_baseline.json` gains the `survival` table; the re-capture is deliberate +
documented per the test docstring's discipline. Because 5.2 may not have filled
values yet when 5.1 lands, the 5.1 oracle asserts the SHAPE (row present,
kind_form correct, empty-payload-where-unfilled) — the value assertions tighten
as 5.2 fills the columns.

**Dependencies.** Step 1 (shared schema + builder), Step 4 (apply add/promote
path the survival write rides alongside). Independent of step 6. PARALLEL with
5.2 (5.1 defines the columns; 5.2 fills them — 5.1's column shape should land or
be agreed before 5.2's values merge).

**Reference.**
- [`../context.md`](../context.md).
- [`data/maintainer-tool/fingerprint-per-kind.md`](../../../../data/maintainer-tool/fingerprint-per-kind.md).
- [`step-5.2-fill-survival-seed.md`](step-5.2-fill-survival-seed.md) — the
  parallel data-fill sub-step.
