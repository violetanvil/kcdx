# 3.1 [ENG] Per-kind dispatch + payload model + the Ambiguous status added to survival::Status

## What

Restructure the C++ engine survival checker (`src/survival.cpp` / `src/survival_pass.cpp`) from
function-hash-only into a per-kind dispatch with a kind-discriminated payload model — the
`SurvivalCheck(kind, payload, derives_from, dll)` shape (`fingerprint-per-kind.md` §"Why a single
uniform hash cannot be the fingerprint") — and add the **`Ambiguous` status** to
`survival::Status` (the callsite multiple-hit verdict, D31a). The existing function-body check is
kept, now reached through the dispatch. This is the foundation the 5 non-function checks (step 2)
+ the live check (step 3) plug into.

## Scope

One commit in kcdx `src/`: the per-kind dispatch entry point + the kind-discriminated payload
type + the `Ambiguous` status added to the existing `survival::Status` enum, with the existing
function-hash check moved under the dispatch (function/function_no_sig/function_variadic →
body-hash; every other kind → a not-yet-implemented stub returning a defined placeholder until
step 2). No non-function check logic yet (step 2); no live check (step 3).

## Test bar

A kcdx test-suite plugin row (a `cap-NN` / `comp-NN` per `test-suite.md`) asserting the dispatch
routes function kinds to the existing body-hash check (the function-kind verdict is unchanged
from today) + the `Ambiguous` status value exists + is reportable. The agent builds
(`pwsh ./build.ps1`), deploys to all relevant trees, hash-verifies, enables dev mode; the user
launches; the agent reads the PASS from `kcdx-dev.log` (`.claude/rules/agent-builds-and-deploys.md`).
A matrix row is recorded (`test-suite.md`). Runnable at this step (the function check already
works) — `.claude/rules/test-discipline.md`, `.claude/rules/incremental-delivery.md`.

## Dependencies

- **0.3** — the pe_helpers scoping finding (scopes how the payload model reads section data;
  determines whether step 2 needs added infra).

## Reference

[`../plan-spec.md`](../plan-spec.md) — Group B (per-kind dispatch, payload model, Ambiguous
status, function-hash-exists); the existing checker `src/survival.cpp` + `src/survival_pass.cpp`.

## Design authority

`data/maintainer-tool/fingerprint-per-kind.md` §"Why a single uniform hash cannot be the
fingerprint" (the `SurvivalCheck(kind_form, payload, derives_from, dll)` one-entry-point /
kind-discriminated-payload model) + §callsite (the multiple-hit → Ambiguous status, D31a). The
schema's survival columns the payload reads are `data/maintainer-tool/design.md` §11.1/§11.2 (the
D22 fold — `aob`/`anchor_string`/`rule`/`slot_count`/`expect_unique`/`derives_from` + the body
`content_hash`/`length`). Build to these, not to this doc's summary.

## UX

Not a UI step (engine code). No maintainer-tool UI; the only user gesture is the game launch
(`.claude/rules/agent-builds-and-deploys.md`).

## Disassembler-test / author-burden

None — internal engine restructure; adds no author-facing input. The dispatch is the engine
carrying the per-kind verify logic so no author supplies per-kind check code.
