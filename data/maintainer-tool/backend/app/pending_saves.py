"""app.pending_saves -- the held-open deferred-commit save registry (step 4b).

WHAT THIS OWNS (design §7 save spine; plan-spec "Deferred commit is THE write
mechanism"; step-4b SQLite thread-affinity constraint)
-----------------------------------------------------------------------------
A save runs validate -> write -> export -> round-trip INSIDE an open transaction and
holds it UNCOMMITTED until the maintainer confirms (commit) or cancels (rollback) --
step 5. The data-core's deferred-commit apply (step 4a) returns a `DeferredCommit`
handle: two OPEN SQLite connections (user + dev) under an outer BEGIN, never
committed. This registry holds that handle across the user's confirm, keyed by a
generated `save_id`, so the step-5 confirm/cancel endpoints can resolve it.

THE THREAD-AFFINITY RESOLUTION (the step's load-bearing constraint)
-------------------------------------------------------------------
The data-core opens the held connections with the default `check_same_thread=True`
(import_to_sqlite._open_rw -> sqlite3.connect(db_path)). FastAPI runs sync endpoints
in a threadpool, so the save handler and a later confirm/cancel handler (step 5) may
run on DIFFERENT threads -- and a sqlite3 connection used from a thread other than the
one that opened it raises sqlite3.ProgrammingError. commit(handle)/rollback(handle)
touch those connections, so they MUST run on the SAME thread that opened them.

The resolution: each pending save owns a dedicated single-worker executor
(concurrent.futures.ThreadPoolExecutor max_workers=1). The data-core write that OPENS
the connections runs ON that executor; the handle + the executor are stored together;
step 5 submits commit/rollback to the SAME executor. Every operation on a given
handle's connections therefore runs on the one thread that opened them -- the
thread-affinity invariant holds by construction, no check_same_thread=False, no
per-handle lock needed (the single worker serializes access for free).

THE CONFIRM/CANCEL SEAM (step 5 plugs in here)
----------------------------------------------
`run_on_executor(save_id, fn)` is the seam: step 5's confirm endpoint calls it with
`data_core.commit`, its cancel endpoint with `data_core.rollback`. Both run on the
save's own executor (thread-affinity), then `discard(save_id)` reaps the executor.
4b builds the held save + the seam + a `discard`/`finish_*` path; the HTTP
confirm/cancel endpoints are step 5.

LEAK NOTE (surfaced -- step 5 owns the reaping policy)
------------------------------------------------------
A save that is never confirmed/cancelled leaks a held transaction + two open
connections + an executor thread (an abandoned tab, a crashed client). This registry
exposes `pending_ids()` + `discard()` so step 5 (or a reaper task it owns) can reap
stale entries; 4b does NOT add a timer/reaper (that is step 5's confirm/cancel
lifecycle decision + would be polling needing approval -- `.claude/rules/polling.md`).
The seam to reap exists; the policy is step 5's.
"""
import logging
import threading
import uuid
from concurrent.futures import ThreadPoolExecutor

log = logging.getLogger(__name__)


class PendingSaveError(KeyError):
    """A `save_id` the registry does not hold (an unknown / already-resolved save).
    A caller-facing error -- the confirm/cancel (step 5) was handed a token the
    registry never minted or already reaped; surfaced, never silently no-op'd."""


class _Pending:
    """One held save: the data-core DeferredCommit handle (the two open uncommitted
    connections + the apply result) and the single-worker executor that OPENED those
    connections -- every operation on the handle runs on this executor's one thread
    (the thread-affinity invariant). `meta` carries the surfaced flags/delta the save
    endpoint returned (for diagnostics / a step-5 re-fetch); it is not load-bearing
    state."""

    __slots__ = ("handle", "executor", "meta")

    def __init__(self, handle, executor, meta):
        self.handle = handle
        self.executor = executor
        self.meta = meta


class PendingSaveRegistry:
    """The process-wide held-save registry. A FastAPI app holds ONE (module
    singleton below). Thread-safe: the save handler and a step-5 confirm/cancel
    handler run on different threadpool threads, so the dict + the lifecycle
    transitions are guarded by one lock (the lock guards the REGISTRY map only --
    the per-save connection work is serialized by each save's own single-worker
    executor, never under this lock)."""

    def __init__(self):
        self._pending = {}
        # WHY a lock: register/get/discard race across FastAPI threadpool threads
        # (save on one, step-5 confirm/cancel on another). It guards only the map;
        # connection access is serialized by the per-save executor, not here.
        self._lock = threading.Lock()

    def run_save(self, save_fn, meta):
        """Run a data-core deferred-commit WRITE on a fresh single-worker executor,
        register the resulting handle keyed by a new save_id, and return
        (save_id, result). `save_fn` is a zero-arg callable invoking the chosen
        db_editor write with defer_commit=True -- it RUNS ON THE EXECUTOR so the
        SQLite connections it opens belong to the executor's thread (thread
        affinity). `result` is what save_fn returned (a bare DeferredCommit handle for
        update_version_row; a {"result": handle, ...} dict for create_version /
        create_entity [AP18 flags] and supersede / deprecate [action + kcdx_id]).

        On a validation/refusal failure save_fn RAISES on the executor thread (the
        data-core's gate -- NO write, NO held txn): the executor is shut down and the
        error re-raised, so NOTHING is registered (no save_id leaks for a failed
        save). The caller maps that error to an HTTP error + logs it.
        """
        executor = ThreadPoolExecutor(max_workers=1,
                                      thread_name_prefix="kcdx-save")
        try:
            # The save (which OPENS the connections) runs on the executor -> the
            # handle's connections belong to this executor's single thread. .result()
            # re-raises any exception save_fn raised on that thread (a validation
            # failure: no write, no handle).
            result = executor.submit(save_fn).result()
        except BaseException:
            # The write failed (validation/refusal) OR a programming error: NO held
            # txn exists (the data-core rolls back + closes on error). Tear down the
            # now-useless executor and propagate -- nothing is registered.
            executor.shutdown(wait=False)
            raise

        handle = _handle_of(result)
        save_id = uuid.uuid4().hex
        pending = _Pending(handle=handle, executor=executor, meta=meta)
        with self._lock:
            self._pending[save_id] = pending
        log.info("pending save registered: save_id=%s kind=%s",
                 save_id, meta.get("kind"))
        return save_id, result

    def get(self, save_id):
        """The DeferredCommit handle for a save_id, or PendingSaveError if unknown
        (never held / already resolved). Step 5's confirm/cancel resolve the handle
        through here before committing/rolling back."""
        with self._lock:
            pending = self._pending.get(save_id)
        if pending is None:
            raise PendingSaveError(
                f"no pending save for save_id {save_id!r} (it was never registered "
                f"or has already been committed/cancelled)")
        return pending.handle

    def run_on_executor(self, save_id, fn):
        """Run `fn(handle)` ON the save's own executor and return its result -- the
        thread-affinity seam step 5 commits/rolls back through. `fn` is
        data_core.commit / data_core.rollback (each takes the handle); running it on
        the executor that OPENED the connections satisfies check_same_thread. Raises
        PendingSaveError for an unknown save_id; propagates whatever `fn` raises (a
        DeferredCommitError on a double-commit, or a split-commit error). Does NOT
        discard -- the caller (step 5) discards on success to reap the executor.
        """
        with self._lock:
            pending = self._pending.get(save_id)
        if pending is None:
            raise PendingSaveError(
                f"no pending save for save_id {save_id!r} (it was never registered "
                f"or has already been committed/cancelled)")
        return pending.executor.submit(fn, pending.handle).result()

    def discard(self, save_id):
        """Remove a save_id from the registry and shut down its executor (reap the
        worker thread). Called by step 5 AFTER a successful commit/rollback (the
        handle's connections are already closed by the data-core's commit/rollback),
        and available for a step-5 reaper to drop a stale held save. Idempotent: an
        unknown save_id is a no-op (it was already reaped). Returns True if a save
        was removed, False if there was nothing to remove."""
        with self._lock:
            pending = self._pending.pop(save_id, None)
        if pending is None:
            return False
        # The executor's worker is idle after commit/rollback ran on it; shut it down
        # so the thread does not leak. wait=False -- nothing is queued after a resolve.
        pending.executor.shutdown(wait=False)
        log.info("pending save discarded: save_id=%s", save_id)
        return True

    def pending_ids(self):
        """The set of currently-held save_ids (diagnostics + the step-5 reaper's
        scan source). A held save_id here is a held transaction + two open
        connections + a live executor thread until it is committed/cancelled."""
        with self._lock:
            return set(self._pending)


def _handle_of(result):
    """The DeferredCommit handle inside a db_editor return. The update / lifecycle
    writes return the handle DIRECTLY; the create writes return a dict whose "result"
    slot carries it (db_editor's documented two shapes). Keys on the dict-vs-handle
    shape, not on the job -- payload-agnostic, so a new create-shape needs no change."""
    if isinstance(result, dict) and "result" in result:
        return result["result"]
    return result


# The process-wide singleton the save router + the step-5 confirm/cancel router
# share. ONE registry per app process holds every in-flight held save.
REGISTRY = PendingSaveRegistry()
