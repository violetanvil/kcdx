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
| [5.2 [ENG/TEST] The kcdx_verify_all console command + the save-load precondition](step-2-eng-console-command-trigger.md) | DONE | 187ad3d |
| [5.3 [TEST] The sweep over the curated set + per-row streaming + v3 report emission + matrix row + 3-tree deploy](step-3-test-sweep-stream-report.md) | DONE | 9b0ee59 |

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

**Gate MET — accepted at the 2026-06-09 live launch** (`kcdx-dev_2026-06-09_22-33-38.log`, deployed
DLL hash `3F92AFE0…7D74A`). The menu run (`world_loaded=no`) correctly skipped the 121 un-observed
live-exercise rows (the precondition gate firing); after a save was loaded the full ladder ran
(`world_loaded=yes`, 0 skipped). The full-run signal: `ACCEPT-RESULT: PASS kcdx_verify_all — v3
report finalized (157/157 rows, 141 passing)` + `ACCEPT-SUITE: 1/1 passing`; `RESULT
name=cap-95-verify-all-command verdict=PASS` (all five sub-checks fired live: command registered;
precondition gated; per-row incremental JSONL flush, never bulk; v3 validates-shaped; string
escaping). The finalized report `kcdx-verify_2026-06-09_22-33-38.json` VALIDATES: `schema_version 3`,
`game_version 1.5.1164953`, `complete: true`, `rows_expected: 157`, 157 rows, `summary
passing=141/total=157` — every row an active attempt + a structured response (4→3 verified_working
rank-1 incl. the cvar getter kcdx_id 156 via the CALLED-by-kcdx record, 138 passed_not_verified, 1
failed, 15 cannot_check, 0 error). The 1 `failed` row (kcdx_id 12 `string_exec_autoexec_cfg`,
`resolved_va_not_in_live_text`) is legitimate sweep output — a real DB-vs-binary divergence the
engine exists to surface (a Phase-6 worklist item), not a producer defect; its matched id is
correctly `null` (the verdict-keyed if/then/else). cap-84 + cap-85 PASS (no regression). The 2 suite
FAILs (CAP-20-addrname / CAP-28-typo-fails-fast) are pre-existing other-lane rows, not Phase-5.
