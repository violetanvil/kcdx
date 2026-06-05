# 2.1 [FE] PE-section scanning foundation + the 4 verdict types

## What

Build the PE-section access foundation the browser checker needs — `.text` / `.data` access
(beyond the `.rdata` the version resolver already reads) + RVA→file-offset mapping — and define
the 4 verdict types (Unchanged / Changed / Ambiguous / CannotCheck, D26/US-11) the per-kind
checks return. This is the shared base every kind-check (steps 3–4) sits on; it extends the
existing PE-parse in `versionResolver.ts` rather than re-parsing the PE from scratch.

## Scope

One commit in the frontend repo: a PE-section access module (section header walk → `.text` /
`.data` / `.rdata` span + RVA↔file-offset conversion) reusing `versionResolver.ts`'s PE-parse
foundation, plus the `Verdict` type (the 4-value union) + its detail shape. No kind-specific
check logic yet (steps 3–4); no decoder (step 2); no UI (steps 5–7).

## Test bar

A Vitest unit test in the frontend repo: asserts section spans + RVA↔offset conversion are
correct against the Phase-0 fixture DLL (a known section's known offset), and the `Verdict`
type's shape. Runnable at this step (the fixture exists from 0.5; the PE-parse is reused) —
`.claude/rules/test-discipline.md`, `.claude/rules/incremental-delivery.md`.

## Dependencies

- **0.1** — the ArrayBuffer-scan feasibility finding (confirms the section-scan budget).
- **0.5** — the cross-impl fixture DLL (the test asserts section offsets against it).

## Reference

[`../plan-spec.md`](../plan-spec.md) — Group A (PE-section scan + verdict types); reuses
`data/maintainer-tool/frontend/src/dll-resolver/versionResolver.ts` (the PE-parse foundation).

## Disassembler-test / author-burden

None — internal infra (PE-section access); no author-facing input. It is the engine doing the
binary parsing so the author never supplies a file offset by hand.
