---
paths:
  - "src/**"
  - "include/**"
  - "test-plugins/**"
---

# Test suite — every feature ships a permanent regression plugin

**Any time new functionality is implemented, a corresponding test-suite plugin is created to test it.** That plugin lives in perpetuity under `test-plugins/` as the permanent regression test. A feature is not "done" until its test plugin exists, reports a result, and the matrix row is recorded.

## What counts as "new functionality"

- A new kcdx primitive or channel (a new `[[...]]` TOML block, a new `kcdx.*` Lua surface, a new C++ interface).
- A new capability on an existing primitive (a new mode/knob, a new arg form, a new resolver tier).
- A new engine behavior a plugin author or user can observe (load-order rule, conflict-detection case, save/cosave path, console command, lifecycle event).

Bug fixes that change observable behavior get a regression test too — ideally a sub-test added to the *existing* plugin for that feature, reproducing the bug so it can never silently return.

## What the test plugin must do

Follow the existing convention in [`test-plugins/README.md`](../../test-plugins/README.md):

1. **Live under `test-plugins/<row-id>-<short-name>/`** — lowercase, dashes. `row-id` is the matrix ID (`cap-NN` for a primitive, `comp-NN` for a conflict/interaction case); pick the next free number.
2. **Be suite-gated**: `[kcdx] test_suite_only = true` so it's silent in production and only runs under dev mode.
3. **Self-check and report**: call `ReportTestResult(...)` (C++) or `kcdx.test.report(...)` (Lua). The aggregator rolls it into the `suite: X/Y passing` line. See [`docs/dev-mode.md`](../../docs/dev-mode.md).
4. **Prefer an auto-pass check** that fires on boot (no player input). If the feature can only be confirmed by an in-game gesture, the plugin still reports PASS on its passive checks and the row is flagged `[manual]`.
5. **Add a matrix row** to `test-plugins/README.md` documenting: What / Engine status / Test plugin path / Auto-pass check / Last result / Notes.

## The test procedure — only the user's keyboard actions

The procedure rendered to the user contains ONLY the physical steps the user performs at the keyboard. Deploying the build, enabling dev mode, and reading the logs are the agent's jobs — they NEVER appear as user steps. The implementing agent does NOT write the procedure and does NOT claim it ran — it declares which mode applies plus any exact gestures. The orchestrator renders the numbered list (`_shared/orchestrator-loop.md` §F).

**Agent-owned, before and after the user's run (never user steps):**

- **Before — deploy + enable dev mode.** Deploy the diff-scoped artifacts to the live install per `loader-architecture.md` (engine change → `kcdx-engine/kcdx.dll`; plugin change → `kcdx-plugins/<name>/`; launcher → `kcdx.exe` at bin root), AND ensure `<kcdx-engine>/engine.toml` has `dev_mode = true` (create it if absent) so the suite runs. The orchestrator does this at the commit gate (`orchestrator-loop.md` §C step 6).
- **After — read the logs.** When the user signals the run is done, the agent reads the newest `<kcdx-engine>/logs/kcdx-dev_<ts>.log` itself, finds the `suite: X/Y passing` line plus any `FAIL` lines, and reports the verdict. The user never reads the log.

**Canonical "launch-to-menu" — the default, covers most changes:**

1. Launch KCD2 from Steam.
2. Reach the main menu.
3. Quit.
4. Tell me it ran (e.g. "test run" / "review logs") — I read the matrix from `kcdx-dev.log` and report the result.

**What the test proves — required, accompanies the procedure.** Each rendered procedure states, in plain English, what the run proves and what the agent will look for in the log: the falsifiable claim (one sentence) + the exact log signal that confirms or denies it (the matrix row(s) and any `FAIL` line text). The user reads this to know why they are running the game; the agent reads it to know what to grep for afterward.

**Test mode — the implementing agent declares exactly one:**

- `boot-only` — launch-to-menu confirms it; no extra user steps.
- `console` — extra user steps: open console (`~`), type the exact command(s). The agent confirms the exact log line / observable afterward from the log.
- `in-game` — extra user steps: load the named save, perform the exact gesture. The agent confirms the observable afterward from the log.

For `console` / `in-game`, the agent supplies the exact command string, save name, gesture, and the falsifiable observable. The orchestrator inserts the user-performed gestures as numbered steps between "Reach the main menu" and "Quit", each tagged with the matrix row it confirms — confirmation of the observable stays with the agent's post-run log read.

## Permanence

- Test plugins are **never deleted** when a feature stabilizes — they are the standing regression net.
- A removed feature's test plugin is removed *with* the feature in the same change, and the matrix row struck — not before.
- Before landing anything touching `src/` or `include/`, re-run the suite (launch with dev mode on, read the summary) and record the state at the commit SHA in the matrix.

## How to apply

About to call a feature done → does a `test-plugins/` plugin exercise it? If not, build one first. If a plugin already covers the feature area, add a sub-test there rather than spawning a redundant plugin.

**Be proactive — the test is part of the work, not a thing you wait to be asked for.** When you add a capability or change observable behavior, you create its regression coverage in the SAME deliverable, unprompted. "Is there a test?" should never be a question the user has to raise. If it is raised, the only valid answers are: (a) point to the plugin + matrix row that already covers it; (b) the change is a pure internal refactor with no observable behavior change → say so explicitly; or (c) the behavior under test is not built yet (e.g. a gate this cycle introduces but a later cycle implements) → surface a deliberately-failing matrix row now that flips to PASS when the behavior lands, pinning the contract. "No, and that's fine" without one of those three is never a stopping point.

Documentation moves with the feature on the same footing — its API-doc entry + glossary term + parity row land in the same change. See `docs-discipline.md`.

Related: `pak-mods.md` (pak-Lua test fixtures), `toml-schema.md` (manifest shape for test plugins), `docs-discipline.md` (the parallel documentation mandate).
