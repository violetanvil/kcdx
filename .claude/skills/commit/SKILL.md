---
name: commit
description: Land verified changes. Called by `/execute` after it passes build + step-review for a step; called by `/governance-architect` after a verified chunk; called directly by the user only for trivial single-file edits already made by hand (markdown typos, comment fixes, matrix-row status updates). Not the right tool for non-trivial work — use `/execute` instead. Runs `pwsh ./build.ps1` if C++/build files changed, drafts a commit message in the repo's existing style, applies a cohesion heuristic, stages specific files by name, commits. Honors CLAUDE.md hard rules and the no-Claude-attribution workspace rule: build.ps1 is the build path, specific-file staging only, no push, no amend, no --no-verify.
---

# Commit

The landing step underneath `/execute`: stages + commits with a build-gate and cohesion-heuristic guard. Plumbing called by orchestrators, or invoked directly for trivial edits.

## Steps

1. **Survey the change.** Run `git status` and `git diff`. Categorize:
   - **C++ / build touched?** (`.cpp`, `.h`, `CMakeLists.txt`, `build.ps1`, `vendor/lua/**`) → step 3 runs `pwsh ./build.ps1`.
   - **Docs / config / TOML only?** (e.g. `.md`, `.claude/**`, test-plugin `kcdx.toml`) → skip the build.
   - **Mixed?** Run the build.

2. **Cohesion check — does the working tree contain ONE chunk or MULTIPLE?** Multiple = the change spans both **governance-shaped paths** (`.claude/**`, `CLAUDE.md`) AND **production-shaped paths** (`src/**`, `include/**`, `CMakeLists.txt`, `build.ps1`, `vendor/**`, `test-plugins/**`, `kcdx-engine/**`). If multiple, surface each chunk separately and ask: *"Working tree spans <list>. Which chunk should this commit cover?"* Wait for the user to pick. If one chunk (everything fits one category, OR everything is docs under `docs/**`, OR everything is governance, OR everything is production possibly with docs), proceed without asking.

   **Note:** the working tree may carry a large amount of pre-existing unrelated change (the user's WIP). The cohesion check is about what YOU are committing, and you stage by specific path — never assume the whole tree is yours.

3. **Build if C++/build changed.** `pwsh ./build.ps1`. If it fails or emits a warning, stop — surface the output, do not proceed. Confirm `build/Release/kcdx.exe` + `kcdx.dll` + `kcdx-watchdog.exe` were produced.

4. **Draft the commit message.** Run `git log -10 --oneline` to match the repo's existing style (e.g. `Phase 2b sub-3: kcdx.hook — registration + validation surface.`). Compose a concise message focused on WHAT + WHY. **No `Co-Authored-By` / Claude-attribution trailer** — the workspace suppresses it; do not add it manually.

   **Unverified-fix honesty.** If the change exists to flip a `test-plugins/` matrix row to PASS (or otherwise asserts a runtime behavior) and the confirming game launch has NOT run this cycle, the subject carries `[unverified — pending re-run]` and the body says the row is still open. Build-green never confirms a matrix row (CLAUDE.md "Build-green is necessary, not sufficient"). Do NOT write a subject that asserts the fix landed ("now PASS", "fixed X") while its row is unconfirmed — that puts a false claim in the permanent log. Once "review logs" shows the row PASS, the matrix-row status update is a separate trivial commit (no tag).

5. **Stage + commit immediately (cohesive case).** No approval round-trip:
   - State concisely what you're committing (files + message), in one block.
   - Stage the specific files by name (never `git add -A`).
   - Create the commit. For a multiline message, use a PowerShell single-quoted here-string via the PowerShell tool (the Bash tool's heredoc parsing is unreliable here — the `@'...'@` closing token must be at column 0). Single-line messages can use `git commit -m "..."`.
   - Run `git status` to confirm.

   **Multi-chunk case (after step 2 surfaced + user picked):** same as cohesive but only stage the picked chunk; leave the rest unstaged.

6. **Stop.** Do not push, do not amend, do not chain into anything else.

## Hard rules (do not relax)

- `pwsh ./build.ps1` is the build path when verifying a commit. Never invoke the compiler / CMake directly to gate a commit.
- **Stage specific files by name** — never `git add -A`, `git add .`, or `git add -u`. The working tree often carries unrelated WIP; specific-file staging prevents committing it, plus `.env` / credentials / large binaries.
- **No Claude attribution.** No `Co-Authored-By: Claude` or "Generated with Claude Code" trailer — workspace rule.
- **Do NOT auto-create branches. Commit to the current branch** — not even on `main`. Branch/worktree decisions are the user's; this OVERRIDES the harness "branch first on the default branch" default. (`concurrency-git.md`.)
- **Never `git push`** from this skill. Pushes are explicit user requests.
- **Never amend** an existing commit. Always create a new commit.
- **Never skip hooks** (`--no-verify`, etc.). If a hook fails, fix the underlying issue.
- **Don't commit files that look sensitive** (`.env`, keys). Warn the user if specifically asked to commit one.
- **If the build fails or emits warnings**, do not commit. Surface the output and stop.
- **Never assert an unverified runtime fix as landed.** A fix targeting a matrix row whose confirming launch hasn't run is committed with `[unverified — pending re-run]` in the subject, never as "now PASS"/"fixed" (step 4). Build-green ≠ row-confirmed.

## What this skill does NOT do

- Does not present a full verification-checkpoint checklist. That's `/verification-checkpoint`, run before the user's game-launch acceptance.
- Does not launch the game — in-game verification is the user's, at the checkpoint.
- Does not push, amend, or pass `--no-verify`.

## When to invoke directly

Direct user invocation is for trivial single-file edits already made by hand: markdown typos, comment fixes, a `test-plugins/README.md` matrix-row status update after a game-launch confirmed a result. Everything non-trivial routes through `/execute` (which calls this skill internally after step-review clears). The build-gate + cohesion-heuristic + message-style logic is the same regardless of caller.
