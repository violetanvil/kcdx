# plan-spec — schema-flatten-survival-fold

The shared spec every step leans on. Steps cross-link here rather than restating context.

## Goal

Fold the `survival` sibling table into `address_versions` and delete it — implement the
settled D22/§11 flat-schema migration so the reference DB is **one flat table per concern,
one typed column per fact, no polymorphic structure**. The end state: `address_versions`
carries every per-version resolve fact (including the former survival payload) as its own
typed column; the `survival` table + its `kind_form` discriminator are gone; every consumer
(importer, exporter, engine, read seam) reads the folded columns.

## The settled design — the authority every step builds to

**[`data/maintainer-tool/design.md`](../../../data/maintainer-tool/design.md) D22 + §11**
(committed `8c87b2f`) — the flat-schema finality decision. §11.1 (the two-flat-table shape),
§11.2 (the survival fold mapping — the authority for which columns move where), §11.3 (the
`ResolveResult` comprehensiveness contract), §11.4 (the closed `kind` universe), §11.5 (the
new-kind migration checklist — the step spine this plan follows).

Supporting authority: `data/seeds/policy.md` §"kind" (the closed kind enum + per-column
semantics), `src/refdb.h` (`ResolveResult` — the comprehensiveness contract every column
back-maps to).

Every step that touches the schema/engine dereferences to the named §section — the step doc
is a pointer, not a replacement (`.claude/rules/spec-conformance.md`).

## The survival fold mapping (D22 §11.2 — verbatim authority)

| Former `survival` column | Folds to | Note |
|---|---|---|
| `aob`, `anchor_string`, `rule`, `slot_count`, `expect_unique` | new nullable typed columns on `address_versions` | the genuinely-survival-only facts; gated by `kind` |
| `derives_from` | a nullable self-FK column on `address_versions` (→ `address_versions.id`) | the survival-DAG edge; same shape as `valid_through`/`superseded_by` — not polymorphism |
| `content_hash`, `length` | **dropped** (redundant) | proven a row-for-row copy of the av row's body fingerprint (157/157 identical); the existing `address_versions.content_hash`/`length` serve both the resolve path and the function-hash survival check |
| `kind_form` | **deleted** (the discriminator) | `kind` already determines the survival form (per `survival_builder._KIND_TO_FORM`) |
| `id`, `address_version_id` | gone | the 1:1 sibling row is gone; the facts live on the av row |

## Cross-step invariants

- **Dual-write → prove equal → delete (the migration's safety spine, user-settled
  2026-06-03).** The fold lands additively first (the columns are added, then populated on
  the av row WHILE the `survival` table is still written in parallel), the equivalence is
  PROVEN (each av row's folded cells == the survival row's, row-for-row — the same 157/157
  check that proved `content_hash` redundant), every consumer is migrated to read the av
  columns, and ONLY THEN is the `survival` table deleted. No step leaves data without a
  home; the delete is the last act, gated on proven equivalence + migrated consumers.
- **The rebuild oracle is the per-step gate** (`tests/test_rebuild_oracle.py`). Each
  data-core step is verifiable against it: an additive-columns step asserts only
  `address_versions` gained NULL cells; a populate step asserts the av columns match the
  survival row; the delete step re-captures the baseline with `survival` gone. A re-capture
  is deliberate + inspected + documented in the oracle's BASELINE PROVENANCE log
  (`test_rebuild_oracle.py` §"Re-capture … ONLY for a deliberate, reviewed output change").
- **Convergence holds (apply == rebuild).** The fold touches `_apply_one_db`'s write path
  (the av row gains the folded cells) — every data-core step pins direct-write == seed-rebuild
  byte-identity for the av columns (the `_db_fingerprint` whole-table oracle, the same
  convergence proof the step-3c/4c work used).
- **The `ResolveResult` comprehensiveness contract (§11.3) is wired at the engine step.**
  Every folded column the engine reads gains a `ResolveResult` field (append-only, per the
  struct's convention); a column with no consuming field is dead weight, a field with no
  column is a hole caught at decode/compile. The engine step is the place this contract is
  satisfied for the folded columns.
- **D13/law 6 — zero rule logic outside the data-core.** The fold's write/read semantics
  stay in `seeds_shared`/`import_to_sqlite`; the backend + engine only consume. The
  `survival_builder`'s `_KIND_TO_FORM` per-kind dispatch (which form each kind takes) is
  preserved — it moves from "build a survival row" to "decide which av columns a kind
  populates," same logic, same single source.
- **Incremental order** (`.claude/rules/incremental-delivery.md`): schema (additive) →
  populate-on-av + dual-write (equivalence provable) → export/round-trip → engine + read
  consumers → delete the sibling. Each step independently verifiable when it lands.

## The new-kind migration checklist (§11.5 — the procedure this plan instantiates)

This plan IS the §11.5 checklist run once (for the survival fold). The checklist itself —
`ResolveResult` → schema → importer → exporter → engine SELECT → baseline re-capture — is the
durable procedure for any FUTURE new-kind migration; it lives in design §11.5, not re-derived
here. This plan's step order mirrors it.

## Reuse — what already exists (do not rebuild)

- **`survival_builder._KIND_TO_FORM`** — the kind→form dispatch; preserved as the
  kind→which-av-columns dispatch.
- **The rebuild oracle + the mini-dump fixture + `_db_fingerprint`** — the per-step gate +
  convergence proof, all landed.
- **The 157-row seeds + the rebuilt DB** (current, post-`71cfce3`) — the equivalence baseline.

## Coverage map — every design element → its step

| Design element | Covered by | Notes |
|---|---|---|
| Flat one-typed-column-per-fact `address_versions` (D22/§11.1) | Phase 1 step 1 (schema) + step 2 (populate) | the folded columns added + filled |
| Survival fold — aob/anchor_string/rule/slot_count/expect_unique → av columns (§11.2) | Phase 1 step 1 (add) + step 2 (populate) | nullable typed columns, kind-gated |
| Survival fold — derives_from → av self-FK (§11.2) | Phase 1 step 1 (add) + step 2 (populate) | self-FK, resolved from seed kcdx_id |
| content_hash/length dropped as redundant (§11.2) | Phase 3 step 6 | the survival-table delete removes the dupes; av columns already serve |
| kind_form discriminator deleted (§11.2) | Phase 3 step 6 | the dispatch moves to the av-row build |
| Exporter + round-trip carry the folded columns | Phase 1 step 3 | byte-identity preserved |
| Engine SELECT + decode + ResolveResult fields (§11.3) | Phase 2 step 4 | the comprehensiveness contract wired |
| Read seam + backend surface the folded columns | Phase 2 step 5 | read_api + the maintainer-tool backend passthrough |
| `survival` table deleted (§11.2) | Phase 3 step 6 | the last act, gated on proven equivalence + migrated consumers |
| 156/157 vtable_slot/struct_offset into columns | Phase 3 step 6 | the columns are now first-class; the RE-handoff slot/offset move from notes prose to cells |
| ResolveResult comprehensiveness contract (§11.3) | Phase 2 step 4 | every folded column the engine reads gains a field |
| new-kind migration checklist (§11.5) | this plan-spec (the procedure) | this plan instantiates it once; the durable checklist stays in design §11.5 |
