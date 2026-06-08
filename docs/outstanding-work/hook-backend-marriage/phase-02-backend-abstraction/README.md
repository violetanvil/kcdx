# Phase 2 — the backend seam at InstallRuntime (function-entry)

**Intent.** Establish the `IDetourBackend` seam at its real home — the install
chokepoint `hook_engine::InstallRuntime` — and migrate the function-entry hook
path onto it. Three ordered steps: extract `MinHookBackend` from `detour_hook`'s
current body (a pure behavior-preserving refactor — every hook still MinHook),
then add `SafetyhookBackend`, route the function-entry install through
`InstallRuntime`, **dissolve `detour_hook`** (the JIT-slot-owner husk — the backend
owns its slot now), and **retire `g_installed`** (one conflict model: the chain's
`FindChain`/`CanCoexist`); far-target reach falls out. Last, the install-context
routing predicate at `InstallRuntime` that selects MinHook for the loader-lock +
bootstrap paths and safetyhook for everything else. The mid-hook path is NOT
touched here (Phase 3); the chain + conflict model + Lua marshaling are unchanged
throughout.

Shared spec: [`../context.md`](../context.md).

## Status ledger (step-grain)

| Step | Status | Commit |
|---|---|---|
| Step 3 — IDetourBackend + MinHookBackend (refactor) | DONE | `64fba7d` |
| Step 4 — SafetyhookBackend; seam → InstallRuntime; detour_hook dissolves; g_installed retires | NOT STARTED | — |
| Step 5 — install-context routing predicate at InstallRuntime | NOT STARTED | — |

## Verification gate

- Step 3: `pwsh ./build.ps1` green; ALL existing cap-NN hook rows pass live
  (the refactor is behavior-preserving — every hook is still MinHook, just behind
  the interface). The proof the seam introduced no regression. **(DONE — `64fba7d`;
  the interface was built behind `detour_hook` and relocates in step 4.)**
- Step 4: function-entry cap-NN rows pass live on the safetyhook backend with the
  install routed through `InstallRuntime`; the cap-22 far-module rows pass with NO
  per-module-pool special-case as the saving mechanism (E9→FF fallback);
  `detour_hook` is removed and `g_installed` is retired with the full suite
  unchanged (the U8 caller-set check confirms chain-only before the map is removed,
  or re-homes a non-chain guard). Agent builds+deploys, user launches, agent reads
  the log.
- Step 5: the routing predicate at `InstallRuntime` selects MinHook for
  early_hook + the update pump (loader-lock safety preserved — early_hook installs
  under the loader lock with zero deadlock, incl. under multitasking load) and
  safetyhook elsewhere; full cap-NN suite green live.
