# Step 5 — confirm transaction: COMMIT the held DB txn + git commit + push (D16) + auth-ready seams (D17)

**What.** Add the **confirm + cancel** endpoints that close the save spine as ONE atomic
transaction (D16): on **Confirm**, COMMIT the held deferred-commit DB transaction (step 4a/4b)
AND do the server-side git **commit + push** of the DB + 3 CSVs — both as one confirm
transaction (the DB COMMIT and the git commit land together, or neither does); on **Cancel**,
ROLLBACK the held DB transaction (nothing lands — the literal "on Cancel nothing lands", design
§7). The git side: stage by **exact path** (only the DB + the 3 CSVs — never `-A`/`.`/`-u`),
respect a live `index.lock` (block-and-retry, never reap), author the commit message, and
**push to the GitHub remote** so the edit is durable beyond the container (`concurrency-git.md`).
Add the **auth-ready seams** (D17): the commit **author identity** comes from the request-context
field the operator's login supplies; the **push credential** is **env-injected** (a documented
env var name). The app builds NO login/auth/hosting — only the seams. A **dev default** lets the
backend boot + run locally without the operator's auth (a fallback identity; push skippable /
against a local remote) so the whole save→confirm→commit path is testable standalone.

**The confirm ORDER (a real correctness decision — surface at build time):** the held DB
transaction COMMIT and the git commit must compose so a failure of one does not orphan the
other (e.g. DB COMMIT succeeds, then the git commit fails — the DB now has an uncommitted-to-git
change; or the reverse). Settle the ordering + the failure handling (DB-commit-then-git, with a
git failure triggering... what? a compensating action? the export-is-from-the-committed-DB so a
git failure leaves the DB ahead) once the exact sequence is written, and SURFACE it to the user
(design-authority) — it composes with 4a's two-DB-commit ordering decision.

**Scope.** The confirm endpoint (COMMIT the held 4a txn + the git commit+push) + the cancel
endpoint (ROLLBACK the held txn), the exact-path/live-lock discipline, the
identity-from-request-context seam, the env-credential seam, and the dev default. The held-txn
registry the save (4b) populated is resolved here. No auth/login/hosting (the operator's, D17).
No frontend, no Docker.

**Test bar.** A backend test (`pytest`): a confirmed save commits the DB + 3 CSVs by exact
path (assert only those paths staged) with the request-context identity as the author;
a live `index.lock` blocks-and-retries (never reaps); the push is asserted against a
**throwaway local bare remote** (or a mocked push) — NOT a real GitHub push; the dev default
boots without the operator's credential. Touches the git layer → the inline impact-analysis
applies (grep how the existing committer discipline in `concurrency-git.md` is honored;
confirm exact-path staging + no broad add).

**Dependencies.** Step 4b (the save endpoints that open + hold the deferred-commit transaction
this step COMMITs/ROLLBACKs) + step 4a (the deferred-commit seam's commit/rollback the confirm
calls). Sequenced after 4b — the confirm has no held transaction to commit until the save spine
opens one.

**Touches existing code / shared tree.** YES — it writes to the shared `.git`/index. Honor
`concurrency-git.md` (exact-path staging, live-lock block-and-retry, no auto-branch, no hand
push to `public`). The push target is the private remote per the repo's remote topology; the
test must NOT push to a real remote.

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§8 (the commit constraint — exact-path/live-lock/push) + §10 D16 (server commit + push) +
D17 (auth-ready seams + dev default). `.claude/rules/concurrency-git.md` (the committer
discipline + the remote topology). The frontend surface:
[`ui/screens/s06-save-confirm.md`](../../../../data/maintainer-tool/ui/screens/s06-save-confirm.md)
(the "Saved" / "blocked — Retry" toast the result feeds; git is invisible — law 5).

**Disassembler-test / author-burden.** N/A — git plumbing; no author-facing game-function
input.
