# Verify Phase 2 — plugin loader

Phase 2 ships the C++ plugin DLL loader: discovery, `kcdxPluginVersionData`
parsing, validation, dependency topo-sort, Preload/Load dispatch.

Static verification already complete:

- `kcdx/include/kcdx/Interfaces.h` published as the public plugin author
  surface. Defines `kcdxPluginVersionData`, `kcdxInterface`,
  `kcdxPluginDependency`, the entry-point typedefs, the version-encoding
  helper, every public enum.
- `kcdx.asi` builds with the loader sources. Binary inspection shows
  `kcdxPlugin_Load`, `kcdxPlugin_Preload`, `kcdxPluginVersionData`, and
  the "Plugin DLL loader" log prefix all embedded.
- `examples/hello-plugin/` builds **standalone** (separate CMake project,
  no kcdx tree linkage — just the public header) to a 135 KB DLL with
  the required `kcdxPluginVersionData` and `kcdxPlugin_Load` exports.

What remains is the in-game dynamic test.

## Steps

1. **Confirm both binaries are built:**

   ```powershell
   cd "C:\Users\Michael\Documents\KCD2 Mods\kcdx"
   pwsh .\build.ps1
   cd examples\hello-plugin
   cmake -S . -B build -G "Visual Studio 17 2022" -A x64
   cmake --build build --config Release
   ```

   Outputs:
   - `kcdx/build/Release/kcdx.asi`
   - `kcdx/examples/hello-plugin/build/Release/hello-plugin.dll`

2. **Install both into the live game:**

   ```
   <game>/Bin/Win64MasterMasterSteamPGO/
     dinput8.dll                          (Ultimate-ASI-Loader)
     plugins/
       kcdx.asi                           (build output)
       hello-plugin/
         hello-plugin.dll                 (the example)
   ```

3. **Launch the game with `-console`** AND attach DebugView from
   Sysinternals (the example uses `OutputDebugStringA`, which appears
   in DebugView but not in the standard `-console` window).

4. **Expected log lines in `kcdx.log`** (timestamps/addresses will
   differ):

   ```
   [HH:MM:SS][INFO] kcdx.asi loaded
   [HH:MM:SS][INFO] module directory: <path>
   [HH:MM:SS][INFO] Discovered 0 patch(es) from 0 config file(s) across 1 plugin folder(s)
   ...
   [HH:MM:SS][INFO] Hooks installed: lua_pcall + update
   [HH:MM:SS][INFO] Detected KCD2 runtime version: 1.5.1164953 (encoded 0x010579D9)
   [HH:MM:SS][INFO] Plugin DLL loader: discovered 1 candidate(s)
   [HH:MM:SS][INFO] Loaded plugin 'violetanvil.hello-plugin' (version 0x00010000) from <path>\hello-plugin.dll [handle=0]
   [HH:MM:SS][INFO] Plugin 'violetanvil.hello-plugin' kcdxPlugin_Load OK
   [HH:MM:SS][INFO] Plugin DLL loader: 1 of 1 plugin(s) loaded successfully
   ```

5. **Expected DebugView output** (from the plugin's
   `OutputDebugStringA` calls):

   ```
   [hello-plugin] kcdxPlugin_Load called
   [hello-plugin] my handle is 0, engine version 0x00010000
   [hello-plugin] 1 plugin(s) loaded total
   ```

6. **Negative test — incompatible game version.** Edit
   `hello-plugin.cpp` and replace the
   `versionIndependence = kcdxVersionIndependent_AddressLibrary` line
   with `versionIndependence = 0`, then add a known-wrong entry:
   `compatibleGameVersions = { 0xFFFFFFFF, 0 }`. Rebuild, reinstall,
   relaunch. Expected log line:

   ```
   [HH:MM:SS][ERROR] Plugin 'violetanvil.hello-plugin' not compatible with running game version 0x010579D9. Its compatibleGameVersions:
   [HH:MM:SS][ERROR]     0xFFFFFFFF
   ```

   And the plugin should be SKIPPED — no `kcdxPlugin_Load OK`,
   no DebugView output.

7. **Negative test — duplicate plugin names.** Copy
   `hello-plugin/` to `hello-plugin-2/` (don't change the source —
   both DLLs export the same `name` string). Relaunch. Expected:

   ```
   [HH:MM:SS][ERROR] Two plugins both export name 'violetanvil.hello-plugin' (<path1> and <path2>) — aborting both.
   ```

   Neither plugin loads.

8. **Clean up**: remove the test plugin folders.

## Failure modes

- **No "Plugin DLL loader: discovered N candidate(s)" line at all** →
  `DiscoverAndLoad` isn't being called. Check `dllmain.cpp` wiring.
- **"missing kcdxPluginVersionData export"** → the plugin's
  `__declspec(dllexport)` isn't producing a properly-named export.
  Verify with `dumpbin /exports hello-plugin.dll`.
- **Plugin loads but `kcdxPlugin_Load` returns false / not OK** →
  the plugin's own logic failed. Check the plugin's OutputDebugString
  trail.
- **`Detected KCD2 runtime version: 0.0.0 (encoded 0x00000000)`** →
  WHGame.dll's VS_VERSIONINFO is missing or unreadable. Plugins
  without AddressLibrary opt-in will all be refused. Known game
  versions in 1.5.x range should report e.g. `0x010579D9`.

## What this test does NOT prove

- Plugin Preload wave (no example exercises it yet)
- Dependency topo-sort with real dependencies (no two-plugin example yet)
- The kcdxInterface sub-interfaces (`Messaging`, `Trampoline`, etc.) —
  those land in Phases 3-7. `QueryInterface` returns null for any
  request in v0.1-phase-2.
- `inlinePatchesToml` — declared in the schema, not yet wired to the
  patch_engine. Land in a follow-up.

Those get verification recipes as later phases ship.
