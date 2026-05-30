# kcdx migration guide — v0.1 → v0.2

This guide walks through the changes between kcdx v0.1 (the
Ultimate-ASI-Loader-based release) and v0.2 (the restructure). It's
written for two audiences:

- **Mod users** updating an existing install.
- **Plugin authors** updating an existing mod against the new schema.

The restructure is staged across multiple phases. Each section below
flags which phase introduced its change so you can find the
corresponding commit + verification gate in
[`outstanding-work/restructure/`](outstanding-work/restructure/README.md).

## Phase 1 — install layout flip

**For users:**

The launcher model changed. kcdx no longer rides Ultimate ASI Loader;
it ships its own launcher exe.

**Old layout (v0.1):**
```
<game>/Bin/Win64MasterMasterSteamPGO/
├── dinput8.dll                  (Ultimate ASI Loader proxy)
├── plugins/
│   ├── kcdx.asi                 (engine, loaded by ASI loader)
│   ├── kcdx-watchdog.exe
│   └── <plugin>/...
└── kcdx-engine/
    ├── engine.toml
    ├── logs/
    └── builtin/
```

**New layout (v0.2+):**
```
<game>/Bin/Win64MasterMasterSteamPGO/
├── kcdx.exe                     (LAUNCHER — user runs this)
├── kcdx-README.txt
├── kcdx-engine/
│   ├── kcdx.dll                 (engine — injected by launcher)
│   ├── kcdx-watchdog.exe
│   ├── engine.toml
│   ├── load_order.toml
│   ├── logs/
│   ├── address-library/
│   └── builtin/
└── kcdx-plugins/                (user/third-party plugins ONLY)
    └── <plugin>/...
```

**To migrate an existing install:**

1. **Delete the old install:**
   - `<game>/Bin/Win64MasterMasterSteamPGO/dinput8.dll`
   - `<game>/Bin/Win64MasterMasterSteamPGO/dinput8.dll.LICENSE.txt`
   - `<game>/Bin/Win64MasterMasterSteamPGO/plugins/kcdx.asi`
   - `<game>/Bin/Win64MasterMasterSteamPGO/plugins/kcdx-watchdog.exe`
   - `<game>/Bin/Win64MasterMasterSteamPGO/kcdx-engine/` (entire folder
     — its contents are recreated by the new install or persist in
     `kcdx-engine/` after re-running the launcher once)

   **Keep `plugins/<your-plugins>/`** — those are your mods, not kcdx
   binaries. They migrate forward when their authors update them for
   the new schema (Phase 5).

2. **Extract the new release zip** into
   `<game>/Bin/Win64MasterMasterSteamPGO/`. This creates `kcdx.exe`,
   `kcdx-engine/`, and an empty `kcdx-plugins/`.

3. **Reinstall your plugin folders** under the new `kcdx-plugins/`. Same
   folder names; the layout under each plugin folder hasn't changed
   yet (that's Phase 5).

4. **Set Steam launch options**:
   - Right-click *Kingdom Come: Deliverance II* in Steam → Properties
   - Launch Options: set to the full path of `kcdx.exe`, e.g.
     `"E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\kcdx.exe"`
   - Save.

5. **Launch normally via Steam.** The Steam overlay is preserved
   (kcdx.exe spawns the game via CreateProcess, keeping Steam's
   tracking intact).

**If kcdx doesn't load**: check
`<game>/Bin/Win64MasterMasterSteamPGO/kcdx-engine/logs/kcdx-launcher_<ts>.log`.
The launcher logs every step; the failure point is named.

**Why the change**: see
[`loader-architecture.md`](loader-architecture.md). Short version:
owning the launcher means no third-party loader dependency, honest
SKSE-style binary naming, and a clean install footprint where only
`kcdx.exe` sits at the game-bin root.

**For plugin authors:**

Phase 1 doesn't change the plugin schema — your existing `kcdx.toml`
+ DLL still load with all v0.1 entry types working. The TOML schema
flip is Phase 5; the Lua/C++ API additions are Phase 2-3. See those
phases' sections below as they land.

## Phase 5 — manifest-only TOML (lands later)

*[This section will be filled in when Phase 5 ships. Currently placeholder.]*

The seven behavior table-arrays (`[[patch]]`, `[[hook]]`,
`[[mid_hook]]`, `[[trampoline]]`, `[[scan]]`, `[[command]]`,
`[[event]]`) are removed from `kcdx.toml`. Plugin behavior moves into
`plugin.lua` (Lua plugins) or `kcdxPlugin_Load` (C++ plugins) via
function-call APIs: `kcdx.hook(...)`, `kcdx.bytes(...)`, etc.

`kcdx.toml` collapses to identity + dependencies + entrypoints. See
[`restructure/00-original-plan.md`](outstanding-work/restructure/00-original-plan.md)
sections "The new TOML manifest shape" and "The Lua API surface".

## See also

- [`outstanding-work/restructure/`](outstanding-work/restructure/README.md) — full restructure spec (phase tree + ledger; the original monolithic plan is preserved at `restructure/00-original-plan.md`)
- [`loader-architecture.md`](loader-architecture.md) — install layout rationale
- [`logging.md`](logging.md) — engine log + per-plugin log conventions
