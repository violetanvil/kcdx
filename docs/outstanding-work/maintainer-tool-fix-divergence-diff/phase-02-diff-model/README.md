# Phase 2 — The `fixDivergence` per-field attribution + diff-model worker

Build the PURE per-field divergence worker — the logic that turns a failing row + the report's DLL
(parsed PE + buffer) + the kind into a per-kind-relevant-field `{field, recorded, actual, diverged}`
list, using the attribution Phase 1 settled. Headless-testable (`.claude/rules/headless-testable.md`)
— no React, no UI; a pure function exercised by vitest over the Phase-1 fixture. This is E3 (the
attribution layer) + E4's logic half (deriving 'actual' from the DLL).

## Step-grain ledger

| Step | Status | Commit |
|---|---|---|
| [2.1 — The `fixDivergence` pure worker + its per-kind tests](step-1-fix-divergence-worker.md) | NOT STARTED | — |

## Phase gate

`npm run build` exit 0 + `npx vitest run` green in the frontend repo, the new `fixDivergence` worker
unit-tested per kind (function / callsite / string_anchor / vtable_base / a derivation kind /
vtable_index) across diverged / not-diverged / cannot-check outcomes against the Phase-1 fixture. No
UI surface this phase (the render is Phase 3); the worker is the pure seam Phase 3 renders.
