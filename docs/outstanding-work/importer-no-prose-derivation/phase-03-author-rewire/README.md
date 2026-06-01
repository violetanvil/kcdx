# Phase 3 — author values + rewire readers + delete prose machinery

**Intent.** Fill the new explicit columns with the real values (hand-authored,
user-verified), rewire both writers to READ the authored columns, and delete the
prose machinery entirely (`kind_offset_and_slot()` + the dead `infer_kind()`).
After this phase the importer reconstructs NO value from prose or inference — the
plan's goal is met. Split into two commit-grain steps so the value-authoring (a
seed data change the user verifies) and the code rewire+delete land as distinct,
individually-green commits.

Shared spec: [`../context.md`](../context.md).

## Step ledger

| Step | Status | Commit |
|---|---|---|
| 3 hand-author the existing values into the new columns (user-verified) | DONE | a72736b |
| 4 rewire both writers to read authored columns + delete prose machinery | DONE | (landed) |

## Step docs

3. [step-3-author-values.md](step-3-author-values.md)
4. [step-4-rewire-and-delete.md](step-4-rewire-and-delete.md)

## Verification gate (phase end)

- The 6 vtable_index rows (ids 19–24) carry their slot int in the new authored
  column (19→4, 20→13, 21→7, 22→16, 23→12, 24→13), **binary-verified against
  WHGame.dll** — a full seed-verification recon (`_research/seed-verification-recon/`)
  confirmed all 6 against the actual CGame/CScriptSystem/CScriptTable vtables
  (anchored on CGame::Update slot 7), not the predecessor-project prose they were
  first transcribed from. The same recon verified all 143 mechanical facts +
  re-derived all 111 function ABIs from the binary (0 mismatches).
- `kind_offset_and_slot()` and `infer_kind()` are DELETED from
  `import_to_sqlite.py`; no regex/prose/inference path produces any DB value
  (the out-of-scope legitimate regexes in `../context.md` remain).
- Both writers (`build_rows`, `_seed_action_rows`/`_apply_one_db`) read the
  authored columns; `row_builder` docstrings updated (they currently claim the
  derivation "stays in import_to_sqlite.py").
- All oracles green; `test_rebuild_oracle` byte-identical after the DELIBERATE
  `oracle_baseline.json` re-capture for the now-populated columns. apply ==
  rebuild holds (both read the same authored columns).
- A grep of the importer for `notes`-as-value-source / `re.search` over `notes` /
  `infer` returns nothing outside the documented legitimate sites.
