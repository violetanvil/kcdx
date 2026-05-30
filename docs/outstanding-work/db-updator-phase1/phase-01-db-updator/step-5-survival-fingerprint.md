# Step 5 — per-kind survival fingerprint: `survival` table + populate

**What.** Add the `survival` sibling table to the schema and have BOTH writers
(rebuild's `build_rows` and the incremental `apply`) populate the per-kind
survival datum for every curated entity. Today only function kinds carry a
survival datum (`content_hash`+`length` on `address_versions`); every other kind
is NULL → the survival check skips it (`survival.cpp` `not_applicable`), a
coverage gap. This step closes the DB-side half of that gap: the schema gains a
place to hold the correct per-kind datum, and the applier writes it.

**This step is DB-side ONLY.** The engine consumer — the per-kind
`SurvivalCheck` dispatch, the production binder feed (`RecordTouchedRef`), the
dependency-DAG walk at check time — is NOT db-updator work. It is engine work
tracked in its own item (see Reference). Step 5 makes the DB CARRY the correct
data; consuming it is separate.

**Authoritative design:**
[`data/maintainer-tool/fingerprint-per-kind.md`](../../../../data/maintainer-tool/fingerprint-per-kind.md)
— the per-kind datum table + the sibling-`survival`-table schema decision (1:1
with `address_versions`, `kind_form` + `derives_from` FK + kind-typed payload)
are settled there. This step builds what that doc specifies, DB-side.

**Scope (commit-grain).**
- **Schema:** add the `survival` table to `seeds_shared/schema.py` — 1:1 with
  `address_versions` (`address_version_id` FK), `kind_form`, `derives_from` FK
  (nullable; the cross-row dependency DAG), and the kind-typed `payload`
  (encoding per the design doc's remaining open decision — typed sub-columns vs
  a small typed blob; pick one and note it). Append-only to the schema; both
  DBs get the table (it is per-curated-entity data, like the existing dev
  sibling tables, but the survival datum applies to the USER curated set too —
  confirm projection while building).
- **Populate (rebuild):** `build_rows` emits a `survival` row per curated entity
  from the seed, deriving the datum per kind:
  - function kinds → `function_hash` form: the `content_hash`+`length` already
    on the bulk row (no new authoring).
  - callsite → `aob` form: the AOB pattern+mask, promoted from the `notes`
    prose (the hex pattern is already there).
  - string_anchor → literal-presence form: the string bytes.
  - instruction_anchor → resolver-chain form: the expected instruction shape +
    `derives_from` its string anchor.
  - data_slot → derivation form: the anchor ref + offset rule (`derives_from`).
  - vtable_base → table-shape form: the slot count + structural assertion.
  - vtable_index → slot-target form: base-ref + index + expected-target hash —
    **population DEFERRED** (gated on the runtime-vtable verification path that
    gives the slot a verified target; emit the row shape with the datum marked
    unpopulated, do not block).
- **Populate (apply):** the incremental `apply` writes/updates the `survival`
  row alongside the `address_versions` row it already writes, through the SAME
  per-kind derivation (shared, so rebuild and apply cannot drift — the same
  invariant step 1 established for the row-builder).
- **Promote the notes prose to the datum.** The AOB hex, the resolver steps,
  the slot counts already live in `notes`; this step reads them into the typed
  `payload`. Where a datum cannot be derived from existing prose, surface it
  (it may need a maintainer-authored field — that is the maintainer-tool's job,
  Phase 2, not a guess here).

**Out of scope (engine work, separate item):** the per-kind `SurvivalCheck`
dispatch in `survival.cpp`/`survival_pass.cpp`; wiring binders to
`RecordTouchedRef`; the dependency-ordered DAG walk at check time; apply-time
`on_changed` enforcement. Step 5 writes the data; the engine consuming it is
tracked separately.

**Disassembler test.** The survival datum is derived by the engine/applier from
what the maintainer already authored (the AOB in notes, the kind, the anchor
refs) — the maintainer does not hand-compute a hash or a file offset. Any datum
that WOULD require hand-authoring (e.g. a new AOB the prose lacks) is surfaced
to the maintainer-tool surface, not demanded inline. Compliant.

**Test bar.** Oracle slice extended to the `survival` table: a rebuild emits the
expected per-kind datum for each kind-class (function hash present; callsite AOB
present; data_slot derivation descriptor present + `derives_from` set;
vtable_index row present but datum unpopulated), and an `apply` that adds/edits a
curated entity writes a `survival` row byte-identical to what a rebuild produces
(apply==rebuild, the standing invariant, now covering the new table). The
`oracle_baseline.json` gains the `survival` table; re-capture is a deliberate,
reviewed baseline change (per the test docstring's re-capture discipline).

**Dependencies.** Step 1 (the shared schema + row-builder this extends), Step 4
(apply's add/promote path the survival-row write rides alongside). Independent
of step 6.

**Reference.**
- [`../context.md`](../context.md) — the shared spec + invariants.
- [`data/maintainer-tool/fingerprint-per-kind.md`](../../../../data/maintainer-tool/fingerprint-per-kind.md)
  — the settled per-kind datum + schema design this step builds.
- Engine consumer (separate, NOT this step): the survival-check half lives in
  the restructure plan's survival lineage — the per-version survival mechanism
  (`survival_pass` / `version_check` plumbing already shipped; the production
  binder feed + per-kind dispatch + apply-time `on_changed` enforcement are the
  owed engine work). Step 5 produces the data that engine work will consume.
