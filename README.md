# kcdx

> **Are you a player who just wants to install a mod?** This GitHub
> repo is the engine source. To install kcdx as a user, grab it from
> [Nexus](https://www.nexusmods.com/kingdomcomedeliverance2/) (when
> v0.1 ships) or the [Releases page](https://github.com/violetanvil/kcdx/releases).
> The rest of this README is for plugin authors.

---

**kcdx is the SKSE-class extender for Kingdom Come: Deliverance II.**
Function hooks, trampolines, console commands, save/load
serialization, inter-plugin messaging, persistent storage. The
plugin API deliberately mirrors SKSE / F4SE so anyone who's shipped
an SKSE plugin can pick this up in an hour.

**Status: v0.1 in development.** This README documents the planned
v0.1 surface; the implementation is being built phase by phase.
See the [roadmap](#roadmap) below for what's shipping when.

## "I just want to flip three bytes" — simple byte patches work in kcdx too

If you only need a same-length byte rewrite, kcdx accepts the exact
mempatch `[[patch]]` schema, identical semantics, identical safety
checks. No DLL, no Lua, no compiler:

```toml
# plugins/my-mod/kcdx.toml
[[patch]]
name        = "outfit_swap_in_combat"
pattern     = "48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0"
offset      = 13
original    = "44 8A F0"
replacement = "45 31 F6"
```

Drop the folder in `plugins/`, launch. Same as mempatch. Same
pre-flight conflict detection, same locator tiers (`pattern`,
`context`, `anchor_string`), same idempotent re-runs. **You do not
have to learn what a hook is to use kcdx for the simple case.**

## What kcdx handles

A single declarative TOML can flip bytes in `WHGame.dll` (full
pre-flight safety checks, no code needed). Beyond that, kcdx covers
**everything else** through one engine:

| Need | Supported |
|---|---|
| Flip a few bytes in `WHGame.dll` | `[[patch]]` |
| Hook a function (before/after/skip) | `[[hook]]` / DLL |
| Allocate executable memory and call into it | `[[trampoline]]` / DLL |
| Register a console command | `[[command]]` |
| Subscribe to game-load / save events | `[[event]]` |
| Persist data across saves | cosave API |
| Expose new functions to KCD2's Lua | Lua bridge |

## SKSE compatibility — naming and conventions

The C++ plugin API mirrors SKSE / F4SE exactly, with `kcdx`
substituted for `SKSE`. A plugin DLL exports `kcdxPlugin_Version`
(data block) and `kcdxPlugin_Load` (entry function). The interfaces
are `kcdxMessagingInterface`, `kcdxTrampolineInterface`,
`kcdxSerializationInterface`, `kcdxTaskInterface`,
`kcdxScriptingInterface` (the SKSE Papyrus interface's equivalent,
binding KCD2's Lua VM instead). Lifecycle messages keep the SKSE
names (`kMessage_PostLoad`, `kMessage_PostPostLoad`,
`kMessage_InputLoaded`, `kMessage_SaveGame`, `kMessage_PreLoadGame`,
`kMessage_PostLoadGame`, `kMessage_DeleteGame`, `kMessage_NewGame`)
so authors don't have to relearn anything.

## Where kcdx enhances SKSE

Five concrete improvements over the SKSE design, each addressing a
documented SKSE weak spot:

1. **Inter-plugin conflict detection.** kcdx ports the pre-flight
   model from mempatch (incidental / write-on-original /
   write-on-write categories with plain-English log lines naming
   the conflicting plugins). SKSE silently lets plugins clobber each
   other.
2. **Declarative TOML plugin path.** A `kcdx.toml` file can declare
   `[[patch]]`, `[[hook]]`, `[[mid_hook]]`, `[[trampoline]]`,
   `[[command]]`, `[[event]]` entries without writing any C++. The
   Lua-callback path is lossy compared to a real DLL but covers most
   gameplay-tweak use cases.
3. **Public `EnumeratePlugins()` API.** SKSE deliberately hides the
   loaded-plugin list; kcdx exposes it for conflict diagnostics and
   config UIs.
4. **Optional dependency graph.** Plugins may declare
   `dependencies = [...]` in their version data; the loader
   topologically sorts before issuing `kcdxPlugin_Load` calls.
5. **In-box Address Library.** SKSE's Address Library is a separate
   community mod; kcdx ships a small address database alongside the
   engine itself, updated per game patch. API:
   `kcdx::ResolveAddress(uint64_t id)`. Community can contribute IDs
   via PR.

## Roadmap

| Phase | Scope | Status |
|---|---|---|
| 1 | Foundation — `[[patch]]` schema (mempatch-equivalent byte rewrites under `kcdx.toml`) | **live-verified** |
| 2 | Plugin loader — DLL discovery, dependency topo-sort, Preload/Load dispatch. Shape C refactor: plugin identity moved from DLL exports to `kcdx.toml [plugin]` schema. | **live-verified** |
| 3 | Messaging + Task + lifecycle messages | **live-verified** |
| 4a | Trampoline allocator (branch + local pools) + `kcdxTrampolineInterface` + per-plugin log files | **live-verified** |
| 4b.1 | `[[hook]]` schema (raw-bytes function-entry detours via MinHook) | **live-verified** |
| 4b.2 | `[[trampoline]]` schema + cross-plugin symbol table (export / target_symbol) | **live-verified** |
| 4b.3 | Unified conflict matrix + global apply order across all entry types | **live-verified** |
| 5c | Lua marshaling — raw Lua C API only (no sol2), `kcdx.memory.*` namespace, pak-Lua-driven runtime hooks via `dynamic_hook`, address resolution via `cfunction_address` | **live-verified** |
| 5d | Lua VM threading constraint documented (Hard Rule #16); no runtime guard in v0.1 | **documented** |
| 5e | `kcdxScriptingInterface` — C++ DLLs register Lua-callable functions via function-pointer struct (no exported Lua C API from kcdx.dll) | **live-verified** |
| 5f | `[[hook]] lua_callback` schema (TOML hook dispatches to pak-Lua function) | **live-verified** |
| 5g | `[[mid_hook]]` schema (mid-instruction hook with register capture) — schema + capture + three-mode `call_original` (true/false/"auto" with Lua-side `args._skip` runtime decision) | **live-verified** |
| 5h | `kcdxMemoryInterface` (C++ DLL surface mirroring `kcdx.memory.*` — ScanPattern, Read/WriteBytes, GetModuleBase) + dev-mode-gated test suite + `kcdxMessage_LuaReady` + modder-UX trace gaps | **live-verified** |
| 6a | Save/load lifecycle hooks (kSaveGame / kPreLoadGame / kPostLoadGame / kDeleteGame / kLoadGameSelected) on `C_SaveGameManager` + slot-resolver | **live-verified** |
| 6b | `kcdxSerializationInterface` (`.kcdx` co-save format + plugin Save/Load/Revert callbacks) | **live-verified** |
| 7  | Address Library (CSV → compiled-in id→RVA table + `ResolveAddress` + `address_id` TOML locator) + `kcdxConsoleInterface` (IConsole::AddCommand wrapper for plugin-registered console commands) | **live-verified** |
| 8 | Docs + examples + v0.1.0 release | not started |

Test suite reporting **`21/21 passing`** on every dev-mode boot
(verified 2026-05-20 18:32; full pass on `update tick`,
`kPreLoadGame`, `kPostLoadGame`) — see
[`test-plugins/README.md`](test-plugins/README.md) for the live
matrix.

**Authoritative spec (v0.2 in progress):** the restructure plan at
[`docs/outstanding-work/restructure-plan.md`](docs/outstanding-work/restructure-plan.md).
[`docs/design.md`](docs/design.md) is the v0.1 spec and is currently
marked SUPERSEDED — most of its schema/lifecycle/install sections are
being replaced phase-by-phase. Read the restructure plan first if
you're writing a plugin or contributing to the engine.

## Installation (v0.2 layout, Phase 1+)

kcdx ships its own launcher (`kcdx.exe`) — Ultimate ASI Loader is no
longer required. Extract the release zip into
`<game>/Bin/Win64MasterMasterSteamPGO/` and set Steam's launch options
to point at `kcdx.exe`. Runtime layout:

```
<game>/Bin/Win64MasterMasterSteamPGO/
├── KingdomCome.exe                  (vanilla)
├── WHGame.dll                       (vanilla)
├── kcdx.exe                         (LAUNCHER — user runs this)
├── kcdx-README.txt                  (ships in the zip)
├── kcdx-engine/                     (everything kcdx-owned)
│   ├── kcdx.dll                     (engine; injected by launcher)
│   ├── kcdx-watchdog.exe            (crash-bundle sidecar)
│   ├── engine.toml                  (engine config)
│   ├── load_order.toml              (user load-order overrides)
│   ├── address-library/
│   ├── logs/
│   │   ├── kcdx_<ts>.log
│   │   ├── kcdx-dev_<ts>.log
│   │   ├── kcdx-launcher_<ts>.log
│   │   └── crash/
│   │       └── crash_<ts>.zip
│   └── builtin/                     (first-party engine fixes)
└── kcdx-plugins/                    (user/third-party plugins ONLY)
    └── <your-plugin>/
        ├── kcdx.toml
        ├── plugin.lua / <your-plugin>.dll
        └── logs/
            └── <manifest.name>_<ts>.log
```

**Steam launch options**: right-click *Kingdom Come: Deliverance II*
in Steam → Properties → Launch Options, paste the quoted full path
of `kcdx.exe`. Steam's overlay is preserved (kcdx.exe spawns the
game via CreateProcess, keeping Steam's tracking intact).

`kcdx.exe` injects `kcdx-engine/kcdx.dll` via CreateRemoteThread +
LoadLibraryW. If injection fails (Windows Defender / third-party AV),
the launcher logs to `kcdx-engine/logs/kcdx-launcher_<ts>.log` and shows
an actionable error dialog.

`kcdx-watchdog.exe` is a ~280KB sidecar that `kcdx.dll` spawns at
startup — it blocks on the game's process handle (zero CPU) and, on
game crash, zips up logs + crash artifacts into
`kcdx-engine/logs/crash/`. See [`docs/logging.md`](docs/logging.md)
§"Crash bundles" for what's in the zip.

**Migrating from a v0.1 install**: see
[`docs/migration.md`](docs/migration.md) for the uninstall + reinstall
steps. Existing plugin folders carry forward into `kcdx-plugins/`.

See [`docs/loader-architecture.md`](docs/loader-architecture.md) for
the rationale behind the new layout.

## Compatibility

kcdx targets KCD2 `release_1_5_1164953_841` (April 2026) at the time
of writing. Like mempatch, kcdx's AOB signatures for the engine's
own hooks may need refreshing on future game updates; abort messages
are explicit and the game launches normally if a sig breaks.

## Credits

kcdx stands on the shoulders of several other projects:

- **SKSE / F4SE** ([skse64](https://github.com/ianpatt/skse64),
  [CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG))
  — the plugin API conventions kcdx mirrors. Not vendored; just
  named after.
- **[yobson1/kcd2lua](https://github.com/yobson1/kcd2lua)** — ASI
  bootstrap scaffold (MIT). Original code by Oren / ecaii.
- **[ReturnOfModding](https://github.com/return-of-modding/ReturnOfModding)**
  — MIT-licensed source files vendored under `vendor/rom-borrowed/`
  for asmjit-driven Lua-callback marshaling (the `dynamic_hook` and
  `dynamic_hook_mid` bindings). Each vendored file carries an
  attribution header.
- **[muyuanjin/kcd2-mod-docs](https://github.com/muyuanjin/kcd2-mod-docs)**
  — reference disassembly notes for KCD2 reverse engineering.
- **[TsudaKageyu/minhook](https://github.com/TsudaKageyu/minhook)** —
  vendored.
- **[asmjit](https://github.com/asmjit/asmjit)** — vendored. Used by
  the typed-marshaling layer for Lua callbacks.
- **[marzer/tomlplusplus](https://github.com/marzer/tomlplusplus)** —
  vendored.
- **Lua 5.1** sources from CryEngine 5.2.3 SDK — same Lua KCD2 ships
  internally.


## License

MIT. See [LICENSE](LICENSE).
