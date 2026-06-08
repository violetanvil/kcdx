# hook-backend-marriage

**Intent.** Marry MinHook + safetyhook behind one uniform `IDetourBackend`
interface, each hook-install path routed to the engine whose strengths fit it —
automatically, by install context. safetyhook for the bulk (thread-safe install,
far-target reach, a vetted mid-hook primitive); MinHook permanent for the two
loader-lock/bootstrap paths it alone can serve. The chain, conflict model, and
Lua/ABI marshaling stay unchanged above the backend layer.

Settled design: [`docs/design/hook-backend-marriage.md`](../../design/hook-backend-marriage.md)
(`62046c3` — v1 + the §4 re-grounding). Shared spec + coverage map:
[`context.md`](context.md).

**Independent of Phase 11 (shim-VM)** — different layers, no build-order
dependency; may run in parallel. See [`context.md`](context.md) §"Independence
from Phase 11".

## Status ledger (phase-grain)

| Step | Status | Commit |
|---|---|---|
| Phase 1 — vendor safetyhook + prove the seam | DONE | `9de81ea` / `9862bf1` |
| Phase 2 — backend seam at InstallRuntime (function-entry) | DONE | (landed) |
| Phase 3 — retire make_jit_midfunc (gated on Phase 1 step 2) | DONE | (landed) |
| Phase 4 — foreign-hook coexistence (core pillar) | NOT STARTED | — |
| Phase 5 — batch install | NOT STARTED | — |
| Phase 6 — backend reference doc | NOT STARTED | — |

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
  rewritten until the spike proves the mechanism (`results-driven.md`). **DONE.**
- **[Phase 2 — backend seam at InstallRuntime (function-entry)](phase-02-backend-abstraction/README.md)**
  — `IDetourBackend` + `MinHookBackend` (step 3, DONE — pure refactor, all still
  MinHook) established the interface behind `detour_hook`; this phase moves the
  seam to its real home. Step 4 adds `SafetyhookBackend`, routes the function-entry
  install through `hook_engine::InstallRuntime` (the actual install chokepoint),
  **dissolves `detour_hook`** (only the JIT slot owner — the backend owns its slot
  now), and **retires `g_installed`** (the chain's `FindChain`/`CanCoexist` is the
  sole conflict model); far-target reach falls out. Step 5 adds the
  install-context routing predicate at `InstallRuntime` (early_hook + update pump →
  MinHook, loader-lock safety preserved).
- **[Phase 3 — retire make_jit_midfunc](phase-03-midhook-replace/README.md)**
  — replace the ~370-line hand-rolled mid-hook codegen with the
  `safetyhook::MidHook` adapter: the three call-original modes via `ctx.rip`,
  named captures via Context64 writeback, `MidDispatch` rewired onto Context64.
  **Provisional — gated on Phase 1 step 2's spike PASS.**
- **[Phase 4 — foreign-hook coexistence (core pillar)](phase-04-foreign-hook/README.md)**
  — detect a pre-existing foreign hook (the prologue classifier), then chain onto
  it (follow the jmp, capture the foreign detour as kcdx's original) so both
  mods' hooks fire; the `comp-NN` two-mod fixture proves it. A CORE v1 pillar (the
  multiplayer/extreme-mod consumer hooks the same functions other mods hook), not
  final-phase polish — built once the safetyhook swap makes patching a shared
  prologue safe.
- **[Phase 5 — batch install](phase-05-batch-install/README.md)**
  — install N detours under ONE thread-suspend window instead of N (safetyhook's
  `enable()` suspends all threads per hook; at TC/multiplayer scale that is
  hundreds of stop-the-world cycles). A kcdx-authored path over safetyhook's
  `StartDisabled` + `trap_threads` (create-all-disabled → one frozen window patches
  N); the multi-target-window reuse is probe-gated, with a per-hook fallback.
- **[Phase 6 — backend reference doc](phase-06-reference-doc/README.md)**
  — the backend-layer subsystem/reference doc (a new responsibility unit gets its
  doc, `structure-by-responsibility.md` §6).
