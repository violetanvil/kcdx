"""app.routes_confirm -- the Confirm transaction + Cancel (design S7 save spine / S8
commit constraint / D16 / D17 / D19 / D20; plan-spec "Save-previews / Confirm-transacts";
step 5, reworked onto the DIRECT-WRITE model).

WHAT CONFIRM IS (the Save-previews / Confirm-transacts model)
-------------------------------------------------------------
POST /confirm runs the WHOLE atomic save SYNCHRONOUSLY in one request while the page
waits. It re-sends the SAME edit body the matching Save (step 4b-rework) previewed, but
drives the data-core DIRECT-WRITE path with defer_commit=True (not validate_only): the
db_editor write routes through apply_direct_edit (4c -- a real INSERT/UPDATE via
_apply_one_db on the held deferred-commit connections), returning a DeferredCommit handle.
The transaction opens AND closes inside this one request -- NOTHING is held across the
maintainer's think-time. On ANY failure NOTHING lands (the robust rollback below); on
success the DB + the derived data/db-export/ CSV record are committed and pushed to the
private remote as the durable mirror.

THE SEQUENCE AS BUILT (read the two subtleties below)
-----------------------------------------------------
  1. resolve_tag(config, version_tag) -> version=(tag, ordinal)  [the 1b adapter, no DLL].
  2. DIRECT-WRITE (deferred): the chosen db_editor write with defer_commit=True, version=,
     dll_path=None, out_dir=config.out_dir -> a DeferredCommit handle (two OPEN,
     uncommitted connections). The write routes through 4c's apply_direct_edit; the
     create-version-at-a-new-tag case is detected INSIDE create_version (it reads the DB's
     known tags + threads new_tag to apply_direct_edit -- the Confirm passes nothing
     extra). The handle ALSO carries the 4d SCOPED restore-point: apply_direct_edit
     captured the touched rows + each DB's sqlite_sequence BEFORE the writes landed (a
     few KB, never the ~1.3GB DEV DB), so the Confirm captures NOTHING itself -- it just
     calls data_core.restore(handle) on a post-commit failure. Validation +
     version/baseline refusals gate BEFORE any DB open, so a bad edit here leaves NOTHING
     written (handle stays None -> no rollback needed).
  3. KEEP a pre-edit copy of the data/db-export/ CSVs (the backend's CSV-revert source --
     see the CSV-revert split below). The data-core's restore-point covers DB ROWS +
     sqlite_sequence ONLY (the write semantics it owns, D13/law 6); the db-export CSVs are
     a backend FILE artifact (D20) the backend reverts itself. A few KB; kept just before
     the irreversible commit so a pre-commit failure pays no copy cost.
  4. COMMIT the DB: data_core.commit(handle) -- USER-first then DEV (the settled 4a
     ordering; re-raises on a DEV-commit split). IRREVERSIBLE (see below).
  5. EXPORT the committed DB -> the data/db-export/ CSV record (D20 -- the derived diff
     layer, NOT data/seeds/ the frozen bootstrap).
  6. INTEGRITY: the CHEAP CSV byte-identity re-export over data/db-export/ (csv_integrity
     -- the round-trip-cost resolution; NOT a 1.3GB full rebuild per save).
  7. GIT: stage the DB files (at config.out_dir) + the 3 data/db-export/ CSVs BY EXACT
     PATH, author with the request-context identity, push to `private` (git_commit).
  ROBUST ROLLBACK: a failure at ANY of 4-7 calls data_core.restore(handle) (undoes the
  committed DB write -- the touched rows + sqlite_sequence, byte-identical incl. PK) AND
  reverts the data/db-export/ CSVs from the pre-edit copy -> the DB + db-export CSVs are
  byte-identical to before, nothing lands. SUCCESS discards the CSV copy.

WHY COMMIT (step 4) RUNS BEFORE EXPORT (step 5) -- the export-visibility constraint
-----------------------------------------------------------------------------------
export_seeds opens its OWN fresh connection to the DB file by PATH. A held, uncommitted
deferred txn (DeferredCommit.ucon/.dcon) is INVISIBLE to that fresh connection (an
uncommitted SQLite txn is invisible to every other connection -- verified in 4a's tests +
read in csv_exporter.export_seeds, which does a fresh sqlite3.connect). So exporting the
in-transaction state from a fresh connection is impossible: it would read the PRE-edit
committed DB and produce stale CSVs. The integrity check is also export-based, so it too
must run post-commit. The order is forced: COMMIT first (the DB is authoritative, D1),
THEN export the committed DB to the data/db-export/ record (deterministic, D1/D20).

THE ROBUST ROLLBACK -- "on failure NOTHING lands" holds LITERALLY (D21, the user's req.)
----------------------------------------------------------------------------------------
Two mechanisms split at the irreversible DB commit (D21):
  - PRE-commit failure (validation / refusal / the direct write itself) -> NOTHING was
    committed; apply_direct_edit rolls back + closes the held txn on any error (incl.
    sqlite_sequence/PK bumps), so "nothing landed" is already literal (the handle stays
    None or the write raised before commit). FAILED status -- no restore call needed.
  - POST-commit failure (the commit split, export, integrity, git stage/commit/push, or a
    live index.lock that blocks the git stage) -> the deferred rollback is GONE
    (data_core.commit() is one-way: it COMMITs both DBs + closes both connections), and
    the export MUST run post-commit (the visibility constraint above). So the 4d SCOPED
    restore-point is the only undo left:
      * data_core.restore(handle) -- the DATA-CORE capability (D13/law 6): it re-opens
        both DBs, restores the touched rows + each DB's sqlite_sequence from the capture
        the handle carries (taken before the commit), byte-identical incl. PK. It undoes
        the DB WRITE SEMANTICS the data-core owns -- it does NOT touch the CSVs.
      * the BACKEND reverts the data/db-export/ CSVs from the pre-edit copy kept in step 3
        -- the CSV-revert SPLIT (D13/law 6): the CSVs are a backend FILE artifact (D20),
        not the data-core's, so the backend owns reverting them. After both, the DB +
        db-export CSVs are byte-identical to before. A local git commit (if one landed
        before a push failure) is a dangling commit over the restored files; the next
        Confirm re-stages from the restored state. The durable mirror (the pushed remote)
        never advanced, so "nothing lands" holds for the durable record too.
WHY 4d, NOT a full-file snapshot (D21, the surfaced design fork): the prior step-5 WIP
snapshotted the two DB FILES whole (shutil.copy2) -- the SAME guarantee, but it copies the
~1.3GB DEV DB on every committing Confirm. D21 settled on the data-core's SCOPED
restore-point instead (only the O(edits) touched rows + the sequence -- a few KB), the
cheaper mechanism the cornerstone order (UX > Capability > Performance; the cheaper
mechanism for the same guarantee wins) selects. The restore-point is a data-core
capability because it owns the write semantics (which rows each job touches); putting it in
the backend would leak that rule logic. So Confirm CALLS data_core.restore(handle) and the
full-file restore_point.py the WIP built is dropped.

THE AUTH-READY SEAMS (D17 -- the SEAMS, not the auth)
-----------------------------------------------------
  - The commit AUTHOR identity comes from the request context: the X-Kcdx-Author-Name /
    X-Kcdx-Author-Email headers the operator's login layer populates (or the same fields
    in the body). ABSENT -> a documented dev-default identity (so the app boots + commits
    locally without auth). The app builds NO login.
  - The push CREDENTIAL is env-injected (KCDX_PUSH_TOKEN -- git_commit reads it). ABSENT
    -> the push is skipped (the dev default -- commit locally). The app provisions NO
    credential.

CANCEL: POST /cancel is a no-op success. The preview model holds NO transaction across
think-time (Save previews, holds nothing; Confirm is the only thing that opens a txn, and
it opens+closes in its own request). So there is nothing to roll back -- Cancel just
acknowledges.
"""
import logging
from typing import Optional

from fastapi import APIRouter, Header, HTTPException
from pydantic import BaseModel

from . import csv_integrity
from . import data_core
from . import git_commit
from .adapter import resolve_tag, VersionTagError
from .config import load_config
from .csv_revert import CsvRevert
from .routes_save import (
    UpdateVersionSave, CreateVersionSave, CreateEntitySave,
    SupersedeSave, DeprecateSave,
)

log = logging.getLogger(__name__)

router = APIRouter()

# The dev-default commit identity (D17): when the request context carries no author
# (the operator's login layer is not wired -- local/test), the app still commits, with
# this documented fallback. The app builds no login; this is the boot-without-auth seam.
DEV_DEFAULT_AUTHOR_NAME = "kcdx maintainer-tool (dev)"
DEV_DEFAULT_AUTHOR_EMAIL = "maintainer-tool@kcdx.local"


def _resolve_author(name_field, email_field, header_name, header_email):
    """The commit author identity, request-context-first (D17 -- the auth-ready seam).

    Priority: an explicit body field > the request header the operator's login populates
    > the documented dev default. Both halves (name + email) resolve independently so a
    partial context still authors a complete identity. The app reads the identity; it
    builds no login layer."""
    name = (name_field or header_name or DEV_DEFAULT_AUTHOR_NAME).strip() \
        or DEV_DEFAULT_AUTHOR_NAME
    email = (email_field or header_email or DEV_DEFAULT_AUTHOR_EMAIL).strip() \
        or DEV_DEFAULT_AUTHOR_EMAIL
    return name, email


# ---------------------------------------------------------------------------
# Confirm request bodies -- the SAME edit shapes the Save preview took (step 4b), each
# extended with the auth-ready identity context (D17). Confirm re-sends the matching
# Save body + the optional author fields; the version_tag + record/edit fields are
# IDENTICAL to the preview (Confirm transacts what Save previewed).
# ---------------------------------------------------------------------------
class _AuthContext(BaseModel):
    """The auth-ready commit-identity seam (D17): the operator's login layer populates
    these (or the X-Kcdx-Author-* headers); absent -> the dev default. The app builds
    no login -- it reads the identity the context supplies."""
    author_name: Optional[str] = None
    author_email: Optional[str] = None


class ConfirmUpdateVersion(UpdateVersionSave, _AuthContext):
    pass


class ConfirmCreateVersion(CreateVersionSave, _AuthContext):
    pass


class ConfirmCreateEntity(CreateEntitySave, _AuthContext):
    pass


class ConfirmSupersede(SupersedeSave, _AuthContext):
    pass


class ConfirmDeprecate(DeprecateSave, _AuthContext):
    pass


# ---------------------------------------------------------------------------
# The shared Confirm drive: resolve the tag, run the deferred DB write, commit the DB,
# export the CSVs, integrity-check, and git commit+push -- the whole atomic transaction
# in one request. Each endpoint supplies its data-core write as a closure + its entity
# label for the result.
# ---------------------------------------------------------------------------
def _run_confirm(body, *, entity_label, write, author):
    """Run the synchronous atomic Confirm transaction over the DIRECT-WRITE model (the
    sequence + the robust-rollback composition are in this module's docstring).

    `write(version)` is a closure invoking the chosen db_editor function with
    defer_commit=True, dll_path=None, version=, out_dir=config.out_dir -- routing through
    4c's apply_direct_edit (a direct INSERT/UPDATE on the held connections), returning a
    DeferredCommit handle directly (the update/lifecycle shapes) or a dict whose "result"
    is the handle (the create shapes; create_version detects new-tag internally).
    `entity_label` names the saved entity for the success result + the commit message.
    `author` is (name, email) from the request context (D17).

    Returns the FastAPI response dict. On ANY failure -- before OR after the DB commit --
    NOTHING lands: a pre-commit failure rolls back the held txn (apply_direct_edit's own
    rollback); a post-commit failure calls data_core.restore(handle) (the 4d scoped
    restore-point -- DB rows + sqlite_sequence/PK) + reverts the db-export CSVs (the backend
    half) -> both byte-identical. A FAILED status is returned in every failure case (the
    robust rollback the user required, D21)."""
    config = load_config()
    author_name, author_email = author

    # 1. Resolve the chosen version tag -> (tag, ordinal). No DLL (the 1b seam). An
    #    unknown tag is the maintainer's bad input -> 422 BEFORE any txn opens.
    try:
        ctx = resolve_tag(config, body.version_tag)
    except VersionTagError as exc:
        log.warning("confirm rejected -- unknown version tag (entity=%s, tag=%s): %s",
                    entity_label, body.version_tag, exc)
        raise HTTPException(status_code=422, detail=str(exc))
    version = (ctx.tag, ctx.ordinal)

    # 2. DIRECT-WRITE under a held deferred-commit txn (4c). Validation + version /
    #    baseline refusals gate BEFORE any DB open -- a rejected edit leaves NOTHING
    #    written and returns NO handle (handle stays None -> no rollback needed). A
    #    pre-commit failure here means the DB file never advanced -- "nothing landed" is
    #    already literal, no restore point needed.
    handle = None
    try:
        handle = _extract_handle(write(version))
    except data_core.DbEditError as exc:
        # A malformed edit SHAPE (an unknown/non-editable column, a stale identity key) --
        # the caller's bug, surfaced before any DB write. Nothing opened. -> 422.
        log.warning("confirm rejected -- malformed edit (entity=%s): %s",
                    entity_label, exc)
        raise HTTPException(status_code=422, detail=str(exc))
    except data_core.VersionResolveError as exc:
        # A version refusal on the supplied (tag, ordinal) -- the data-core's own version
        # verdict. No DB write (the direct drive rolls back + closes on error). Sad-path.
        log.warning("confirm failed -- version refusal (entity=%s): %s",
                    entity_label, exc)
        return _failed_response(entity_label, str(exc))
    except (ValueError, RuntimeError) as exc:
        # The shared validator's verdict on the prospective DB state (a duplicate tuple, a
        # partial trio, a supersession cycle, a missing required column, BaselineRefusal,
        # ...). apply_direct_edit validates BEFORE any DB open + rolls back + closes the
        # held txn on any error, so NOTHING is committed -- the DB + CSVs byte-identical.
        log.warning("confirm failed -- validator rejected the edit (entity=%s): %s",
                    entity_label, exc)
        return _failed_response(entity_label, str(exc))

    # The direct write succeeded -> a held, uncommitted DeferredCommit handle CARRYING
    # the 4d scoped restore-point (apply_direct_edit captured the touched DB rows +
    # sqlite_sequence before the writes landed -- a few KB, not the ~1.3GB DEV DB). We are
    # about to do the IRREVERSIBLE commit. The DB-side undo is data_core.restore(handle)
    # (D21 -- the data-core owns the write semantics); the db-export CSVs are the backend's
    # to revert (the CSV-revert split, D13/law 6). KEEP a pre-edit copy of the three CSVs
    # NOW (just before the commit) so a post-commit failure reverts them to their pre-edit
    # bytes. A pre-edit failure above never reaches here -> pays no copy cost.
    csv_revert = CsvRevert(config.db_export_files).capture()
    try:
        # 4. COMMIT THE DB (before the export -- the export-visibility constraint: a fresh
        #    export connection cannot see the held txn). USER-first then DEV (the settled
        #    4a ordering); a DEV-commit split re-raises. Whatever raised from here on, the
        #    restore point undoes it (the except below restores + returns FAILED).
        try:
            data_core.commit(handle)
        except Exception as exc:
            # The commit itself failed/split. commit() marks the handle finished + closes
            # both connections even on a split (USER may have committed, DEV not). The
            # held txn is gone -- the deferred rollback cannot undo it. The 4d restore-point
            # DOES: data_core.restore(handle) re-opens the DBs + restores the touched rows
            # + sqlite_sequence; the backend reverts the db-export CSVs -> byte-identical,
            # nothing landed. (A DEV-side split also restores cleanly -- restore() re-opens
            # both DBs and converges each to its captured pre-edit state.)
            log.warning("confirm DB commit failed/split (entity=%s): %s -- restoring "
                        "the pre-Confirm DB (robust rollback)", entity_label, exc)
            _robust_rollback(handle, csv_revert)
            return _failed_response(
                entity_label,
                f"the DB commit failed or split across the two reference DBs and was "
                f"rolled back (nothing landed): {exc}")

        # 5. EXPORT the COMMITTED DB -> the data/db-export/ CSV record (D20 -- the derived
        #    diff layer, NOT data/seeds/). A fresh export connection now sees the
        #    committed change (the visibility constraint is satisfied post-commit).
        data_core.export_seeds(config.user_db, config.db_export_dir)

        # 6. INTEGRITY: the cheap CSV byte-identity re-export over data/db-export/ (the
        #    round-trip-cost resolution -- NOT a 1.3GB full rebuild). A divergence is a
        #    tool bug; the restore point rolls the whole Confirm back.
        csv_integrity.assert_csv_export_deterministic(
            config.user_db, config.db_export_dir)

        # 7. GIT: stage the DB files (at config.out_dir) + the 3 data/db-export/ CSVs BY
        #    EXACT PATH, author with the request-context identity, push to `private`.
        rel_paths = _staged_rel_paths()
        message = _commit_message(entity_label, version)
        git_report = git_commit.commit_and_push(
            config.checkout_path, rel_paths, message=message,
            author_name=author_name, author_email=author_email)
    except csv_integrity.CsvIntegrityError as exc:
        # Post-commit integrity divergence -> roll back the whole Confirm: the 4d restore-
        # point undoes the committed DB write (rows + sqlite_sequence), the backend reverts
        # the db-export CSVs. Nothing landed.
        log.warning("confirm integrity check failed (entity=%s): %s -- restoring the "
                    "pre-Confirm DB + db-export (robust rollback)", entity_label, exc)
        _robust_rollback(handle, csv_revert)
        return _failed_response(
            entity_label,
            f"the committed CSV export did not round-trip (a tool bug) and the save was "
            f"rolled back (nothing landed): {exc}")
    except git_commit.IndexLockBusy as exc:
        # A live shared .git/index.lock blocked the git stage/commit (event-driven, off
        # git's own exit -- NEVER reaped, NEVER polled). Roll the whole Confirm back so
        # nothing lands, and surface Retry -- the maintainer re-Confirms when the lock
        # clears (a different writer holds it now).
        log.warning("confirm blocked -- shared git index locked (entity=%s, stage=%s): "
                    "%s -- restoring + surfacing Retry", entity_label, exc.stage, exc)
        _robust_rollback(handle, csv_revert)
        return _lock_busy_response(entity_label, str(exc))
    except git_commit.GitCommitError as exc:
        # A git stage/commit/push failed AFTER the DB commit. The user required robust
        # rollback -> the 4d restore-point undoes the committed DB write + the backend
        # reverts the db-export CSVs: both byte-identical, the durable mirror (the remote)
        # never advanced -> nothing lands. (A local git commit that landed before a push
        # failure now dangles over the restored files; the next Confirm re-stages from the
        # restored state.)
        log.warning("confirm git step failed (entity=%s, stage=%s): %s -- restoring the "
                    "pre-Confirm DB + db-export (robust rollback)", entity_label,
                    exc.stage, exc)
        _robust_rollback(handle, csv_revert)
        # The maintainer-facing detail is GIT-FREE (design S7 law 5 -- git is invisible to
        # the maintainer). The git stage + the raw git stderr (exc) are the OPERATOR's
        # diagnostic and stay in the log.warning above; they NEVER reach the rendered
        # detail. git_stage is a structured field for the frontend's own logic, not a
        # maintainer-rendered string (the frontend shows `detail`, never git_stage).
        return _failed_response(
            entity_label,
            "the save couldn't be recorded and was rolled back -- nothing landed.",
            git_stage=exc.stage)
    except Exception as exc:
        # Any other post-commit failure (a stray export error, an unexpected raise) ->
        # restore + FAILED. Never leave the DB ahead (the user's robust-rollback req.).
        log.warning("confirm post-commit step failed (entity=%s): %s -- restoring the "
                    "pre-Confirm DB + db-export (robust rollback)", entity_label, exc)
        _robust_rollback(handle, csv_revert)
        return _failed_response(
            entity_label,
            f"the save failed after the DB commit and was rolled back (nothing landed): "
            f"{exc}")

    # SUCCESS -- the whole transaction landed (DB committed, db-export CSVs exported, and
    # -- unless the edit was a DB no-op with no on-disk delta -- git committed + pushed
    # when a credential was present). Discard the pre-edit CSV copy (nothing to revert; the
    # DB-side 4d restore-point is discarded with the now-committed handle). "Saved <entity>
    # <version>". A no_delta edit committed the DB exactly as confirmed but produced no file
    # change, so there is no git commit to record (no_delta True) -- still a save, surfaced
    # so the page can say so.
    csv_revert.discard()
    log.info("confirm saved %s %s (pushed=%s, no_delta=%s)", entity_label, version[0],
             git_report["pushed"], git_report.get("no_delta", False))
    return {
        "status": "saved",
        "entity": entity_label,
        "version": version[0],
        "pushed": git_report["pushed"],
        "push_skipped_reason": git_report["push_skipped_reason"],
        "no_delta": git_report.get("no_delta", False),
    }


def _extract_handle(write_result):
    """Normalize a db_editor write's return to the DeferredCommit handle. The
    update/lifecycle shapes return the handle directly (their _drive returns the apply
    result, which in deferred mode IS the handle); the create + lifecycle shapes wrap it
    in {"result": <handle>, ...flags}. A defer_commit=True apply_seeds returns a
    DeferredCommit; so the handle is either the value itself or its "result" slot."""
    if isinstance(write_result, dict) and "result" in write_result:
        return write_result["result"]
    return write_result


def _staged_rel_paths():
    """The files Confirm stages, as paths RELATIVE to the checkout root, BY EXACT NAME
    (never a broad add): the two reference DBs (at config.out_dir -- data/, where the
    data-core builds + amends them) PLUS the three derived-export CSVs at
    data/db-export/ (D20 -- NOT data/seeds/, which holds the frozen bootstrap CSVs the
    maintainer tool never writes). The DB is the originator; the data/db-export/ CSVs are
    its git-tracked diff record. These DB paths MUST match config.out_dir (data/) --
    staging a path the data-core never wrote would commit a stale/absent file."""
    return [
        # The reference DBs -- the data-core amends them in place at config.out_dir (data/).
        "data/reference.sqlite",
        "data/reference-dev.sqlite",
        # The derived CSV record (D20) -- the export target, NOT the bootstrap seeds.
        "data/db-export/module_seed.csv",
        "data/db-export/address_names_seed.csv",
        "data/db-export/address_versions_seed.csv",
    ]


def _robust_rollback(handle, csv_revert):
    """The POST-commit robust rollback (D21) -- the two-half undo run on ANY failure after
    data_core.commit(handle): (1) data_core.restore(handle) -- the 4d SCOPED restore-point
    re-opens both DBs + restores the touched rows + each DB's sqlite_sequence from the
    capture the handle carries (byte-identical incl. PK; the data-core owns the write
    semantics, D13/law 6); (2) csv_revert.revert() -- the backend reverts the
    data/db-export/ CSVs from the pre-edit copy (the CSV-revert SPLIT: the CSVs are a
    backend FILE artifact (D20), not the data-core's). After both, the DB + db-export CSVs
    are byte-identical to before the Confirm -- "on failure nothing lands" holds literally.
    The DB restore runs FIRST (it is the authoritative state, D1); the CSV revert second.
    discard() reaps the kept-copy snapshot dir after the revert (else a failed Confirm
    leaks a tempdir; the success path discards at the end of _run_confirm)."""
    data_core.restore(handle)
    csv_revert.revert()
    csv_revert.discard()


def _commit_message(entity_label, version):
    """The commit message body. Git is invisible to the maintainer (design S7 -- they
    read "Saved <entity> <version>", never a hash); this is the durable record a
    reviewer reads in the private repo's history."""
    return (f"maintainer-tool: save {entity_label} ({version[0]})\n\n"
            f"DB-direct edit committed via the maintainer tool Confirm transaction "
            f"(DB + 3 db-export CSVs, exact-path staged).\n")


def _failed_response(entity_label, detail, *, git_stage=None):
    """The FAILED response -- nothing landed. Used for EVERY Confirm failure: a pre-commit
    rejection (the held txn rolled back) AND a post-commit failure (the 4d restore-point
    rolled the DB rows + sqlite_sequence back + the backend reverted the db-export CSVs --
    both byte-identical to pre-Confirm). The page renders "nothing landed" + the cause
    (design S7 write-failure state). `committed` is False and `retry` is False in both cases
    -- the robust rollback (D21) makes "on failure nothing lands" literal, so there is no
    half-landed state for the maintainer to retry past."""
    resp = {"status": "failed", "entity": entity_label, "detail": detail,
            "committed": False, "retry": False}
    if git_stage is not None:
        resp["git_stage"] = git_stage
    return resp


def _lock_busy_response(entity_label, detail):
    """The Retry response for a live shared .git/index.lock (event-driven off git's own
    exit -- never reaped, never polled). The whole Confirm was rolled back (nothing
    landed), and the lock is transient (another writer holds it now) -- so this is the ONE
    failure the page surfaces with retry=True: re-Confirm when the lock clears. The page
    renders "Save blocked -- files locked, Retry" (design S7)."""
    return {"status": "busy", "entity": entity_label, "detail": detail,
            "committed": False, "retry": True}


# ---------------------------------------------------------------------------
# The Confirm endpoints -- one per job shape. Each resolves the author from the request
# context (body field or header, dev default fallback), supplies its db_editor write as
# a closure (defer_commit=True, dll_path=None, version=), and runs the transaction.
# ---------------------------------------------------------------------------
@router.post("/confirm/update-version")
def confirm_update_version(
        body: ConfirmUpdateVersion,
        x_kcdx_author_name: Optional[str] = Header(default=None),
        x_kcdx_author_email: Optional[str] = Header(default=None)):
    """Confirm a re-verify / full-column UPDATE (US-3 / US-5): the atomic transaction."""
    author = _resolve_author(body.author_name, body.author_email,
                             x_kcdx_author_name, x_kcdx_author_email)
    return _run_confirm(
        body, entity_label=f"version-row kcdx_id={body.kcdx_id}", author=author,
        write=lambda version: data_core.update_version_row(
            load_config().out_dir, None, body.kcdx_id, body.valid_from_version,
            body.edits, version=version, defer_commit=True))


@router.post("/confirm/create-version")
def confirm_create_version(
        body: ConfirmCreateVersion,
        x_kcdx_author_name: Optional[str] = Header(default=None),
        x_kcdx_author_email: Optional[str] = Header(default=None)):
    """Confirm a new version (US-6) for an existing entity (AP18 -- the new-row addition
    commits here; the GUI gated ap18_new_row at the preview)."""
    author = _resolve_author(body.author_name, body.author_email,
                             x_kcdx_author_name, x_kcdx_author_email)
    return _run_confirm(
        body, entity_label=f"new version kcdx_id={body.kcdx_id} "
                           f"@ {body.valid_from_version}", author=author,
        write=lambda version: data_core.create_version(
            load_config().out_dir, None, body.kcdx_id, body.valid_from_version,
            body.columns, version=version, defer_commit=True))


@router.post("/confirm/create-entity")
def confirm_create_entity(
        body: ConfirmCreateEntity,
        x_kcdx_author_name: Optional[str] = Header(default=None),
        x_kcdx_author_email: Optional[str] = Header(default=None)):
    """Confirm a brand-new entity (US-7) (AP18 -- the new-entity addition commits here)."""
    author = _resolve_author(body.author_name, body.author_email,
                             x_kcdx_author_name, x_kcdx_author_email)
    return _run_confirm(
        body, entity_label=f"new entity {body.name!r}", author=author,
        write=lambda version: data_core.create_entity(
            load_config().out_dir, None, body.name, body.first_version_columns,
            version=version, defer_commit=True))


@router.post("/confirm/supersede")
def confirm_supersede(
        body: ConfirmSupersede,
        x_kcdx_author_name: Optional[str] = Header(default=None),
        x_kcdx_author_email: Optional[str] = Header(default=None)):
    """Confirm a supersession (US-8 / Job 4): an UPDATE -- not AP18-gated."""
    author = _resolve_author(body.author_name, body.author_email,
                             x_kcdx_author_name, x_kcdx_author_email)
    return _run_confirm(
        body, entity_label=f"supersede kcdx_id={body.kcdx_id}", author=author,
        write=lambda version: data_core.supersede_entity(
            load_config().out_dir, None, body.kcdx_id, body.superseded_by,
            body.superseded_at_version, version=version, defer_commit=True))


@router.post("/confirm/deprecate")
def confirm_deprecate(
        body: ConfirmDeprecate,
        x_kcdx_author_name: Optional[str] = Header(default=None),
        x_kcdx_author_email: Optional[str] = Header(default=None)):
    """Confirm a deprecation (US-8 / Job 5): an UPDATE -- not AP18-gated."""
    author = _resolve_author(body.author_name, body.author_email,
                             x_kcdx_author_name, x_kcdx_author_email)
    return _run_confirm(
        body, entity_label=f"deprecate kcdx_id={body.kcdx_id}", author=author,
        write=lambda version: data_core.deprecate_entity(
            load_config().out_dir, None, body.kcdx_id,
            is_deprecated=body.is_deprecated,
            deprecated_at_version=body.deprecated_at_version,
            deprecation_replacement=body.deprecation_replacement,
            version=version, defer_commit=True))


# ---------------------------------------------------------------------------
# Cancel -- a no-op success. The preview model holds NO transaction across think-time
# (Save previews + holds nothing; Confirm opens+closes its txn in its own request). So
# there is no held txn to roll back -- Cancel just acknowledges (design S7: on Cancel,
# nothing lands -- which is already true, because nothing was started).
# ---------------------------------------------------------------------------
@router.post("/cancel")
def cancel():
    """Acknowledge a Cancel -- a no-op success. Nothing was started (Save is preview-only,
    Confirm opens+closes its own txn), so there is nothing to roll back. Returns
    {"status": "cancelled"} so the page can reset cleanly."""
    return {"status": "cancelled"}
