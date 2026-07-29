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
| Flip bytes in `WHGame.dll` (same-length rewrite) | `kcdx.bytes{}` |
| Hook a function (before / after / around / replace / mid / callsite) | `kcdx.hook{}` |
| React to load / save / lifecycle events | `kcdx.on(event, fn)` |
| Register a console command | `kcdx.command{}` |
| Cross-plugin events | `kcdx.publish(event, payload)` + `kcdx.on("<plugin>:<event>", fn)` |
| Read/write memory, call game functions | `kcdx.memory.*` |
| Address Library lookup (name → pointer) | `kcdx.addr.*` |
| Allocate code / trampolines | `kcdx.code{}` — *planned, not yet built* |
| Persist data across saves | `kcdx.cosave.*` — *planned, not yet built* |
| Diagnostic AOB scan (top-level verb) | `kcdx.scan{}` — *planned; use `kcdx.memory.scan_pattern` today* |

The first seven rows are live today. The last three are on the
roadmap and **not callable yet** — do not write code against them
(see [`docs/lua/planned.md`](docs/lua/planned.md)).

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
5. **In-box Address Library.** SKSE's Address Library is a separate
   community mod; kcdx ships an address database alongside the engine
   itself, updated per game patch — surfaced to Lua as `kcdx.addr.*`
   and to C++ as `ResolveAddress` / `ResolveAddressByName`. Community
   can contribute IDs via PR.

## Status

**Development stopped mid-restructure.** This section is the honest
inventory for anyone deciding whether to pick it up.

### What works

The Lua authoring surface — the `kcdx.*` model above — is live and
exercised by the regression suite: `kcdx.hook` (before / after /
around / replace / mid / callsite), `kcdx.bytes`, `kcdx.on` (the
`ready` event + the 9 game-lifecycle events), `kcdx.command` +
`kcdx.console.execute`, `kcdx.publish` cross-plugin pub/sub,
multi-file plugins (`require`), and the both-phase (before-game /
after-game) execution model in both Lua and C++. The launcher,
engine injection, plugin loading, conflict detection, Address
Library, and crash-bundle sidecar are all built and live-verified.

The dev-mode regression suite was at **58/60 passing** at the last
checkpoint (the remaining two rows are manual save/load gestures, not
failures).

### What is not built

`kcdx.code` (trampolines), `kcdx.cosave` (per-save persistence), and
`kcdx.scan` are **declared but not callable** — do not write code
against them (see [`docs/lua/planned.md`](docs/lua/planned.md)). The
C++ mirror of the newer `kcdx.hook`-family interfaces was the next
parity backfill and was never done.

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
  [`docs/lua/index.md`](docs/lua/index.md). Its main body is the live API
  surface — if a verb is documented there it is built and callable; the
  [§Planned](docs/lua/planned.md) section lists what is declared but not
  yet callable.
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
known to have been tested on any later build. kcdx's AOB signatures
for the engine's own hooks will need refreshing on future game
updates; abort messages are explicit and the game launches normally
if a signature breaks. Refreshing them is the first maintenance task
a fork inherits.

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
