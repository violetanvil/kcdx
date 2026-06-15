# Phase 2 — The `fixDivergence` per-field attribution + diff-model worker

Build the PURE per-field divergence worker — the logic that turns a failing row + the report's DLL
(parsed PE + buffer) + the kind into a per-kind-relevant-field `{field, recorded, actual, diverged}`
list, using the attribution Phase 1 settled. Headless-testable (`.claude/rules/headless-testable.md`)
— no React, no UI; a pure function exercised by vitest over the Phase-1 fixture. This is E3 (the
attribution layer) + E4's logic half (deriving 'actual' from the DLL).

## Step-grain ledger

| Step | Status | Commit |
|---|---|---|
| [2.1 — The `fixDivergence` pure worker + its per-kind tests](step-1-fix-divergence-worker.md) | DONE | FE:3d89897 |

Built `frontend/src/editor/fixDivergence.ts` + `.test.ts` (7/7 green). `computeFixDivergence(row, pe,
buffer, candidateRows) → { fields: {field, recorded, actual, diverged, status}[], status, reason }`.
Honors the Phase-1 findings: reuses `runVerdictCheck` (E1, divergent-DLL-safe); function `signature`
is `status: "cannot-derive"` match-or-not (E2 — never a false `not-diverged`); input built via
`savedSeedRow(row)` (E3). Two settled decisions folded in during the cycle:
- **Narrow divergence-field set** (settled this cycle): the worker diffs ONLY the kind's survival-check
  field(s) — `function → rva + signature`, `callsite/instruction_anchor → survival_aob`, `string_anchor
  → survival_anchor_string`, `vtable_base → survival_slot_count`, `data_slot → survival_rule` (a worker-
  local `DIVERGENCE_FIELDS` map, DELIBERATELY narrower than `KIND_FIELD_RELEVANCE`, per s04 §"Per-field
  recorded-vs-actual"). NOT every editor-shown cell (which would over-attribute one verdict).
- **Phase-3 input — concrete `actual` scalar (DEFERRED to Phase 3 by user decision):** the worker's
  per-field `actual` is `null` (the reused `runVerdictCheck` returns a row-level verdict, not a build-
  derived scalar); `recorded` + `status` carry the divergence. Phase 3's render (3.2) owns realizing
  s04's "the derived actual (what the linked build shows)" — the found AOB bytes/offset, the on-disk
  slot count, the resolved site. This is a Phase-3 render-step input, surfaced + tracked here, not a
  worker gap.

## Phase gate

`npm run build` exit 0 + `npx vitest run` green in the frontend repo, the new `fixDivergence` worker
unit-tested per kind (function / callsite / string_anchor / vtable_base / a derivation kind /
vtable_index) across diverged / not-diverged / cannot-check outcomes against the Phase-1 fixture. No
UI surface this phase (the render is Phase 3); the worker is the pure seam Phase 3 renders.
