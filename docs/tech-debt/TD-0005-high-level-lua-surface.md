---
id: TD-0005
opened: 2026-06-05
status: Open
area: lua high-level gameplay surface (kcdx.player.* / kcdx.world.* / kcdx.dialogue.* / kcdx.quest.*)
closure_gate: a dedicated high-level-Lua-surface build phase
owner: the high-level-Lua-surface build phase (when scheduled)
commit_at_filing: 264d3951fbf4b5b1df710f15ebd95cfa4e7325f8
related:
  - design-gaps.md #16 (high-level Lua surface for gameplay)
---

# TD-0005 — high-level Lua gameplay surface, deferred

## Context

kcdx's Lua surface today gives authors only the **low-level** primitives
(`kcdx.hook`, `kcdx.bytes`, `kcdx.code`, `kcdx.scan`, `kcdx.declare`). There is
**no gameplay-level surface**: a modder who wants to change player health must
hook a function and do the pointer arithmetic themselves. The high-level surface
(`kcdx.player.*` / `kcdx.world.*` / `kcdx.dialogue.*` / `kcdx.quest.*`) is the
gameplay-tier where the author writes a named gameplay value and the engine
holds the Address-Library entry + verified ABI + typed access — the disassembler
test (`cornerstones.md`) applied at gameplay tier. This addresses
`docs/design-gaps.md` gap #16.

This surface was the restructure's **Phase 9** — a non-blocking leaf (it consumes
the Address Library from 9.1 and the existing Lua binders; nothing downstream
consumes `kcdx.player.*`). It is being carried as deliberate debt rather than
held as an open active-plan phase: the work is real and specced but not on any
critical path, so it is parked here under one named blocker with each surface
enumerated, instead of sitting indefinitely `NOT STARTED` on the active
restructure ledger. The full per-step spec the build phase consumes lives at
`docs/outstanding-work/restructure/phase-09-high-level-lua/` (the 5 step docs +
`00-original-plan.md` §"Phase 9") — this TD does not duplicate that spec; it is
the tracked-debt handle that points at it.

What the code does today: zero `kcdx.player.*` / `kcdx.inventory.*` /
`kcdx.world.*` / `kcdx.dialogue.*` / `kcdx.quest.*` binder in
`src/lua_bind*.cpp`; the namespace stub tables are not registered; no
`cap-XX-player-*` / `cap-XX-inventory-*` test plugin exists.

What it does at closure: the three end-to-end capabilities + the namespace stubs
+ their regression plugins land, proving the architecture end-to-end (Address
Library lookup → pointer arithmetic → typed Lua read/write), each enumerated
below.

## Closure blocker

**A dedicated high-level-Lua-surface build phase is scheduled and run** — the
work specced in `docs/outstanding-work/restructure/phase-09-high-level-lua/`.
The blocker is the scheduling+execution of that phase, not any external
dependency: every prerequisite already exists (the Address Library DB from
Phase 9.1, the Lua binder infrastructure). The phase lands the enumerated items;
on its acceptance this TD closes (per-item rows below all built + tested), the
restructure Phase-9 tree's ledger flips, and the design-gap #16 entry resolves.

Each enumerated item carries its own per-item gating unknown (the RE its
Address-Library entry needs). Those are the work, not separate blockers — the
ONE closure blocker is the phase that does them.

## Enumerated items (each carried under this one blocker)

Each new Address-Library entry is **AP18-gated** — explicit user approval per
entity before its seed row lands. Each surface ships its `docs/lua/` entry +
its permanent `test-plugins/` regression row in the same step (test-suite +
docs-discipline).

| # | Surface | State today | What the build phase lands | Per-item RE / gating unknown |
|---|---------|-------------|----------------------------|------------------------------|
| 1 | `kcdx.player.health` — `:get()` / `:set(n)` / `:add(n)` | not built | typed read/write of the player health field; test `set(50)` → `get() == 50` | player struct + health field offset (Address-Library entry) |
| 2 | `kcdx.player.position` — `:get()` (Vec3) | not built | `:get()` reads a Vec3 from the player struct | player position Vec3 offset (Address-Library entry) |
| 2b | `kcdx.player.position:set()` (teleport-like write) | not built | ships **only if** RE confirms a safe write path; otherwise `:get()`-only and `:set()` stays its own tracked follow-up — no deferred-correctness shortcut (AP13) | a probe confirming a safe player-position write (no crash / desync) |
| 3 | `kcdx.inventory.add(item_id, count)` | not built | grants an item to the player inventory; the name carries the verified ABI (engine holds the signature, not the author); test add-a-known-item → count rose | the inventory-grant game function + its ABI (Address-Library entry, abi_walker-verified) |
| 4 | namespace stubs — `kcdx.player.*` (other) / `world.*` / `dialogue.*` / `quest.*` | not built | register the stub tables; each documented function logs a structured "not yet implemented; tracking design-gap #16" line and returns nil (loud + meaningful, not a nil-index crash) | none (pure binder) |
| 5 | regression plugins — `cap-XX-player-health` / `cap-XX-player-position` / `cap-XX-inventory-add` + the stub-NYI sub-test | not built | permanent `test-plugins/` rows; the 3 real items are `in-game` test mode (loaded save with a player) | none (test plugins) |

The **scoping discipline** the build phase must preserve: it ships the **3 real
items + stubs**, NOT a namespace of stubs alone. Stubs-only would be the
"ships 80%" anti-pattern — a namespace that looks complete but does nothing. The
3 real items exist to prove the architecture works end-to-end and unblock TC
authors on the most-common use case (player state mutation).

## Activity log

- **2026-06-05** — Initial filing. Re-homed from the active restructure Phase-9
  plan tree to carried debt (non-blocking leaf; closure blocker = a dedicated
  build phase). The per-step spec stays at
  `docs/outstanding-work/restructure/phase-09-high-level-lua/`; this TD is the
  debt handle pointing at it.

## What this entry does NOT do

- Does not double as a bug report (no runtime defect — this is unbuilt-but-specced
  capability carried as debt).
- Does not duplicate the Phase-9 step spec — `docs/outstanding-work/restructure/phase-09-high-level-lua/`
  remains the authoritative per-step build spec the closing phase consumes.
- Closure is appended by the skill that lands the fix (the build phase, via
  `/feature` / `/execute`), which then moves this file to `closed/` + reindexes
  per `doc-organization.md` — never at filing.
