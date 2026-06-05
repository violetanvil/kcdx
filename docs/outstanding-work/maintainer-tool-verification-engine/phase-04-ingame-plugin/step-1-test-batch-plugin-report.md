# 4.1 [TEST] The batch verification plugin + JSON report emission + matrix row + 3-tree deploy

## What

Build the batch verification plugin — a kcdx test-suite plugin (runs in the live game like every
`cap-NN` / `comp-NN` test) whose ONE job is to drive the engine LIVE functional check (Phase 3
step 3) over EVERY DB row and emit a structured **JSON verification report** (to the Phase-1
schema) alongside `kcdx-dev.log`: per row `kcdx_id`, resolved version, verdict (`resolves+works`
/ `dead` / `wrong-target` / `cannot-check`), detail (D28). This is the producer of the cross-repo
report contract the FE s08 worklist (Phase 5) consumes — a standing regression + the batch-sweep
producer.

## Scope

One commit in kcdx `test-plugins/`: the batch plugin that iterates every DB row, runs the engine
live check on each, writes the JSON report (Phase-1 schema, validated) to a known location
alongside `kcdx-dev.log`, and self-reports its suite verdict. Plus the test-suite **matrix row**
(`test-suite.md`) and **deploy to all 3 plugin trees** (`kcdx-engine/builtin/`, `kcdx-plugins/`,
`kcdx-plugins/test-suite/` — per the deploy-all-trees discipline). No FE ingestion (Phase 5); the
producer + report only.

## Test bar

The plugin IS a test-suite plugin — its matrix row is the test bar (`test-suite.md`): at a live
launch it drives the engine checker over every row and emits a schema-valid JSON report; the
plugin self-reports `ACCEPT-SUITE` / `ACCEPT-RESULT` to `kcdx-dev.log`. The agent builds
(`pwsh ./build.ps1`), deploys to all 3 trees, hash-verifies each, enables dev mode; the user
launches; the agent reads the suite PASS + confirms the report validates against the Phase-1
schema (`.claude/rules/agent-builds-and-deploys.md`, `.claude/rules/acceptance-signal.md`). The
matrix row is recorded. Runnable at this step (the engine live check + the schema exist) —
`.claude/rules/test-discipline.md`, `.claude/rules/incremental-delivery.md`.

## Dependencies

- **3.3** — the engine LIVE functional check (the plugin drives it per row).
- **1.2** — the JSON report schema (the report is emitted to it).

## Reference

[`../plan-spec.md`](../plan-spec.md) — Group D (the batch plugin, report emission, write-location,
matrix row + 3-tree deploy); cross-step invariant 4 (the JSON report is the cross-repo seam).

## Design authority

`data/maintainer-tool/design.md` **D28** — "a kcdx test-suite plugin … for every row runs the
LIVE functional check (D25) and writes a structured JSON report alongside `kcdx-dev.log` (per
row: `kcdx_id`, resolved version, verdict `resolves+works` / `dead` / `wrong-target` /
`cannot-check`, detail)". The report's schema is Phase 1 step 1.2 (the frozen contract). Build to
D28's named fields + verdict enum, not to this doc's summary.

## UX

Not a maintainer-tool UI step (an in-game plugin). The only user gesture is the game launch
(`.claude/rules/agent-builds-and-deploys.md`) — Launch → reach menu → tell me it ran → Quit; the
agent reads the suite verdict + the report from the log/install, the user never reads a log line.

## Disassembler-test / author-burden

None — a test-suite plugin; adds no author-facing input. The plugin is the engine doing the
whole-DB verify sweep so the maintainer never hand-checks rows one by one.
