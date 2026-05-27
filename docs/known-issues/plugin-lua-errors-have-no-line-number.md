# plugin.lua errors surface with no line number or detail

## Symptom

When a plugin's `plugin.lua` throws (typo, nil-index, undefined global,
syntax error), the author sees in the log:

```
plugin.lua:0: [Error] Lua error. Please run with -lua_storedebug 1 to enable lua debug info.
```

No line number, no detail (not even the offending symbol name). An author
debugging their mod is blind — this blocked the in-session diagnosis of
CAP-20-target-nosig twice. Violates the cornerstones.md UX rule "errors
teach WHERE (file:line)". AP12 fix #3.

## Facts (static, verified before any probe)

- The string comes from `luaG_runerror` (seed id 1187, ldebug.c): it tests
  `G(L)->storedebug` at `[g+0x22]`; SET → `luaO_pushvfstring(L, fmt, argp)`
  (real error w/ detail), CLEAR → `luaO_pushfstring(L, "[Error] Lua
  error...")` (the useless string). (seed prose 1187)
- The engine DELIBERATELY clears storedebug: `CScriptSystem::Init` (seed id
  1196) sets `storedebug=0` after `lua_newstate` (which defaults it to 1) —
  "CryEngine memory-save." (seed prose 1196)
- `lua_storedebuginfo(L, enable)` is a real callable Lua C function; kcdx
  exposes it to plugins as `StoreDebugInfo` (scripting_interface.cpp:263,
  Interfaces.h:724) but kcdx NEVER calls it for plugin.lua.
- plugin.lua runs via `lua_plugin_loader.cpp::LoadOneFileGuarded`:
  `luaL_loadfile(L, absPath)` (line 49, COMPILE/parse) then
  `lua_pcall(L,0,0,0)` (line 60, execute), capturing `lua_tostring(L,-1)`
  into `ctx->err`.
- **Parse-time sub-hypothesis:** Lua line info is attached to the chunk at
  PARSE/compile time (`luaL_loadfile`), not at pcall. So storedebug likely
  must be ON *before* `luaL_loadfile` (line 49), not merely before the
  pcall. The probe sets it before loadfile.

## Open questions

**Does setting `lua_storedebuginfo(L,1)` before plugin.lua load restore
file:line + detail in the error, and does the game stay stable?** — the
engine cleared it intentionally for memory.

Probe: in `LoadOneFileGuarded`, BEFORE `luaL_loadfile`: read + log
`G(L)->storedebug` (ground truth — is it really 0?), set it to 1 via
`lua_storedebuginfo(L,1)`, run loadfile+pcall as today, and ALSO add a
deliberate-error throwaway test plugin so an error actually fires. Log the
captured `ctx->err`. (Restore storedebug to its prior value after, so the
engine steady-state is unchanged — the surgical window.)

Outcome map (all equally real):
- **A** — err gains `plugin.lua:<line>: <detail>` AND game boots/stays
  stable → the fix: toggle storedebug ON around plugin.lua load (surgical
  window, restore after). Author gets real line numbers.
- **B** — err gains line+detail BUT game destabilizes / memory blows up →
  the surgical narrow-window form (already what the probe uses) is the fix;
  re-confirm stability with the window kept tight.
- **C** — err STILL has no line → re-observe. The pre-toggle storedebug-byte
  dump tells us if the flag was actually 0 and whether our set took. If it
  flipped and still no line, cause is elsewhere (timing/wrong-state); do NOT
  hop theories — re-observe the raw byte + the raw error.

## Active diagnostic instrumentation

| File | Change | Keep/revert |
|---|---|---|
| (pending probe A) | | |

## Trail

| Action | Result |
|---|---|
| (Phase 1) Static read of luaG_runerror seed prose + LoadOneFileGuarded + StoreDebugInfo binding | Ground truth gathered (see Facts). storedebug is the gate; engine clears it; toggle must precede luaL_loadfile (parse-time line info). |
| PROBE A: log storedebug_was, set lua_storedebuginfo(L,1) BEFORE luaL_loadfile, restore after; throwaway plugin throws a nil-index | **OUTCOME A.** storedebug_was=0 (engine had it off); captured err = `plugin.lua:13: attempt to index global 'kcdx_probe_nil' (a nil value)` — real file:line+symbol. Game booted stable, suite 43/44 (only CAP-04c), no crash/heap/AV. The toggle-before-loadfile + restore is the fix. |

## Resolution

**Cause:** the engine's `CScriptSystem::Init` clears `G(L)->storedebug` (memory-save), so `luaG_runerror` emits the bare `[Error] Lua error...` with no line for any plugin.lua error. **Fix:** in `LoadOneFileGuarded`, set `lua_storedebuginfo(L, 1)` BEFORE `luaL_loadfile` (line info is baked at parse time) and restore the prior value after — a surgical per-plugin-load window that leaves the engine's steady-state `storedebug=0` untouched. PROBE A confirmed this restores `plugin.lua:<line>: <detail>` with no instability (Outcome A). The fix lands by stripping PROBE A's diagnostic logging (keep the set/restore), removing the throwaway probe plugin, and shipping a regression. Commit: (pending /execute).
