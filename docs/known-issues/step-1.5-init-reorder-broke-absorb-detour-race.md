# Step 1.5 init reorder lost the SELECT-detour install/fire race

**Status:** Diagnosed, fix pending implementation.
**Bisected to:** commit `d1866f9` (Phase 9.0 step 1.5 — init reorder).
**Repro:** 1/1, every boot post-`d1866f9`.

## Symptom

Pak mods don't mount. User observation: FastLaunch (the installed intro-skip mod) failed — vanilla intro plays. Every other pak mod (cheat, easytoseeherbs, kcdx_test_paklua, lua_memory_verify, luck_laid_bare, mh_rebalanced_sharpening) is also unmounted. Plugins load via kcdx's own loader (unaffected), but their Data/*.pak (if any) wouldn't mount either via the absorb path.

## Facts (verified, not inferred)

- Pre-`d1866f9` logs (12:46/13:32/13:36/13:58 today): `[MOD_ABSORB] enabled_list_built count=71 vanilla=7 plugins=64 dropped=0` — production absorb fires, every pak mod gets a `takeover_record` line + its bytes are read via FOpen.
- Post-`d1866f9` logs (18:18, 18:55, 21:07): ZERO `takeover_record` lines. ZERO `takeover_repoint` lines. ZERO FOpen reads against any `mods\<modname>\Data\*.pak`. The synthetic enabled-list-builder selftest's `count=3` line is the ONLY `enabled_list_built` line per boot.
- The SELECT detour install line (`MOD_ABSORB: ModManager_Select detour installed at <RVA> — mod-loader takeover armed`) IS present post-`d1866f9`. Install succeeds; the detour is registered and enabled.
- Disk is clean — `mods/mod_order.txt` + `kcdx-engine/load_order.toml` contain only real mods/plugins, no selftest pollution.

## Trail

| Action | Result |
|---|---|
| Read `src/mod_absorb/select_detour.cpp`. | `enabled_list_built` log token isn't here — it's inside `BuildEnabledList` (`enabled_list_builder.cpp:197`). Reframed: missing `count=71` line ≠ silent absorb; it means `BuildEnabledList` was never called from the production path. |
| Read `src/mod_absorb/enabled_list_builder.cpp`. | One callsite for production (`select_detour.cpp:120`) + one selftest callsite (`enabled_list_builder_selftest.cpp:135`). Selftest builds `count=3` over synthetic zeta/alpha/p_one — confirms post-reorder logs' `count=3` is the selftest. |
| Read `src/mod_absorb/mod_absorb_e2e_selftest.cpp` + `enabled_list_builder_selftest.cpp`. | Selftests do `ClearRegistry()` + push synthetic mods + `Resolve()` + `BuildEnabledList`, then `restore()` via Snapshot. Disk evidence (clean `mod_order.txt`/`load_order.toml`) **disconfirms** the "selftest pollution leaks to disk" theory. Re-observe. |
| Compare pre-reorder vs post-reorder timestamps for SELECT detour install vs first `takeover_record`. | Pre-reorder: install at `13:58:34.836`, first fire at `13:58:36.653` → **detour available ~1.82s before SELECT runs**. Post-reorder: install at `21:07:43.864` (AFTER `DiscoverAndLoad` ran ~2.1s through ~64 plugins) → **SELECT had already been called by CSystem::Init before the detour was registered, so the detour never fires**. The race window flipped. |

## Diagnosis

Step 1.5 moved `RegisterHandlers` × 2 + `DiscoverAndLoad` + `AdvanceTo(PluginsLoaded)` to BEFORE `InstallSelectDetour`, on the assumption that "plugins loaded before the absorb fires" required moving plugin-load ahead of the install point. But the absorb's **FIRE** happens inside `CSystem::Init` (on the game's main thread, later than the worker thread that runs init); the install is just registering the hook. CSystem::Init calls `ModManager_Select` ~1-2 seconds into game init — and `DiscoverAndLoad` (loading ~64 plugins) consumed ~2.15s of the worker thread between EngineHooksInstalled and the new install point. The detour install lost the race; the engine's original `ModManager_Select` was called before MinHook had the detour in place.

Conflation: "install" treated as if it were "fire." The two are separate — install is registration with MinHook, fire happens whenever the original RVA is called next. Plugin-load must precede the FIRE (so `BuildEnabledList` reads loaded plugins), not the INSTALL.

## Resolution (pending)

Restore `InstallSelectDetour` to its pre-reorder position (right after `hooks::Install` / `AdvanceTo(EngineHooksInstalled)`), AND keep `RegisterHandlers` × 2 + `DiscoverAndLoad` + `AdvanceTo(PluginsLoaded)` AFTER it. The detour fires inside `CSystem::Init` (later, on the main thread), which is after `DiscoverAndLoad` finishes on the worker thread regardless — so when `HookedSelect` runs, `g_manifests` and `Registry()` are both populated and `BuildEnabledList` returns the real ~71-record list.

Init phases: `PluginsLoaded` keeps its current numeric position (>= `ModLoaderTakeoverArmed`) for the `AdvanceTo` monotonic gate. Either renumber the enum back, or accept that `ModLoaderTakeoverArmed` advances before `PluginsLoaded` (the install IS in fact earlier; the model accurately reflects the new call order). The latter is the cleaner choice — the phase model should mirror reality, not the goal.

## Open questions

None. The cause is pinned by static reads + timestamp comparison; no live probe required (results-driven §4: static evidence sufficed). Fix is direct.

## Active diagnostic instrumentation

None. The investigation used only existing log lines + timestamps. No code added to the engine. Live install is disarmed via the pre-reorder DLL (rebuilt from commit `9515576` at `/tmp/kcdx-disarm-9515576`) so pak mods mount normally while the fix lands.
