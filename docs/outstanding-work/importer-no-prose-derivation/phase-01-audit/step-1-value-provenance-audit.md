# Step 1 — exhaustive value-provenance audit + per-kind column plan

**What.** Audit the refdata importer end-to-end and produce a complete
value-provenance table: for EVERY value written to an `address_versions`,
`address_names`, or `survival` DB column, name its source and classify it. Then
produce the per-kind explicit-column plan Phase 2 builds from. This is the
"prove nothing is missed" step — no code or seed change.

**Scope (commit-grain).**
- Walk `import_to_sqlite.py` (`build_rows`, `_seed_action_rows`, `_apply_one_db`,
  the bulk pass) + `seeds_shared/row_builder.py` (`build_bulk_row`,
  `build_curated_row`) + `seeds_shared/survival_builder.py`. For each column a row
  carries, record where its value comes from.
- Classify each source: **AUTHORED** (a seed CSV column), **DUMP** (a dump table
  field — functions/signatures/caller_reg_args/statements/…), **GAME-VERSION**
  (the resolved `.rdata`/json version string), **FORMAT-VALIDATOR** (a regex that
  validates an authored column's shape, not a value source), or **PROSE/INFERENCE**
  (a value reconstructed from `notes` text or a heuristic — the defect class).
- Confirm the four known findings (F1 `kind_offset_and_slot` slot regex; F2
  `offset` hardwired NULL; F3 dead `infer_kind`; F4 `value`/`offset`/`vtable_slot`
  sprawl — see `../context.md`) AND surface any additional prose/inference site.
- Produce the **per-kind column plan**: for each PROSE/INFERENCE finding, name the
  explicit authored column that replaces it (per decision 2 — each kind's datum is
  its own named column), and name what the `value`/`offset`/`vtable_slot` sprawl
  collapses into. Enumerate which kinds need which datum column (vtable_index → a
  slot column; data_slot → an offset column; callsite → its offset; etc.).
- Land the audit table + column plan into `../context.md` (extend it) or a
  dedicated `audit.md` it links. Docs-only.

**Test bar.** No code change → no test runs. The deliverable is the audit table
itself; acceptance = it covers every written column with a classified source and
the per-kind column plan is concrete enough for Phase 2 to implement without a
fresh design decision. (If the audit surfaces a value whose correct authored
form is genuinely ambiguous, that is an unsettled fork → STOP and route to the
user, do not guess.)

**Dependencies.** None — first step.

**Reference.** [`../context.md`](../context.md) findings F1–F4 + the out-of-scope
legitimate-regex list. The importer:
`data/refdata-extractor/python/import_to_sqlite.py`; the shared builders:
`data/refdata-extractor/python/seeds_shared/`.
