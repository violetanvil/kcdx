# 0.2 [FE] Probe — minimal JS x86 decoder follows a RIP-relative `disp32` on the real DLL

## What

Probe whether a MINIMAL JS x86 decoder (just enough to follow a RIP-relative LEA/MOV
`disp32` — NOT a full disassembler, per D26) can correctly re-derive a real
`instruction_anchor` / `data_slot` target on WHGame.dll, checked against a Ghidra-confirmed
ground truth. This de-risks the hardest part of the browser checker (the 2 derivation kinds);
the 4 pure-byte kinds need no decoder. Throwaway probe; finding captured, probe removed.

## Scope

One commit in the frontend repo: a throwaway harness that decodes the LEA/MOV at a known
anchor site (e.g. id 9 instruction_anchor / id 10 `gEnv->pConsole` data_slot) and follows the
`disp32` to compute the target RVA, comparing it to the Ghidra-confirmed value. Captures the
finding to `_research/`. NO production decoder; NO UI. (The decoder ITSELF is built in
Phase 2 step 2; this probe only proves the minimal approach lands on the right target.)

## Test bar

Outcome→meaning map (`.claude/rules/results-driven.md`) — observe ground truth (the
Ghidra-confirmed RVA) FIRST, then check whether the decoder reproduces it:

| Outcome | Meaning | Next action |
|---|---|---|
| Decoded `disp32` follow == the Ghidra-confirmed target | A minimal decoder suffices for the derivation kinds | Proceed; Phase 2 step 2 builds the decoder to this minimal scope |
| Lands wrong / needs more instruction forms than the minimal LEA/MOV | The minimal scope is too narrow — the real anchors use more encodings | Capture the encodings observed; surface the widened scope to the user (`design-authority.md`) before Phase 2 step 2 |
| The anchor chain itself is mis-modeled | The `fingerprint-per-kind.md` derivation description needs verification | STOP — route to `/research-disassembly` to re-verify the anchor chain; do not build a decoder against a wrong chain |

## Dependencies

None (de-risks Phase 2 step 2 + step 4). May run in parallel with 0.1.

## Reference

[`../plan-spec.md`](../plan-spec.md) — Group A; TRD D26 (minimal in-browser x86 decoder);
`data/maintainer-tool/fingerprint-per-kind.md` §instruction_anchor + §data_slot.

## Disassembler-test / author-burden

None — a probe. (Proves the engine can re-derive the anchor target itself, so the author
never hand-follows a `disp32` — the engine-does-the-heavy-lifting direction.)
