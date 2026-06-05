# 3.4 [ENG] The cross-impl agreement test (JS browser checker == C++ engine checker on the same bytes)

## What

Land the **JS↔C++ cross-implementation agreement test** — the pin that makes the engine the
authority and the browser checker its faithful mirror (D27): the JS browser static check and the
C++ engine static check MUST return the SAME verdict on the SAME DLL bytes for every in-scope
kind. This is the conformance gate that proves the two implementations agree (the
`version_resolver.py` test-of-record pattern, now applied to the full per-kind survival check).
Only the static byte-level checks are mirrored (the live-functional half is engine-only).

## Scope

One commit landing the JS↔C++ agreement test: run the C++ engine static check + the JS browser
static check over the SAME Phase-0 fixture bytes and assert identical verdicts per in-scope kind
(both already independently pinned to the Phase-0 expected verdicts via their own agreement tests
— this step adds the direct engine↔browser pin). The harness spans the two repos via the shared
fixture; it asserts the engine verdict == the browser verdict == the fixture's expected verdict.
No new check logic (steps 1–3 + Phase 2 built it); the agreement assertion only.

## Test bar

The agreement test itself is the test bar (a conformance test, `.claude/rules/spec-conformance.md`
+ D27): for each in-scope kind, `engine_static_verdict(bytes) == browser_static_verdict(bytes)`
on the Phase-0 fixture. It runs in the appropriate harness (the engine side via a test-suite
plugin row reading the fixture, or a CI cross-check that loads both verdict sets); a divergence is
a FAIL naming the kind + the two verdicts. Runnable at this step (both checkers + the fixture
exist) — `.claude/rules/test-discipline.md`, `.claude/rules/incremental-delivery.md`.

## Dependencies

- **3.2** — the C++ static non-function checks (the engine side of the pin).
- **2.3 / 2.4** — the JS pure-byte + derivation checks (the browser side of the pin).
- **0.5** — the cross-impl fixture (the shared bytes + expected verdicts).

## Reference

[`../plan-spec.md`](../plan-spec.md) — Group C (JS↔C++ agreement test); cross-step invariant 3
(engine is authority, JS mirrors — pinned by this test).

## Design authority

`data/maintainer-tool/design.md` **D27** — "a cross-implementation agreement test pins the two
to the SAME verdict on the same DLL bytes — the exact pattern D15 already established for
`version_resolver.py` (test-of-record); the live-functional half is engine-only." Build the test
to D27's contract, not to this doc's summary.

## UX

Not a UI step (a conformance test). If the engine side runs via a test-suite plugin, the only
user gesture is the game launch (`.claude/rules/agent-builds-and-deploys.md`).

## Disassembler-test / author-burden

None — a conformance test between two engine implementations; adds no author-facing input.
