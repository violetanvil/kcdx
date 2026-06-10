# Phase 5 — the console-triggered batch sweep + v3 report (the producer)

**Intent:** build the producer side of the cross-repo report contract — a dev-mode-gated kcdx
test-suite plugin that, on a **console command (`kcdx_verify_all`) the maintainer runs AFTER
loading a save** (D33-revised — the live-exercise tier needs a loaded world), drives the Phase-4
engine rank-ladder over the curated USER set, **streams a per-row line to the console** as each
attempt completes (so a 157-row sweep never reads as a hang), attributes each result to the matched
`address_version` row (D34), and emits the **v3 JSON verification report** (D36's 7-state enum +
`method_rank` + `invoke_attempted` + `invoke_skip_reason`) alongside `kcdx-dev.log`. The FE consumer
is Phase 6. In kcdx `src/` + `test-plugins/` (+ the v3 schema in `data/refdata-extractor/`) — gated
by `pwsh ./build.ps1` + a live `kcdx_verify_all` launch + the test-suite matrix
(`.claude/rules/agent-builds-and-deploys.md`).

**Design authority:** `data/maintainer-tool/design.md` D36 + D33(rev) + D34 + D28 + D29; the report
schema `data/maintainer-tool/report-schema/verification-report.schema.json` (v2 → v3). Build to
those, not this README's summary.

## Step-grain ledger

| Step | Status | Commit |
|---|---|---|
| [5.1 [CORE] Report schema v3 — the 7-state enum + method_rank/invoke fields + redefined summary](step-1-core-report-schema-v3.md) | DONE | (landed) |
| [5.2 [ENG/TEST] The kcdx_verify_all console command + the save-load precondition](step-2-eng-console-command-trigger.md) | NOT STARTED | — |
| [5.3 [TEST] The sweep over the curated set + per-row streaming + v3 report emission + matrix row + 3-tree deploy](step-3-test-sweep-stream-report.md) | NOT STARTED | — |

## Phase verification gate

Phase 5 is done when: the schema v3 round-trips (5.1, data-core pytest green) AND the plugin builds
clean (`pwsh ./build.ps1`), is deployed to all 3 plugin trees + hash-verified, and at a live launch
(dev_mode on) the maintainer loads a save + runs `kcdx_verify_all` → the sweep streams a per-row
line per curated row, drives the Phase-4 ladder per row (every row an active attempt + a structured
response), and writes a v3 JSON report (the 7-state verdict + `method_rank` + `invoke_attempted` +
`invoke_skip_reason` + the matched `address_version_id`) that VALIDATES against the v3 schema — the
agent reads the `ACCEPT-SUITE` PASS + confirms the report validates from the live install
(`.claude/rules/agent-builds-and-deploys.md`, `.claude/rules/acceptance-signal.md`); the test-suite
matrix row is recorded (`test-suite.md`). The user gesture is: launch → load a save → open console
(`~`) → type `kcdx_verify_all` → tell me it ran. Build-green is necessary, not sufficient — the
report's correctness is confirmed at the launch (`.claude/rules/skeptical-expert.md`).
