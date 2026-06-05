# 0.1 [FE] Probe — 86MB WHGame.dll ArrayBuffer + full `.text` AOB scan in-browser

## What

Probe whether the browser can load the real ~86MB WHGame.dll as an `ArrayBuffer` (File API)
AND run a full `.text` AOB byte-pattern scan over it within an acceptable perf/memory budget.
This is the feasibility floor under the whole browser static checker (D26 asserts "WHGame.dll
≈ 86 MB — within browser limits"; this probe confirms it on the real binary, not on the
assertion). Throwaway probe code in the frontend repo; the finding is captured, the probe
removed (no residue in live source — `.claude/rules/working-artifacts.md`).

## Scope

One commit in the frontend repo: a throwaway probe harness (a Vitest/bench or a tiny dev page)
that reads the real DLL, times a full-`.text` AOB scan (the `callsite` kind's worst case — a
unique-match scan across the whole section), and records peak memory + wall time. Captures the
finding to `_research/` (kcdx tree). NO production checker code; NO UI.

## Test bar

A probe step's "test" is its outcome→meaning map (`.claude/rules/results-driven.md`):

| Outcome | Meaning | Next action |
|---|---|---|
| Loads + scans in an acceptable budget (sub-second-ish, no OOM) | In-browser static scan is feasible as designed | Proceed to Phase 2 with the straightforward scan approach |
| Loads but the scan is too slow / janky | Feasible but needs a worker/chunked-scan strategy | Capture the budget; Phase 2 step 1/3 adopts the worker strategy (still client-side) |
| Cannot load 86MB / OOMs | The no-upload client-side premise is at risk | STOP — surface to the user (`design-authority.md`); do not proceed to Phase 2 on a broken premise |

## Dependencies

None (first step; de-risks Phase 2).

## Reference

[`../plan-spec.md`](../plan-spec.md) — Group A; TRD D26 (client-side, 86MB within limits).
Reuses the PE-parse in `data/maintainer-tool/frontend/src/dll-resolver/versionResolver.ts`.

## Disassembler-test / author-burden

None — a probe adds no author-facing input. (Confirms the engine can do the byte work in the
browser, so the author never supplies a hash/pattern by hand — the disassembler-test direction.)
