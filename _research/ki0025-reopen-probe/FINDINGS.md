# KI-0025 reopen probe — FINDINGS

Investigation into why the kcdx_id=12 interval-reopen `/confirm` fails, and whether a
"reopen capability" needed building.

## Verified facts (probed, not theorized)

- **PROBE H** (`probe_reopen_validate.py`, headless validate-only): the kcdx_id=12
  interval-reopen (`edits={valid_through_version:""}`) VALIDATES CLEAN through
  `update_version_row(validate_only=True)` → `{'tag':'1.5.1164953','ordinal':1}`. The
  data-core's interval-REOPEN path ALREADY EXISTS: `_seed_action_rows`
  (import_to_sqlite.py L1807) emits the baseline-tag action carrying `valid_through_tag`
  (L1840); `_apply_one_db`'s interval branch (L2520-2530) writes
  `UPDATE address_versions SET valid_through = NULL` when the cleared close differs from
  the row's current value. **The recorded PROBE G ("baseline-tag reopen is dropped, build
  a reopen capability") was a MISREAD** of the db_editor.py:356-362 KI-0008 comment (that
  comment governs the NON-baseline `update_target` INJECTION, not baseline rows).

- **PROBE I** (in-source traceback at the `_resolve_derives_from_av_id` raise site,
  re-run live `/confirm` which self-reverts on failure → live DB untouched): the actual
  `/confirm` failure is NOT in validation and NOT a stale backend. The raise fires at
  `_present_row_non_trio_differs` (L2209), called from `_apply_one_db` (L2519), resolving
  `df_kid=12` against the open connection where kcdx_id=12 shows `[(321128, 12, 1, 1)]`
  (valid_from=1, valid_through=1 — CLOSED). Stack (verbatim): `confirm_update_version` →
  `_run_confirm` (L259) → `update_version_row` (L363) → `_drive_direct_over_prospective_seed`
  (L237) → `apply_direct_edit` (L3977) → `_apply_one_db` (L2519) →
  `_present_row_non_trio_differs` (L2209) → `_resolve_derives_from_av_id` RAISE.

- **Seed ground truth** (`data/db-export/address_versions_seed.csv`): kcdx_id=12's
  `survival_derives_from` is EMPTY (does not derive from itself). **kcdx_id=9** carries
  `survival_derives_from=12`. So the `df_kid=12` being resolved is kcdx_id=9's edge.

## Root mechanism (the real blocker)

`apply_direct_edit` → `_apply_one_db` walks the action set from `_seed_action_rows`, which
emits an action for EVERY baseline-tag row — including kcdx_id=9 (PRESENT, unchanged).
For kcdx_id=9's no-op action, `_present_row_non_trio_differs` EAGERLY resolves its
`survival_derives_from=12` edge (just to COMPARE whether the survival cells differ) via
`_resolve_derives_from_av_id(con, 12)`, which looks for kcdx_id=12's OPEN row. In the same
apply pass, kcdx_id=12 is still closed at the point kcdx_id=9's comparison runs → the edge
resolution RAISES before the kcdx_id=12 reopen interval-write (L2524) can take effect.

This is the **chicken-and-egg PROBE E flagged**, now mechanism-pinned: the broken 9→12
edge is re-resolved by ANY apply pass that touches the baseline action set, so the
single-row update path for kcdx_id=12 cannot self-heal — kcdx_id=9's eager edge-resolution
trips on the pre-reopen state.

## Implication for the fix

- Fix "build a reopen capability" is MOOT (the path exists — PROBE H).
- The real blocker is the eager survival-edge resolution during a no-op comparison of an
  UNRELATED present row (kcdx_id=9) against a DB state where its dependency (kcdx_id=12) is
  still closed. The repair must make kcdx_id=12 open in the state kcdx_id=9's comparison
  sees — a genuine design fork surfaced to the user (how to break the chicken-and-egg).

## Reusable wiring

- `probe_reopen_validate.py` — headless validate-only driver for any `update_version_row`
  edit (no DB write, no backend). Reuse for "does this edit pass pre-write validation?".
- PROBE I recipe: a 6-line traceback dump at the `_resolve_derives_from_av_id` raise
  (L1976) + a live `/confirm` (self-reverts) is the cheapest way to locate which apply
  caller resolves a survival edge against what DB state. Reconstruct from here, not source.
