---
id: KI-0007
opened: 2026-06-07
status: open
commit_at_filing: 55b5c2129b311450cb3ecee11cfab940e29bb0d4
---

# KI-0007 — /confirm/update-version 500s on a version-row edit (partial-unique-index collision)

**Status:** open (investigating)

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
