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

That said: if you *only* need byte patches, [kcd2-mempatch][mp] is
the lighter tool — smaller, faster to load, ships independently.
Use kcdx when you have a byte patch *and* something else (a hook,
a console command, save data) in the same plugin folder, or when
you'd rather learn one mental model instead of two.

[mp]: https://github.com/violetanvil/kcd2-mempatch

## Two engines, one workspace

kcdx is the heavyweight cousin of
[**kcd2-mempatch**](https://github.com/violetanvil/kcd2-mempatch).
mempatch handles **declarative same-length byte rewrites** — flip
three bytes via a TOML file, no code needed, full pre-flight safety
checks. kcdx handles **everything else**: code injection, hooks,
plugin lifecycle, save serialization.

Pick based on what you need:

| Need | Use |
|---|---|
| Flip a few bytes in `WHGame.dll` | mempatch |
| Hook a function (before/after/skip) | kcdx |
| Allocate executable memory and call into it | kcdx |
| Register a console command | kcdx |
| Subscribe to game-load / save events | kcdx |
| Persist data across saves | kcdx |
| Expose new functions to KCD2's Lua | kcdx |
| All of the above | Ship both a `mempatch.toml` and a `kcdx.toml` (or DLL); the two engines coexist. |

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
| 5e | `kcdxScriptingInterface` — C++ DLLs register Lua-callable functions via function-pointer struct (no exported Lua C API from kcdx.asi) | **live-verified** |
| 5f | `[[hook]] lua_callback` schema (TOML hook dispatches to pak-Lua function) | **live-verified** |
| 5g | `[[mid_hook]]` schema (mid-instruction hook with register capture) — partial: schema + capture work, "skip-original" semantics blocked on MinHook design limit; v0.2 needs new primitive | **partial, design limit documented** |
| 5h | `kcdxMemoryInterface` (C++ DLL surface mirroring `kcdx.memory.*` — ScanPattern, Read/WriteBytes, GetModuleBase) + dev-mode-gated test suite + `kcdxMessage_LuaReady` + modder-UX trace gaps | **live-verified** |
| 6 | Save/load + `kcdxSerializationInterface` (`.kcdx` co-save) | not started |
| 7 | Address Library + console commands (`[[command]]`) | not started |
| 8 | Docs + examples + v0.1.0 release | not started |

Test suite reporting `12/13 passing` on every dev-mode boot — see
[`test-plugins/README.md`](test-plugins/README.md) for the live
matrix. The single deferred FAIL is CAP-03 awaiting a boot-firing
hook target (see [`docs/design-gaps.md`](docs/design-gaps.md) for
the broader Phase 5 follow-up list).

**Authoritative v0.1 spec:** [`docs/design.md`](docs/design.md). The
full TOML schema, every C++ interface signature, the lifecycle
message catalog, the symbol-table contract, the conflict matrix,
and worked examples for each entry type all live there. Read that
first if you're writing a plugin or contributing to the engine.

## Installation (placeholder)

Same install model as kcd2-mempatch: drop `dinput8.dll` next to
`KingdomCome.exe` in `<game>/Bin/Win64MasterMasterSteamPGO/`, drop
`kcdx.asi` and any plugin folders into the `plugins/` subdir
alongside it. Runtime layout:

```
<game>/Bin/Win64MasterMasterSteamPGO/
├── KingdomCome.exe                   (vanilla)
├── dinput8.dll                       (Ultimate-ASI-Loader)
└── plugins/
    ├── kcdx.asi                      (this engine)
    ├── kcdx.log                      (runtime log)
    ├── <your-plugin>/
    │   ├── kcdx.toml                 (declarative path)
    │   └── <your-plugin>.dll         (C++ plugin path)
    └── <other-plugin>/
        ├── mempatch.toml             (mempatch coexists)
        └── kcdx.toml                 (paired plugins ship both)
```

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
- **[kcd2-mempatch](https://github.com/violetanvil/kcd2-mempatch)**
  — sibling project. kcdx's locator pipeline (`pattern` / `context`
  / `anchor_string` / pre-flight conflict detection) is copied from
  mempatch, MIT-licensed.
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
