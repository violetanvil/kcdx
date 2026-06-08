# Phase 3 — retire make_jit_midfunc

**Intent.** Replace the ~370-line hand-rolled asmjit mid-hook codegen
(`make_jit_midfunc`) with a `safetyhook::MidHook` adapter: the three call-original
modes via `ctx.rip`, named captures via `Context64` register writeback, the
existing `MidDispatch` rewired to read `Context64` instead of the JIT payload.
This retires the project's most fragile, most-bled-on code (the cap-04 scar
tissue). **PROVISIONAL — gated on Phase 1 step 2's spike PASS** (the
`ctx.rip`→three-modes mapping must be observed before this rewrite lands,
`results-driven.md`).

Shared spec: [`../context.md`](../context.md).

## Gate from Phase 1

Do NOT start Step 6 until Phase 1 step 2 (the cap-04 spike) returned PASS on all
four CAP-04 rows. A spike FAIL means the mid-hook replacement is reconsidered (the
design's one deliberately-gated path) — surface to the user; do not build the
production replacement on an unproven mechanism.

## Status ledger (step-grain)

| Step | Status | Commit |
|---|---|---|
| Step 6 — replace make_jit_midfunc with MidHook adapter | NOT STARTED | — |

## Verification gate

- Step 6: the cap-04 matrix (CAP-04a/b/c/d — True/False/Auto) AND cap-21 (mid
  hook with captures) pass live on the production `safetyhook::MidHook` adapter;
  named-capture read + writeback confirmed (an author mutating a captured register
  sees it take effect). `make_jit_midfunc` is deleted, the live source returns to
  pure production logic. Agent builds + deploys, user launches, agent reads the
  log.
