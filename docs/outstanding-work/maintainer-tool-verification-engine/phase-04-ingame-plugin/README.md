# Phase 4 — In-game plugin + report

**Intent:** build the batch verification plugin — a kcdx test-suite plugin that drives the
engine startup verification pass (version-applicability + reachability, Phase 3 step 3) over EVERY
DB row in the running game and emits the
**JSON verification report** (to the Phase-1 schema) alongside `kcdx-dev.log` (D28). This is the
producer side of the cross-repo report contract; the FE consumer is Phase 5. In kcdx
`test-plugins/` — gated by `pwsh ./build.ps1` + a live launch + the test-suite matrix
(`.claude/rules/agent-builds-and-deploys.md`).

## Step-grain ledger

| Step | Status | Commit |
|---|---|---|
| [4.1 [TEST] The batch verification plugin + JSON report emission + matrix row + 3-tree deploy](step-1-test-batch-plugin-report.md) | NOT STARTED | — |

## Phase verification gate

Phase 4 is done when: the batch plugin builds clean (`pwsh ./build.ps1`), is deployed to all 3
plugin trees + hash-verified, and at a live launch drives the engine checker over every DB row +
emits a JSON report validating against the Phase-1 schema — the agent reads the PASS + the report
from the live install (`.claude/rules/agent-builds-and-deploys.md`); the test-suite matrix row is
recorded (`test-suite.md`). Not a maintainer-tool UI phase — the only user gesture is the game
launch. Build-green is necessary, not sufficient — the report's correctness is confirmed at the
launch (`.claude/rules/skeptical-expert.md`).
