---
id: TD-0013
opened: 2026-06-11
status: Open
area: kcdx.behavior.* window law / test-suite (Phase 9.5 P1 s4)
closure_gate: Phase 11 P5's lua_before slot lands (the first Lua early stop) — then the Lua early-stop out-of-window fixture leg is written and this TD closed
owner: continuous (the Phase 11 P5 lua_before build, or the next behavior window-law work after it lands)
commit_at_filing: ddd442dd99c27e43d6b29422c4cc2047b0ee5435
related:
  - TD-0007 (Lua loader symbols — also gated on Phase 11 P5; both are P5-blocked Lua-window items)
affected_sites:
  - src/lua_bind_behavior.cpp  (the window-law wall — built, but its Lua early-stop trip path needs a lua_before caller to exist)
  - test-plugins/cap-100-behavior/plugin.lua  (the single-plugin behavior fixtures; the Lua early-stop row lands here or in a paired comp fixture when lua_before exists)
  - docs/outstanding-work/restructure/phase-09.5-behaviors/behavior-design.md  (§6 window law, §14 — the Lua leg's named trigger)
  - docs/outstanding-work/restructure/phase-09.5-behaviors/plan-spec.md  (the Lua early-stop fixture row, marked DEFERRED → this TD)
---

# TD-0013 — Lua early-stop window-law out-of-window fixture leg

## Context

Phase 9.5 P1 s4 built the behavior **window law** (design §6): a PLUGIN-tier
behavior resolves only at the MAIN stop; a `set` from an EARLY stop against a
plugin-tier behavior is OUT-OF-WINDOW and fails loud with a teaching error
(catalog-tier `kcdx.behavior.*` names are settable from any stop). The
out-of-window WALL is built in `src/lua_bind_behavior.cpp` (the set path
rejects a plugin-tier set when the init-phase is before the main Lua wave).

The wall's two early-stop trippers are:

1. **The C++ early stop** (`kcdxPlugin_Load` on the worker thread) — exercised
   via `kcdxBehaviorInterface::Set` once that interface exists. The C++
   behavior interface ships in **Phase 9.5 P2** (per the plan-spec coverage
   map: "§6 window law … C++ early-stop fixture P2 s2"); the C++ early-stop
   fixture lands with it. There is no C++ behavior seam today (no
   `kcdxInterface_Behavior` / `kcdxBehaviorInterface` in
   `include/kcdx/Interfaces.h`), so the C++ early-stop fixture is NOT
   constructible in P1 s4 — it correctly rides P2 s2.

2. **The Lua early stop** (a future `lua_before` slot) — there is **no Lua
   early stop today**. Every `plugin.lua` runs at the game-thread first-tick
   MAIN stop; the Lua binder is itself a main-stop caller, so it can never trip
   the out-of-window wall. A `lua_before` slot (a worker-side / pre-boot Lua
   entry) is the first Lua early stop, and it arrives with **Phase 11 P5's
   startup-sequence contract** (the `lua_before` slot — design
   `phase-11-shim-vm/phase-05-startup-sequence-contract/`).

This entry covers leg 2 — the **Lua** early-stop out-of-window fixture. The
user approved deferring it (2026-06-10; design §14: "the Lua leg's early stop
is P5's `lua_before`; its acceptance lands with that trigger"). It is a
**bucket-2 test debt** (`.claude/rules/test-discipline.md` §"Bucket 2"): the
fixture becomes constructible once the named future thing lands — Phase 11 P5's
`lua_before` slot. The wall ships now; the Lua early-stop row is the deferred
coverage, with a SPECIFIC named trigger (not a vague "later").

The P1 s4 matrix row for the Lua early-stop leg notes "deferred → TD-0013".
The producible window-law / resolution-branch fixtures (the reorder, typo,
bare-name, absent-owner, and failed-declarer branches) ship in P1 s4; only the
Lua early-stop out-of-window leg is deferred here.

## Closure blocker

**Phase 11 P5's `lua_before` slot lands** — the first Lua early stop (a Lua
entry that runs BEFORE the main Lua wave). It is a named, designed future
capability (`phase-11-shim-vm/phase-05-startup-sequence-contract/`), not a
vague trigger.

Once `lua_before` exists: add a fixture in which a plugin's `lua_before` entry
sets a PLUGIN-tier behavior and asserts the out-of-window teaching error
(symmetric with the C++ early-stop leg P2 builds — the same wall, the same
error wording: "plugin behaviors resolve at the main stop; set from your main
entry"), plus the catalog-tier counterpart (a `lua_before` set on a
`kcdx.behavior.*` name RESOLVES — settable from any stop). The fixture row
flips from DEFERRED to GREEN; this TD closes (move to `closed/` + reindex per
`doc-organization.md`).

## Activity log

- **2026-06-11** — Initial filing. P1 s4 (window law + resolution errors)
  shipped the out-of-window wall + the producible resolution-branch fixtures;
  the Lua early-stop out-of-window fixture deferred with user approval, named
  trigger = Phase 11 P5's `lua_before` slot (design §14). The C++ early-stop
  fixture is NOT here — it rides P2 s2 (when the C++ behavior interface exists);
  this TD is the Lua leg only.

## What this entry does NOT do

- Does not double as a bug report — the window-law wall is built and correct;
  this is a deferred *coverage* gap for one early-stop caller that does not
  exist yet, not a runtime defect.
- Does not block any current capability — every behavior surface that exists
  today (declare/set/get/list, the apply boundary, the resolution branches) is
  LIVE and tested; only the Lua early-stop trip of the wall is unexercised,
  because no Lua early stop exists.
- Does not cover the C++ early-stop fixture — that is P2 s2's deliverable (the
  C++ behavior interface), not deferred debt.
- Closure is appended by the skill that lands the `lua_before` early-stop
  fixture row (the Phase 11 P5 build, or the next behavior window-law step),
  which then moves this file to `closed/` + reindexes per `doc-organization.md`
  — never at filing.
