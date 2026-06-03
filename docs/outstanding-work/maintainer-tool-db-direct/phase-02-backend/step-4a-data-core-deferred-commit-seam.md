# Step 4a — data-core deferred-commit seam (THE maintainer-tool write mechanism)

**What.** Add a **deferred-commit mode** to the data-core's write path so a DB change runs
validate → write → round-trip INSIDE an open transaction and **returns the open, uncommitted
connections** instead of committing — the caller (the backend, step 4b) holds them across the
user's confirm and COMMITs on Confirm / ROLLBACKs on Cancel. **This is THE write mechanism for
the maintainer tool, full stop** (user-settled 2026-06-03, plan-spec §"Cross-step invariants"):
every DB change the tool makes commits only on confirm, so "on Cancel nothing lands" (design
§7) holds literally — an uncommitted transaction is invisible and discardable, with no file
copy and no live mutation before confirm.

**Ground truth (probed, not theorized — results-driven).** `import_to_sqlite.apply_seeds`
today: opens BOTH DBs (`reference.sqlite` + `reference-dev.sqlite`) via `_open_rw`
(import_to_sqlite.py:1784-1786), applies user-then-dev with each action in its OWN internal
`BEGIN`/`COMMIT` (`_apply_one_db`, lines ~1524/1602), then **closes both connections** before
returning (lines 1799-1801). The transaction is fully internal and gone by return — there is
NO seam to hold it open. 4a builds that seam.

**Scope (additive + oracle-preserving — the 1b pattern; the existing internal-commit path
stays untouched for desktop/CLI/tests):**
- A deferred-commit MODE on `apply_seeds` (a keyword flag, e.g. `defer_commit=False` default —
  decide the exact surface; additive). When set: run validate → the per-DB writes under ONE
  outer transaction per DB (NOT per-action auto-commit — the per-action `COMMIT`s become
  savepoints or a single deferred outer txn) → the round-trip oracle check → and instead of
  committing+closing, RETURN the two OPEN connections + the result dict. The default
  (`defer_commit=False`) path is byte-identical to today (internal commit + close).
- A `commit(handle)` and a `rollback(handle)` the data-core exposes, where `handle` carries the
  two open connections (a small dataclass / namedtuple — a DeferredCommit handle). `commit`
  COMMITs both + closes; `rollback` ROLLBACKs both + closes. Both are idempotent-safe on a
  half-used handle (a second commit/rollback is a no-op or a clear error, not a crash).
- The six `db_editor` write functions thread the deferred-commit mode through (they already
  funnel through `_drive_apply_over_prospective_seed` → `apply_seeds` — the 1b chokepoint), so
  each write function can run in deferred mode and return the handle. Additive: the existing
  immediate-commit call shape is unchanged.
- **The two-DB commit ORDERING sub-decision (surface at build time, do NOT pre-decide):** user
  + dev are two separate SQLite files; a COMMIT of the first succeeding then the second failing
  (disk-full mid-commit) is a split state. Settle the ordering + the failure handling (commit
  dev-then-user? a WAL/checkpoint trick? accept best-effort + log?) once the exact `commit()`
  sequence is written, and SURFACE it to the user (design-authority) — it is the "atomic"
  guarantee's edge, not an agent call.

**Out of scope.** No backend endpoints (4b). No git commit (step 5). No removal/change of the
existing immediate-commit path (the desktop/CLI/test callers + every landed oracle keep it).

**Test bar (same change; the data-core test tree + the mini-dump fixture exist):**
a new `data/refdata-extractor/tests/test_deferred_commit.py`:
- **Deferred write is invisible until commit:** open a deferred-commit update via a write
  function; BEFORE `commit(handle)`, a SEPARATE read-only connection to the DB sees the OLD
  value (the change is uncommitted/invisible); after `commit(handle)`, it sees the NEW value.
- **Rollback discards:** open a deferred update, `rollback(handle)`, then a fresh read shows the
  DB byte-identical to before (nothing landed — the literal "on Cancel nothing lands" proof).
- **Commit lands the same bytes as the immediate path:** the SAME edit applied via
  deferred-then-commit produces a DB byte-identical to the same edit via the existing
  immediate-commit path (the deferred seam changes WHEN it commits, not WHAT it writes — pin to
  the immediate path's result, the convergence proof, like 1b's version-vs-dll convergence).
- **A validation failure in deferred mode** raises (RuntimeError) with NO transaction left open
  + nothing written (the validator gates before any DB open, same as immediate).
- **The handle's commit/rollback idempotency** (a double-commit / commit-after-rollback is a
  clear error or no-op, never a crash or a partial state).
- **The two-DB commit:** both DBs reflect the change after commit; the chosen failure-handling
  behaves as decided (a test for the settled ordering).
- The full data-core suite stays green (additive — a new mode + new tests; the default path
  untouched). Baseline as of step 3: capture the data-core suite on HEAD first
  (`1 failed = TD-0004 pre-existing / N passed / 1 skipped`), confirm 4a turns no green oracle
  red. Run: `python -m pytest data/refdata-extractor/tests/ -q`.

**Dependencies.** Step 1b (the `version=` seam — the deferred mode threads alongside it). The
landed data-core (Phase 1). This is the PRODUCER; step 4b (the save endpoints) + step 5 (the
commit-on-confirm) consume it (`.claude/rules/incremental-delivery.md`).

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§7 (the save spine: validate → write → export → round-trip → field-delta confirm → on Confirm
ONE atomic commit, on Cancel nothing lands) + §10 D13 (the data-core is the single gate) + D16
(the confirm is one atomic transaction). [`../plan-spec.md`](../plan-spec.md) §"Cross-step
invariants" (the deferred-commit record — THE tool's write mechanism, user-settled 2026-06-03).
`policy.md` (the validator the deferred write still runs — not reimplemented).

**UX.** N/A — a data-core transaction seam; no user-facing surface.

**Disassembler-test / author-burden.** N/A — internal Python transaction seam.
