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
