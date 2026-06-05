---
id: TD-0007
opened: 2026-06-05
status: Open
area: Phase 11 Lua shim (src/lua_shim.cpp)
closure_gate: Phase-11 P5 (drop static Lua) — classify before P5 lands
owner: Phase-11
commit_at_filing: e8c6b11fb6b44aae81fe74c2ab2ec151192057cc
affected_sites:
  - src/lua_shim.cpp  (the 3 names left in the stub-seam, neither forwarded nor stubbed)
related:
  - the Phase 11 tree docs/outstanding-work/restructure/phase-11-shim-vm/ (P2 step 1 builds the shim; P5 is the blocker)
---

# TD-0007 — three standard Lua C API functions are unclassified (neither seeded nor catalogued)

## Context

Phase 11's FIX A symbol shim (`src/lua_shim.{h,cpp}`) forwards each `LUA_API`/
`LUALIB_API` symbol to WHGame.dll's compiled Lua by resolving its canonical name
through `refdb::ResolveAddrByName`. The harvest
(`docs/outstanding-work/fix-a-drop-static-lua.md`) classified the Lua API into two
buckets: **resolved** (seeded by name, forwarded) and **inlined/stripped**
(catalogued with a per-function kcdx-side stub strategy).

Building P2 step 1 surfaced a THIRD bucket the design did not account for:
**`luaL_loadbuffer`, `luaL_loadstring`, `luaL_gsub` are NEITHER seeded in the
Address Library NOR listed in the harvest doc's inlined/stripped catalogue.** They
are unclassified — the shim can neither forward them (no seed row → `ResolveAddrByName`
returns 0) nor stub them per a verified strategy (not in the catalogue).

As built, P2 step 1 forwards the **90** seeded `LUA_API`/`LUALIB_API` symbols
correctly and leaves these 3 in the shim's stub-seam (skipped by `Resolve()`, not
yet a required-miss). This is correct for the forward layer; the 3 are carried debt.
The design §6.1 headline count "93 resolved" is corrected to **"90 seeded + 3
unclassified (this TD) + ~24 stubbed."**

## Closure blocker

**Phase 11 P5 (drop static Lua).** Today the shim coexists with kcdx's
static-linked vendored Lua, so an unresolved symbol is harmless (the static copy
serves it). At P5, `vendor/lua/*.c` is dropped — every Lua symbol the engine or a
plugin needs must then resolve-by-name or have a verified stub, or
`lua_shim::Resolve()` bails loud (and kcdx refuses to touch the VM). So: **before P5
lands, a `/research-disassembly` pass must classify each of the 3.**

Per-function path (the lauxlib wrappers are almost certainly inlined, but UNVERIFIED
— `results-driven`: classify, don't assume):
- **`luaL_loadbuffer` / `luaL_loadstring`** — thin lauxlib wrappers over `lua_load`,
  which IS seeded (id 66). If resolved in WHGame.dll → seed each (AP18-gated, per
  entity). If inlined-by-PGO → add a stub to P2 step 2's catalogue (reconstructable
  over the seeded `lua_load`).
- **`luaL_gsub`** — a lauxlib string helper. Same fork: seed if resolved, stub if
  inlined.

Closure = each of the 3 classified + the shim forwards or stubs it + P2 step 2's
catalogue / the seed reflects it + this TD moves to `closed/`. Must complete BEFORE
P5's drop-static-Lua step lands.

## Activity log

- **2026-06-05** — Initial filing. Surfaced during P2 step 1 (the shim forward
  layer) when the subagent verified the actual seed (90 resolvable) against the
  design's "93" headline. User decision: track as tech-debt with the P5 blocker
  (not block step 1, not classify-now, not scope-out).

## What this entry does NOT do

- Does not double as a bug report — the shim is not defective; the 3 are unbuilt
  classification, carried on purpose with a named blocker.
- Closure is appended by the skill that lands the fix (the `/research-disassembly`
  classification + seed/stub work, then `/execute`), which moves this file to
  `closed/` + reindexes per `doc-organization.md` — never at filing.
