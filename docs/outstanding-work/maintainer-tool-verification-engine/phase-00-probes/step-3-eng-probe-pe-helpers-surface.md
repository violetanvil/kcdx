# 0.3 [ENG] Probe — read the C++ pe_helpers surface (does it expose spans + a disp32 follower?)

## What

Read the existing kcdx C++ PE-helper surface (the code `survival.cpp` / `survival_pass.cpp`
already use to read the on-disk DLL) and determine whether it exposes `.text` / `.data` /
`.rdata` section spans + a RIP-relative `disp32` follower, OR whether that is NEW infra
Phase 3 must build. This is a READ/probe — the outcome is a SCOPING finding for the engine
extension (Phase 3), not code. No build, no launch (a static source read).

## Scope

A read-only investigation: grep + read `src/survival.cpp`, `src/survival_pass.cpp`, and the
PE-helper headers they include; enumerate what section-access + derivation primitives already
exist vs what the 5 non-function static kinds (callsite/string_anchor/instruction_anchor/
data_slot/vtable_base) need. Capture the scoping finding (what exists / what is new infra) to
`_research/`. NO production code, NO new helpers — only the finding that scopes Phase 3.

## Test bar

A probe step's "test" is its outcome→meaning map (`.claude/rules/results-driven.md`); the
outcome is the scoping finding:

| Outcome | Meaning | Next action |
|---|---|---|
| pe_helpers already exposes section spans + a disp32 follower | Phase 3 reuses them; the kind-checks are thin | Phase 3 steps 1–2 scope to dispatch + per-kind logic only |
| Spans exist, disp32 follower is missing | Phase 3 step 2 adds the follower as named infra (one earlier sub-unit) | Note it in the Phase 3 step-2 dependency list |
| Neither exists | Phase 3 needs a PE-section-access infra step first | Surface the added scope to the user; Phase 3 gains an earlier infra step (`incremental-delivery.md`) |

## Dependencies

None (de-risks Phase 3). May run in parallel with 0.1 / 0.2.

## Reference

[`../plan-spec.md`](../plan-spec.md) — Group D (the pe_helpers scoping row); the existing
checker `src/survival.cpp` + `src/survival_pass.cpp`; `data/maintainer-tool/fingerprint-per-kind.md`
(the per-kind checks the engine must run).

## Disassembler-test / author-burden

None — a read-only scoping probe; adds no author-facing input.
