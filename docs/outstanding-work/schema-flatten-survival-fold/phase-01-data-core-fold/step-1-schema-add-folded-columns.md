# Step 1 — schema + row-builder: add the folded columns to address_versions

**What.** Add the genuinely-survival-only columns to the `address_versions` schema, additively:
`aob`, `anchor_string`, `rule`, `slot_count`, `expect_unique` (nullable typed), and
`derives_from` (a nullable self-FK → `address_versions.id`). This is the additive first move of
the fold — the columns exist, NO reader populates or consumes them yet, the `survival` table is
untouched. The rebuild oracle stays green (the new cells are NULL on every row; the survival
table is unchanged) after a deliberate baseline re-capture reflecting the additive columns.

**Scope.** `seeds_shared/schema.py` — add the six columns to `SCHEMA["address_versions"]`
(matching the survival table's column SQL types: `aob`/`anchor_string`/`rule` TEXT,
`slot_count`/`expect_unique`/`derives_from` INTEGER), to `USER_COLUMNS["address_versions"]` (they
ship to USER — curated survival data the engine survival pass reads), and the `expect_unique`
dict-encoding move if survival's was dict-encoded (check `DICT_COLS`; survival's columns were
not dict-encoded, so likely no `DICT_COLS` change — confirm). `row_builder.py`
(`build_curated_row`) accepts + carries the six new columns (defaulting NULL). NO importer
populate logic (step 2), NO exporter (step 3), NO survival-table change. One commit.

**Test bar.** `tests/test_rebuild_oracle.py` re-captured (deliberate + inspected, a BASELINE
PROVENANCE entry): a fresh rebuild's `address_versions` gained the six NULL columns on every row,
the `survival` table is byte-identical, no other table moved. Plus a `validate_db_shape.py` /
`test_survival_table.py`-adjacent assertion that the new columns exist on `address_versions` with
the right SQL types. Runnable now — purely additive, the oracle rebuilds from the current seeds.

**Dependencies.** None — the additive first step. (Rests on the current 157-row seeds + the
landed rebuild oracle, both present.)

**Reference.** [`../plan-spec.md`](../plan-spec.md) §"The survival fold mapping".

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§11.2 (the fold mapping — exactly which columns move + their note: aob/anchor_string/rule/
slot_count/expect_unique → nullable typed columns, derives_from → self-FK) + §11.1 (the flat
shape). The SQL types match the former `survival` SCHEMA entry (`schema.py`).

**Disassembler-test / author-burden.** N/A — a schema column addition resolves no game-function
input; the columns hold already-authored survival data (the maintainer authors them in the seed,
no hex/ABI burden).
