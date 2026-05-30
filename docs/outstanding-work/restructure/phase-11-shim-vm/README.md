# Phase 11 — force-load WHGame.dll + use its compiled Lua

**Status: BLOCKED** on the FIX A Lua-symbol harvest. Detail:
[`../00-original-plan.md`](../00-original-plan.md) §"Phase 11" + the four sub-phase
sections; unblocker spec at [`../fix-a-drop-static-lua.md`](../../fix-a-drop-static-lua.md).

kcdx.dll's DllMain force-loads WHGame.dll, then uses the FIX A symbol shim to spin
up the Lua VM via WHGame's compiled `luaL_newstate`. ONE compiled Lua body in the
process (WHGame's) — the dual-Lua sentinel hazard dies by construction (kcdx has no
compiled Lua of its own post-FIX-A). Lua plugins gain the `before_game` zone.

## Blocker

FIX A symbol harvest must hit 100% (~110 Lua RVAs verified, Address Library
populated). At last writing ~38% mapped (`_research/phase8-fix-a/`). Phases 1–10
run in parallel with the remaining harvest; Phase 11a starts when FIX A reports
done.

## Step ledger

| Step | Status | Commit |
|---|---|---|
| [11a — FIX A shim integration](step-1-shim-integration.md) | BLOCKED — on FIX A 100% | — |
| [11b — force-load WHGame.dll from kcdx.dll DllMain](step-2-force-load.md) | BLOCKED — on 11a | — |
| [11c — Lua VM startup via shim, hook game's luaL_newstate](step-3-vm-startup.md) | BLOCKED — on 11a | — |
| [11d — lift Lua-in-before_game restriction, drop static Lua](step-4-drop-static-lua.md) | BLOCKED — on 11a–c | — |

## Why this kills the hazard

The dual-Lua sentinel hazard exists because two compiled Lua bodies (kcdx's
static-linked + WHGame's) operate on one `lua_State`, each comparing against its
own `.rdata` sentinels. After FIX A, kcdx has NO compiled Lua — all forwarded
through the shim to WHGame's functions. One body, one sentinel set, hazard
impossible. (The bidirectional sentinel hazard — KI-0001's reverse direction —
also retires here.)

## First consumer

The bugsplat-filename-fix builtin DLL (deferred from Phase 4) — the canonical
"intercept a function in a non-WHGame DLL, mutate a string arg, call original"
case, via before_game hooks ([`../before-game-hooks.md`](../../before-game-hooks.md)).
