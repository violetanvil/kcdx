# Phase 4 — early Lua slot + boot-asset swap (KI-0005)

The running VM (Phase 3) lets a plugin's early Lua run BEFORE the engine's boot asset
open — so a `kcdx.assets.replace` in that early slot wins the boot asset. This is the
user-required game-load swap capability (KI-0005's deferred Lua-runtime boot serve).
The slot's author-facing shape is the Phase-1-decided one.

## Step ledger (step-grain)

Status: `NOT STARTED` · `BLOCKED` · `DONE` · `NEEDS REWORK`. Commit = short hash when
`DONE`, `—` otherwise.

| Step | Status | Commit |
|---|---|---|
| [1 — the early Lua slot + ordering guard vs the boot open](step-1-early-lua-slot.md) | NOT STARTED | — |
| [2 — boot-asset swap serves (KI-0005) + AP14 warn decision](step-2-boot-asset-swap.md) | NOT STARTED | — |

## Phase verification gate

- **Build green + user-facing acceptance** (`.claude/rules/ux-first-class.md` — the
  gate includes the user seeing the swap, not only build/test green). A boot asset
  the user perceives (e.g. the menu logo) renders REPLACED via the runtime path — the
  user confirms the visible swap; the agent confirms `rt=HIT` from the dev log.
- The boot-open path WAITS on (and BLOCKS until) the early-slot's signaled readiness
  event before resolving a boot overlay — the happens-before EVENT GATE holds (design
  §5). An ungated / un-signaled boot open is the failure, NOT a wall-clock margin;
  timing-based ordering ("the register ran before the open") is the forbidden race the
  gate exists to kill (`.claude/rules/concurrency.md`). The order-inversion regression
  (steps 1+2) is the falsifiable proof.
- The AP14 teaching warn is narrowed/removed per the build-time decision (lean:
  narrow to late-slot boot targets), with a regression row asserting a late-slot boot
  target still warns (never a silent no-op — the KI-0005 regression).
- A permanent regression row self-reports the boot-swap serve.
