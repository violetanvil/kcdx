# Step 4b — save (preview) endpoints (validate + return the field-delta; NO write)

**What.** Add the **Save** endpoints the maintainer hits after editing an entity's fields,
for all six job shapes — re-verify / full-column UPDATE, create version, create entity,
supersede, deprecate. **Save is preview-only**: it VALIDATES the prospective edit through the
data-core's validator (so an invalid edit — malformed date, partial trio, duplicate tuple,
supersession cycle, missing required column — is caught and shown as an error BEFORE the
maintainer reviews the diff) AND returns the **field-delta** (`field: old → new`) for review.
**It writes NOTHING to the DB and holds NO transaction** — the maintainer reviews the diff and
then hits Confirm, which (step 5) runs the whole atomic transaction. Save = "here is what will
change, and it validates"; Confirm = "do it."

**Model note (revised 2026-06-03).** This step was first built (commit `671859f`) on a
held-transaction model — the save opened a deferred-commit transaction at save time and held it
open in a registry across the user's think-time (an executor-per-save, a leak, a reaper). That
model is WRONG (plan-spec §"Cross-step invariants", the Save-previews/Confirm-transacts
revision): nothing touches the DB until Confirm. So this step is REWORKED to preview-only, and
the held-txn machinery (`pending_saves.py` + the held-txn save path) is DELETED.

**Scope.**
- The Save endpoint(s) per job shape: deserialize the prospective edit + the chosen version
  tag → run the data-core validator against the prospective seed (a dry validate — NO DB write,
  NO commit) → return `{field_delta, valid, errors?, ap18_new_row (D11, create), nothing_changed
  (D12)}`. The field-delta reuses step 3's `field_delta` shaping (saved-vs-prospective).
- DELETE `app/pending_saves.py` (the held-save registry / executor / reaper — solves a problem
  the revised model does not have) + the held-txn save router; sweep every reference to them
  (deletion-hygiene): the save router's held-txn imports, the tests asserting the held-but-
  uncommitted property, the plan/doc language.
- The dry-validate seam: the data-core validates the WHOLE prospective seed state before any DB
  open (`apply_seeds` step 2, `_validate_full_seed_state`). A Save preview needs that validation
  verdict WITHOUT the write. Determine the cleanest seam — a validate-only entry the data-core
  exposes, OR running `apply_seeds`/`db_editor` in a mode that validates + returns the verdict
  without opening the DBs for write. If a pure dry-validate needs a small data-core seam (the
  validator runs but no DB write), SURFACE it (it would be a tiny producer sub-step, the 1b/4a
  pattern); if the existing surface already gives a no-write validation path, use it. READ the
  validator surface (`validators.py`, `_validate_full_seed_state`) before deciding.

**Out of scope.** The Confirm transaction (step 5 — the actual write + commit + git). No held
transaction (deleted). No frontend.

**Test bar.** A backend test (`pytest`) on the mini-dump checkout, real API → real data-core:
- each job shape's Save returns the field-delta + `valid: true` for a valid edit, with the
  AP18 flag on create and the nothing-changed verdict where applicable;
- an invalid edit per shape returns `valid: false` + the validator's error (the data-core's
  verdict, surfaced) AND **the DB is byte-identical** (Save wrote nothing — the load-bearing
  proof: a Save, valid or invalid, never touches the DB);
- the version= path: a valid tag resolves via the adapter; an unknown tag → the adapter's
  error → an HTTP error;
- NO held transaction exists after a Save (there is no registry to check — the proof is the
  byte-identical DB + no open connection).
Runnable now (the data-core validator + field_delta + the adapter + the mini-dump fixture
exist).

**Dependencies.** Step 1 (the backend + the version-tag adapter) + step 1b (the `version=`
seam, threaded to the validate path) + step 3 (the field-delta shaping reused in the response)
+ step 2a/2b (the read path that gives the "saved" side of the delta). Phase 1 (`db_editor` +
the validator — landed). Independent of step 4a's deferred-commit seam (Save does not transact;
Confirm/step 5 uses 4a).

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§6 US-3…US-8 (the six jobs) + §7 (the save spine: validate → … → field-delta confirm) + §10
D8 (the field delta is the acceptance signal) + D11 (AP18) + D12 (nothing-changed) + D13.
[`../plan-spec.md`](../plan-spec.md) §"Cross-step invariants" (the Save-previews/Confirm-transacts
model). `policy.md` (the column invariants the validator enforces — not reimplemented here).

**UX.** N/A directly — a JSON API; its `{field_delta, valid, errors}` is what makes s06's
field-delta-confirm + validation-error states renderable. The user-facing Confirm is step 5 +
the frontend (Phase 3 s06).

**Disassembler-test / author-burden.** N/A — Save validates + previews an already-authored edit.
