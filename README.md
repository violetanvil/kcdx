# kcdx

> **⚠️ Unmaintained — offered to the community.**
>
> I no longer work on kcdx. The source is here, MIT-licensed, for
> anyone who wants to pick it up, fork it, or salvage parts of it.
> There is no maintainer, no release, and no support. **Read
> [Status](#status) before you build on it** — it is a working engine
> with a known unresolved boot failure in its newest subsystem.
>
> Forks are welcome and need no permission. If you take it somewhere,
> the license is MIT — do what you like.

---

**kcdx is an SKSE-class extender for Kingdom Come: Deliverance II.**
Function hooks, trampolines, console commands, save/load
serialization, inter-plugin messaging. Two first-class authoring
languages — **Lua** (a `plugin.lua`, no compiler) and **C++** (a
plugin DLL), at feature parity — and the C++ surface deliberately
mirrors SKSE / F4SE so anyone who's shipped an SKSE plugin can pick
this up in an hour.

## How you write a plugin

A plugin is a **folder**. It carries a **manifest-only** `kcdx.toml`
— identity, entrypoints, and load-order, with **no behavior in it**
— plus a `plugin.lua` and/or a DLL. All behavior is **code**, written
against the `kcdx.*` surface.

The smallest working Lua plugin is a two-key manifest and a one-line
script:

`kcdx.toml`:

```toml
[plugin]
name    = "kcdx.my-first-plugin"
version = "0.1.0"

[entrypoints]
lua = "plugin.lua"
```

`plugin.lua`:

```lua
kcdx.on("ready", function()
    kcdx.log.info("MYMOD", "hello from my first plugin")
end)
```

Drop the folder in `kcdx-plugins/`, launch. Everything an author
calls hangs off the single `kcdx` global: the core actions you do
to register intent with the engine are `kcdx.<verb>`; everything
else is a grouped domain, `kcdx.<domain>.<verb>`. When you are
*configuring* something you pass a `{ named = table }`; when you are
just *doing* something you pass positional args.

### What kcdx handles — and the call that does it

| Need | Surface |
|---|---|
| Hook a function (before / after / around / replace / mid / callsite) | `kcdx.hook{}` |
| Flip bytes in `WHGame.dll` (same-length rewrite) | `kcdx.bytes{}` |
| Edit code statically, zero per-call cost | `kcdx.statement.*` + `kcdx.locator.*` + `kcdx.op.*` |
| React to load / save / lifecycle events | `kcdx.on(event, fn)` |
| Register a console command | `kcdx.command{}` |
| Cross-plugin events | `kcdx.publish(event, payload)` + `kcdx.on("<plugin>:<event>", fn)` |
| Read/write memory, call game functions | `kcdx.memory.*` |
| Address Library lookup (name → pointer) | `kcdx.addr.*` |
| Allocate code / trampolines / extension points | `kcdx.code{}` |
| Persist data across saves | `kcdx.cosave.*` |
| AOB scan (runtime verb + `kcdx_scan` console command) | `kcdx.scan{}` |
| Find a game function from what you know about it | `kcdx.find{}` (dev-mode) |
| Name a code site the engine doesn't name yet | `targets.toml` |
| Expose your own DLL's functions for other mods to hook | `kcdx.dll.declare(...)` |
| Declare / set a named behavior | `kcdx.behavior.*` |
| Read a game CVar | `kcdx.cvar.*` |

Every row above is **live and callable today**, each exercised by a
plugin in the regression suite. What is *not* built is the gameplay
convenience layer — `kcdx.player.*`, `kcdx.world.*`, `kcdx.quest.*`,
`kcdx.inventory.*`, `kcdx.dialogue.*`, `kcdx.assets.*` — which was
roadmap only (see [`docs/lua/planned.md`](docs/lua/planned.md)).

### The engine does the heavy lifting

**You declare WHAT you want; the engine resolves the WHERE and HOW.**
`kcdx.hook{ target = "IsInCombat" }` gives you the address **and** the
verified ABI from the name alone — you write no hex and no signature.
Raw byte patterns, RVAs, and hand-written signatures are an
expert-only escape hatch for targets the Address Library can't name
yet, never the common path.

A real hook is just as short:

```lua
kcdx.hook{
    name   = "boost_score",
    target = "Score",                      -- name supplies address AND signature
    after  = function(ret) return ret + 1000 end,
}
```

**Full API reference: [`docs/lua/index.md`](docs/lua/index.md)** — every
verb, domain, accessor, argument, call shape, and error mode, with
the full manifest schema.

## SKSE parity — Lua and C++ are both first-class

The C++ plugin DLL surface mirrors SKSE / F4SE, with `kcdx`
substituted for `SKSE`. A plugin DLL exports `kcdxPlugin_Load` (entry)
and declares its identity in the manifest's `[plugin]` block. The
interface family is `kcdxMessagingInterface`, `kcdxTaskInterface`,
`kcdxTrampolineInterface`, `kcdxSerializationInterface`,
`kcdxConsoleInterface`, `kcdxScriptingInterface` (the SKSE Papyrus
interface's equivalent, binding KCD2's Lua VM), plus an in-box Address
Library. Lifecycle messages keep the `kcdxMessage_*` names
(`PostLoad`, `PostPostLoad`, `InputLoaded`, `NewGame`, `PreLoadGame`,
`PostLoadGame`, `SaveGame`, `LoadGameSelected`, `DeleteGame`) so SKSE
authors don't relearn anything.

Lua and C++ are **two expressions of one model at feature parity** —
same concepts, same names, each idiomatic in its language. Anything
you can do in Lua you can do in C++ and vice-versa; neither is the
"real" surface. (Parity is not complete as shipped: the newer
`kcdx.hook`-family verbs landed in Lua first, and the C++ backfill
was never finished — see [Status](#status).)

Concrete improvements over the SKSE design:

1. **Inter-plugin conflict detection.** kcdx detects when two plugins
   touch the same bytes or function and mediates by load order with
   plain-English log lines naming the plugins. SKSE silently lets
   plugins clobber each other.
2. **Two first-class authoring languages — no behavior-in-TOML.**
   Write your whole mod in Lua (`plugin.lua`, no compiler) or in C++
   (a DLL), at parity. The `kcdx.toml` is a manifest only — identity,
   entrypoints, load-order — so there is no lossy "declarative
   behavior" tier to outgrow.
3. **Public `EnumeratePlugins()` API.** SKSE deliberately hides the
   loaded-plugin list; kcdx exposes it for conflict diagnostics and
   config UIs.
4. **Optional dependency graph.** Plugins declare `[[plugin.dependencies]]`
   in the manifest; the loader topologically sorts before issuing
   `kcdxPlugin_Load` calls.
5. **In-box Address Library, plus the tool that maintains it.** SKSE's
   Address Library is a separate community mod; kcdx ships the address
   database alongside the engine itself — surfaced to Lua as
   `kcdx.addr.*` and to C++ as `ResolveAddress` / `ResolveAddressByName`.
   The curated data lives in [`data/db-export/`](data/db-export/), and
   [`data/maintainer-tool/`](data/maintainer-tool/) is the web app that
   edits it: a browsing/editing UI over the entries, with a verification
   engine that checks an authored entry against a real game DLL on your
   own machine (the DLL never leaves it). That is how a new game build
   gets absorbed — refresh the data, not the code.

## Status

**Development stopped mid-restructure.** This section is the honest
inventory for anyone deciding whether to pick it up.

### What works

The Lua authoring surface is essentially complete — every row in the
table above is callable, and the regression suite carries **107
capability plugins** exercising them against a live game.

That covers hooking in six modes (before / after / around / replace /
mid-function / callsite redirect), byte patching, static code editing
via the locator+op model, trampolines and code allocation, per-save
persistence, AOB scanning, console commands, the full game-lifecycle
event set, cross-plugin pub/sub messaging, multi-file plugins with
`require` and per-file attribution, author-declared targets, named
behaviors, and the `kcdx.find` function-discovery workbench.

Around it: the launcher and engine injection, a plugin loader with a
dependency graph, inter-plugin conflict detection (two mods touching
the same bytes or function are detected and mediated by load order,
reported in plain English), the in-box Address Library with per-game-
version and per-DLL-version resolution, and a watchdog that bundles
logs plus crash artifacts when the game dies.

### What is not built

The **gameplay convenience domains** — `kcdx.player.*`, `kcdx.world.*`,
`kcdx.quest.*`, `kcdx.inventory.*`, `kcdx.dialogue.*`, `kcdx.assets.*`
— were roadmap and never got binders. Do not write code against them
(see [`docs/lua/planned.md`](docs/lua/planned.md)).

**C++ parity was in progress, not finished.** Sixteen interfaces ship
(`kcdxMessagingInterface`, `kcdxTaskInterface`, `kcdxTrampolineInterface`,
`kcdxSerializationInterface`, `kcdxConsoleInterface`, `kcdxScriptingInterface`,
`kcdxBytesInterface`, `kcdxHookInterface`, `kcdxStatementInterface`,
`kcdxBehaviorInterface`, `kcdxMemoryInterface`, `kcdxDeclareInterface`,
`kcdxDllInterface`, `kcdxFunctionsInterface`, `kcdxAssetInterface`, and the
root `kcdxInterface`), and per-save co-save is built on both surfaces.
But several Lua-first capabilities still carry a tracked C++ parity
debt — the locator-based deferred byte-rewrite model, the
`dynamic_call` / `dynamic_hook` peers, and the newer locator/op/find
mirrors. Each NYI marker is recorded in the file it belongs to; start
at [`docs/cpp/planned.md`](docs/cpp/planned.md) for the full list.

### The known blocker

The most recent work was a **filesystem takeover** — routing the
engine's file operations through kcdx so plugins could override game
assets. It is **incomplete and currently breaks boot**: with the
takeover enabled the game reaches a state where audio runs but no
frame is ever presented and input is dead — a running process that
never renders. The cause was not identified before work stopped.

If you are picking this up, the pragmatic starting point is to build
with the filesystem takeover disabled; everything in "What works"
above predates it and is unaffected.

### Where to look

- **"What can I call today?"** →
  [`docs/lua/index.md`](docs/lua/index.md) (Lua) and
  [`docs/cpp/index.md`](docs/cpp/index.md) (C++). If a verb is documented
  there it is built and callable; the only not-built surface is the
  gameplay convenience layer in [`planned.md`](docs/lua/planned.md), plus
  the C++ parity debts in [`docs/cpp/planned.md`](docs/cpp/planned.md).
- **"What does the regression suite cover?"** → the plugin folders under
  [`test-plugins/`](test-plugins/). Each is a self-contained plugin whose
  `kcdx.toml` documents the capability it exercises; they run in dev mode
  against a live game (see [`docs/dev-mode.md`](docs/dev-mode.md)).
- **"How does the loader work?"** →
  [`docs/loader-architecture.md`](docs/loader-architecture.md).

## Installation (v0.2 layout)

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

**Migrating from a v0.1 install**: uninstall the old ASI-loader files
(`dinput8.dll` and the `kcdx.asi` under `plugins/`), then reinstall per
the steps above. Existing plugin folders carry forward into
`kcdx-plugins/`.

See [`docs/loader-architecture.md`](docs/loader-architecture.md) for
the rationale behind the new layout.

## Compatibility

kcdx was developed against KCD2 `release_1_5_1164953_841` and is not
known to have been tested on any later build. Addresses and AOB
signatures will need refreshing on a newer game version; abort
messages are explicit and the game launches normally if a signature
breaks. Refreshing them is the first maintenance task a fork inherits
— and the system was built for exactly that: the entries live as data
in [`data/db-export/`](data/db-export/), and the maintainer tool
verifies each one against the DLL you point it at, so absorbing a new
game build is a data update rather than a code change. That design
was never exercised against a real post-development patch, so treat
it as untested rather than proven.

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
