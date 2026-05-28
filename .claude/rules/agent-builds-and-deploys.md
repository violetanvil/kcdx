---
paths:
  - "**/*"
---

# Agent builds and deploys; the user only launches

The agent runs `pwsh ./build.ps1`, copies artifacts to the live install, verifies
deploy hashes, enables dev mode, and reads `kcdx-dev.log` after the run. The
user runs ONE thing in the loop: the game launch (and tells you it ran).

This is the universal flow for every skill (`/execute`, `/debug`, `/feature`,
`/code-review`, `/verification-checkpoint`, and any future skill that touches a
build or a deploy). A skill or agent that asks the user to build, copy a file
to the live install, or read a log line is a FLOW defect — fix the surface, do
not perform the ask.

## Rules

1. **The agent runs `pwsh ./build.ps1`.** Never narrate the build as a user
   step. Never ask "should I build?" — that's not a decision. Run the command
   via your PowerShell/Bash tool surface; read the actual output; confirm
   exit 0 and the three artifacts produced (`build/Release/kcdx.exe` +
   `kcdx.dll` + `kcdx-watchdog.exe`). A subagent's "it builds" claim is not
   evidence — the manager re-runs it (`_shared/orchestrator-loop.md` §C).

2. **The agent runs the deploy.** Copy every artifact this change rebuilt to
   its live-install destination per `loader-architecture.md` deploy mapping
   AND CLAUDE.md "Game install paths". Hash-verify each copy against its
   `build/Release/...` source (PowerShell `Get-FileHash`). Mismatch on any
   artifact = deploy failed; surface the stale list, do not proceed.

3. **The agent enables dev mode.** Ensure
   `<game-bin>/kcdx-engine/engine.toml` has `dev_mode = true` (create it if
   absent, per `docs/dev-mode.md`) before handing the launch over. Without
   dev mode the test suite self-skips.

4. **The agent reads `kcdx-dev.log` after the run.** On the user's run signal
   ("test run", "ran", "back from launch"), read the newest
   `<game-bin>/kcdx-engine/logs/kcdx-dev_<ts>.log` directly, find the
   `suite: X/Y passing` line and any `FAIL <row>:` lines, and report the
   verdict. Never ask the user to paste log lines.

5. **The user runs ONE thing: the game launch.** Renderable as a procedure
   the user follows (Launch → reach menu → tell me it ran → Quit, plus any
   declared `console`/`in-game` gestures). Anything before "Launch" or after
   "tell me it ran" is the agent's, not the user's.

6. **Probes follow the same rule.** A `/debug` probe is engine-side throwaway
   code (`// === DIAGNOSTIC (PROBE X)`) or a `test-plugins/` probe plugin.
   The agent writes it, builds it, deploys it, hands the user the launch,
   reads the resulting log. Asking "should I write the probe?" or "should
   I build it?" is a FLOW defect (see `results-driven.md` §"Live-game
   unknowns").

## Triggers

- **About to write "Run `pwsh ./build.ps1`" in user-facing output → STOP.**
  Run it yourself; report what it produced.
- **About to write "Copy kcdx.dll to ..." in user-facing output → STOP.**
  Do the copy; hash-verify; report.
- **About to ask "should I build / deploy / write the probe?" as a decision
  → STOP.** That's not a decision (none of the options are valid). The rule
  is here.
- **About to ask the user to read a log line → STOP.** Read the log
  yourself.

## What this rule does NOT change

- **The user owns the launch.** kcdx's strongest tests are live launches; the
  agent never launches the game.
- **The user owns design decisions.** This rule narrows what the agent
  surfaces as a *flow* question — never what it surfaces as a *design*
  question (`cornerstones.md` decides those; `architectural-review.md`
  §"Verification order" defines the bright line).
- **Build-green is necessary, not sufficient.** A clean build does not
  prove the feature works in-game (`anti-patterns.md` §invariants-vs-gates);
  the matrix is confirmed by the user's launch, the verdict read from the
  log by the agent.

Related: `loader-architecture.md` (deploy mapping),
`_shared/orchestrator-loop.md` §C (build + deploy + hash-verify procedure),
`verification-checkpoint/SKILL.md` (deploy-freshness gate),
`results-driven.md` §"Live-game unknowns" (probe-specific instance of this
rule), `anti-patterns.md` AP8 (gates → evidence).
