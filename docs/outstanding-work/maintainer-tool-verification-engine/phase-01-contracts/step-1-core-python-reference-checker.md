# 1.1 [CORE] The Python per-kind reference checker (test-of-record)

## What

Build the Python per-kind survival reference checker in `seeds_shared/` — the
**test-of-record** the JS browser checker is pinned against (D27, extending
`version_resolver.py`'s established test-of-record role from version-read to the full per-kind
survival check). Given a DLL's bytes + an authored row, it returns that row's per-kind static
verdict (Unchanged / Changed / Ambiguous / CannotCheck) for the 8 in-scope kinds. It is the
canonical reference implementation; the JS port (Phase 2) must agree with it on the same bytes.

## Scope

One commit under `data/refdata-extractor/python/seeds_shared/`: a headless per-kind reference
checker module implementing the 6 checkable static kinds (function body-hash, callsite AOB
scan, string_anchor `.rdata` search, instruction_anchor derivation, data_slot derivation,
vtable_base table-shape) + the CannotCheck path for vtable_index, dispatched on `kind`,
honoring the anchor-dependency DAG order. No engine code, no JS, no UI — the Python reference
only.

## Test bar

A pytest in `seeds_shared/`: `test_reference_checker.py` — runs the reference checker over the
Phase-0 cross-impl fixture (step 0.5) and asserts each fixture row's verdict == the fixture's
declared expected verdict, per kind (incl. callsite multiple-hit → Ambiguous, a Changed case,
and vtable_index → CannotCheck). Runnable at this step (the fixture exists from 0.5) —
`.claude/rules/test-discipline.md`, `.claude/rules/incremental-delivery.md`.

## Dependencies

- **0.5** — the cross-impl known-DLL fixture + expected verdicts (the test asserts against it).
- **0.2** — the x86-decoder feasibility finding (the derivation-kind checks rely on the
  `disp32`-follow approach the probe confirmed).

## Reference

[`../plan-spec.md`](../plan-spec.md) — Group C (Python ref); cross-step invariant 3 (engine is
authority; this is the cross-impl reference the JS mirror is pinned to).

## Design authority

`data/maintainer-tool/fingerprint-per-kind.md` §"Per-kind fingerprint table" + §"The anchor
dependency (cross-row survival)" — each kind's check (the survival datum + the check at survival
time) and the dependency-order walk are built to that section; the verdict enum (Unchanged /
Changed / Ambiguous / CannotCheck) is per US-11 / D26.

## Disassembler-test / author-burden

None — a headless reference checker over already-authored rows; adds no author-facing input.
The checker does the byte/derivation work so the author's row is verified for them.
