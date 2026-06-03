# Step 4b — backend save endpoints (the six job shapes, deferred-commit held across confirm)

**What.** Add the save endpoints that drive the data-core write path for **all six job shapes**
— re-verify / full-column UPDATE (`update_version_row`), create version (`create_version`),
create entity (`create_entity`), supersede (`supersede_entity`), deprecate (`deprecate_entity`)
— each through the **deferred-commit seam** (step 4a). A save runs validate → write → export →
round-trip INSIDE an open transaction and returns the result + the field delta + the AP18/D12
flags for the confirm gate, **holding the transaction open** (uncommitted) until the user
confirms. The commit (the held transaction + the git commit) is step 5; a cancel ROLLBACKs.
The backend computes NOTHING (law 6/D13) — it calls the data-core write function with
`version=` (the 1b seam — no DLL server-side) + the deferred-commit mode (4a), and surfaces what
the data-core returns.

**Scope.** The save endpoint(s) mapping each job shape to its `db_editor` entry point, each
opening a deferred-commit transaction via 4a and passing `version=(tag, ordinal)` from the
step-1 adapter (`dll_path=None`). The response carries the save result + the field delta (so the
confirm screen shows it — reuse step 3's `field_delta` shaping) + the AP18 new-row flag (D11,
create version/entity) + the D12 nothing-changed verdict where applicable. The open transaction
handle is held server-side keyed to the save (a pending-save registry the confirm/cancel
endpoints in step 5 resolve). A validation failure aborts with NO write + NO open transaction
(the data-core's gate). This step replaces `adapter.data_core_dll_param`'s NotImplementedError
placeholder (the A2 + 4a decisions now settle the threading — surface a deletion-hygiene sweep
of that stale "surfaced fork" docstring).

**SQLite thread-affinity constraint (from 4a's review — load-bearing).** The 4a deferred-commit
handle holds two open SQLite connections across the user's confirm. The data-core opens them with
the default `check_same_thread=True`, and FastAPI runs sync endpoints in a threadpool — so the
confirm/cancel handler (step 5) may run on a DIFFERENT thread than the save handler that opened
the connections, which would make `commit(handle)`/`rollback(handle)` raise `ProgrammingError`.
4b must resolve this when it holds the handle: EITHER open the held connections with
`check_same_thread=False` and serialize access itself (the held-txn registry is single-owner per
save, so a simple lock/single-consumer suffices — `concurrency.md`), OR pin the save→confirm→
cancel handlers for one pending save to a single thread/executor. Decide + document the mechanism;
a held connection used cross-thread is the failure this guards.

**Out of scope.** The git commit + push + the confirm/cancel endpoints that COMMIT/ROLLBACK the
held transaction (step 5). No frontend. No new validation/SQL/rule logic (the data-core's).

**Test bar.** A backend test (`pytest`) on the mini-dump fixture, real API → real data-core →
real DBs:
- each of the six job shapes opens a deferred-commit save: the change is written-but-uncommitted
  (a separate read sees the OLD value until step-5 commit; this step asserts the save SUCCEEDS +
  returns the result/delta/flags + leaves the txn open, NOT that it committed);
- an invalid edit per shape (malformed date, partial trio, duplicate tuple, supersession cycle,
  missing required column) aborts with NO write + NO open txn (the DB byte-identical, the data-
  core validator's verdict surfaced as the error);
- the AP18 flag is set on create-version/create-entity; the nothing-changed verdict fires for an
  identical new version;
- the save passes `version=` (no DLL read — the no-DLL-server-side path) — a save resolves the
  version via the adapter, never a dll_path;
- ROLLBACK (the cancel path's data-core call) leaves the DB byte-identical (the held txn
  discarded).
The endpoints SURFACE the data-core's result/flags (the write correctness is 4a's + the data-
core oracles'; this step asserts the endpoint drives the deferred seam + surfaces the result).
Runnable now (4a + the six write shapes + the adapter exist).

**Dependencies.** Step 4a (the deferred-commit seam — the write mechanism these endpoints open)
+ step 1 (the backend + the version-tag adapter) + step 1b (the `version=` seam) + step 3 (the
field-delta shaping reused in the response). Phase 1 steps 3/4/5 (`db_editor` — landed).
Sequenced after 4a (the consumer after its producer, `.claude/rules/incremental-delivery.md`).

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§6 US-3…US-8 (the six jobs) + §7 (the save spine + the write-failure/rollback state) + §10 D11
(AP18) + D12 (nothing-changed) + D13 (the applier path) + D16 (the confirm transaction).
[`../plan-spec.md`](../plan-spec.md) §"Cross-step invariants" (the deferred-commit mechanism).
`policy.md` (the column invariants the data-core validator enforces — not reimplemented here).

**UX.** N/A directly — a JSON API; its result/delta/flags are what make s06's confirm states
(field-delta confirm, AP18 approval banner, write-failure) renderable. The user-facing confirm
is step 5 + the frontend (Phase 3 s06).

**Disassembler-test / author-burden.** N/A — the save API drives already-authored edits; the
create flows' expert RVA/signature authoring is the frontend's (s05) + AP18-gated; the backend
persists what the validator accepts.
