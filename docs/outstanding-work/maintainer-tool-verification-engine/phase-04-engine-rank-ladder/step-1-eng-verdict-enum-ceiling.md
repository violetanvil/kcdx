# 4.1 [ENG] The 7-state verdict enum + the ceiling rule in survival_verify

## What

Extend the Phase-3 `survival_verify` result model from its 4-verdict static set
(`resolves_works` / `wrong_target` / `dead` / `cannot_check`) to D36's **7-state verdict enum**
(`verified_working` / `passed_not_verified` / `failed` / `not_applicable` / `cannot_check` /
`skipped` / `error`) carrying a `method_rank` (1–5) per row, and implement the **ceiling rule**: a
row's verdict is the CEILING of the strongest method that ran, and a `failed` outcome at any rank
overrides the ceiling downward to `failed`. This is the verdict-mapping spine the rank-1 (4.2),
rank-2 (4.3), and per-kind-matrix (4.4) steps plug their methods into; build it first so the later
steps attribute a `method_rank` against a defined ceiling.

## Scope

One commit in kcdx `src/survival_verify.{h,cpp}` (+ the cap-84 self-test in
`src/survival_dispatch_selftest.cpp`): the `RowVerdict` enum extended to the 7 D36 states, a
`method_rank` field (1–5) on the per-row result, and the ceiling-rule mapping (`verdict = ceiling
of strongest method that ran`; `failed` overrides downward). The static checks Phase 3 already
runs (on-disk hash = rank 4, reachability = rank 3) keep their behavior but now report through the
**full 7-state mapping** — every one of the 7 verdicts is PRODUCED by this mapping, none is a
schema-only token: a static pass → `passed_not_verified` at its rank; a static mismatch →
`failed`; a deferred kind → `cannot_check`; **the rank-4 version-applicability check finding the
running build's version is NOT covered by the row (the gap case) → `not_applicable`** (distinct
from `cannot_check` — the check RAN and found non-coverage, vs. lacked inputs — D36 forbids
collapsing them); **a fault in the check/dispatch itself (a probe/read that threw, caught) →
`error`** (distinct from `failed` — the TEST blew up, the ROW may be fine — D36 forbids collapsing
them). (`skipped` is produced upstream by the precondition gate, P5 step 2; `verified_working` and
the rank-1/2 producers are 4.2/4.3.) No new method tiers yet (4.2/4.3 add rank-1/rank-2); this step
is the enum + the ceiling arithmetic + the version-gap (`not_applicable`) and fault (`error`)
verdict producers.

## Test bar

cap-84 self-test sub-checks (`src/survival_dispatch_selftest.cpp`, synthetic data, boot, no game
state — the same self-test that already covers Phase-3's dispatch): assert (a) the 7-state enum
round-trips the cache codec byte-identically (the codec already round-trips `Ambiguous`; add the
new states); (b) the ceiling rule — a synthetic row whose strongest run method is rank-4-static-pass
maps to `passed_not_verified` (NOT `verified_working`), and a synthetic rank-4 hash MISMATCH maps to
`failed` (the override-downward); (c) **the version-gap producer — a synthetic row whose resolved
build version is NOT covered by the row maps to `not_applicable`, NOT `cannot_check`** (the check
ran + found non-coverage); (d) **the fault producer — a synthetic check that throws (caught) maps to
`error`, NOT `failed`** (the harness faulted, the row is not condemned). FALSIFIABLE: a rank-4
static pass reading `verified_working`, a mismatch reading anything but `failed`, a version-gap row
reading `cannot_check` instead of `not_applicable`, or a thrown check reading `failed` instead of
`error` — each fails the row. Runnable AT this step (synthetic verdicts need no game state). Per
`.claude/rules/test-discipline.md`, `.claude/rules/incremental-delivery.md`.

## Dependencies

- **Phase 3** (3.1–3.4, all DONE) — the `survival_verify` startup pass + the cap-84 self-test +
  the version_check_cache codec this extends.

## Reference

[`../plan-spec.md`](../plan-spec.md) — the D36 active-attempt model + the verdict-vocabulary
supersession.

## Design authority

`data/maintainer-tool/design.md` **D36** — the 7-state verdict enum (the exact token set + each
token's meaning) + the ceiling rule ("a verdict is the CEILING of the strongest method that ran …
ONLY rank-1 can award `verified_working`; ranks 2–5 cap at `passed_not_verified`"; a `failed`
overrides downward). Build to D36's named tokens + the ceiling arithmetic, not this doc's summary.
The report-side encoding of these tokens is the v3 schema (Phase 5 step 5.1); this step is the
in-engine enum.

## UX

Not a maintainer-tool UI step (engine internals). The only user gesture is the boot launch
(`.claude/rules/agent-builds-and-deploys.md`).

## Disassembler-test / author-burden

None — no author-facing input; an internal verdict enum.
