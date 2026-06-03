# Step 5 — Confirm: one synchronous atomic transaction (direct-write → export → commit DB → git)

**What.** Add the **Confirm** + **Cancel** endpoints that close the save spine as ONE synchronous
atomic transaction while the page waits (D16/D19). On Confirm, the backend takes the entity + the
edit + the chosen version tag and runs, in the one request:
1. resolve the version tag (the 1b adapter, `version=`, no DLL server-side);
2. **start the deferred-commit transaction** (the 4a seam — open both DBs under a held outer txn);
3. **run the DIRECT-DB write** (the 4c direct-write path — direct INSERT/UPDATE reusing
   `_apply_one_db`'s helpers, validated against the prospective DB state, deferred/uncommitted);
4. **export** the in-transaction DB state to the **`data/db-export/`** CSVs (D20 — the derived
   record, NOT `data/seeds/`), diff-preserved;
5. **cheap integrity check** (re-export the committed DB, assert the CSV record is byte-identical
   — NOT the full bidirectional round-trip, which rebuilds the 1.3 GB bulk DB + needs the dump);
6. **commit the DB** (the 4a `commit(handle)` — USER-first then DEV, the settled ordering);
7. **git commit + push**: stage the DB + the 3 `data/db-export/` CSVs by **exact path** (never
   `-A`/`.`/`-u`), author the commit (the request-context identity), push to the **private**
   remote (the env-credential seam).

**If ANY step fails, roll back everything** — the **robust rollback** the user required, two
mechanisms split at the irreversible commit (D21): a **PRE-commit failure** (the direct-write /
validation) → the deferred-commit `rollback(handle)` (4a) discards the held txn incl.
`sqlite_sequence`/PK bumps (nothing committed). A **POST-commit failure** (export / integrity /
git) → the **4d scoped restore-point** undoes the committed write (the touched rows + the
`sqlite_sequence` + the `data/db-export/` CSVs restored) — the deferred rollback is gone once the
txn commits. Either way, nothing lands; the page gets a FAILURE status. On success, "Saved
`<entity> <version>`". The transaction opens AND closes inside the one Confirm request — nothing
held across think-time.

**Reuse the kept step-5 WIP.** The uncommitted step-5 machinery — `routes_confirm.py`,
`git_commit.py`, `csv_integrity.py` — is KEPT in the tree and reused: the Confirm-endpoint shape,
the git commit/push (exact-path staging, push-to-private, the auth seams), and the cheap integrity
check survive the direct-write pivot. The rework changes only: (a) the WRITE underneath (route
through 4c's direct-write, not the seed-rebuild `db_editor`); (b) the export target
(`data/db-export/`, not `data/seeds/`); (c) the index.lock handling (below); (d) the post-commit
rollback — DROP the WIP's full-file snapshot (`restore_point.py`) and CALL the **4d data-core
scoped restore-point** (capture-before-commit, restore-on-post-commit-failure; a few KB, never the
1.3GB DEV DB).

**Event-driven index.lock (no poll — user-settled).** The previous WIP used a sleep-poll loop
waiting for a live `.git/index.lock` to clear (`polling.md` violation). Rework it event-driven:
run the git command and **key off git's own non-zero exit** (git fails immediately with
`Unable to create '.git/index.lock': File exists` on a locked index) → surface "busy — retry" to
the page (or a bounded retry driven by git's RETURN, not a clock). NO `sleep`-loop / file-stat
poll. The lock is NEVER reaped (reaping a live lock = index corruption, `concurrency-git.md`
rule 5).

**The confirm ORDER + post-DB-commit failure (settled by the two-mechanism rollback, D21).**
The direct-write + validation run inside the deferred txn — a failure there `rollback(handle)`s
cleanly (nothing committed). After the DB commit (which is irreversible), the export / integrity
/ git steps run (the export MUST read the committed DB — `export_seeds` opens its own fresh
connection, can't see the uncommitted txn). If any of THOSE fails, the deferred rollback is gone,
so the **4d scoped restore-point** (captured before the commit) undoes the committed write — the
touched rows + the `sqlite_sequence` (PK reset) + the `data/db-export/` CSVs restored — so DB and
git stay in lockstep, "on failure nothing lands" holds literally. (Reverting a committed SQLite
write is what the 4d scoped restore-point provides; the deferred ROLLBACK covers only the
pre-commit failure. **4d owns the restore-point capture** — a data-core capability, D13/law 6.)

**Auth-ready seams (D17).** The commit **author identity** comes from the request-context field
the operator's login supplies; the **push credential** is **env-injected** (a documented env var
name). NO login/auth/hosting built — only the seams. A **dev default** lets the backend boot + run
locally without the operator's auth (a fallback identity; push skippable / against a local remote)
so the whole Save→Confirm→commit path is testable standalone.

**Scope.** The Confirm endpoint (the synchronous transaction, steps 1–7) + the Cancel endpoint
(a no-op success — nothing was started). The direct-write call (4c), the `data/db-export/` export,
the cheap integrity check, the exact-path/event-driven-lock git discipline, the robust rollback,
the identity-from-request-context seam, the env-credential seam, the dev default. No auth/login/
hosting (the operator's, D17). No frontend, no Docker.

**Test bar.** A backend test (`pytest`) on the mini-dump checkout, real API → real data-core →
real DBs + a **throwaway local bare git remote** (NOT real GitHub):
- a successful Confirm (UPDATE + at least one CREATE shape + a create-version-at-a-new-tag): the
  DB holds the new value, the 3 `data/db-export/` CSVs are updated, a git commit staged ONLY the
  DB + the 3 `data/db-export/` CSVs by exact path (assert only those staged) with the
  request-context identity as author, AND the push reached the throwaway remote;
- a failure injected at each step (invalid edit → caught at the direct-write; integrity
  divergence; git failure) → **the DB + CSVs byte-identical to before, INCLUDING the
  `sqlite_sequence` values** (the robust-rollback proof) + a FAILURE status;
- a live `.git/index.lock` → git's own non-zero exit surfaces "busy — retry" (NO poll, NEVER
  reaps — assert the lock still present after);
- the dev default boots + commits without the operator's credential;
- Cancel is a no-op (nothing written).
Touches the git layer → honor `concurrency-git.md` (exact-path staging, no broad add, push to
private only, no hand-push to public, no auto-branch).

**Dependencies.** Step 4c (the direct-write path Confirm calls) + step 4d (the scoped
restore-point Confirm calls on a post-commit failure) + step 4b-rework (the preview the maintainer
reviews before Confirm — Confirm re-sends the same edit) + step 4a (the deferred-commit seam
Confirm opens+commits in the one request) + step 1b (the `version=` adapter). The kept step-5 WIP
(`routes_confirm.py` / `git_commit.py` / `csv_integrity.py`) is the reuse base; its full-file
`restore_point.py` is dropped (replaced by the 4d call). Sequenced after 4c + 4b-rework + 4d.

**Touches existing code / shared tree.** YES — it writes to the shared `.git`/index. Honor
`concurrency-git.md` (exact-path staging, event-driven lock handling, no auto-branch, no hand push
to `public`). The push target is the private remote; the test must NOT push to a real remote.

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§7 (the save spine) + §8 (the commit constraint — exact-path/lock/push) + §10 **D19** (direct-write
+ robust rollback) + **D20** (the `data/db-export/` target) + D1 (DB authoritative) + D16 (server
commit + push) + D17 (auth-ready seams + dev default). `.claude/rules/concurrency-git.md` (the
committer discipline + the remote topology) + `.claude/rules/polling.md` (the event-driven lock
handling, no poll). The frontend surface:
[`ui/screens/s06-save-confirm.md`](../../../../data/maintainer-tool/ui/screens/s06-save-confirm.md)
(the "Saved" / "blocked — Retry" toast the result feeds; the page WAITS for the status; git is
invisible — law 5).

**Disassembler-test / author-burden.** N/A — git plumbing; no author-facing game-function input.
