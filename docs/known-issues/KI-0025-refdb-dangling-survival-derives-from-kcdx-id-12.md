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
| 2026-06-15 | PROBE F: attempt the repair — `/save`+`/confirm/update-version` on kcdx_id=12 with `edits:{valid_through_version:""}` (reopen) | PARTIALLY BLOCKED. Preview `valid:true` but `field_delta:[]` (the interval edit is applied via the prospective-seed CSV edit + whole-state validation, NOT the field_delta display computation — an empty preview delta is EXPECTED, not the cause; a CLOSE on an open entity also shows `[]`). `/confirm` FAILED with the SAME `survival_derives_from kcdx_id=12 ...` and rolled back. First attempt had a STALE-backend confound ([Errno 10048] port-bind); after a clean kill (PID 25640) + restart, ONE clean re-attempt: backend log "confirm failed -- validator rejected the edit", NO DB change (kcdx_id=12 still closed). So the failure is the apply's PRE-WRITE whole-state validation, not the post-commit re-export. |
| 2026-06-15 | PROBE G: read the update-version apply path (`update_version_row` db_editor.py:243; `apply_direct_edit`/`_drive_direct_over_prospective_seed`) | The edit DOES thread into the prospective seed CSV (`seed_csv_edit.update_row_in_place` clears `valid_through_version`). MISREAD (corrected by PROBE H below): I read the db_editor.py:356-362 KI-0008 comment ("adds the single UPDATE action ONLY when the edited row's tag is NON-baseline") as "a baseline-tag reopen is dropped." That comment is about the `update_target` NON-baseline INJECTION (apply_direct_edit L3923-3928) — it does NOT gate baseline rows out. `_seed_action_rows` (L1807) iterates the BASELINE-tag rows and emits an action for each (incl. kcdx_id=12). The conclusion "no UPDATE action → no write" was wrong. |
| 2026-06-15 | PROBE H: code-read the apply path end-to-end + run a headless validate-only reopen (`_research/ki0025-reopen-probe/probe_reopen_validate.py` — `update_version_row(kcdx_id=12, edits={valid_through_version:""}, validate_only=True)`, opens NO DB) | REOPEN PATH ALREADY EXISTS. `_seed_action_rows` (L1807) emits kcdx_id=12's baseline-tag action carrying `valid_through_tag` (L1840); `_apply_one_db`'s interval branch (L2520-2530) computes `want_vt_id=None` for the cleared close, `interval_changed=True`, and writes `UPDATE address_versions SET valid_through = NULL`. The validate-only run returns `{'tag':'1.5.1164953','ordinal':1}` — VALIDATES CLEAN. So PROBE G's "fix step 1 (build a reopen capability)" is MOOT — the capability is present. The PROBE F `/confirm` failure was a DIFFERENT cause. (CONFIRMED a stale backend PID 15948 WAS still holding port 8000 — the `[Errno 10048]` confound PROBE F half-saw.) NEXT: re-run the real repair through a guaranteed-clean backend. |
| 2026-06-15 | PROBE I: re-ran the real `/confirm/update-version` (kcdx_id=12 reopen) through a GUARANTEED-CLEAN backend (killed the stale PID, fresh process), with a temporary traceback at the `_resolve_derives_from_av_id` raise site (the confirm SELF-REVERTS on failure → live DB untouched; probe removed after, no residue, captured to `_research/ki0025-reopen-probe/FINDINGS.md`) | STILL FAILS — and the clean backend PROVES it is not the stale-backend confound. Verbatim stack: the raise fires at `_present_row_non_trio_differs` (L2209), called from `_apply_one_db` (L2519), resolving `df_kid=12` against the open conn where kcdx_id=12 = `[(321128,12,1,1)]` (CLOSED). Seed ground truth: kcdx_id=12's `survival_derives_from` is EMPTY; **kcdx_id=9** carries `survival_derives_from=12`. ROOT MECHANISM (pinned): `_apply_one_db` walks EVERY baseline-tag action incl. kcdx_id=9 (PRESENT, unchanged); for kcdx_id=9's no-op comparison `_present_row_non_trio_differs` EAGERLY resolves its 9→12 edge against the open DB where kcdx_id=12 is still closed → RAISES before kcdx_id=12's reopen interval-write (L2524) takes effect. The broken 9→12 edge is re-resolved by ANY apply pass touching the baseline set → the single-row kcdx_id=12 update cannot self-heal (PROBE E's chicken-and-egg, now mechanism-exact). The repair needs kcdx_id=12 OPEN in the state kcdx_id=9's comparison sees — a genuine fix fork. |

## Validator hardening — BUILT, tests authored (uncommitted, gated on the repair)

`check_live_entity_has_open_interval` (validators.py) wired into all 3 integrity-pass
sites (import_to_sqlite.py: build_rows, _build_curated_overlay, _validate_full_seed_state)
+ exported from `seeds_shared`. Test `test_live_entity_has_open_interval_accepts_rejects`
(test_db_editor_interval.py): 5 cases (live-closed-only REJECT incl. the kcdx_id=12
repro; deprecated/superseded EXEMPT; live-with-open ACCEPT). **State: the 2 pure-dict
validator tests PASS; the 3 fixture-rebuild tests ERROR in setup — because the `baseline`
fixture rebuilds from the committed seed, which the new check CORRECTLY rejects (the real
kcdx_id=12 defect). This is the validator working, AND the hard ordering proof: the
validator cannot land green until the seed is repaired.** Uncommitted in the working tree.

## Facts (empirical only)

- The integrity check is `SELECT id FROM address_versions WHERE kcdx_id=? AND valid_through IS NULL` — it requires an OPEN-interval row, no version join (PROBE A).
- kcdx_id=12 has exactly one `address_versions` row, `valid_through=1` (closed); the query returns None; it is the only 1-of-159 entity with no open row (PROBE B).
- kcdx_id=12 (`string_exec_autoexec_cfg`) is live — not deprecated, not superseded (PROBE C).
- Its seed row uniquely carries a filled `valid_through_version=1.5.1164953`, which import resolves to the closed interval; `build_curated_row` otherwise always mints `valid_through=None` for a current form (PROBE C/D, `import_to_sqlite.py:1259-1260,2351`).
- The bad close was authored by commit `f15ae4f` (a batch version-row UPDATE); the D40 validator does not front open-row presence, so it passed (PROBE D).

## Root cause (mechanism — VERIFIED, supersedes the filing's UNVERIFIED guess)

TWO coupled defects (a DATA defect + a CODE defect), both verified:

**1. The DATA defect (the bad close).** A batch version-row UPDATE (`f15ae4f`) set
`valid_through_version=1.5.1164953` on kcdx_id=12's SOLE `address_versions` row,
which on import resolves to a closed interval `[1,1]` with no `valid_through IS
NULL` row. kcdx_id=12 is a LIVE, non-deprecated entity (`string_exec_autoexec_cfg`,
the gEnv-resolver anchor), so its current curated form should be an OPEN interval
— but it now has none. The D40 interval-integrity validator checks closed-row
consistency (`valid_through >= valid_from`, no closed-interval overlap) but did NOT
check that a non-deprecated entity retains an open interval, so the bad close
passed validation.

**2. The CODE defect (the eager-resolution chicken-and-egg) — why the repair was
blocked (PROBE I).** `_apply_one_db` walks EVERY baseline-tag action incl.
kcdx_id=9 (PRESENT, UNCHANGED, edge → 12). For kcdx_id=9's no-op comparison,
`_present_row_non_trio_differs` (import_to_sqlite.py) EAGERLY resolved its
`survival_derives_from=12` edge via `_resolve_derives_from_av_id` (which demands
`valid_through IS NULL`) — just to COMPARE whether the survival cells differ. With
kcdx_id=12 still closed, that read-only comparison RAISED, before kcdx_id=12's own
reopen interval-write could take effect. A read-only predicate wrongly required
global interval state, so ANY apply pass over the baseline set re-resolved the
broken 9→12 edge — the single-row reopen could not self-heal. This is why even the
correct reopen edit failed at `/confirm`.

The filing's "integer version-id vs tag" guess is FALSIFIED (PROBE A — the query
has no version join). The "no reopen path / build a reopen capability" theory
(PROBE G) is FALSIFIED (PROBE H — the reopen path exists; PROBE I — the real
blocker was the eager comparison-resolution).

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

REORDER (discovered building step 1, `.claude/rules/incremental-delivery.md`): the
standing-invariant check, the moment it is active, correctly detects the
already-committed broken kcdx_id=12 — so EVERY rebuild/integrity pass over the
current committed seed fails (the test fixtures that rebuild from the seeds fail).
The validator step is therefore NOT independently verifiable while the seed is
broken. The repair must land FIRST (through the OLD code, no new check), cleaning
the seed; THEN the validator lands and every rebuild passes. The pure-dict
validator tests (crafted seed rows, no fixture rebuild) pass standalone either way
— they prove the check accepts legal/exempt + rejects the live-closed-only shape.

SECOND REORDER SUPERSEDED (PROBE H, 2026-06-15): the SECOND REORDER above rested on
PROBE G's misread — it claimed the maintainer tool has NO REOPEN. PROBE H refuted
that by direct code-read + a headless validate-only run: the reopen path EXISTS
(`_seed_action_rows` emits kcdx_id=12's baseline-tag action; `_apply_one_db`'s
interval branch L2520-2530 writes `valid_through = NULL` on a cleared close), and the
kcdx_id=12 reopen VALIDATES CLEAN. So **fix step 1 (build a reopen capability)
COLLAPSES — there is nothing to build.** The PROBE F `/confirm` rejection was the
stale-backend confound (`[Errno 10048]` — the old pre-edit backend kept serving), not
a dropped action. The user (2026-06-15) chose to repair through the EXISTING validated
path now. The fix returns to TWO parts (repair + validator hardening).

THIRD REVISION (PROBE I + Gate-A, 2026-06-15): the real blocker is a CODE defect
(eager survival-edge resolution during a no-op comparison), not just data. The
user chose **Fix A** (fix the eager resolution at the root — Gate-A `forward-and-wait`
recommended A; B/C masking + cornerstone-disqualified). The fix is now: Fix A (code
+ test) → repair → validator hardening → re-add FOpenRaw.

| Step | Action | Status |
|------|--------|--------|
| ~~0~~ | ~~Reopen capability (data-core, NEW)~~ — **COLLAPSED (PROBE H): the reopen path already exists; nothing to build.** | n/a — moot |
| 1 | **Fix A — the eager-resolution defect** (import_to_sqlite.py `_present_row_non_trio_differs`): compare the derives-from edge in kcdx_id space (map the stored av_id back to its kcdx_id via a PK lookup — no `valid_through IS NULL` predicate), so a NO-OP comparison of an unchanged dependent NEVER requires the dependency's open row. Fixes the CLASS, not just kcdx_id=12. + same-change test (`test_present_row_no_op_does_not_require_open_dependency`: no-op doesn't raise on a closed dependency; a genuine edge change is still detected). | DONE — uncommitted; test PASSES |
| 2 | **Repair kcdx_id=12** through the EXISTING validated `/confirm/update-version` (Fix A unblocked it). Both DBs + the git-tracked CSV export now show kcdx_id=12 OPEN (`valid_through IS NULL`); the 9→12 edge resolves. | DONE — backend commit `8ec66e3` |
| 3 | **Validator hardening + test** (`check_live_entity_has_open_interval`, all 3 integrity-pass sites + a 5-case test): now the seed is repaired, the fixture-rebuild tests go GREEN. Also reshaped `test_extend_and_close_land_through_batch` to deprecate before fully-closing a sole interval (the new invariant makes a bare sole-interval close illegal — the test now models the legal retire-then-close path). | DONE — uncommitted; full interval suite 6/6 PASS |
| 4 | **Acceptance**: re-add CCryPak_FOpenRaw (kcdx_id=160, AP18 already approved) cleanly via `/add-db-entity` — a new-entity `/confirm` succeeds. Then the fs-takeover cutover resumes (`../outstanding-work/file-system-takeover/RESUME.md`). | pending |

## Resolution

(unfilled — open)
