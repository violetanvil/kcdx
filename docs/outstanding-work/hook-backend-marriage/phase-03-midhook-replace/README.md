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
| Step 6a — capture-form enumeration + Context64 mapping (probe, resolves U4) | DONE | `b568600` |
| Step 6b — replace make_jit_midfunc with the MidHook adapter | DONE | (landed) |

## Verification gate

- Step 6a: every capture form `make_jit_midfunc` supports AND every form cap-21 +
  the mid-hook tests exercise is enumerated and mapped to a `Context64` field
  (register → `ctx.<reg>`; stack-expr `[rbp+N]` → `ctx` via `trampoline_rsp`+offset).
  A form with NO `Context64` equivalent (U4) is SURFACED, not silently dropped. The
  deliverable is the capture map (a durable finding in `_research/`), no code change
  to the live mid path yet — its verification is the map's completeness (every form
  covered or surfaced), not a live run.
- Step 6b: the cap-04 matrix (CAP-04a/b/c/d — True/False/Auto) AND cap-21 (mid
  hook with captures) pass live on the production `safetyhook::MidHook` adapter;
  named-capture read + WRITEBACK confirmed (an author mutating a captured register
  sees it take effect — the writeback half the spike did NOT exercise, proven live
  HERE). `make_jit_midfunc` is deleted, the live source returns to pure production
  logic. Agent builds + deploys, user launches, agent reads the log.
