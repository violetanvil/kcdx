---
id: KI-0007
opened: 2026-06-07
status: Closed
closed: 2026-06-08
closed_by_commit: 68fc471
commit_at_filing: 55b5c2129b311450cb3ecee11cfab940e29bb0d4
---

# KI-0007 — /confirm/update-version 500s on a version-row edit (partial-unique-index collision)

**Status:** closed (root cause = the full-column UPDATE clobbered `valid_through` to NULL, re-opening a closed interval → the `ix_av_open_unique` partial index tripped; fixed in `68fc471` by excluding the non-editable identity/interval columns from the UPDATE). User-confirmed: the IntegrityError/500 no longer fires on a version-row edit. A SEPARATE, distinct defect — an OPEN-interval row's edit silently no-ops (the value never lands) — surfaced during verification and is tracked as its own known-issue (the action-derivation drops the current-row edit; NOT this crash, NOT this fix).

## Symptom

Editing any existing `address_versions` row's value in the maintainer tool and
confirming (the s04 field editor → s06 Confirm) fails: `POST
/confirm/update-version` returns **500**. The DB write is rolled back, so nothing
persists — the value stays unchanged. The UI surfaces it as "Save failed —
nothing was written. network error calling /confirm/update-version: NetworkError
when attempting to fetch resource" (the browser rendering the 500's CORS-less
error response as a NetworkError).

## Evidence

Captured live 2026-06-07 in the running backend (`127.0.0.1:8000`,
`KCDX_CHECKOUT=kcdx-maintainer-data`). The server-side traceback on every
`POST /confirm/update-version`:

```
sqlite3.IntegrityError: UNIQUE constraint failed: address_versions.kcdx_id
```

The crash site + call chain (verbatim from the traceback):

- `import_to_sqlite.py:1730` `_projected_update` — the full-column
  `UPDATE address_versions SET {set_clause} WHERE id = ?` write.
- ← `import_to_sqlite.py:1712` `_full_column_update_one`
- ← `import_to_sqlite.py:1855` `_apply_one_db`
- ← `import_to_sqlite.py:3193` `apply_direct_edit`
- ← `db_editor.update_version_row` → `routes_confirm.confirm_update_version:469`.

The reproduced session edited entity **158** rows (entity 158 had a
**create-version** earlier in the same session — a 2nd row authored at a new game
version `1.6`, which returned 200). Every subsequent `update-version` confirm on
158 returned 500 with the above crash.

## Facts

- The crash is `sqlite3.IntegrityError: UNIQUE constraint failed:
  address_versions.kcdx_id` at `_projected_update`'s full-column UPDATE
  (static — read from the captured traceback).
- The constraint is a **PARTIAL unique index**: `Partial UNIQUE (kcdx_id) WHERE
  kcdx_id IS NOT NULL AND valid_through IS NULL` — "at most one current
  (open-interval) form per CURATED entity" (`schema.py:136-139`, read verbatim).
  NOT a composite `(kcdx_id, valid_from)` key (the filing lead's guess, refuted
  by the schema read before any probe).
- `_projected_update`'s `set_cols` = every `address_versions` column except the
  autoincrement `id` — so the UPDATE rewrites `kcdx_id` AND `valid_through`
  (`import_to_sqlite.py:1726-1731`, read verbatim).
- The `update-version` write path is `apply_direct_edit` → `_apply_one_db` →
  `_full_column_update_one` → `_projected_update`, last touched by step-3c
  (`f0dbf10`, the full-column UPDATE applier) — BEFORE this session. NOT a
  2.6-verdict-badge change (2.6 was read-path + frontend only).
- Entity 158 ("test") holds exactly ONE open-interval row in the live USER DB:
  `id=321146 valid_from=2 valid_through=NULL` (open) + `id=321145 valid_from=1
  valid_through=1` (CLOSED). The DB satisfies the partial unique index — it is a
  CLEAN state, not a pre-existing two-open-rows defect (PROBE A).
- The live index is `ix_av_open_unique`: `CREATE UNIQUE INDEX ix_av_open_unique
  ON address_versions(kcdx_id) WHERE kcdx_id IS NOT NULL AND valid_through IS
  NULL` (PROBE A — read from `sqlite_master`).

## Fix design (Gate A architect-review: `hold` — design-determined, no user fork)

The invariants determine the fix (no design fork to surface): US-5 names
`valid_from_version` + `kcdx_id` (+ entity identity) as **non-editable**, and the
interval model reserves `valid_through` open/close to **create-version**
(US-6) + **re-verify** (D29/D34/D35) — an US-5 column *edit* NEVER changes the
interval or identity. So the full-column UPDATE must NOT rewrite `kcdx_id` /
`valid_from` / `valid_through`. The architect traced the code: `kcdx_id` (=`kid`)
and `valid_from` (=`vf_id`) **round-trip** to the existing values (both sourced
from the row's match key), so `valid_through` is the SOLE clobber — but the
MECHANISM is "a full-column UPDATE rewrites columns US-5 declares immutable."

**Fix (B, recommended):** exclude `kcdx_id` / `valid_from` / `valid_through` from
the UPDATE's `set_cols` in the US-5 update write path — never rewrite the
non-editable identity/interval columns. Makes all three correct by CONTRACT (the
code's own docstring already claims "`(kcdx_id, valid_from)` identity key …
valid_through unchanged" — which the code violates for valid_through). Fallback
**(A)** (preserve `valid_through` from the existing row, mirroring the existing
`av_row["id"] = av_id` override) only if (B) entangles the shared
`_promote_bulk_in_place` helper. **Cause-test:** edit a CLOSED row through
`/confirm/update-version`, assert (1) no 500, (2) the edited row's `valid_through`
is UNCHANGED (the closed interval PRESERVED — the AP17 mechanism check), (3)
`kcdx_id`/`valid_from` unchanged, (4) exactly one open-interval row remains.

## Open questions

- **Does the full-column `_projected_update` write `valid_through = NULL` onto a
  CLOSED row?** PROBE A proved the live DB is CLEAN (158 has exactly one
  open-interval row; the create-version closed the prior interval correctly). So
  the collision is not a bad pre-existing state — it is manufactured by the
  UPDATE itself. The leading hypothesis: editing the CLOSED row (id=321145,
  valid_through=1) confirms a full-column update whose built `av_row` carries
  `valid_through = NULL` (the `build_curated_row` default / the prospective seed
  dropped the closed-interval marker), nulling the closed row → a SECOND
  open-interval row (321145 + 321146) → the partial unique index trips. (Probe B
  — observe the `av_row` the UPDATE builds, specifically its `valid_through`.)
  [causal — unverified]

## Trail

| Date       | Action                                                                                  | Result  |
|------------|-----------------------------------------------------------------------------------------|---------|
| 2026-06-07 | Filed from the live 8000 session; schema read refuted the composite-key lead → partial unique index `(kcdx_id) WHERE valid_through IS NULL` is the real constraint | symptom + facts captured; PROBE A pending |
| 2026-06-07 | PROBE A: read-only dump of entity 158's address_versions rows + the index DDL from the live USER DB | 158 has exactly 1 open-interval row (id=321146 valid_through=NULL; id=321145 valid_through=1, CLOSED). DB is CLEAN — the create-version closed the prior interval correctly. Refutes "two open rows pre-existing"; the collision is manufactured BY the update. Index confirmed: `ix_av_open_unique ON address_versions(kcdx_id) WHERE valid_through IS NULL`. |
| 2026-06-07 | PROBE B (static): read `_full_column_update_one` (1672/1696) + `build_curated_row` (row_builder.py) — what `valid_through` does the built av_row carry? | CONFIRMED nulled. The MINT branch hardcodes `"valid_through": None` (row_builder.py:159); the PROMOTE branch `v = dict(base_row)` carries the bulk row's `valid_through` (also None — bulk rows are open) and never overrides it. Neither call site (1672/1696) passes valid_through. `_projected_update` writes EVERY column → editing a CLOSED row (valid_through=1) overwrites it to NULL → a 2nd open-interval row → `ix_av_open_unique` trips. Root cause established (static evidence, no live mutation). |
| 2026-06-07 | FIX (B): exclude `kcdx_id`/`valid_from`/`valid_through` from the US-5 UPDATE's set_cols (`_UPDATE_PRESERVE_COLUMNS`, `import_to_sqlite.py:1732`); cause-test `test_closed_row_edit_preserves_interval` asserts a CLOSED-row edit preserves valid_through | landed `68fc471`. Gate: data-core 139 passed (the 1 red is the pre-existing rebuild-oracle seed-drift), backend 66/66. Step-review PROCEED (all 5 properties; revert-to-red confirmed the cause-test fails with the exact KI IntegrityError without the fix). |

## Resolution

- **Root cause:** the US-5 in-place version-row UPDATE was a *full-column* write
  that rewrote a column it must never touch. `_full_column_update_one`
  (`import_to_sqlite.py:1712`) built a fresh `av_row` via `build_curated_row` —
  which **always** produces `valid_through = None` (the MINT branch hardcodes it,
  `row_builder.py:159`; the PROMOTE branch copies a bulk `base_row` whose
  `valid_through` is also `None`, `row_builder.py:88`, and never overrides it) —
  and neither call site (`import_to_sqlite.py:1672/1696`) passed a
  `valid_through`. `_projected_update` then wrote **every** column except the
  autoincrement `id`, so editing a CLOSED row (one with a real `valid_through`
  ordinal — e.g. entity 158 "test", `id=321145 valid_through=1`, closed when its
  sibling `id=321146` was authored as a newer version) overwrote that row's
  `valid_through` from its real ordinal to `NULL`. That re-opened the closed
  interval, leaving the entity with **two** rows where `valid_through IS NULL`
  (321145 + 321146), which violates the partial unique index `ix_av_open_unique
  ON address_versions(kcdx_id) WHERE kcdx_id IS NOT NULL AND valid_through IS
  NULL` ("at most one open-interval row per curated entity",
  `schema.py:136-139`). SQLite raised `IntegrityError: UNIQUE constraint failed:
  address_versions.kcdx_id` (the error truncates to the indexed column), the
  transaction rolled back, and the endpoint returned 500 — so nothing persisted
  (the UI's "saved-then-NetworkError" was the browser rendering that 500). The
  original path made the wrong write inevitable because a full-column UPDATE
  rewrites the interval/identity columns US-5 declares non-editable from a builder
  that has no knowledge of the existing row's interval; `kcdx_id` and `valid_from`
  escaped corruption only because they are sourced from the row's own match key,
  while `valid_through` had no such protection.

- **Fix (`68fc471`):** exclude the three non-editable identity/interval columns —
  `kcdx_id`, `valid_from`, `valid_through` — from the US-5 UPDATE's `set_cols`
  (`_UPDATE_PRESERVE_COLUMNS` in `import_to_sqlite.py`). The UPDATE no longer
  touches those columns, so the DB keeps their stored values (a closed interval
  stays closed); every other editable column still writes. This makes the code
  match its own docstring contract (it already claimed the `(kcdx_id, valid_from)`
  identity key + `valid_through` were preserved — a claim the code violated for
  `valid_through`). Scoped to the UPDATE path: `_projected_update` has exactly one
  caller (`_full_column_update_one`); the ADD/promote path uses the separate
  `_projected_insert` / `_promote_bulk_in_place`, so a freshly-authored row still
  correctly gets `valid_through = None` (a new row is open). Architect-review
  (Gate A): `hold` — design-determined by US-5 + the interval model, no user fork.

- **Verification:** the cause-test `test_closed_row_edit_preserves_interval`
  (`test_db_editor_update.py`) reproduces the closed+open two-row shape, edits the
  CLOSED row, and asserts (1) no `IntegrityError`/500, (2) the closed row's
  `valid_through` is PRESERVED (the mechanism check), (3) `kcdx_id`/`valid_from`
  unchanged, (4) the edit applied, (5) exactly one open-interval row remains.
  Revert-to-red confirmed: without the fix the test fails with the exact KI
  `IntegrityError` at `import_to_sqlite.py:1730`. Gate B (root-cause-verifier):
  `land-fix`. User-confirmed: on the live re-run the user did NOT hit the
  IntegrityError/500 (the original symptom — "i didnt hit the error message
  this time"), and a throwaway-DB probe of the live update path confirms a
  CLOSED-row edit now succeeds + persists (`id=321145 rva→9`, `valid_through=1`
  preserved, no IntegrityError). The crash this KI tracked is gone.

- **Distinct follow-on (NOT this crash):** during verification the user hit a
  SEPARATE defect — editing an OPEN-interval (current) row silently no-ops (the
  value never lands, the confirm returns 200, nothing persists). Probed to the
  action-derivation layer (the closed-row edit writes; the open-row edit's value
  is dropped before the apply). Tracked as its own known-issue; it is a different
  mechanism from the `valid_through`-clobber crash fixed here.
