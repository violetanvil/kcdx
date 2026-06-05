# 0.5 [CORE] Establish the cross-impl known-DLL fixture + known per-kind verdicts

## What

Establish the shared cross-implementation fixture: a known DLL (or a small set of known DLL
slices) + the KNOWN-CORRECT per-kind survival verdict for a chosen set of rows (one per kind:
function, callsite, string_anchor, instruction_anchor, data_slot, vtable_base — plus the
CannotCheck case for vtable_index). This is the evidence/contract the JS↔Python (Phase 2) and
JS↔C++ (Phase 3) agreement tests pin against — a single source of ground-truth verdicts all
three implementations must reproduce (D27 — the test-of-record pattern). Ordered FIRST in the
contract chain so the agreement tests have something to assert against.

## Scope

One commit under `data/refdata-extractor/python/seeds_shared/` (+ a fixture data location the
agreement tests load): the known-DLL fixture reference + a declared table of
`(kcdx_id, kind) → expected verdict (+ detail)`, derived from a verified source (an existing
`_research/` finding or a fresh `/research-disassembly` pass), with a pytest asserting the
fixture loads + the verdict table is well-formed. The verdicts are GROUND TRUTH (primary
evidence, not agent hypothesis — `.claude/rules/working-artifacts.md` trust axis). NO checker
logic here (the Python reference checker is Phase 1 step 1).

## Test bar

A pytest in `seeds_shared/` (the repo's CORE test layer): `test_cross_impl_fixture.py` —
asserts the fixture DLL/slice loads, the expected-verdict table parses, every in-scope kind
has at least one row with a declared expected verdict, and vtable_index's row declares the
CannotCheck expectation. This is a real, runnable-at-this-step unit test
(`.claude/rules/test-discipline.md`, `.claude/rules/incremental-delivery.md`).

## Dependencies

None (the contract chain's first step). The fixture exists before any agreement test
references it; Phase 1 step 1 (Python reference checker), Phase 2 step 3/4 (JS↔Python), and
Phase 3 step 4 (JS↔C++) all consume it.

## Reference

[`../plan-spec.md`](../plan-spec.md) — Group C (cross-impl); TRD D27 (cross-impl agreement,
test-of-record); `data/maintainer-tool/fingerprint-per-kind.md` (the per-kind verdict
definitions the expected table encodes).

## Design authority

`data/maintainer-tool/fingerprint-per-kind.md` §"Per-kind fingerprint table" — the per-kind
"what 'still valid' means" + "the check" define the expected verdict for each fixture row; the
fixture's verdict table is built to that section, never invented.

## Disassembler-test / author-burden

None — a test fixture; adds no author-facing input. The verdicts are derived from verified
evidence, not hand-authored hex (the ground-truth-first direction).
