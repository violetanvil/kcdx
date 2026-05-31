# Context — importer: no prose-derivation, every field an authored column

Shared spec for the `importer-no-prose-derivation` plan. Every step doc leans on
this; steps cross-link here rather than restating.

## Goal

The seed CSV is the de-facto, versioned, single source of **every** field the
refdata importer needs. Eliminate **all** prose-derivation and inference from
`data/refdata-extractor/python/import_to_sqlite.py` + `seeds_shared/`: ZERO
regex, ZERO `notes`-parsing, ZERO heuristic inference for any value the importer
writes to a DB column. Each address-kind's needed datum is its **own explicit,
named, authored column** in the seed CSV. `notes` remains human commentary only —
never parsed.

## Settled design decisions (locked by the user — verbatim)

1. **Full audit + fix ALL.** Not just the one regex found — sweep the whole
   importer for every prose-derived / inferred value and fix every one. The
   Phase-1 audit must *prove* each value the importer produces comes from an
   authored column (or a legitimate non-prose source), so nothing is missed.
2. **Every item type gets its own column.** Each kind's datum is its own named
   seed column. Do NOT overload one shared `value` column for "slot int for
   vtable_index, offset for data_slot" — that sprawl (`value` + `offset` +
   `vtable_slot` all overlapping) is collapsed into explicit per-kind columns.
3. **Hand-author the 6 existing vtable_index slots** (ids 19–24). Read each
   row's `notes`, transcribe the slot int into the new column by hand, the user
   verifies the 6 values. NO regex, not even as a transient migration aid.

## Confirmed audit findings (entry state — Phase 1 re-proves + extends)

From the pre-plan audit of `import_to_sqlite.py`:

- **F1 — `kind_offset_and_slot()` regex-scrapes the vtable slot from `notes`.**
  Lines ~321–334. `re.search(r"index\s*=\s*(\d+)")` (+ a fallback
  `r"slot\s+(\d+)\s*\(0-indexed\)"`) pulls the slot int out of prose. LIVE on
  BOTH writers: rebuild `build_rows` (~line 564) and apply `_seed_action_rows`
  (~line 1238). The slot int currently exists ONLY in `notes` prose (e.g. id 20
  `"vtable index = 13 (0-indexed)"`); the CSV `value`/`vtable_slot` columns are
  empty for ids 19–24.
- **F2 — `offset` is hardwired NULL with no authored column.** Same function's
  docstring: "offset stays NULL for the unblock." For `data_slot` the consumer
  offset is a needed value with no authoring surface — it can never be set.
- **F3 — `infer_kind()` is dead prose-sniffer code.** Lines ~295–318. Sniffs
  `kind` from `name`/`notes` substrings. Replaced by the authored `kind` column
  (`authored_kind`); grep confirms ZERO live callers. Still present in the file →
  a loaded gun to delete.
- **F4 — column sprawl.** Schema carries three overlapping columns for the same
  conceptual datum: `value` ("slot int for vtable_index, offset for data_slot"),
  `offset` ("callsite consumer offset"), `vtable_slot` ("mirrors value for that
  kind"). `row_builder.build_curated_row` mirrors `vtable_slot` → `value`
  (~line 154). Decision 2 collapses this into explicit per-kind columns.

## Out of scope — legitimate non-prose regex (do NOT touch)

These are NOT prose-derivation; leave them:

- `seeds_shared/version_resolver.py` `_VERSION_RE` and `import_to_sqlite.py`
  `read_game_version` regexes — parse the GAME's own version string from
  `whdlversions.json` / the DLL `.rdata`. Authoritative external source, not seed
  prose.
- `seeds_shared/validators.py` `_VERIFIED_DATE_RE`, `_AOB_TOKEN_RE` — FORMAT
  validators on already-authored columns (date shape, AOB hex-token shape). They
  validate a column's value; they do not reconstruct a value from prose.

## Cross-step invariants

- **apply == rebuild, byte-identical.** Every change preserves it. The gate per
  phase: the mini-dump oracles (`test_apply_add_entity`, `test_apply_reverify`,
  `test_survival_table`, `test_apply_deprecate_supersede`) + the full-dump
  `test_rebuild_oracle` all green; `test_rebuild_oracle` byte-identical to the
  recorded baseline.
- **`oracle_baseline.json` re-capture is deliberate.** A change to authored
  columns / row shape re-captures the baseline ON PURPOSE, with the reason noted
  in the capture (the schema-column change is the documented intended diff).
- **Seed edits are UPDATEs to existing rows, not new rows.** Adding a column +
  authoring values into existing ids 19–24 (and any data_slot offsets) is an
  UPDATE — AP18's new-row-approval gate does NOT apply (see
  `.claude/rules/address-library.md` + `data/seeds/policy.md`).
- **One writer, two callers.** `build_rows` (rebuild) and `_seed_action_rows` /
  `_apply_one_db` (apply) must read the SAME authored columns the SAME way —
  the shared `seeds_shared/row_builder.build_curated_row` is the single column
  assembler; both paths feed it from the authored columns, never from prose.

## Source material

- The importer: `data/refdata-extractor/python/import_to_sqlite.py`.
- The shared builder + schema + validators: `data/refdata-extractor/python/seeds_shared/`.
- Authoring law: `data/seeds/policy.md`; Address Library rule:
  `.claude/rules/address-library.md`.
- The seed CSVs: `data/seeds/address_versions_seed.csv` (per-version facts incl.
  the per-kind datum columns), `data/seeds/address_names_seed.csv` (entity-level,
  incl. `notes`).
