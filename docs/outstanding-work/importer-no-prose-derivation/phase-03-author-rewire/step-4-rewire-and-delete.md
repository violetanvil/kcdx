# Step 4 — rewire both writers to read authored columns + delete prose machinery

**What.** Point both importer writers at the new authored columns, then delete
the prose machinery entirely. After this step the importer reconstructs NO value
from `notes` or inference — the plan's goal is met.

**Scope (commit-grain).** Per the resolved column plan in `../context.md`:
- Rewire `build_rows` (~line 564) and `_seed_action_rows` (~line 1238) +
  `_apply_one_db` to READ the authored `vtable_index` column instead of calling
  `kind_offset_and_slot()`. The slot value flows from the CSV cell straight into
  `build_curated_row`'s `vtable_slot` param. (No `offset` to feed — that column
  is deleted.)
- DELETE `kind_offset_and_slot()` entirely (lines ~321–334).
- DELETE the dead `infer_kind()` (lines ~295–318 — F3, zero live callers).
- Update `seeds_shared/row_builder.py`'s module docstring: it currently states
  "The kind/offset/vtable_slot DERIVATION (infer_kind + kind_offset_and_slot)
  stays in import_to_sqlite.py" — that is now false; rewrite to "read from
  authored columns; no derivation."
- Remove the now-unused `notes_by_kid` plumbing IF the audit confirmed `notes` is
  no longer read for any value (keep `notes` as a written commentary column;
  only remove the code that PARSED it).
- Re-capture `oracle_baseline.json` (DELIBERATE) for the now-populated columns
  flowing into rows; note the reason in the capture provenance.

**Test bar.** The four mini-dump apply oracles
(`test_apply_add_entity`, `test_apply_reverify`, `test_survival_table`,
`test_apply_deprecate_supersede`) + full-dump `test_rebuild_oracle` all PASS;
`test_rebuild_oracle` byte-identical to the re-captured baseline; apply ==
rebuild (both now read the same authored columns). Plus a grep proof: no
`re.search`/`re.match` over `notes`, no `infer`, no `notes`-as-value-source
remains outside the documented legitimate sites (`../context.md` out-of-scope
list). The vtable_index rows resolve their slot from the authored column (verify
a row's emitted `vtable_slot`/new-column value equals the hand-authored cell).

**Dependencies.** Step 3 (values must be authored before the readers switch to
them — otherwise the rewire reads empty columns and silently NULLs the slots).

**Reference.** [`../context.md`](../context.md) findings F1–F4 + the goal
statement. Importer: `data/refdata-extractor/python/import_to_sqlite.py`; shared
builder: `data/refdata-extractor/python/seeds_shared/row_builder.py`.
