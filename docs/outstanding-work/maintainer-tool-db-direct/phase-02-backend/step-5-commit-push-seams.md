# Step 5 — Confirm: one synchronous atomic transaction (DB ops → CSV → commit DB → commit git)

**What.** Add the **Confirm** endpoint that runs the whole queued save as ONE synchronous,
atomic transaction while the page waits for a success/failure status (and a trivial **Cancel**
that does nothing — no state was held). On Confirm, the backend takes the entity + the edit +
the chosen version tag and runs, in the one request:
1. **start the transaction** (the 4a deferred-commit seam — open both DBs under a held outer txn);
2. **run all DB operations** (the chosen `db_editor` write, `version=` from the 1b adapter, deferred);
3. **run all CSV operations** (export the in-transaction DB state to the `data/seeds/` CSVs,
   diff-preserved) + the **round-trip oracle** (byte-identity — the design §7 round-trip, owed here);
4. **commit the DB** (the 4a `commit(handle)` — USER-first then DEV, the settled ordering);
5. **commit + push git** (stage the DB + 3 CSVs by **exact path** — never `-A`/`.`/`-u` — respect
   a live `index.lock` block-and-retry, author the commit message, push to the private remote).

**If ANY step fails, roll back everything** (the deferred txn's `rollback(handle)` undoes the DB;
the CSV export is reverted to its pre-Confirm state; no git commit happens) and return a FAILURE
status to the waiting page. On success, return "Saved `<entity> <version>`". This is the literal
"on Cancel/failure nothing lands" (design §7) — the transaction opens AND closes inside this one
request, nothing is held across the maintainer's think-time (plan-spec §"Cross-step invariants").

**The post-DB-commit failure (settled by the model).** Steps 1–4 are inside the deferred txn, so
a failure there rolls back cleanly (nothing committed). The remaining edge: step 4 (DB commit)
succeeds, then step 5 (git) fails. The model's answer is **roll back everything** — but a
committed SQLite txn cannot be un-committed, so order to AVOID the edge: run the CSV export +
round-trip (step 3) and stage the git changes BEFORE the DB commit where possible, so the only
post-DB-commit action is the git `commit`+`push` of already-staged content; a git failure there
leaves the DB committed + the CSVs written + staged (a re-Confirm/retry re-commits the same staged
content — git is idempotent on identical content) and the page shows "saved, commit failed —
retry". Settle the exact step order + the git-failure recovery when the sequence is written, and
SURFACE it (it is the DB-authoritative D1 edge: the DB + CSVs are the source of truth, git is the
durable mirror that retries). The two-DB-commit ordering (USER-first, 4a) composes under it.

**Auth-ready seams (D17).** The commit **author identity** comes from the request-context field
the operator's login supplies; the **push credential** is **env-injected** (a documented env var
name). The app builds NO login/auth/hosting — only the seams. A **dev default** lets the backend
boot + run locally without the operator's auth (a fallback identity; push skippable / against a
local remote) so the whole Save→Confirm→commit path is testable standalone.

**Scope.** The Confirm endpoint (the 5-step synchronous transaction) + the Cancel endpoint
(a no-op success — nothing was started). The exact-path/live-lock git discipline, the
identity-from-request-context seam, the env-credential seam, the dev default. No auth/login/hosting
(the operator's, D17). No frontend, no Docker.

**Test bar.** A backend test (`pytest`) on the mini-dump checkout, real API → real data-core →
real DBs + a **throwaway local bare git remote** (NOT real GitHub):
- a successful Confirm: the DB holds the new value AND the 3 `data/seeds/` CSVs are updated AND a
  git commit landed staging ONLY the DB + 3 CSVs by exact path (assert only those paths staged)
  with the request-context identity as the author, AND the push reached the throwaway remote;
- a failure injected at each step (an invalid edit → caught at DB ops; a round-trip divergence; a
  git failure) → **the DB + CSVs are byte-identical to before** (rollback-everything — the
  load-bearing atomicity proof) and the page gets a FAILURE status;
- a live `index.lock` blocks-and-retries (never reaps);
- the dev default boots + commits without the operator's credential;
- Cancel is a no-op (nothing was written).
Touches the git layer → the inline impact-analysis applies (honor `concurrency-git.md` exact-path
staging + no broad add).

**Dependencies.** Step 4b (the Save preview the maintainer reviews before Confirm — Confirm
re-sends the same edit) + step 4a (the deferred-commit seam Confirm opens+commits in the one
request) + step 1b (the `version=` adapter) + step 2a (the export/round-trip surface). Sequenced
after 4b.

**Touches existing code / shared tree.** YES — it writes to the shared `.git`/index. Honor
`concurrency-git.md` (exact-path staging, live-lock block-and-retry, no auto-branch, no hand push
to `public`). The push target is the private remote; the test must NOT push to a real remote.

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§7 (the save spine) + §8 (the commit constraint — exact-path/live-lock/push) + §10 D1 (DB
authoritative, CSVs the derived layer) + D16 (server commit + push) + D17 (auth-ready seams + dev
default). `.claude/rules/concurrency-git.md` (the committer discipline + the remote topology).
The frontend surface: [`ui/screens/s06-save-confirm.md`](../../../../data/maintainer-tool/ui/screens/s06-save-confirm.md)
(the "Saved" / "blocked — Retry" toast the result feeds; the page WAITS for the status; git is
invisible — law 5).

**Disassembler-test / author-burden.** N/A — git plumbing; no author-facing game-function input.
