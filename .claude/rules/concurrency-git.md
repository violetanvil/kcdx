---
paths:
  - "**/*"
---

# Git concurrency — do not auto-branch; this repo runs parallel chats

Multiple Claude Code chats run against this ONE repository at once — one shared working tree, `.git`, HEAD, index. The chats are NOT isolated. `git checkout -b` / `git switch -c` is global state: it switches the OTHER chat's branch too, and its next commit lands on yours, tangling two histories.

## Rules

1. **Never auto-create a branch.** Do not run `git checkout -b`, `git switch -c`, or `git branch <new>` on your own initiative — **not even when on `main`.** Committing to `main` is correct here: this repo is prerelease, single-author, no PR flow, and `main` is where all work lives. This **overrides** the harness default "if on the default branch, branch first." Branch and worktree decisions belong to the user; if you think a branch is warranted, ask — don't create one.

2. **Commit to the current branch, stage by specific path.** Always `git add <specific files>`, never `git add -A` / `.` / `-u`. The index is shared between chats — a broad add can sweep the OTHER chat's in-flight files into your commit. Staging only your own named files is what keeps two parallel chats from cross-contaminating a commit.

3. **Never switch branches or stash on your own initiative.** `git checkout <other-branch>`, `git switch`, `git stash` all mutate the shared working tree — they can clobber or hide the other chat's uncommitted edits (`git stash push -u` silently captures files you didn't author). Don't. The one sanctioned exception is the `/debug` historical-commit probe, which uses a **detached worktree** (`git worktree add -d`), never a checkout/stash in the live tree.

4. **True isolation = a worktree, not a branch.** If genuinely-parallel isolated work is needed, the mechanism is a separate git worktree (its own working dir + HEAD + index, shared history) — e.g. dispatch an agent with `isolation: "worktree"`, or the user opens one. A branch alone does NOT isolate, because the working tree is still shared. Do not reach for this unilaterally; surface it to the user.

5. **Check before you assume.** Before any commit, `git branch --show-current` + `git status` — another chat may have changed the branch or left the tree dirty. Read state, don't assume.

## Remotes — this IS the private repo; public is an allowlisted snapshot

This repo has TWO remotes. The local tree is the comprehensive **private** repo.

- `private` → `violetanvil/kcdx-private` — holds everything not in `.gitignore` (incl. `.claude/`, `CLAUDE.md`, `_research/`, `third-party-ghidra/` scripts, `test-fixtures/`, all of `docs/`). `main` tracks `private/main`, so **a bare `git push` goes to private.** This is the everyday push. `.gitignore` here is the ordinary private-repo ignore list (build output, machine state, heavy/reproducible binaries) — it does NOT define the public boundary.
- `public` → `violetanvil/kcdx` — a fresh single-commit snapshot on an unrelated history, force-pushed each publish. Reached ONLY via `pwsh ./publish-public.ps1` (`-DryRun` previews).

The public boundary is an **ALLOWLIST inside the script**, not a denylist and not `.gitignore`. The script publishes only its listed public dirs (`src`, `include`, `vendor`, `data`, `examples`, `kcdx-engine`, `test-plugins`, `tools`, `docs`) + root files (`README.md`, `LICENSE`, `CMakeLists.txt`, `build.ps1`, `package-release.ps1`). **Everything else defaults to private** — a new top-level dir, `.claude/`, `CLAUDE.md`, `.gitignore`, `publish-public.ps1` itself. To make something public, add it to the allowlist; omission keeps it private (fails safe). The public repo intentionally shows no trace of AI-assisted development.

6. **Never push to `public` directly.** `git push public ...`, `git push --all`, or any hand-push to the public remote ships the comprehensive tree — the private materials are tracked (not gitignored), so they WILL be in a direct push, leaking them and the AI-development trail. The public remote is updated ONLY by `publish-public.ps1`. If a public update is wanted, run the script (allowlist projection); never push public by hand. The script builds the snapshot in a throwaway worktree, so it does not touch the shared live tree.
