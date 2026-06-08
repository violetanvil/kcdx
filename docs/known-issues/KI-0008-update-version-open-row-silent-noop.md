---
id: KI-0008
opened: 2026-06-08
status: open
commit_at_filing: 68fc4719e3a8c0a0e8c0e8c0e8c0e8c0e8c0e8c0
---

# KI-0008 — editing an OPEN-interval version row silently no-ops (the value never persists)

**Status:** open (root cause CONFIRMED — `_seed_action_rows` filters out non-baseline-tag rows; fix design pending Gate A)

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

## Root cause (CONFIRMED — static probe of `_seed_action_rows`)

`apply_direct_edit` builds its action set via `_seed_action_rows(state)`
(`import_to_sqlite.py:3187`, commented "current-(GAME_VERSION_)tag actions
only"). `_seed_action_rows` (`import_to_sqlite.py:1154`) **filters out every seed
row whose `valid_from_version` is not `GAME_VERSION_TAG`** (`= "1.5.1164953"`,
`import_to_sqlite.py:133`):

```python
vfv_tag = vs["valid_from_version"].strip()
if vfv_tag != GAME_VERSION_TAG:      # import_to_sqlite.py:1170-1172
    continue
```

So editing the **v1.6** row produces a prospective seed with v1.6→rva2 correctly,
but `_seed_action_rows` SKIPS the v1.6 row (its tag `1.6` ≠ `1.5.1164953`) and
emits only the (unchanged) v1.5 action → `_present_row_non_trio_differs` returns
False → no write → silent no-op + a 200 confirm. A v1.5 (baseline-tag) edit is
NOT filtered, so it writes — the exact closed-vs-open discriminator.

**Why inevitable:** the action-derivation was written for the baseline-rebuild
path, where only `GAME_VERSION_TAG` rows are actions. It was never extended to
emit an UPDATE action for an edit to an existing NON-baseline-tag row. The
`new_tag` create-version path (`apply_direct_edit`'s `new_tag`/`new_tag_kcdx_id`)
handles a non-baseline tag, but ONLY as an INSERT of a brand-new version — not an
UPDATE of an existing non-baseline-tag row. So editing any version row that is
NOT at the baseline tag falls through the gap: filtered out of the action set,
and not a new-tag insert.

This is a genuine action-derivation gap (a design-surface boundary), not a typo.
The fix must make the update path emit an UPDATE action for the edited row at its
OWN tag (the `version=(tag,ordinal)` already passed to `apply_direct_edit`),
without breaking the baseline-rebuild action set or the new-tag create path —
routes through Gate A (architect-review) before landing.

## Fix design (Gate A architect-review: `hold` — design-determined, no user fork)

US-5 (`design.md:360-367`) requires editing ANY existing version row, so the
non-baseline no-op is a design-conformance defect, not a choice. The
`GAME_VERSION_TAG` filter in `_seed_action_rows` is load-bearing for the
baseline-REBUILD path (the convergence oracle's reference `apply_seeds`, whose
import resolves only `GAME_VERSION_TAG`, `import_to_sqlite.py:445/453`) — it must
NOT be touched. So the only valid fix:

**Add a single-row UPDATE action for the edited non-baseline-tag row in the
interactive update path (`apply_direct_edit`).** The edited tag is already in hand
(`version=(tag,ordinal)`, currently unused for action-building, `:3176`). When
`version`'s tag != `GAME_VERSION_TAG` AND `new_tag is None`, build ONE UPDATE
action for the edited `(kcdx_id, tag)` from the prospective seed — mirroring the
existing `_new_tag_action_from_seed` precedent (`:3083`) but as a PRESENT/UPDATE
action, not an INSERT — and apply it through the existing `_apply_one_db` PRESENT
path (which already matches any tag by `(kcdx_id, valid_from)`,
`:1825-1889`). `_seed_action_rows` + the rebuild/oracle path stay UNTOUCHED.
(Rejected: parameterizing `_seed_action_rows`'s filter — it would emit an action
for EVERY non-baseline row, a multi-row write blowout worse than the no-op.)

**Note (not a fork, surfaced to the user):** the new non-baseline UPDATE path is
NOT convergence-oracle-coverable — the rebuild reference is structurally
baseline-only, so it cannot reach this path. Its correctness rests on the
cause-test + the reused PRESENT machinery, a deliberate consequence of the
rebuild being baseline-only.

**Cause-test:** a DB with an existing NON-baseline-tag row (a v1.6 row) + the
baseline v1.5 row; edit a non-trio column on the v1.6 row via the update path;
assert (1) the v1.6 edit PERSISTS (the mechanism check), (2) the baseline v1.5
edit still persists (no regression), (3) no OTHER v1.6 row is touched (guards the
multi-row blowout), (4) the v1.6 row's kcdx_id/valid_from/valid_through unchanged
(the KI-0007 identity-preservation lesson, pinned for the non-baseline path).

## Trail

| Date       | Action                                                                                  | Result  |
|------------|-----------------------------------------------------------------------------------------|---------|
| 2026-06-08 | Filed from the live session; probed the update path on a throwaway DB copy (closed-row edit writes; open-row edit drops, even in the in-flight txn). Narrowed to the action-derivation: the action reaching the apply targets the WRONG row (v1.5 closed) with the UNCHANGED value, not v1.6 with rva=2. Seed-edit layer verified correct. | symptom + facts captured; the action-derivation probe is the next step (which action(s) `_seed_action_rows`/the diff builds for the 158 edit) |
| 2026-06-08 | PROBE (static): read `_seed_action_rows` (`import_to_sqlite.py:1154`) + its caller `apply_direct_edit:3187` — does it filter actions by tag? | ROOT CAUSE FOUND. `_seed_action_rows` does `if vfv_tag != GAME_VERSION_TAG ("1.5.1164953"): continue` (`:1170-1172`) — it SKIPS every non-baseline-tag seed row. A v1.6 edit's row is filtered out → no action → no write → silent no-op. The baseline-tag (v1.5) edit is not filtered → writes. The update path was never extended to emit an UPDATE action for an existing non-baseline-tag row (only the baseline-rebuild action set + the new-tag INSERT path exist). Design-surface gap → Gate A next. |
