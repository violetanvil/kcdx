# Step 5 — git commit + push on confirm (D16) + auth-ready seams (D17)

**What.** Add the server-side **commit + push** that completes the save spine on a confirmed
edit (D16): stage by **exact path** (only the DB + the 3 CSVs — never `-A`/`.`/`-u`), respect
a live `index.lock` (block-and-retry, never reap), author the commit message, AND **push to
the GitHub remote** so the edit is durable beyond the container (`concurrency-git.md`). Add
the **auth-ready seams** (D17): the commit **author identity** comes from the request-context
field the operator's login supplies; the **push credential** is **env-injected** (a documented
env var name). The app builds NO login/auth/hosting — only the seams. A **dev default** lets
the backend boot + run locally without the operator's auth (a fallback identity; push
skippable / against a local remote) so the whole save→commit path is testable standalone.

**Scope.** The commit+push wiring on the save endpoint (step 4), the exact-path/live-lock
discipline, the identity-from-request-context seam, the env-credential seam, and the dev
default. No auth/login/hosting (the operator's, D17). No frontend, no Docker.

**Test bar.** A backend test (`pytest`): a confirmed save commits the DB + 3 CSVs by exact
path (assert only those paths staged) with the request-context identity as the author;
a live `index.lock` blocks-and-retries (never reaps); the push is asserted against a
**throwaway local bare remote** (or a mocked push) — NOT a real GitHub push; the dev default
boots without the operator's credential. Touches the git layer → the inline impact-analysis
applies (grep how the existing committer discipline in `concurrency-git.md` is honored;
confirm exact-path staging + no broad add).

**Dependencies.** Step 4 (the save endpoint the commit completes). Sequenced after step 4 —
the commit has nothing to land until the save spine writes.

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
