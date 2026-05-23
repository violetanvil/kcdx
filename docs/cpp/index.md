# kcdx C++ API — author reference

Reference documentation for the kcdx C++ authoring surface (DLL plugins), as
built. Every interface and method marked **built** here is verified declared in
[`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h) — the
authoritative source of truth for the C++ ABI. If a built call is not in these
files, it does not exist yet (see [planned.md](planned.md) and the NYI-marked
entries).

This is the C++ mirror of [`docs/lua/`](../lua/index.md). The two surfaces are
ONE model in two languages (`.claude/rules/lua-api-surface.md`): the concepts,
names, and structure match; only the spelling is idiomatic to each language.
The shipped end-state is full feature parity. Where the engine has built one
surface ahead of the other, the lagging side carries a **not-yet-implemented
(NYI)** entry that maps the planned mirror shape — tracked parity debt, not a
permanent single-surface capability (`.claude/rules/docs-discipline.md`
criterion 3). C++-specific facts that exist only because the language provides
them natively are marked **single-surface** with their reason.

These files are a reference, not a tutorial. Each entry states the call shape
(the real C++ signature for a built interface), the arguments (type + meaning),
the return value, the error behaviour, and a minimal correct snippet.

---

## 1. The model

The C++ surface is the same model as the Lua surface ([`docs/lua/`](../lua/index.md)
§1), expressed as engine **interfaces** a plugin DLL receives at load. Hold
these rules in your head and you can predict where any call lives and what shape
it takes:

1. **One entry interface: `kcdxInterface`.** Your DLL's exported
   `kcdxPlugin_Load(const kcdxInterface* api)` receives a single root interface.
   It carries the always-available calls directly (`Log`, `ResolveAddress`,
   `ResolveSymbol`, `ReportTestResult`, plugin introspection) and hands out
   every other capability through `api->QueryInterface(id, version)`. This is
   the C++ spelling of Lua's "one global, `kcdx`".

2. **Grouped capability interfaces, fetched by ID.** Each capability domain is
   a typed sub-interface fetched once via `QueryInterface` — `kcdxMessaging­Interface`,
   `kcdxTrampolineInterface`, `kcdxConsoleInterface`, `kcdxMemoryInterface`,
   `kcdxScriptingInterface`, `kcdxTaskInterface`, `kcdxSerializationInterface`.
   This is the C++ spelling of Lua's `kcdx.<domain>.<verb>` grouping. A null
   return means the running engine does not implement that interface/version.

3. **Call shape: configuring → options-struct, doing → typed params.** A
   registration call describing a thing with several options takes a struct (the
   C++ spelling of Lua's `{ named table }` — e.g. a future `kcdxHookOptions`);
   a simple "do a thing" call takes positional typed parameters (the C++
   spelling of Lua's positional calls — `Log(self, level, category, msg)`,
   `RegisterListener(self, sender, cb)`). The verb `kcdx.hook{...}` (Lua)
   becomes `kcdxHookInterface::Install(...)` / `K.hook->Install(...)` (C++).

That is the whole surface model. A call you have never seen still resolves to
the right place by these rules — and to its Lua counterpart by the same model.

### `kcdxInterface::kcdxVersion`

`kcdxInterface::kcdxVersion` is a `uint32_t` field (packed `0xMMmmpp00`)
carrying the engine version; `runtimeGameVersion` carries the live KCD2 build.
Read them to gate on engine/game capabilities — the C++ spelling of Lua's
`kcdx.version`.

---

## 2. Glossary

Shared terms with [`docs/lua/`](../lua/index.md) §2 — same definitions, C++
spellings noted where they differ:

- **plugin** — a unit of mod content kcdx loads. A C++ plugin is a DLL exporting
  `kcdxPlugin_Load`, declared via `[plugin]` in its `kcdx.toml`. Identified by
  the `name` in its manifest. See [plugin-shell.md](plugin-shell.md).

- **manifest (`kcdx.toml`)** — the per-plugin config file. C++ plugins point
  `[entrypoints] dll` at their DLL. See [plugin-shell.md](plugin-shell.md).

- **entrypoint** — the exported function kcdx calls to set your plugin up. C++
  spelling: `kcdxPlugin_Preload` (preload wave), `kcdxPlugin_Load` (load wave,
  the *before* slot — mirrors Lua `lua`), `kcdxPlugin_PostGameLoad` (after-game
  slot — mirrors Lua `lua_after`).

- **interface** — a typed struct of function pointers a capability domain is
  exposed through, fetched via `QueryInterface`. The C++ analogue of a Lua
  domain sub-table.

- **handle (`kcdxPluginHandle`)** — the opaque `uint32_t` ID kcdx assigns each
  plugin. Passed as the `self`/`owner`/`sender` argument to most calls. Fetch
  your own via `api->GetPluginHandle("your.name")`. `kcdxInvalidPluginHandle`
  is the lookup-miss sentinel. (Distinct from the Lua *handle* userdata that
  `kcdx.hook`/`kcdx.bytes` return — that is the NYI `kcdxHookInterface`'s
  return type; see [hook.md](hook.md).)

- **lifecycle event / message** — a named moment in the game's run. C++ spelling:
  a `kcdxMessage_*` value delivered to a `kcdxMessagingCallback` you registered
  via `kcdxMessagingInterface::RegisterListener`. See [lifecycle.md](lifecycle.md).

- **load-order priority / zone** — where a plugin sits within `before_game` /
  `after_game`. Same `default_position` / `default_priority` manifest keys as
  Lua. C++ `PostGameLoad` exports run in load-order priority.

- **locator / signature / hook mode** — how a hook finds its target and the ABI
  string the engine carries for it. These are the NYI `kcdxHookInterface`
  vocabulary (the C++ mirror of the Lua `target`/`signature`/`before`/… surface);
  see [hook.md](hook.md).

- **dev mode** — engine setting (`engine.toml`, `dev_mode = true`) gating the
  test suite and debug/trace logging. Same as Lua; `ReportTestResult` is a no-op
  when off.

- **deferred-apply model** — `kcdx.hook`/`kcdx.bytes` queue intent and install
  in one end-of-zone apply pass. The built `kcdxMemoryInterface::WriteBytes`
  applies *immediately* (it is the runtime byte-write peer); the deferred,
  conflict-resolved registration model is the NYI `kcdxHookInterface` surface.

- **co-save** — a `.kcdx` sidecar holding per-plugin save data. C++ spelling:
  `kcdxSerializationInterface` (built, Phase 6). See [cosave.md](cosave.md).

---

## 3. The map

Every C++ interface and method, mapped to the file that documents it, alongside
its Lua counterpart. **Built** = a real interface in `Interfaces.h`. **NYI** =
the planned C++ mirror of a built Lua surface (no interface in the header yet).
**Single-surface** = C++ gets this from the language; no kcdx interface owed.

### Core verbs

| C++ surface | Status | Lua counterpart | File |
|---|---|---|---|
| the DLL plugin shell (`kcdxPlugin_Load`, `[entrypoints] dll`, QueryInterface handshake) | Built | the plugin shell / manifest | [plugin-shell.md](plugin-shell.md) |
| `kcdxHookInterface` (function interception) | **NYI** | `kcdx.hook` | [hook.md](hook.md) |
| `kcdxMemoryInterface::WriteBytes`/`ReadBytes` (byte rewrite) | Built (runtime) / locator-mirror **NYI** | `kcdx.bytes` | [bytes.md](bytes.md) |
| `kcdxMessagingInterface::RegisterListener` (event/lifecycle) | Built | `kcdx.on` | [on.md](on.md) |
| `kcdxConsoleInterface::RegisterCommand`/`GetArg*` (console command) | Built | `kcdx.command` | [command.md](command.md) |
| `kcdxMessagingInterface::Dispatch` (custom event broadcast) | Built | `kcdx.publish` | [publish.md](publish.md) |
| `kcdxTrampolineInterface::AllocateFromBranchPool`/`LocalPool` (code allocation) | Built | `kcdx.code` | [code.md](code.md) |
| `kcdxScanInterface` (diagnostic AOB scan / address-discovery workbench) | **NYI** | `kcdx.scan` | [scan.md](scan.md) |
| `kcdxTargetInterface::RegisterTarget` (author-declared named targets) | **NYI** | author-declared targets (`targets.toml`) | [targets.md](targets.md) |
| `kcdxTargetInterface::RegisterAlias` (short local name handle) | **NYI** | `kcdx.alias` | [alias.md](alias.md) |
| `require` (sibling-file loading) | Single-surface | `require` | [require.md](require.md) |

### Domains

| C++ surface | Status | Lua counterpart | File |
|---|---|---|---|
| `kcdxInterface::Log` + `kcdxLogger` + `kcdxLogLevel` | Built | `kcdx.log.*` | [log.md](log.md) |
| `kcdxMemoryInterface::ScanPattern`/`GetModuleBase`/`Read`/`WriteBytes` (+ NYI dynamic_call/dynamic_hook) | Built / partial | `kcdx.memory.*` | [memory.md](memory.md) |
| `kcdxInterface::ResolveAddress`/`ResolveAddressByName`/`ResolveSymbol` | Built | `kcdx.addr` | [addr.md](addr.md) |
| `kcdxConsoleInterface::ExecuteString` | Built | `kcdx.console.*` | [console.md](console.md) |
| `kcdxInterface::ReportTestResult` | Built | `kcdx.test.*` | [test.md](test.md) |
| `kcdxSerializationInterface` (per-save plugin data; `OpenRecordNamed`/`GetRecordTagName`, Version 2) | Built (name-derived UID **NYI**) | `kcdx.cosave.*` | [cosave.md](cosave.md) |
| dev-mode introspection | **NYI** | `kcdx.dev.*` | [dev.md](dev.md) |
| `kcdxScriptingInterface` (Lua C-API surface; VM interop) | Built | the `kcdx.lua` domain | [lua.md](lua.md) |

### Lifecycle & cross-cutting

| C++ surface | Status | Lua counterpart | File |
|---|---|---|---|
| the `kcdxMessage_*` enum catalog | Built | `kcdx.on` lifecycle events | [lifecycle.md](lifecycle.md) |
| cross-cutting rules (threading via `kcdxTask`, precision, errors, ABI append-only) | Built | cross-cutting rules | [cross-cutting.md](cross-cutting.md) |

### Planned

| C++ surface | Status | Lua counterpart | File |
|---|---|---|---|
| planned / not yet available | mixed | planned | [planned.md](planned.md) |
