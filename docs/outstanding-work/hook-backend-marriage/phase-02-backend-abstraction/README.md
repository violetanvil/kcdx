# Phase 2 — the backend abstraction (function-entry)

**Intent.** Build the `IDetourBackend` seam and migrate the function-entry hook
path onto it. Three ordered steps: extract `MinHookBackend` from `detour_hook`'s
current body (a pure behavior-preserving refactor — every hook still MinHook),
add `SafetyhookBackend` + the get_original bridge and route function-entry hooks
to it (far-target reach falls out), then the install-context routing predicate
that selects MinHook for the loader-lock + bootstrap paths and safetyhook for
everything else. The mid-hook path is NOT touched here (Phase 3); the chain +
conflict model + Lua marshaling are unchanged throughout.

Shared spec: [`../context.md`](../context.md).

## Status ledger (step-grain)

| Step | Status | Commit |
|---|---|---|
| Step 3 — IDetourBackend + MinHookBackend (refactor) | DONE | (landed) |
| Step 4 — SafetyhookBackend + get_original bridge | NOT STARTED | — |
| Step 5 — install-context routing predicate | NOT STARTED | — |

## Verification gate

- Step 3: `pwsh ./build.ps1` green; ALL existing cap-NN hook rows pass live
  (the refactor is behavior-preserving — every hook is still MinHook, just behind
  the interface). The proof the seam introduced no regression.
- Step 4: function-entry cap-NN rows pass live on the safetyhook backend; the
  cap-22 far-module rows pass with NO per-module-pool special-case as the saving
  mechanism (E9→FF fallback). Agent builds+deploys, user launches, agent reads
  the log.
- Step 5: the routing predicate selects MinHook for early_hook + the update pump
  (loader-lock safety preserved — early_hook installs under the loader lock with
  zero deadlock, incl. under multitasking load) and safetyhook elsewhere; full
  cap-NN suite green live.
