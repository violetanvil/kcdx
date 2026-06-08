---
id: KI-0008
opened: 2026-06-08
status: open
commit_at_filing: 68fc4719e3a8c0a0e8c0e8c0e8c0e8c0e8c0e8c0
---

# KI-0008 — editing an OPEN-interval version row silently no-ops (the value never persists)

**Status:** open (root cause narrowed to the action-derivation layer; not yet fully isolated)

## Symptom

Editing the value of an **open-interval (current)** `address_versions` row in the
maintainer tool (e.g. the v1.6 row's `rva` 1→2) and confirming **reports success**
(`/confirm/update-version` returns 200, the UI shows "saved") but **nothing
persists** — a refresh / compare still shows the old value. No error, no crash.
A **closed-interval** row's edit, by contrast, writes and persists correctly.

## Evidence

Observed live 2026-06-08 (user): "updating the test item for v1.6 RVA from 1 > 2
saved but never actually showed on the screen. after a refresh, it still said 1."
Confirmed by direct probe of the live USER DB and the update path (on a throwaway
copy — nothing real mutated):

- The v1.6 row (`id=321146 kcdx_id=158 valid_from=2 valid_through=NULL`, OPEN)
  holds `rva=1` after the "successful" save — the edit did not land.
- Running the real update path (`db_editor.update_version_row(out_dir, None, 158,
  '1.6', {'rva':'2'}, version=('1.6',2), defer_commit=True)` → `commit(handle)`)
  on a throwaway copy: rva stays `1` **even in the in-flight uncommitted `ucon`
  connection** — so the write never APPLIES to the row; it is not a commit
  problem.
- The CLOSED v1.5 row edit (`update_version_row(..., '1.5.1164953', {'rva':'9'},
  version=('1.5.1164953',1))` → commit) WRITES correctly (`id=321145 rva=9`,
  `valid_through=1` preserved). The discriminator: closed-row edit lands;
  open-row edit drops.

## Facts

- The bug is in the WRITE path, not the seed-edit layer. The seed edit is
  correct: `seed_csv_edit.update_row_in_place((158,'1.6'), {rva:2})` on the
  EXPORTED prospective seed changes the v1.6 row's rva to `2` (the v1.5 row stays
  `0x00000001`) — verified by direct call (PROBE: exported-seed edit).
- The action reaching `_full_column_update_one` for the 158 edit was for the
  **WRONG row + WRONG value**: `non_trio_differs(av_id=321145, a[rva]=1,
  a[valid_from_tag]='1.5.1164953')` — i.e. the action targets the **v1.5 CLOSED
  row** with the **unchanged rva=1**, not the v1.6 row with rva=2 (PROBE: spy on
  `_present_row_non_trio_differs`/`_full_column_update_one` for kcdx_id==158).
  `non_trio_differs` correctly returns False for that (nothing changed for v1.5)
  → no write → silent no-op.
- `kind=2` = `callsite` (so the update takes the MINT branch of
  `_full_column_update_one`, not the function-promote branch).
- The failing path (`apply_direct_edit` → `_apply_one_db` →
  `_full_column_update_one`) is the same pre-existing step-3c update path
  (`f0dbf10`) as KI-0007 — distinct mechanism (KI-0007 was the
  `valid_through`-clobber CRASH, now fixed).

## Open questions

- **Why does the action-derivation produce a v1.5 (closed) action with rva=1
  instead of a v1.6 (open) action with rva=2?** The edited prospective seed has
  v1.6→rva2 correctly, but the action that reaches `_full_column_update_one` is
  for v1.5 with the unchanged value. The defect is in how `apply_direct_edit` /
  `_seed_action_rows` / the current-DB-vs-prospective-seed diff converts the
  edited seed into per-row actions — it is mis-attributing the v1.6 change (or
  dropping it and emitting only the unchanged v1.5 action). [causal — unverified]
- Candidate: the diff/action-builder keys the changed row on something that maps
  the open-interval (`valid_through IS NULL`) row's edit onto the closed sibling,
  OR the open-interval row is excluded from the action set entirely (so only the
  unchanged v1.5 action is emitted). Probe `_seed_action_rows` / the
  prospective-vs-current diff for the 158 rows to see which action(s) it builds
  and from which seed row. [causal — unverified]

## Trail

| Date       | Action                                                                                  | Result  |
|------------|-----------------------------------------------------------------------------------------|---------|
| 2026-06-08 | Filed from the live session; probed the update path on a throwaway DB copy (closed-row edit writes; open-row edit drops, even in the in-flight txn). Narrowed to the action-derivation: the action reaching the apply targets the WRONG row (v1.5 closed) with the UNCHANGED value, not v1.6 with rva=2. Seed-edit layer verified correct. | symptom + facts captured; the action-derivation probe is the next step (which action(s) `_seed_action_rows`/the diff builds for the 158 edit) |
