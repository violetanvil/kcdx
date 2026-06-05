# 2.2 [FE] The minimal in-browser x86 decoder (RIP-relative `disp32` follow)

## What

Build the minimal in-browser x86 decoder — just enough to follow a RIP-relative LEA/MOV
`disp32` (NOT a full disassembler, D26) — as its OWN named sub-unit (D26/§5 — "its own named
sub-unit, not folded into the checker"). It is consumed by the 2 derivation-kind checks
(instruction_anchor, data_slot — step 4). Ordered before step 4 so the derivation checks have
the decoder to build on; the scope is the minimal scope the Phase-0 probe (0.2) confirmed.

## Scope

One commit in the frontend repo: a named decoder sub-unit that, given a `.text` offset, decodes
the LEA/MOV at that site and computes the RIP-relative `disp32` target RVA, to exactly the
minimal scope 0.2 validated. It is a standalone module (consumed by step 4, not before). No
kind-check wiring (step 4); no UI.

## Test bar

A Vitest unit test in the frontend repo: decodes a known anchor site on the Phase-0 fixture DLL
and asserts the followed `disp32` target == the fixture's Ghidra-confirmed RVA (the same ground
truth 0.2 used). Runnable at this step — `.claude/rules/test-discipline.md`,
`.claude/rules/incremental-delivery.md`.

## Dependencies

- **0.2** — the x86-decoder feasibility probe (confirmed the minimal scope lands on the right
  target; this step builds the production decoder to that confirmed scope).
- **2.1** — PE-section foundation (the decoder reads `.text` via the section access).

## Reference

[`../plan-spec.md`](../plan-spec.md) — Group A (the minimal x86 decoder, named sub-unit); TRD
D26 (minimal decoder, not a full disassembler); `data/maintainer-tool/fingerprint-per-kind.md`
§instruction_anchor + §data_slot (the derivation chains it serves).

## Disassembler-test / author-burden

None — the decoder is the engine doing the derivation so the author never hand-follows a
`disp32`. This sub-unit IS the disassembler-test resolution for the derivation kinds: the
engine carries the decode, the author authors only the anchor.
