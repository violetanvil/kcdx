## Repo additions — commit

- **Fast build command** — `pwsh ./build.ps1` (§3 runs it when the staged chunk touches code).

- **Code-type triggers** — `.cpp`, `.h`, `CMakeLists.txt`, `build.ps1`, `vendor/**`, `lua/**`. Changes outside these (docs, config) skip the build.

- **Path categories (§1/§2 cohesion check)** —
  - **production** — `src/**`, `include/**`, `vendor/**`, `test-plugins/**`, `kcdx-engine/**`.
  - **docs** — `docs/**`.
  - (governance is `.claude/**` + `CLAUDE.md` + `settings.json` generically.)

- **Multiline-message mechanism** — PowerShell-first repo: use a single-quoted here-string (`@'...'@`, closing `'@` at column 0); bash heredoc parsing is unreliable here.

- **Acceptance check (§4 honesty tag)** — game-launch confirms the test-suite matrix; build-green does NOT cover it. Mark a matrix-row claim `[unverified — pending launch]` until the user's launch + the agent's `kcdx-dev.log` read confirm it.

- **Branch / attribution overrides** — commit to the CURRENT branch; do NOT auto-create a branch (parallel chats share one tree — overrides the harness branch-first-on-default default; `.claude/rules/concurrency-git.md`). Stage by specific path, never `git add -A`. No Co-Authored-By / tool-attribution trailer.
