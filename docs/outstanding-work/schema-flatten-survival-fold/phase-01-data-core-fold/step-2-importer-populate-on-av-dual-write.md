# Step 2 — importer populate-on-av + dual-write (the equivalence proof)

**What.** Populate the folded columns ON the `address_versions` row from the per-kind dispatch,
WHILE still writing the `survival` table in parallel (dual-write). Both the rebuild path and
`_apply_one_db` write the per-kind survival cells onto the av row — reusing
`survival_builder._KIND_TO_FORM` (the dispatch moves from "build a survival row" to "decide which
av columns a kind populates"; same logic, same single source). The `survival` table is STILL
written (unchanged) — so this step can PROVE each av row's folded cells equal its survival row,
row-for-row (the 157/157 equivalence, the safety proof that makes the Phase-3 deletion lossless).

**Scope.** `import_to_sqlite.py` — the rebuild's curated-row build + `_apply_one_db`'s write path
populate the folded av columns per kind (function kinds: `derives_from` only if seeded;
callsite/instruction_anchor: `aob` + `expect_unique`; string_anchor: `anchor_string` +
`expect_unique`; data_slot: `rule` + `derives_from`; vtable_base: `slot_count`; vtable_index:
deferred-empty + `derives_from` if seeded — the same per-kind payload `_KIND_TO_FORM` dispatches).
`survival_builder.py` — refactor so its per-kind dispatch feeds BOTH the (still-written) survival
row AND the av columns (one dispatch, two write targets during dual-write). The `survival` table
write is UNCHANGED (dual-write). One commit.

**Test bar.** A new equivalence assertion (in `test_survival_table.py` or a new
`test_survival_fold.py`): for every curated av row, its folded cells (`aob`/`anchor_string`/
`rule`/`slot_count`/`expect_unique`/`derives_from`) EQUAL its `survival` row's corresponding
cells, row-for-row, across the rebuilt DB (the 157/157 proof). PLUS the convergence pin:
direct-write == seed-rebuild byte-identity for the av columns (`_db_fingerprint`). PLUS the
rebuild oracle re-captured (the av columns now non-NULL where the kind populates them; the
survival table unchanged). Runnable now — the columns exist (step 1), the seeds carry the
survival data.

**Dependencies.** Step 1 (the folded columns exist on `address_versions` + `build_curated_row`
carries them).

**Reference.** [`../plan-spec.md`](../plan-spec.md) §"Cross-step invariants" (dual-write → prove
equal → delete; convergence).

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§11.2 (the fold mapping — which kind populates which column) + the preserved
`survival_builder._KIND_TO_FORM` per-kind dispatch (the single source of kind→form, unchanged).

**Disassembler-test / author-burden.** N/A — the populate reads already-authored seed survival
columns; no game-function input is resolved here.
