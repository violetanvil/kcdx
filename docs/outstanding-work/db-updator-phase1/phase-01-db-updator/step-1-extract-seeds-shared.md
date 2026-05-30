# Step 1 — extract `seeds_shared/`; rebuild becomes a thin caller

**What.** Pull the schema declaration, the seed validators, the dict-encoding
codec, and (newly factored) the single `address_versions` row-builder out of the
~1490-line `import_to_sqlite.py` into a new private package
`data/refdata-extractor/python/seeds_shared/`. The rebuild path is rewired to
call the extracted module instead of its inline copies. This step adds no new
behaviour — it is the behaviour-preserving refactor that creates the shared
foundation every later `apply` step builds on, satisfying R3
(extract-don't-duplicate) and the one-file/one-concern bar (the importer is well
past ~300 lines). See [`../context.md`](../context.md) decision 1, `plan.md` §5.

**Scope (commit-grain).**
- New `seeds_shared/schema.py` — `SCHEMA`, `USER_COLUMNS`, `DEV_TABLES`,
  `USER_TABLES`, `DICT_COLS`, `EVIDENCE_KIND_ENUM`, `ADDRESS_KINDS` moved
  verbatim from the importer.
- New `seeds_shared/validators.py` — `read_module_seed`,
  `read_address_names_seed`, `read_address_versions_seed`, plus the post-loop
  cross-row checks (supersession acyclicity, pair integrity, FK closure,
  audit-trio integrity, `(kcdx_id, valid_from_version)` uniqueness).
- New `seeds_shared/dict_codec.py` — the `Dicts` class.
- New `seeds_shared/row_builder.py` — **the single function mapping a validated
  seed row → the `address_versions` column dict.** Factored OUT of `build_rows`'
  step-6 promote/mint logic so it is callable standalone. This is the
  highest-consequence extraction (`../context.md` cross-step invariant: the
  oracle holds because both writers share this).
- `import_to_sqlite.py` imports from `seeds_shared/` and deletes the inlined
  copies; `build_rows` step-6 calls `row_builder.build`.
- `seeds_shared/__init__.py`.

**Disassembler test.** No author-facing input added (internal refactor of
maintainer tooling). N/A.

**Test bar.** Oracle slice: `--rebuild` produces a DB byte-identical to a
pre-refactor rebuild on the same seeds + dump (the extraction changed no output).
A Python equality check (row-set per table, dict tables included) under
`data/refdata-extractor/`.

**Dependencies.** None — this is the foundation step.

**Reference.** [`../context.md`](../context.md);
[`data/maintainer-tool/plan.md`](../../../../data/maintainer-tool/plan.md) §5, §8.
