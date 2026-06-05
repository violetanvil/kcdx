# 2.4 [FE] The 2 derivation-kind checks + the anchor-dependency DAG ordering

## What

Build the 2 derivation-kind static checks in JS — instruction_anchor (re-run the resolver
chain: find the `.rdata` anchor, scan `.text` for the LEA whose RIP-relative target == the
string, walk the byte-shape back; verify the final instruction shape) and data_slot (follow the
`disp32` from the anchor / a fixed offset from another slot; Unchanged iff it lands in `.data`
at a consistent offset — NO content hash) — on top of the step-2 decoder. Add the
**anchor-dependency DAG ordering**: a dependent kind whose anchor is itself Changed is
transitively CannotCheck (a dead `string_anchor` → everything downstream CannotCheck-with-reason).
Extend the JS↔Python agreement test to these kinds. This completes the 8-in-scope-kind browser
checker (vtable_index returns CannotCheck — population deferred).

## Scope

One commit in the frontend repo: the instruction_anchor + data_slot check functions over the
step-2 decoder, plus the dependency-ordered survival walk (resolve `derives_from` edges; mark a
dependent CannotCheck when its anchor is Changed), plus the vtable_index → CannotCheck path. The
JS↔Python agreement test extended to the derivation kinds + the transitive-CannotCheck case. No
UI.

## Test bar

Vitest unit tests for instruction_anchor + data_slot (Unchanged + a Changed-anchor → transitive
CannotCheck case) + vtable_index → CannotCheck, over the Phase-0 fixture, PLUS the extended
**JS↔Python agreement test** covering all 8 in-scope kinds. Runnable at this step (decoder +
reference + fixture all exist) — `.claude/rules/test-discipline.md`,
`.claude/rules/incremental-delivery.md`.

## Dependencies

- **2.2** — the x86 decoder (the derivation checks consume it).
- **2.3** — the pure-byte checks + the JS↔Python agreement harness (extended here).
- **1.1 / 0.5** — the Python reference + the fixture (the agreement authority).

## Reference

[`../plan-spec.md`](../plan-spec.md) — Group A (the 2 derivation kinds + the anchor DAG) + Group
C (JS↔Python agreement); the deferred vtable_index population is in the coverage map's Group I.

## Design authority

`data/maintainer-tool/fingerprint-per-kind.md` §instruction_anchor + §data_slot + §"The anchor
dependency (cross-row survival)" — the resolver chains, the no-content-hash data_slot rule, and
the dependency-order DAG are built to that section; vtable_index's CannotCheck (population
deferred) is §vtable_index + §"Status flag".

## Disassembler-test / author-burden

None — the decoder + the resolver-chain re-run are the engine doing the derivation; the author
authors only the anchor + the `derives_from` edge (already part of the row), never a hand-followed
displacement.
