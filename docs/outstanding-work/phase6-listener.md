# Phase 6 — CryEngine IGameFrameworkListener (deferred to v0.2)

**Status:** explicitly deferred 2026-05-19. Phase 6 ships via four function-entry detours on `wh::framework::C_SaveGameManager` for v0.1.

## Trigger to revisit

1. **Cross-check trigger.** If live-test of the four detours reveals surprising fire semantics (e.g. `LoadGame` returns before deserialization, making function-entry hook the wrong frame — Open Question #1 in `_research/phase6-save-load/SAVE-LOAD-CANDIDATES.md`), the listener probe becomes a fast diagnostic step.
2. **v0.2 `[[event]]` schema generalization.** When `[[event]]` stops being Lua-callback-only and starts subscribing to engine messages declaratively, the listener path is the natural internal fan-out mechanism for messages that have a CryEngine equivalent. The gEnv resolver stops being one-off probe code and becomes shared infrastructure for `gEnv->pConsole` (`[[command]]`) and `gEnv->pScriptSystem` (alternate Lua-state capture).

## Why deferred

1. **Coverage gap.** Listener only fires `OnSaveGame` / `OnLoadGame`. No `OnDeleteGame`, no `OnPreLoadGame`. Two of the four `kcdxMessage_*` lifecycle messages have no listener equivalent.
2. **Infrastructure cost.** ~400 LOC of new kcdx glue:
   - gEnv resolver via muyuanjin's `"exec autoexec.cfg"` string-anchor chain
   - `IGame::CompleteInit` vtable hook
   - `IGameFrameworkListener`-derived class with 3 virtual slots
   - CryEngine type headers from `_research/predecessor-sigs/muyuanjin-kcd2db/external/cryengine/`
3. **Schema gap.** No declarative TOML path for plugin authors to subscribe to a framework listener event today.

## When implementing

Promote gEnv resolution to first-class subsystem at `src/gEnv.{h,cpp}` rather than glue specific to save/load. Multiple v0.2 features depend on it.

## Full design-gaps entry

`docs/design-gaps.md` §10 — "CryEngine `IGameFrameworkListener` second-source-of-truth for save/load".
