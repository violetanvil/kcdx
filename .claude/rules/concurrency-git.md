---
paths:
  - "**/*"
---

# Git concurrency — do not auto-branch; this repo runs parallel chats

Multiple Claude Code chats run against this ONE repository at once — one shared working tree, `.git`, HEAD, index. The chats are NOT isolated. `git checkout -b` / `git switch -c` is global state: it switches the OTHER chat's branch too, and its next commit lands on yours, tangling two histories.

## Rules

1. **Never auto-create a branch.** Do not run `git checkout -b`, `git switch -c`, or `git branch <new>` on your own initiative — **not even when on `main`.** Committing to `main` is correct here: this repo is prerelease, single-author, no PR flow, and `main` is where all work lives. This **overrides** the harness default "if on the default branch, branch first." Branch and worktree decisions belong to the user; if you think a branch is warranted, ask — don't create one.

2. **Commit to the current branch, stage by specific path.** Always `git add <specific files>`, never `git add -A` / `.` / `-u`. The index is shared between chats — a broad add can sweep the OTHER chat's in-flight files into your commit. Staging only your own named files is what keeps two parallel chats from cross-contaminating a commit. **This rule constrains HOW to commit, not WHETHER.** Cadence is governed by CLAUDE.md's "Commit at coherent milestones" hard rule (close-loop artifacts trigger `/commit` without asking); specific-file staging is the concurrency-safe HOW.

3. **Never switch branches or stash on your own initiative.** `git checkout <other-branch>`, `git switch`, `git stash` all mutate the shared working tree — they can clobber or hide the other chat's uncommitted edits (`git stash push -u` silently captures files you didn't author). Don't. The one sanctioned exception is the `/debug` historical-commit probe, which uses a **detached worktree** (`git worktree add -d`), never a checkout/stash in the live tree.

4. **True isolation = a worktree, not a branch.** If genuinely-parallel isolated work is needed, the mechanism is a separate git worktree (its own working dir + HEAD + index, shared history) — e.g. dispatch an agent with `isolation: "worktree"`, or the user opens one. A branch alone does NOT isolate, because the working tree is still shared. Do not reach for this unilaterally; surface it to the user.

5. **Check before you assume.** Before any commit, `git branch --show-current` + `git status` — another chat may have changed the branch or left the tree dirty. Read state, don't assume.

## Destructive ops — confirm LIVE state, never act on stale/cancelled output

A destructive command (`git revert`, `git reset --hard`, `git clean -f`, `rm -r`/`Remove-Item -Recurse`, `git checkout -- <path>`, `git restore <path>`) acts on the working tree's CURRENT state. A tool result that was true when produced is NOT a confirmed fact now — a cancelled call, a superseded parallel-tool output, or a parallel chat's edit can have invalidated it. Acting destructively on a stale reading deletes the wrong thing.

1. **Re-confirm before destroying.** Before issuing any destructive command, RE-OBSERVE the state it acts on: re-run the read that justified it (a cancelled or superseded parallel-tool result is not evidence), then `git status` + `git branch --show-current`. The shared tree means another chat's edits may be present that your earlier read never saw (rule 1).

2. **Target by exact path, never blanket.** Delete/revert the specific files you confirmed, never a directory-wide `rm -r` / `git clean` / `git reset --hard` on an inferred set. Blanket destruction on a stale reading is the misfire this section exists to stop.

3. **Recoverability axis — local is reflog-recoverable; network is gone.** Tier by what can be undone:
   - **Recoverable (WARN):** `git revert` (a new commit), `git checkout -- <file>` / `git restore <file>` (tracked, restorable from HEAD).
   - **Local-irreversible (BLOCK):** `rm -r` / `Remove-Item -Recurse` / `git clean -f` (untracked, no reflog) and `git reset --hard` (uncommitted edits gone). A no-pathspec form hits the ENTIRE shared tree — every parallel chat's edits — not a scoped subset; scope it.
   - **Network-irreversible (the worst — auto-safened):** `git push --force` past a peer's pushed commits has NO remote reflog. The auto-safener rewrites bare `--force`/`-f` → `--force-with-lease`, which REFUSES on a remote that moved under you (the parallel-chat collision) instead of obliterating it. A non-colliding force-push still succeeds — flow uninterrupted.

   Treat each irreversible tier as a deliberate stop: re-confirm fully, then re-issue.

4. **Broad staging — scope-fenced (BLOCK only on a stranger's file).** Broad staging (`git add -A`/`-u`/`--all`/`.`, `git commit -a`) can sweep ANOTHER chat's in-flight files into your commit (rule 2) — the documented contamination vector. A per-session touched-set (every file THIS chat edited, recorded by `track-touched-files.ps1`) lets the guard fire ONLY on the contaminating case: a dirty tracked file the session never touched → BLOCK; all-own-work → silent; no touched-set yet → WARN. Stage by exact path to stay clear of it entirely. `git checkout <branch>` / `git switch` / `git stash` mutate the ONE tree every chat shares (rule 3) — these WARN (`stash push -u` silently captures untracked files another chat authored).

5. **Shared `.git/index.lock` — the parallel-index race.** Parallel chats share ONE `.git`. When one holds `index.lock` mid-mutation, another chat's index-mutating command (`add`/`commit`/`rebase`/…) races it — corruption or a hard error. `guard-git-lock.ps1` turns that into a clean BLOCK-and-retry: a LIVE lock (young, or a `git` process running) → block the command (re-run when it clears) and block any delete OF the lock (reaping a live lock = two writers in the index); a STALE lock (old, no git running) → WARN, pass.

The command-text guards above are blind to a deletion done by an invoked SCRIPT or a variable/glob-expanded `rm` — the dangerous part isn't a literal token they can match. `tripwire-dynamic-deletion.ps1` (`PostToolUse`) is the consequence-side backstop: after a script/interpreter invocation or a dynamic `rm`, it checks the working tree for an UNSTAGED tracked deletion (a file gone that the agent did NOT `git rm`) and loudly WARNS if one appears. Detection-only (the action already ran) — recover with `git checkout -- <path>` BEFORE committing.

Seven hooks enforce this. `PreToolUse` on `Bash`/`PowerShell` (command-time, in order): `guard-git-lock.ps1` blocks on a live shared-index lock; `guard-push-target.ps1` blocks a hand `git push public`/`--all`/`--mirror` (§Remotes leak) and WARNS remote/identity mutation; `guard-force-push.ps1` auto-safens `--force`→`--force-with-lease` (rewrites, never blocks); `guard-destructive-ops.ps1` runs the scope-fence + WARNS the recoverable/shared-tree set + BLOCKS the local-irreversible set. `PostToolUse`: `tripwire-dynamic-deletion.ps1` (`Bash`/`PowerShell`) warns on a surprise unstaged deletion; `track-touched-files.ps1` (`Edit`/`Write`) records this session's touched-set for the scope-fence. A block is a pause, not a veto: re-confirm live state, then re-issue.

## Remotes — this IS the private repo; public is an allowlisted snapshot

This repo has TWO remotes. The local tree is the comprehensive **private** repo.

- `private` → `violetanvil/kcdx-private` — holds everything not in `.gitignore` (incl. `.claude/`, `CLAUDE.md`, `_research/`, `third-party-ghidra/` scripts, `test-fixtures/`, all of `docs/`). `main` tracks `private/main`, so **a bare `git push` goes to private.** This is the everyday push. `.gitignore` here is the ordinary private-repo ignore list (build output, machine state, heavy/reproducible binaries) — it does NOT define the public boundary.
- `public` → `violetanvil/kcdx` — a fresh single-commit snapshot on an unrelated history, force-pushed each publish. Reached ONLY via `pwsh ./publish-public.ps1` (`-DryRun` previews).

The public boundary is an **ALLOWLIST inside the script**, not a denylist and not `.gitignore`. The script publishes only its listed public dirs (`src`, `include`, `vendor`, `data`, `examples`, `kcdx-engine`, `test-plugins`, `tools`, `docs`) + root files (`README.md`, `LICENSE`, `CMakeLists.txt`, `build.ps1`, `package-release.ps1`). **Everything else defaults to private** — a new top-level dir, `.claude/`, `CLAUDE.md`, `.gitignore`, `publish-public.ps1` itself. To make something public, add it to the allowlist; omission keeps it private (fails safe). The public repo intentionally shows no trace of AI-assisted development.

6. **Never push to `public` directly.** `git push public ...`, `git push --all`, or any hand-push to the public remote ships the comprehensive tree — the private materials are tracked (not gitignored), so they WILL be in a direct push, leaking them and the AI-development trail. The public remote is updated ONLY by `publish-public.ps1`. If a public update is wanted, run the script (allowlist projection); never push public by hand. The script builds the snapshot in a throwaway worktree, so it does not touch the shared live tree. `guard-push-target.ps1` (`PreToolUse`) mechanically BLOCKS a hand `git push public`/`--all`/`--mirror` and WARNS on `git remote add/set-url` / `git config user.email|name` drift — but the hook is a backstop, not a license; this rule stands on its own.
