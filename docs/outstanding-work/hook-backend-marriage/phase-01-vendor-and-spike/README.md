# Phase 1 — vendor safetyhook + prove the seam

**Intent.** Get safetyhook into the repo (license-checked, vendored, building),
then run the keystone spike that resolves the design's load-bearing build-gated
unknowns BEFORE any production code rests on them. The spike ports cap-04 onto
`safetyhook::MidHook` behind a throwaway seam and re-runs the cap-04 matrix —
proving `ctx.rip` carries the three call-original modes, the trampoline is
callable from the existing JIT thunk, who owns resume_addr, and whether every
stack-expression capture form maps. **This phase gates Phase 3** (the
`make_jit_midfunc` retirement is provisional on the spike, `results-driven.md`).

Shared spec: [`../context.md`](../context.md).

## Status ledger (step-grain)

| Step | Status | Commit |
|---|---|---|
| Step 1 — license-check + vendor safetyhook | NOT STARTED | — |
| Step 2 — keystone spike: cap-04 on safetyhook::MidHook | NOT STARTED | — |

## Verification gate

- Step 1: `pwsh ./build.ps1` green with safetyhook vendored + wired; the
  dependency manifest row landed; the header facts (`Context64`, `enable()`
  thread-suspend, E9→FF) re-confirmed against the source now on disk.
- Step 2: the cap-04 matrix (CAP-04a/b/c/d) passes live via the throwaway
  safetyhook-MidHook seam — the user launches, the agent reads `kcdx-dev.log`.
  Outcome: ALL FOUR pass → Phase 3 proceeds; any FAIL → the mid-hook replacement
  is reconsidered (the design's one deliberately-gated path; surface to the user).
