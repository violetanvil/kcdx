# 2.1 [FE] The `fixDivergence` pure worker + its per-kind tests (E3, E4-logic)

## What

Build the PURE per-field divergence worker — the logic that turns a failing row + the report's DLL
(parsed PE + buffer) + the kind into a **per-kind-relevant-field divergence list**, each entry
`{ field, recorded, actual, diverged }` (the field name, the recorded DB value, the value derived
from the linked build, and whether they diverge). It reuses `runVerdictCheck` (D45 fact 1 — no new
check) and adds the per-field ATTRIBUTION the Phase-1 probe settled (E3), deriving 'actual' from the
report's DLL bytes (E4's logic half). For a kind whose row-level verdict cannot split a per-field
attribution (per the 1.1 finding — likely `function`), the worker performs the per-field derivation
1.1 specified. Headless — a pure function, no React, exercised by vitest (`.claude/rules/headless-testable.md`).

## Scope

One commit in the frontend repo (`data/maintainer-tool/frontend/src/editor/`):
- A new pure module `fixDivergence.ts` (named worker per `.claude/rules/structure-by-responsibility.md`
  — the FieldEditor coordinates, this does the one job) exporting a function:
  `computeFixDivergence(row, pe, buffer, candidateRows) → { fields: {field, recorded, actual, diverged}[], status }`
  where `status` distinguishes diffed / no-divergence-found / cannot-check (the E10 honest outcomes).
  It runs `runVerdictCheck` + the per-kind attribution from 1.1; PURE / no I/O (the bytes are the
  in-page buffer — the no-upload invariant D26); re-implements NO check.
- `fixDivergence.test.ts` — vitest unit tests over the Phase-1 divergent-DLL fixture, per kind
  (function / callsite / string_anchor / vtable_base / a derivation kind / vtable_index): a diverged
  case (the field's recorded≠actual, `diverged: true`), a not-diverged case (recorded==actual against
  a matching build, `diverged: false`), and a cannot-check case (e.g. `vtable_index` deferred, or a
  function row with no recorded `content_hash` → `status: cannot-check` with an honest reason).

Does NOT render anything (3.2 owns the UI); does NOT change the carry channel (3.1); does NOT touch
`verdictCheck.ts`'s check logic (reuses it).

## Test bar

`fixDivergence.test.ts` (vitest), runnable AT this step (the worker + the Phase-1 fixture exist).
Each kind asserts the `{field, recorded, actual, diverged}` list + the `status`:
- A `function` divergent fixture → the diverged field(s) (signature and/or rva per the 1.1
  attribution) carry `diverged: true` with the recorded value and the build-derived actual.
- A matching fixture → every field `diverged: false`, `status: no-divergence-found` (NOT an empty
  list silently — E10 / AP14).
- A `vtable_index` row → `status: cannot-check` with the deferral reason; a function row with no
  recorded `content_hash` → `status: cannot-check` with that reason.

**FALSIFIABLE:** a worker that returns `diverged: false` for a field that genuinely diverged in the
fixture fails; a worker that returns an empty/`diverged: true` for `no-divergence-found` instead of
the honest `status` fails; a worker that throws on a cannot-check kind (instead of the honest
`status`) fails. Gate: `npm run build` exit 0 + `npx vitest run` green.

## Dependencies

- **1.1** — the divergent-DLL + per-field-attribution finding + the captured fixture. Hard
  prerequisite: the worker's attribution logic is whatever 1.1 settled, and its test runs over 1.1's
  fixture. Build to the finding, not a guess.
- The existing `runVerdictCheck` (`frontend/src/editor/verdictCheck.ts`) + `extractSurvivalCheck` +
  `checker` + `peSections` — reused, not re-implemented.

## Reference

[`../plan-spec.md`](../plan-spec.md) — E3, E4 (logic half); the settled facts (fact 1 — reuse the
existing check; fact 2 — the attribution the probe settled); the "reuse, never re-implement"
invariant.

## Design authority

`data/maintainer-tool/design.md` D45 fact (1) (the in-browser check reused) + fact (2) (the
per-field attribution) + `data/maintainer-tool/ui/screens/s04-field-editor.md` §"Per-field
recorded-vs-actual" (the `{recorded, actual, diverged}` per kind-relevant field — the column→datum
map the worker produces) + §"Field relevance by kind" (which fields each kind diffs). The worker's
output shape is what s04's render (3.2) consumes; build the shape to the screen spec.

## Disassembler-test / author-burden

None — a pure maintainer-tool FE worker; no author-facing plugin input, no game-function target.
