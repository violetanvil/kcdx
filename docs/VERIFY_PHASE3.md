# Verify Phase 3 — messaging + task + lifecycle

Phase 3 ships the inter-plugin pub/sub message bus, the main-thread
task queue, and the engine-fired lifecycle messages.

Static verification complete:

- `kcdxMessagingInterface` + `kcdxTaskInterface` published via
  `kcdxInterface::QueryInterface`. Both interface IDs / versions are
  defined in `include/kcdx/Interfaces.h`.
- Lifecycle messages fired:
  - `kcdxMessage_PostLoad` from `DiscoverAndLoad` after the load wave
  - `kcdxMessage_PostPostLoad` from `DiscoverAndLoad` after PostLoad
    handlers ran
  - `kcdxMessage_InputLoaded` from `HookedUpdate` on the first tick
  - `kcdxMessage_{NewGame,PreLoadGame,PostLoadGame,SaveGame,DeleteGame}`
    declared in the enum but NOT YET FIRED (Phase 6).
- Hooked update tick drains the task queue every frame.
- Both registries are mutex-protected — safe to call from any thread.
- Binary inspection shows expected strings:
  `Firing kcdxMessage_PostLoad`, `Firing kcdxMessage_PostPostLoad`,
  `Firing kcdxMessage_InputLoaded`, `Messaging::RegisterListener`,
  `Messaging::Dispatch`, `Task::AddTask`.
- Updated `examples/hello-plugin/` subscribes to engine messages and
  submits a `HelloTask` to demonstrate the surface.

## Steps

1. **Confirm both binaries are built:**

   ```powershell
   cd "C:\Users\Michael\Documents\KCD2 Mods\kcdx"
   pwsh .\build.ps1
   cd examples\hello-plugin
   cmake --build build --config Release
   ```

2. **Install:** the staged artifacts in `kcdx/test-install/plugins/`
   should be copied to the live game's `plugins/` folder:

   ```powershell
   $Stage = "C:\Users\Michael\Documents\KCD2 Mods\kcdx\test-install\plugins"
   $Live  = "E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\plugins"
   Copy-Item -Force "$Stage\kcdx.asi" "$Live\kcdx.asi"
   if (-not (Test-Path "$Live\hello-plugin")) { New-Item -ItemType Directory -Path "$Live\hello-plugin" | Out-Null }
   Copy-Item -Force "$Stage\hello-plugin\hello-plugin.dll" "$Live\hello-plugin\hello-plugin.dll"
   ```

3. **Open DebugView** (Sysinternals) BEFORE launching the game.
   The hello-plugin uses `OutputDebugStringA`, which appears in
   DebugView but not in the standard kcdx.log / `-console` window.

4. **Launch the game with `-console`** (Steam launch option).

5. **Expected `kcdx.log` lines** (timestamps differ):

   ```
   [HH:MM:SS][INFO] kcdx.asi loaded
   ...
   [HH:MM:SS][INFO] Hooks installed: lua_pcall + update
   [HH:MM:SS][INFO] Detected KCD2 runtime version: 1.5.1164953 (encoded 0x010579D9, source: kcd_launcher.log)
   [HH:MM:SS][INFO] Plugin DLL loader: discovered 1 candidate(s)
   [HH:MM:SS][INFO] Loaded plugin 'violetanvil.hello-plugin' (version 0x00010000) from <path> [handle=0]
   [HH:MM:SS][INFO] Plugin 'violetanvil.hello-plugin' kcdxPlugin_Load OK
   [HH:MM:SS][INFO] Plugin DLL loader: 1 of 1 plugin(s) loaded successfully
   [HH:MM:SS][INFO] Firing kcdxMessage_PostLoad...
   [HH:MM:SS][INFO] Firing kcdxMessage_PostPostLoad...
   ...
   [HH:MM:SS][INFO] First update tick with live lua_State — registering KCDX + applying patches
   [HH:MM:SS][INFO] Firing kcdxMessage_InputLoaded...
   ```

6. **Expected DebugView output** (interleaved with the above):

   ```
   [hello-plugin] kcdxPlugin_Load called
   [hello-plugin] my handle is 0, engine version 0x00010000, game version 0x010579D9
   [hello-plugin] 1 plugin(s) loaded total
   [hello-plugin] subscribed to engine messages
   [hello-plugin] submitted a HelloTask
   [hello-plugin] engine message: PostLoad (type=1)
   [hello-plugin] engine message: PostPostLoad (type=2)
   [hello-plugin] HelloTask::Run on main thread
   [hello-plugin] engine message: InputLoaded (type=3)
   ```

   The first six lines are from `kcdxPlugin_Load`. The next two
   (`PostLoad`, `PostPostLoad`) fire AFTER `kcdxPlugin_Load`
   returns but BEFORE the first update tick. `HelloTask::Run` fires
   on the first update tick (main thread), and `InputLoaded` fires
   at the same moment (also on the main thread, immediately before
   the task queue drains).

7. **Functional verification:** the outfit-swap patch from Phase 1
   should STILL work — Phase 3 didn't touch the patch engine, but
   it's good to confirm regression-free.

8. **Clean up:** remove the test-install symlink copies, restore
   mempatch if you want.

## Failure modes

- **DebugView shows nothing** → hello-plugin.dll didn't load. Check
  the kcdx.log for `Plugin DLL loader: discovered 0 candidate(s)`.
  Could be a folder layout problem or a missing
  `kcdxPluginVersionData` export.
- **Lifecycle messages don't fire** → check the kcdx.log for
  `Firing kcdxMessage_*` lines. If they're there but DebugView
  doesn't show the corresponding `engine message:` lines, the
  RegisterListener call probably failed (check the
  `subscribed to engine messages` line).
- **HelloTask::Run doesn't appear in DebugView** → task queue
  isn't draining on update tick. The first tick should have it.

## What this test does NOT prove

- Plugin-to-plugin messaging (needs two example plugins; we only
  have one). The infrastructure is built and tested via the engine's
  own `FireEngineMessage`, but inter-plugin Dispatch is exercised
  only by the listener-resolution path, not by end-to-end.
- Task latency under load (we run one task on the first tick).
- The `kcdxMessage_NewGame` / `kPreLoadGame` / `kPostLoadGame` /
  `kSaveGame` / `kDeleteGame` messages — Phase 6 (Ghidra session
  for save/load entry points) fires those.
