# 2.3 [FE] The 4 pure-byte kind checks + the JS↔Python cross-impl agreement test

## What

Build the 4 pure-byte per-kind static checks in JS (no decoder needed): function body-hash
(re-hash `[rva, rva+length)` vs `content_hash`), string_anchor `.rdata`-search (+ optional
`expect_unique` xref assert), vtable_base table-shape (N qwords each → `.text`), callsite
AOB-scan (unique → Unchanged + relocate; zero → Changed; multiple → Ambiguous, D31a). Land the
**JS↔Python cross-impl agreement test** for these kinds — the JS checker must return the same
verdict as the Phase-1 Python reference on the same bytes (D27). This is the first slice where
the browser checker actually checks an authored row.

## Scope

One commit in the frontend repo: the 4 pure-byte kind-check functions (dispatched on `kind`,
returning a `Verdict`) over the PE-section foundation, plus the JS↔Python agreement test
harness for these 4 kinds against the Phase-0 fixture. No derivation kinds (step 4); no UI
(steps 5–7).

## Test bar

Vitest unit tests per kind (Unchanged / Changed / Ambiguous-callsite / a `string_anchor`
absent → Changed) over the Phase-0 fixture, PLUS the **JS↔Python agreement test**: each
fixture row's JS verdict == the Python reference checker's verdict (1.1) == the fixture's
declared expected verdict (0.5). Runnable at this step (fixture + Python reference both exist) —
`.claude/rules/test-discipline.md`, `.claude/rules/incremental-delivery.md`,
`.claude/rules/spec-conformance.md` (the cross-impl test is the conformance gate, D27).

## Dependencies

- **2.1** — PE-section foundation + verdict types.
- **1.1** — the Python reference checker (the agreement test's authority).
- **0.5** — the cross-impl fixture + expected verdicts.

## Reference

[`../plan-spec.md`](../plan-spec.md) — Group A (the 4 pure-byte kinds) + Group C (JS↔Python
agreement); cross-step invariant 3 (engine is authority; JS mirrors, pinned by the agreement
test).

## Design authority

`data/maintainer-tool/fingerprint-per-kind.md` §function / §string_anchor / §vtable_base /
§callsite — each check's "survival datum" + "the check" defines the JS check exactly; the
callsite multiple-hit → Ambiguous posture is **D31a** (warn-and-steer, never refuse).

## Disassembler-test / author-burden

None — the checks consume already-authored row fields (rva/hash/aob/anchor); the engine does
the scan/hash, the author supplies only the named row. No new hand-hex input.
