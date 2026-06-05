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
| [1.1 [CORE] The Python per-kind reference checker (test-of-record)](step-1-core-python-reference-checker.md) | NOT STARTED | — |
| [1.2 The JSON verification report schema (the cross-repo contract)](step-2-json-report-schema.md) | NOT STARTED | — |

## Phase verification gate

Phase 1 is done when: 1.1's Python reference checker reproduces the Phase-0 fixture's known
per-kind verdicts (pytest green) and is the declared test-of-record the JS port pins against;
1.2's report schema is frozen + versioned, with a schema-validation test asserting a sample
report validates and a malformed one is rejected. No UI is touched in Phase 1 — no user-facing
acceptance applies. Both are headless, test-gated contracts (`.claude/rules/headless-testable.md`).
