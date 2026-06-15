# KI-0025 — reference-DB integrity: dangling `survival_derives_from` → kcdx_id=12 blocks new-entity confirms

**Status:** Open
**Reported:** 2026-06-15
**Severity:** blocks all new Address Library entity additions (the `/confirm` full-DB integrity gate fails for ANY new-entity add, not just the one that surfaced it)

## Symptom

A new-entity add through the maintainer-tool backend validates clean at PREVIEW
(`/save/create-entity` → `valid: true`) but FAILS at `/confirm/create-entity`'s
full-DB integrity check and rolls back (nothing written):

```
{"status":"failed",
 "detail":"survival_derives_from kcdx_id=12 has no curated address_versions row in the DB (add the dependency entity first)",
 "committed":false,"retry":false}
```

Surfaced 2026-06-15 while adding `CCryPak_FOpenRaw` (kcdx_id=160, slot 35) for
file-system-takeover step 3.2. The FOpenRaw row itself is clean and unrelated to
kcdx_id=12 — the integrity check is whole-DB, so it fails the transaction on a
PRE-EXISTING inconsistency that any new add now trips over.

## Evidence (probed, not theorized — `results-driven.md`)

- **kcdx_id=12 DOES have an `address_versions` row** in `data/reference.sqlite`
  (`id=321128`, `kind=5` = string_anchor, `anchor_string='exec autoexec.cfg'`,
  `valid_from=1`, `last_verified_at_version=1`, `verified_by=VioletAnvil`). It is
  the gEnv-resolver string anchor.
- **Two entities reference kcdx_id=12** (`data/db-export/address_versions_seed.csv`):
  - kcdx_id=9 (`instruction_anchor`, RVA 0x0086AD99, AOB `48 8B 0D ?? ?? ?? ??`) —
    `survival_derives_from=12`.
  - kcdx_id=23 (`vtable_index`) — `vtable_slot=12` (note: a different column — this
    one is a slot index that happens to be 12, likely NOT a derives-from ref; the
    derives-from referer is kcdx_id=9).
## Investigation trail

| Date | Action | Result |
|------|--------|--------|
| 2026-06-15 | PROBE A: read the exact integrity-check query in `import_to_sqlite.py` (`_resolve_derives_from_av_id`, L1963-1965) | `SELECT id FROM address_versions WHERE kcdx_id=? AND valid_through IS NULL`. It does NOT join on version — KILLS the integer-vs-tag theory. It demands the OPEN-interval row. |
| 2026-06-15 | PROBE B: run that exact query for kcdx_id=12 against `reference.sqlite` + dump all its rows | Returns None. kcdx_id=12 has ONE row (id=321128) with `valid_through=1` — a CLOSED interval, no open row. Every healthy entity (131/152/9) has `valid_through IS NULL`. 1 of 159 entities has no open row: kcdx_id=12 alone. |
| 2026-06-15 | PROBE C: is kcdx_id=12 deprecated/superseded? + what does the seed CSV carry? | NOT deprecated (`is_deprecated=0`, `superseded_by=None`) — a live entity. `address_versions_seed.csv:13` carries `valid_through_version=1.5.1164953` (FILLED) — the source of the closed interval. 1 of 159 seed rows has `valid_through_version` filled: kcdx_id=12. |
| 2026-06-15 | PROBE D: git-blame the kcdx_id=12 seed row + read the D40 interval-integrity validator | Written by `f15ae4f` ("maintainer-tool: batch save 1 version-row UPDATE(s)", a parallel-chat op). D40 (`check_address_version_intervals`, L611-616) checks closed-row consistency but explicitly NOT open-row presence — so closing a live entity's only interval passes every validator. |
| 2026-06-15 | PROBE E (fix-design facts; Gate-A-substitute, the dispatched architect-review died on a token limit): read `_resolve_derives_from_av_id` callers + `check_address_version_intervals` + the update-version confirm path | Fork-1: the `update-version` confirm resolves only the EDITED row's own derives-from (L2187); kcdx_id=12 is a leaf anchor with none → no self-trip. BUT the post-commit CSV byte-identity re-export (`assert_csv_export_deterministic`) rebuilds the whole DB → re-resolves the 9→12 edge. So a repair that REOPENS kcdx_id=12 (clears its `valid_through_version`) is self-consistent and passes; a repair touching only kcdx_id=9 would not. Fork-2: `check_address_version_intervals` (validators.py:549) DELIBERATELY does not front open-row presence (legit transient two-open-rows during create-version); the DB `ix_av_open_unique` index caps at ≤1 open but permits ZERO. No guard exists for "a live entity has ≥1 open interval" — the hole. The bad row is ALREADY committed, so a write-time-only guard can't detect it; a standing invariant check is needed too. |

## Facts (empirical only)

- The integrity check is `SELECT id FROM address_versions WHERE kcdx_id=? AND valid_through IS NULL` — it requires an OPEN-interval row, no version join (PROBE A).
- kcdx_id=12 has exactly one `address_versions` row, `valid_through=1` (closed); the query returns None; it is the only 1-of-159 entity with no open row (PROBE B).
- kcdx_id=12 (`string_exec_autoexec_cfg`) is live — not deprecated, not superseded (PROBE C).
- Its seed row uniquely carries a filled `valid_through_version=1.5.1164953`, which import resolves to the closed interval; `build_curated_row` otherwise always mints `valid_through=None` for a current form (PROBE C/D, `import_to_sqlite.py:1259-1260,2351`).
- The bad close was authored by commit `f15ae4f` (a batch version-row UPDATE); the D40 validator does not front open-row presence, so it passed (PROBE D).

## Root cause (mechanism — VERIFIED, supersedes the filing's UNVERIFIED guess)

A batch version-row UPDATE (`f15ae4f`) set `valid_through_version=1.5.1164953` on
kcdx_id=12's SOLE `address_versions` row, which on import resolves to a closed
interval `[1,1]` with no `valid_through IS NULL` row. kcdx_id=12 is a LIVE,
non-deprecated entity (`string_exec_autoexec_cfg`, the gEnv-resolver anchor), so
its current curated form should be an OPEN interval — but it now has none. The
D40 interval-integrity validator checks closed-row consistency (`valid_through >=
valid_from`, no closed-interval overlap) but does NOT check that a non-deprecated
entity retains an open interval, so the bad close passed validation. Every
survival-DAG resolution from kcdx_id=9 (`survival_derives_from=12`) then fails
`_resolve_derives_from_av_id`'s open-row lookup, and the whole-DB integrity check
at `/confirm` refuses EVERY new-entity add.

The filing's "integer version-id vs tag" guess is FALSIFIED (PROBE A — the query
has no version join).

## The two-part fix (data repair + validator gap — Gate A pending)

1. **Data repair (the immediate unblock):** reopen kcdx_id=12's interval — clear
   its `valid_through_version` so its sole row is the open current form. An UPDATE
   to an existing row through the validated maintainer-tool path (NOT a hand-edit
   of the CSV).
2. **Validator gap (prevent recurrence):** D40 interval-integrity should reject an
   UPDATE that closes the LAST open interval of a live (non-deprecated/
   non-superseded) entity — closing a current form with no successor + no
   deprecation is the illegal state that slipped through. (A data-core change —
   crosses the design-surface threshold → Gate A.)

## Why it matters

The `/confirm` integrity gate is whole-DB, so this latent inconsistency blocks
EVERY new-entity addition until repaired — not just CCryPak_FOpenRaw. The gate is
working correctly (it refuses to commit on a referential break); the defect is the
pre-existing data inconsistency it is catching.

Do NOT hand-edit `data/db-export/*.csv` or force the write — the integrity gate is
correct; fix the data at its source through the validated path.

## Fix plan (user-decided: repair + harden, full fix — 2026-06-15)

Ordered for incremental verifiability (`.claude/rules/incremental-delivery.md`).

| Step | Action | Status |
|------|--------|--------|
| 1 | **Validator hardening + test** (data-core): a write-time guard rejecting an UPDATE that closes the LAST open interval of a non-deprecated/non-superseded entity, AND a standing integrity check that every live entity has ≥1 open interval. A test reproduces the kcdx_id=12 close (the write-time guard rejects it) AND asserts the standing check flags an already-broken row. The standing check must be ORDERED in the integrity pass so it does NOT block the legitimate repair (step 2) — i.e. the repair reopens the row, then the check passes; the guard fires on a CLOSE, the standing check on the post-state. | pending |
| 2 | **Data repair**: reopen kcdx_id=12 via the validated `update-version` path (clear its `valid_through_version`). Verified self-consistent (PROBE E) — the post-commit re-export then resolves the 9→12 edge. | pending |
| 3 | **Acceptance**: a new-entity `/confirm` succeeds — re-add CCryPak_FOpenRaw (kcdx_id=160, AP18 already approved) cleanly. | pending |

## Resolution

(unfilled — open)
