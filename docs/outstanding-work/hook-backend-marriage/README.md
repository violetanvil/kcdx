# hook-backend-marriage

**Intent.** Marry MinHook + safetyhook behind one uniform `IDetourBackend`
interface, each hook-install path routed to the engine whose strengths fit it —
automatically, by install context. safetyhook for the bulk (thread-safe install,
far-target reach, a vetted mid-hook primitive); MinHook permanent for the two
loader-lock/bootstrap paths it alone can serve. The chain, conflict model, and
Lua/ABI marshaling stay unchanged above the backend layer.

Settled design: [`docs/design/hook-backend-marriage.md`](../../design/hook-backend-marriage.md)
(`c93d25d`). Shared spec + coverage map: [`context.md`](context.md).

**Independent of Phase 11 (shim-VM)** — different layers, no build-order
dependency; may run in parallel. See [`context.md`](context.md) §"Independence
from Phase 11".

## Status ledger (phase-grain)

| Step | Status | Commit |
|---|---|---|
| Phase 1 — vendor safetyhook + prove the seam | DONE | (landed) |
| Phase 2 — the backend abstraction (function-entry) | NOT STARTED | — |
| Phase 3 — retire make_jit_midfunc (gated on Phase 1 step 2) | NOT STARTED | — |
| Phase 4 — foreign-hook coexistence | NOT STARTED | — |
| Phase 5 — backend reference doc | NOT STARTED | — |

The per-step ledgers live in each `phase-NN-*/README.md`. A top row flips to
`DONE` only when every step in that phase is `DONE` (the orchestrator owns the
cascade).

## Phases

- **[Phase 1 — vendor safetyhook + prove the seam](phase-01-vendor-and-spike/README.md)**
  — vendor safetyhook (license-check + CMake + manifest row), then the keystone
  spike: port cap-04 onto `safetyhook::MidHook` behind a throwaway seam and re-run
  the cap-04 matrix. The spike resolves the design's load-bearing build-gated
  unknowns (`ctx.rip`→three-modes, the trampoline-callable contract, resume_addr
  ownership, stack-expr coverage) and **gates Phase 3**. Nothing fragile is
  rewritten until the spike proves the mechanism (`results-driven.md`).
- **[Phase 2 — the backend abstraction (function-entry)](phase-02-backend-abstraction/README.md)**
  — introduce `IDetourBackend` + extract `MinHookBackend` from `detour_hook`
  (pure refactor, all still MinHook), add `SafetyhookBackend` + the get_original
  bridge and route function-entry hooks to it (far-target reach falls out), then
  the install-context routing predicate (early_hook + update pump → MinHook,
  loader-lock safety preserved).
- **[Phase 3 — retire make_jit_midfunc](phase-03-midhook-replace/README.md)**
  — replace the ~370-line hand-rolled mid-hook codegen with the
  `safetyhook::MidHook` adapter: the three call-original modes via `ctx.rip`,
  named captures via Context64 writeback, `MidDispatch` rewired onto Context64.
  **Provisional — gated on Phase 1 step 2's spike PASS.**
- **[Phase 4 — foreign-hook coexistence](phase-04-foreign-hook/README.md)**
  — detect a pre-existing foreign hook (the prologue classifier), then chain onto
  it (follow the jmp, capture the foreign detour as kcdx's original) so both
  mods' hooks fire; the `comp-NN` two-mod fixture proves it.
- **[Phase 5 — backend reference doc](phase-05-reference-doc/README.md)**
  — the backend-layer subsystem/reference doc (a new responsibility unit gets its
  doc, `structure-by-responsibility.md` §6).
