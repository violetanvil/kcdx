"""app.git_commit -- the Confirm transaction's git plumbing (design S8 / D16; the
backend's OWN concern, NOT data-core rule logic).

WHAT THIS OWNS (and what it does NOT)
-------------------------------------
The git commit + push half of the Confirm transaction (step 5): stage the three
data/db-export/ CSVs by EXACT PATH (the DB is the local originator, NOT git-tracked --
D1/D20; only its derived CSV export is the git record), author the commit with the
request-context identity, and push to the PRIVATE remote with the env-injected
credential. This module stages whatever exact-path list the caller hands it; the
caller (routes_confirm._staged_rel_paths) decides the set. This is plumbing the
backend owns -- it computes nothing the data-core owns (validate / SQL / delta /
round-trip are the data-core's; staging+committing+pushing the resulting files is the
backend's, design S5 law 6). It runs `git` via subprocess with `git -C <checkout>`
(NEVER `cd` -- a parked CWD silently breaks the session, `.claude/rules/shell-cwd-stability.md`).

THE GIT DISCIPLINE (`.claude/rules/concurrency-git.md`, the load-bearing contract)
----------------------------------------------------------------------------------
  - EXACT-PATH STAGING: `git -C <checkout> add -- <csv> <csv> <csv>` -- only the three
    db-export CSVs the Confirm wrote, BY NAME (the DB is the local originator, NOT
    committed -- D1/D20). NEVER `-A` / `.` / `-u` / `--all`: the index is shared across
    parallel chats, so a broad add sweeps another chat's in-flight files into this commit
    (the documented contamination vector). Staging only the named files is what keeps
    Confirm's commit clean.
  - NO AUTO-BRANCH: Confirm commits to the CURRENT branch (`main`). It never runs
    `checkout -b` / `switch -c` / `branch` -- that is global state tangling parallel
    histories. (This module issues no branch command at all.)
  - PUSH TARGETS `private` ONLY: the everyday push target (the comprehensive private
    repo). NEVER `public` / `--all` / `--mirror` -- a hand push there ships the private
    tree and leaks it; `public` is reached ONLY via publish-public.ps1. This module
    pushes exactly `git -C <checkout> push <PRIVATE_REMOTE> HEAD` -- the named private
    remote, the current HEAD, nothing else.
  - LIVE index.lock -> EVENT-DRIVEN on git's OWN exit, never reap, never poll: a
    parallel chat holding `.git/index.lock` mid-mutation races this stage/commit.
    git itself FAILS IMMEDIATELY with `Unable to create '.git/index.lock': File
    exists` on a locked index -- so this module keys off git's non-zero exit + that
    stderr signature and surfaces "busy -- retry" RIGHT AWAY (the page renders Retry).
    There is NO sleep-loop and NO file-stat poll waiting for the lock to clear
    (`.claude/rules/polling.md` -- event-driven is the default; the previous WIP's
    `_wait_for_index_lock_to_clear` sleep-poll was the violation this rework removes).
    The lock is NEVER deleted (reaping a live lock = two writers in the index =
    corruption, `.claude/rules/concurrency-git.md` rule 5).

THE AUTH-READY SEAMS (D17 -- the SEAMS, not the auth)
-----------------------------------------------------
  - The commit AUTHOR identity is supplied by the caller (the request-context identity
    the operator's login layer populates; a documented dev-default when absent). This
    module takes (author_name, author_email) and authors with them via
    `--author=...` + the committer env -- it builds no login/auth.
  - The push CREDENTIAL is ENV-INJECTED: KCDX_PUSH_TOKEN (a documented env var). When
    absent, the push is SKIPPED (the dev default -- the app boots + commits locally
    without the operator's auth, design D17). When present, it is wired into the push
    via an ephemeral credential (an `http.extraheader` Authorization, never written to
    disk, never logged). This module reads os.environ; it provisions no credential.

This module raises a typed error on ANY git-step failure (stage / commit / push); it
NEVER swallows one. The CALLER (routes_confirm) composes recovery: with the robust
rollback (D19 -- the restore point), a git failure after the DB commit triggers the
backend restore point so the DB + db-export CSVs roll back to their pre-Confirm bytes
("on failure nothing lands" holds literally). git is the durable mirror, and a failure
to update it rolls back the local commit too -- DB and git stay in lockstep.
"""
import logging
import os
import subprocess

# The PRIVATE remote -- the everyday push target (`.claude/rules/concurrency-git.md`
# Remotes: a bare push goes to private; public is reached ONLY via publish-public.ps1).
# Confirm pushes HERE, never `public`. Named once so the push target is auditable.
PRIVATE_REMOTE = "private"

# The env var the operator wires the push credential to (D17 -- the auth-ready seam).
# A GitHub token (PAT / installation token) the operator's deployment injects. ABSENT
# -> the push is skipped (the dev default -- commit locally, boot without auth). The
# app provisions NO credential; it reads this name.
PUSH_TOKEN_ENV_VAR = "KCDX_PUSH_TOKEN"

# The substring git prints to stderr when it cannot take the shared index lock (a
# parallel chat holds `.git/index.lock` mid-mutation). We detect it on git's OWN
# non-zero exit -- the event git itself raises -- and surface "busy -- retry" with NO
# poll and NO reap (`.claude/rules/polling.md` + `.claude/rules/concurrency-git.md`
# rule 5). git's message is stable across versions ("Unable to create '.../index.lock':
# File exists"); match the lock-file token, case-insensitively, so a path/locale variant
# still classifies as the lock-contention case rather than a generic stage failure.
_INDEX_LOCK_STDERR_TOKEN = "index.lock"

log = logging.getLogger(__name__)


class GitCommitError(RuntimeError):
    """A git step (stage / commit / push) failed. Carries `.stage` (which step) and the
    git stderr so the caller composes the right recovery: a stage/commit failure means
    NOTHING was committed (the caller rolls back the held DB txn -- but Confirm commits
    the DB BEFORE git, so in practice a git failure here is post-DB-commit -> surface
    retryable, the DB change is real, D1); a push failure means the commit IS landed
    locally and a retry re-pushes the same content (git is idempotent)."""

    def __init__(self, stage, message):
        super().__init__(message)
        self.stage = stage


class IndexLockBusy(GitCommitError):
    """The shared `.git/index.lock` is held -- a parallel chat is mid-commit. Detected
    EVENT-DRIVEN, off git's OWN non-zero exit (git fails immediately with "Unable to
    create '.git/index.lock': File exists"), NOT a sleep-poll waiting for it to clear
    (`.claude/rules/polling.md`). The Confirm surfaces this as "Save blocked -- files
    locked, Retry" (design S7 save-result state); the lock is NEVER reaped
    (`.claude/rules/concurrency-git.md` rule 5 -- reaping a live lock corrupts the
    shared index). `.stage` is the git step that hit the lock (add / commit / push)."""

    def __init__(self, stage, message):
        super().__init__(stage, message)


def _is_index_lock_stderr(stderr):
    """True when git's stderr is the shared-index-lock-contention signature -- the
    event git raises when another writer holds `.git/index.lock`. Matched on the lock
    token (case-insensitive) so a path/locale variant still classifies as lock
    contention (Retry) rather than a generic stage failure."""
    return _INDEX_LOCK_STDERR_TOKEN in (stderr or "").lower()


def _run_git(checkout_path, args, *, stage, env=None):
    """Run one `git -C <checkout> <args>` with a clear error on failure. Uses `git -C`
    (NEVER `cd` into the checkout -- a parked CWD silently breaks the session,
    `.claude/rules/shell-cwd-stability.md`). A non-zero exit raises GitCommitError(stage)
    carrying git's stderr; the caller maps the stage to its recovery. A non-zero exit
    whose stderr is the index-lock signature raises IndexLockBusy(stage) instead --
    EVENT-DRIVEN off git's OWN exit (no poll, no reap), surfaced as Retry."""
    cmd = ["git", "-C", checkout_path] + list(args)
    proc = subprocess.run(cmd, capture_output=True, text=True, env=env)
    if proc.returncode != 0:
        stderr = (proc.stderr or proc.stdout or "").strip()
        if _is_index_lock_stderr(stderr):
            # The shared index is locked by a live parallel writer. git ITSELF refused
            # to take the lock (it never overwrites a held index.lock) -- so we surface
            # Retry off that exit, immediately, with no wait-loop and no reap.
            # logging.md: log before raising -- name the stage + that the lock is held.
            log.warning("git %s blocked -- shared .git/index.lock is held by a live "
                        "writer (checkout=%s); surfacing Retry, NOT reaping the lock",
                        stage, checkout_path)
            raise IndexLockBusy(
                stage,
                f"the shared git index is locked (another writer holds "
                f".git/index.lock); retry the save -- the lock is never force-removed "
                f"(reaping a live lock corrupts the index): {stderr}")
        # logging.md: every git failure is logged before it raises -- name the stage +
        # git's own stderr (NOT the token-bearing env). The push env is built per-call
        # and never logged (the credential stays out of logs, by construction).
        log.warning("git %s failed (checkout=%s): %s", stage, checkout_path, stderr)
        raise GitCommitError(stage, f"git {stage} failed: {stderr}")
    return proc.stdout


def commit_and_push(checkout_path, rel_paths, *, message, author_name, author_email):
    """Stage `rel_paths` by EXACT PATH, commit with the request-context author, and push
    to the PRIVATE remote -- the git half of the Confirm transaction (design S8 / D16).

    The order is deliberately stage -> commit -> push so the post-DB-commit window the
    caller composes around is minimal: the DB is already committed (Confirm step 4) when
    this runs, so a failure HERE leaves the DB change real (D1) and is surfaced as
    retryable. A commit then push: the commit is local + durable; the push is the durable
    mirror, retryable on its own (git is idempotent on identical content).

    Parameters:
      checkout_path -- the git repo root (config.checkout_path). `git -C` targets it;
                       the CWD is never changed (shell-cwd-stability.md).
      rel_paths     -- the files to stage, as paths RELATIVE to checkout_path (the 3
                       data/db-export/ CSVs; the DB is the local originator, NOT staged --
                       D1/D20). Staged BY NAME -- never `-A`/`.`/`-u`
                       (concurrency-git.md rule 2 -- the shared-index contamination vector).
      message       -- the commit message body (the caller authors it -- "Saved <entity>
                       <version>" style; git is invisible to the maintainer, design S7).
      author_name / author_email -- the commit author identity from the request context
                       (D17 -- the operator's login supplies it; a dev default when absent).

    Returns a dict report: {"committed": True, "pushed": <bool>, "head": <sha>,
      "push_skipped_reason": <str|None>}. `pushed` is False with a reason when the env
      credential is absent (the dev default -- commit locally, no push).

    Raises:
      IndexLockBusy  -- the shared index is locked by a live writer, detected off git's
                        OWN non-zero exit (no poll, no reap); surfaced as Retry.
      GitCommitError -- a stage / commit / push step failed (`.stage` names which);
                        the caller composes recovery (a push failure = commit landed,
                        retryable; a stage/commit failure = nothing committed by git).
    """
    # 1. Stage BY EXACT PATH. The `--` separates the pathspec so a file named like a flag
    #    is unambiguous; the explicit file list is the whole point (never a broad add).
    #    A live shared index.lock makes git itself fail here -- _run_git raises
    #    IndexLockBusy off that exit (event-driven, no pre-check poll, no reap).
    _run_git(checkout_path, ["add", "--"] + list(rel_paths), stage="add")

    # 2b. A genuine no-delta edit (the DB committed but mutated no on-disk content -- e.g.
    #     an edit the applier classifies as a no-op against the current baseline) leaves
    #     NOTHING staged. `git commit` would then error "nothing to commit". That is not a
    #     failure -- the DB committed exactly what the maintainer confirmed; there is just
    #     no file delta to record. Report it as a no-delta success (no commit, no push)
    #     rather than committing nothing or erroring. `diff --cached --quiet` exits 0 when
    #     the index matches HEAD (nothing staged), 1 when there IS a staged delta.
    if _nothing_staged(checkout_path):
        head = _run_git(checkout_path, ["rev-parse", "HEAD"], stage="rev-parse").strip()
        log.info("Confirm produced no on-disk delta (DB no-op); no git commit needed "
                 "(HEAD=%s)", head[:12])
        return {"committed": False, "pushed": False, "head": head,
                "push_skipped_reason": "no on-disk delta (the edit was a DB no-op)",
                "no_delta": True}

    # 3. Commit with the request-context author. The committer is set via env so the
    #    audit trail records the same identity (the operator's login supplies it, D17);
    #    a dev default is the caller's fallback. The message is read from stdin (-F -)
    #    so an arbitrary maintainer-facing body needs no shell escaping.
    commit_env = dict(os.environ)
    commit_env["GIT_COMMITTER_NAME"] = author_name
    commit_env["GIT_COMMITTER_EMAIL"] = author_email
    # A parallel writer can take index.lock between the add and the commit; git's commit
    # then fails with the lock signature and _commit_with_message raises IndexLockBusy
    # off that exit (event-driven, no poll) -- the caller surfaces Retry.
    _commit_with_message(checkout_path, message, author_name, author_email, commit_env)

    head = _run_git(checkout_path, ["rev-parse", "HEAD"], stage="rev-parse").strip()

    # 4. Push to the PRIVATE remote -- the durable mirror (D16). Skipped (dev default)
    #    when no env credential is present; the commit is already local + durable.
    pushed, skip_reason = _push_to_private(checkout_path)
    if pushed:
        log.info("Confirm committed %s and pushed to %s (HEAD=%s)",
                 list(rel_paths), PRIVATE_REMOTE, head[:12])
    else:
        log.info("Confirm committed %s locally (HEAD=%s); push skipped: %s",
                 list(rel_paths), head[:12], skip_reason)
    return {"committed": True, "pushed": pushed, "head": head,
            "push_skipped_reason": skip_reason, "no_delta": False}


def _nothing_staged(checkout_path):
    """True when the index matches HEAD after staging -- i.e. nothing is staged (a
    no-delta edit). `git diff --cached --quiet` exits 0 when there is NO staged change,
    1 when there IS one (any other exit is a git error -> surface it). Used to skip an
    empty commit (a DB no-op leaves no file delta)."""
    proc = subprocess.run(
        ["git", "-C", checkout_path, "diff", "--cached", "--quiet"],
        capture_output=True, text=True)
    if proc.returncode == 0:
        return True
    if proc.returncode == 1:
        return False
    stderr = (proc.stderr or proc.stdout or "").strip()
    log.warning("git diff --cached --quiet failed (checkout=%s): %s",
                checkout_path, stderr)
    raise GitCommitError("diff-cached", f"git diff --cached failed: {stderr}")


def _commit_with_message(checkout_path, message, author_name, author_email, env):
    """`git commit -F - --author=...` reading the message from stdin (no shell escaping
    of an arbitrary maintainer-facing body). The author identity is the request
    context's (D17). A nothing-staged commit would error -- but Confirm always stages a
    real DB+CSV delta, so an empty commit is itself a defect to surface, not to allow
    with --allow-empty."""
    author = f"{author_name} <{author_email}>"
    cmd = ["git", "-C", checkout_path, "commit", "-F", "-", f"--author={author}"]
    proc = subprocess.run(cmd, input=message, capture_output=True, text=True, env=env)
    if proc.returncode != 0:
        stderr = (proc.stderr or proc.stdout or "").strip()
        if _is_index_lock_stderr(stderr):
            # A live writer took the index lock between the stage and the commit -- git
            # refused to commit. Event-driven Retry off git's exit (no poll, no reap).
            log.warning("git commit blocked -- shared .git/index.lock held by a live "
                        "writer (checkout=%s); surfacing Retry, NOT reaping", checkout_path)
            raise IndexLockBusy(
                "commit",
                f"the shared git index is locked (another writer holds "
                f".git/index.lock); retry the save -- the lock is never force-removed: "
                f"{stderr}")
        log.warning("git commit failed (checkout=%s): %s", checkout_path, stderr)
        raise GitCommitError("commit", f"git commit failed: {stderr}")


def _push_to_private(checkout_path):
    """Push HEAD to the PRIVATE remote with the env-injected credential (D17). Returns
    (pushed: bool, skip_reason: str|None).

    NO credential in env -> SKIP (the dev default -- the app boots + commits locally
    without the operator's auth, design D17 / S8). The credential, when present, is
    injected as an ephemeral `http.extraheader` Authorization on the single push command
    (`git -c http.<remote-url>.extraheader=...`) -- never written to a config file, never
    logged. The push targets exactly `PRIVATE_REMOTE HEAD` -- never `public`, never
    `--all`/`--mirror` (concurrency-git.md rule 6 -- a hand push to public leaks the
    private tree)."""
    token = os.environ.get(PUSH_TOKEN_ENV_VAR)
    if not token:
        # The dev default (D17): no operator credential -> commit landed locally, push
        # skipped. This is the normal local/test posture, not a failure.
        return (False, f"no {PUSH_TOKEN_ENV_VAR} in env (dev default -- committed "
                       f"locally, push skipped)")

    # The credential as an ephemeral Authorization header on THIS push only. A GitHub
    # token authenticates as `x-access-token:<token>` via Basic, or directly as a Bearer;
    # the Basic form is the portable one for a PAT/installation token over HTTPS. The
    # header is passed via `-c` so it lives only in this process's git invocation -- not
    # in .git/config, not on disk, not in the log line.
    import base64
    basic = base64.b64encode(f"x-access-token:{token}".encode()).decode()
    header_cfg = f"http.extraheader=Authorization: Basic {basic}"

    # The push reads/writes refs, not the index, so it does not contend for index.lock;
    # it runs directly (no lock pre-check). A push failure means the local commit IS
    # landed -- the caller surfaces it as retryable (the durable mirror lagged).
    cmd = ["git", "-C", checkout_path, "-c", header_cfg,
           "push", PRIVATE_REMOTE, "HEAD"]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        stderr = (proc.stderr or proc.stdout or "").strip()
        # Redact any echo of the header in git's stderr defensively (git does not print
        # it, but never log the credential). Name the remote + that the commit is landed.
        safe = stderr.replace(basic, "<redacted>")
        log.warning("git push to %s failed (checkout=%s): %s -- the commit IS landed "
                    "locally; a retry re-pushes the same content",
                    PRIVATE_REMOTE, checkout_path, safe)
        raise GitCommitError("push", f"git push to {PRIVATE_REMOTE} failed: {safe}")
    return (True, None)
