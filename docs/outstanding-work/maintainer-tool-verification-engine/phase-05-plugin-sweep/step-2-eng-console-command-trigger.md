# 5.2 [ENG/TEST] The kcdx_verify_all console command + the save-load precondition

## What

Register the **`kcdx_verify_all` console command** (dev-mode-gated) that triggers the batch
verification sweep, and implement its **save-load precondition** — the command runs the full ladder
(incl. the rank-1 live-exercise tier) only when a save/world is loaded; run from the main menu, the
live-tier rows resolve `skipped` (a precondition-not-met response, not a fabricated pass — D36).
This replaces D33's original boot-automatic trigger: the live-exercise tier needs a loaded world, so
the maintainer loads a save, opens the console, and runs the command. The command is the producer's
entry point; the sweep body + report emission is 5.3.

## Scope

One commit in kcdx `src/` (the console-command registration — reuse the `kcdx.command` /
`IConsole_AddCommand` path in `src/console.cpp`) + a `test-plugins/` row asserting it:
- register `kcdx_verify_all` via the existing console-command surface, dev-mode-gated (self-skips
  outside `dev_mode`, like every suite plugin).
- the command handler detects whether a save/world is loaded (the loaded-world signal the engine
  already exposes) and sets the per-row precondition: world loaded → run the full ladder; menu →
  the live-exercise rows resolve `skipped` with the precondition reason.
- the handler invokes the Phase-4 `survival_verify` ladder (the sweep body + streaming + report is
  5.3 — this step lands the command + the gating; 5.3 fills the sweep).

## Test bar

A `test-plugins/cap-NN` row (the matrix row 5.3 finalizes; this step asserts registration + gating):
assert `kcdx_verify_all` is registered (present in the console command table) AND the save-load
precondition gates correctly — invoked with no world loaded, the live-exercise rows report `skipped`
(not `verified_working`, not `failed`). FALSIFIABLE: the command absent from the table fails the row;
a live-exercise row reading `verified_working` when no save is loaded fails the row (the precondition
didn't gate). Runnable AT this step via the console gesture (the agent supplies the exact command
string; the user types it). Per `.claude/rules/test-discipline.md`, `.claude/rules/test-suite.md`.

## Dependencies

- **Phase 4** (4.1–4.4) — the rank-ladder the command drives.
- **`src/console.cpp`** — the `kcdx.command` / `IConsole_AddCommand` registration path this reuses
  (no new game-function target; the command surface exists).

## Reference

[`../plan-spec.md`](../plan-spec.md) — the console-after-save-load trigger (D33-revised).

## Design authority

`data/maintainer-tool/design.md` **D33** (revised) — "a console command (`kcdx_verify_all`) the
maintainer runs AFTER loading a save … NOT a boot sweep … the live-exercise tier needs a loaded
world"; **D36** — the `skipped` verdict ("a precondition wasn't met THIS run — needs a loaded save,
run from menu … the only 'didn't run,' and it names exactly why"). Build to D33(rev)'s trigger +
D36's `skipped` semantics, not this doc's summary.

## UX

The user gesture (per `.claude/rules/agent-builds-and-deploys.md`, `.claude/rules/acceptance-signal.md`):
launch → load a save → open console (`~`) → type `kcdx_verify_all`. The console-streaming + report
read are the agent's (5.3). This step's user-facing surface is the console command itself — its name
+ that it is dev-mode-only; an in-menu run reports a clear `skipped` reason, never a silent no-op.

## Disassembler-test / author-burden

None — the command reuses the existing console-command registration surface; it adds no
author-facing hex/ABI input and no new game-function target (no AP18 addition).
