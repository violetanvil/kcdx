# Phase 11 — kcdx owns the one Lua VM (worker-built, engine-adopted)

**Status: IN PROGRESS** — P1–P3 landed (the keystone VM build + adopt is live-confirmed at `3b99fea`; P2 shim + P3 early-hook relocate are `[unverified — pending launch]`); P4–P7 remain. Design settled 2026-06-05, decomposed into this tree.

**kcdx builds the ONE Lua VM itself** on its worker thread (after the game maps
WHGame — NO force-load; PROBE P3 verified a kcdx force-load is impossible +
unnecessary, design §6.2) via the FIX A symbol shim, and the engine ADOPTS it — its
`CScriptSystem::Init` VM-creation is intercepted so the engine never creates its own.
ONE compiled Lua body in the process; the dual-Lua sentinel hazard dies by
construction. Lua plugins gain the `before_game` zone; boot-asset Lua swaps (KI-0005)
+ served-`.lua` execute (KI-0006) become reachable.

- **Settled design:** [`lua-vm-design.md`](lua-vm-design.md).
- **Shared spec + coverage map:** [`plan-spec.md`](plan-spec.md).
- **FIX A harvest (the RE evidence):** [`../fix-a-drop-static-lua.md`](../../fix-a-drop-static-lua.md) (93/117 LUA_API resolved + ~24 inlined/stripped catalogued).

## No longer blocked

The prior "~38% mapped / BLOCKED" note was STALE — it predated the 2026-05-21 harvest
completion (93/117 + the inlined/stripped catalogue). The harvest is done enough to
build; what remained was design + build, now settled in the design doc and decomposed
below. Step 1 of the build is the keystone probe (observe `CScriptSystem::Init`
before wiring the interception — `.claude/rules/results-driven.md`).

The legacy 11a–11d step stubs that lived here described the superseded "hook game's
`luaL_newstate`" framing; the settled design corrected the mechanism (kcdx builds the
state, the engine adopts it). The build order below replaces them.

## Phase ledger (phase-grain)

Status: `NOT STARTED` · `BLOCKED` · `DONE` · `NEEDS REWORK`. Commit = short hash when
`DONE`, `—` otherwise. A landed step flips its row in its phase README; the last step
of a phase flips that phase's row here (the orchestrator owns the cascade).

| Phase | Status | Commit |
|---|---|---|
| [1 — keystone probe (Init + lua_newstate observation)](phase-01-probe/README.md) | DONE | f0a0dc9 |
| [2 — the symbol shim (forward 90 by name + stub 31; 3 unclassified + 2 not-usable carried)](phase-02-shim/README.md) | DONE | 54d98c8 |
| [3 — worker builds the VM + Init adoption (no force-load — PROBE P3)](phase-03-force-load-adopt/README.md) | DONE | 3b99fea |
| [4 — the cross-thread foundation (event gate + RegisterRuntimeOverlay CAS)](phase-04-early-slot-boot-swap/README.md) | NOT STARTED | — |
| [5 — the startup-sequence author contract (control · visibility · docs)](phase-05-startup-sequence-contract/README.md) | NOT STARTED | — |
| [6 — drop static Lua (hazard-killing step)](phase-06-drop-static-lua/README.md) | NOT STARTED | — |
| [7 — served-.lua execute confirmation (KI-0006)](phase-07-serve-execute/README.md) | NOT STARTED | — |

## Build order rationale

Dependency-topological (`.claude/rules/incremental-delivery.md`): the probe (P1)
resolves the intercept point + boot-swap reachability + early-slot shape that every
later phase rests on; the shim (P2) is the machinery the VM build needs; worker
VM-build + adopt (P3) stands up the one VM (no force-load — PROBE P3); the
cross-thread foundation (P4) — the event gate + the two-writer CAS — is the
race-safety infrastructure the early surface reuses; the startup-sequence author
contract (P5) promotes the internal phase model to the author-facing
control/visibility/docs surface, moves console+cvar to the worker (ordered-init,
PROBE INITORDER), and delivers the before_game early slot + boot swap (KI-0005);
dropping static Lua (P6) is the final hazard-killing collapse; serve-execute (P7)
confirms the last open capability. Each phase ends buildable; each step is
independently verifiable when it lands.

## First consumer (rides this phase, not a step here)

The bugsplat-filename-fix builtin DLL (deferred from Phase 4) — the canonical
"intercept a function in a non-WHGame DLL, mutate a string arg, call original" case,
via before_game hooks ([`../before-game-hooks.md`](../../before-game-hooks.md) §6).
