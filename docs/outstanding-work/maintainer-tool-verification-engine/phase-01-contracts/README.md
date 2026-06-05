# Phase 1 — Shared contracts (the test-of-record + the cross-repo report schema)

**Intent:** land the two shared contracts the rest of the build conforms to BEFORE any
consumer is built — the **Python per-kind reference checker** (the test-of-record the JS port
is checked against, D27) and the **JSON verification report schema** (the single contract
crossing the two-repo split — produced by the in-game plugin, consumed by FE s08, D28/D31b).
Ordered before Phase 2–5 so every consumer has a settled contract to build to
(`.claude/rules/incremental-delivery.md`).

## Step-grain ledger

| Step | Status | Commit |
|---|---|---|
| [1.1 [CORE] The Python per-kind reference checker (test-of-record)](step-1-core-python-reference-checker.md) | DONE | e8a06cc |
| [1.2 The JSON verification report schema (the cross-repo contract)](step-2-json-report-schema.md) | DONE | 35445b7 |
| [1.3 [CORE] Report schema v2 — add `matched_address_version_id`, bump `schema_version` 1→2](step-3-core-report-schema-v2.md) | NOT STARTED | — |

## Phase verification gate

Phase 1 is done when: 1.1's Python reference checker reproduces the Phase-0 fixture's known
per-kind verdicts (pytest green) and is the declared test-of-record the JS port pins against;
1.2's report schema is frozen + versioned, with a schema-validation test asserting a sample
report validates and a malformed one is rejected; **1.3's schema-v2 bump adds the per-row
`matched_address_version_id` (the D34 attribution field) and pins `schema_version: 2`, with the
validator asserting a v2 report validates and a v1-shaped / missing-field report is rejected**. No
UI is touched in Phase 1 — no user-facing acceptance applies. All are headless, test-gated
contracts (`.claude/rules/headless-testable.md`). (Phase 1 reopened 2026-06-05 for 1.3 — the
batch-verify design D34 added the attribution field after 1.2 froze v1.)
