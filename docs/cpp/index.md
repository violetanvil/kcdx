# kcdx C++ API — author reference

Reference documentation for the kcdx C++ authoring surface (DLL plugins), as
built. Every interface and method marked **built** here is verified declared in
[`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h) — the
authoritative source of truth for the C++ ABI. If a built call is not in these
files, it does not exist yet (see [planned.md](planned.md) and the NYI-marked
entries).

This is the C++ mirror of [`docs/lua/`](../lua/index.md). The two surfaces are
ONE model in two languages: the authoring surface is one learnable model in two
languages (Lua + C++), with mirrored `kcdx.*` naming and call-shape — the
concepts, names, and structure match; only the spelling is idiomatic to each
language. The shipped end-state is full feature parity. Where the engine has
built one surface ahead of the other, the lagging side carries a
**not-yet-implemented (NYI)** entry that maps the planned mirror shape — tracked
parity debt, not a permanent single-surface capability. C++-specific facts that
exist only because the language provides them natively are marked
**single-surface** with their reason.

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
  the `<author>.<name>` pair in its manifest (`[plugin].author` +
  `[plugin].name` — the qualified namespace prefix the engine stamps on every
  shared name the plugin exports). See [plugin-shell.md](plugin-shell.md).

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
  your own via `api->GetPluginHandle("your.name")` (the `[plugin].name` from
  your manifest). `kcdxInvalidPluginHandle` is the lookup-miss sentinel.
  (Distinct from the Lua *handle* userdata that `kcdx.hook`/`kcdx.bytes`
  return — the built `kcdxHookInterface` returns a `kcdxHookHandle`; see
  [hook.md](hook.md).)

- **lifecycle event / message** — a named moment in the game's run. C++ spelling:
  a `kcdxMessage_*` value delivered to a `kcdxMessagingCallback` you registered
  via `kcdxMessagingInterface::RegisterListener`. See [lifecycle.md](lifecycle.md).

- **load-order priority / zone** — where a plugin sits within `before_game` /
  `after_game`. Same per-plugin `[load_order]` table (`zone` / `priority`
  manifest keys) as Lua. C++ `PostGameLoad` exports run in load-order priority.

- **locator / signature / hook mode** — how a hook finds its target and the ABI
  string the engine carries for it. These are the built `kcdxHookInterface`
  vocabulary (the C++ mirror of the Lua `target`/`signature`/`before`/… surface);
  see [hook.md](hook.md).

- **named target** — an entry in the unified named-target table the hook /
  bytes verbs consume by name. Two population sources: **curated targets**
  (engine-shipped, maintained by the kcdx maintainer, pre-checked for byte-
  survival across game versions) and **declared targets** (plugin-supplied
  via `kcdxDeclareInterface::Declare`, owned by the declaring plugin's
  `<author>.<plugin>` namespace, per-version mapping owned by the author).
  The consumer cannot tell which source backed a name — both reach the hook
  / bytes verbs through the same resolver and install identically. See
  [declare.md](declare.md).

- **smart resolver** — the engine's name → address-and-verified-ABI lookup
  the hook / bytes verbs route through. Walks **self > engine > other-plugin**
  precedence: a bare name resolves to the calling plugin's own declarations
  first, then engine-shipped names, then other plugins' declarations.
  Explicit `"<author>.<plugin>.<bare>"` and `"kcdx.<seedname>"` forms bypass
  the precedence walk and resolve directly.

- **dev mode** — engine setting (`engine.toml`, `dev_mode = true`) gating the
  test suite and debug/trace logging. Same as Lua; `ReportTestResult` is a no-op
  when off.

- **deferred-apply model** — `kcdx.hook`/`kcdx.bytes` queue intent and install
  in one end-of-zone apply pass. The built `kcdxMemoryInterface::WriteBytes`
  applies *immediately* (it is the runtime byte-write peer); the deferred,
  conflict-resolved registration model is the built `kcdxHookInterface` (hooks)
  and `kcdxBytesInterface` (byte rewrites) surfaces.

- **co-save** — a `.kcdx` sidecar holding per-plugin save data. C++ spelling:
  `kcdxSerializationInterface` (built). See [cosave.md](cosave.md).

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
| **`Kcdx.h` empowered wrapper (the common path)** — `kcdx::hook::Before/After/Around/Replace<Sig,&fn>(K, target)` + `struct Kcdx` | Built | `kcdx.hook` sub-verbs (typed natural callback, auto-threaded `owningPlugin`) | [wrapper.md](wrapper.md) |
| `kcdxHookInterface` (raw floor under the wrapper; the only path for `Mid` / `Callsite`) | Built | `kcdx.hook` raw `{...}` form | [hook.md](hook.md) |
| `kcdxHookInterface` bootstrap targets (hook the engine's own boot/runtime sites — `lua_pcall`, `engine.savegame`, …; they chain like any other target) | Built | `kcdx.hook` bootstrap targets | [hook.md](hook.md#bootstrap-targets) |
| **`Kcdx.h` empowered wrapper (the common path)** — `kcdx::bytes::Write(K, target, replacement)` + `struct Kcdx` | Built | `kcdx.bytes` (positional name + replacement, auto-threaded `owningPlugin`) | [wrapper.md](wrapper.md) |
| `kcdxBytesInterface::Register` (raw floor under the wrapper; the only path for the `[advanced]` `pattern` / `addressId` / `targetSymbol` locators) + `kcdxMemoryInterface::WriteBytes`/`ReadBytes` (immediate raw write) | Built | `kcdx.bytes` raw `{...}` form | [bytes.md](bytes.md) |
| `kcdxMessagingInterface::RegisterListener` (event/lifecycle) | Built | `kcdx.on` | [on.md](on.md) |
| `kcdxConsoleInterface::RegisterCommand`/`GetArg*` (console command) | Built | `kcdx.command` | [command.md](command.md) |
| `kcdxMessagingInterface::Dispatch` (custom event broadcast) | Built | `kcdx.publish` | [publish.md](publish.md) |
| `kcdxTrampolineInterface` (code allocation) — `Allocate` (all-in-one alloc+fill+pad+export) + `Export` (standalone publish) + the raw `AllocateFromBranchPool`/`LocalPool` floor (v2) | Built | `kcdx.code` | [code.md](code.md) |
| `kcdxInterface::GetConflictReport` (enumerate patch / hook / kcdx.hook entries at a target — winners + rejected losers) | Built | `kcdx.conflict` (**NYI** — owed Lua mirror) | [hook.md](hook.md#conflict-report) |
| `kcdxScanInterface` (diagnostic AOB scan / address-discovery workbench) | **NYI** | `kcdx.scan` | [scan.md](scan.md) |
| `kcdxLocatorInterface` (locator values + `Resolve(module, target)` — where in a function an op applies) | **NYI** | `kcdx.locator.*` | [locator.md](locator.md) |
| `kcdxOpInterface` (static-bytes op values + `EmitFor(kind, byteRangeLen)` — what static change an op makes) | **NYI** | `kcdx.op.*` | [op.md](op.md) |
| `kcdxFunctionsInterface` (function-reference values + `Resolve` — name a game-engine or plugin function → address + verified signature) | **NYI** | `kcdx.functions.*` | [functions.md](functions.md) |
| `kcdxDllInterface::Declare` (declare your own DLL's functions, signature from source → exposed for cross-plugin hooking by name) | **NYI** | `kcdx.dll.declare` | [dll.md](dll.md) |
| `kcdxDeclareInterface::Declare` / `Get` (author-declared per-version named targets + value reads) | Built | `kcdx.declare` / `kcdx.declared` | [declare.md](declare.md) |
| `kcdxTargetInterface::RegisterTarget` (author-declared named targets) | **NYI** | author-declared targets (`targets.toml`) | [targets.md](targets.md) |
| asset replacement — the `replaces.toml` sidecar (no-code, language-neutral) + `kcdxAssetInterface::GetByPath` / `GetByName` / `Declare` / `Register` / `Replace` (programmatic, in-code) | Built | asset replacement (`replaces.toml`) + `kcdx.assets.*` | [assets.md](assets.md) |
| `kcdxTargetInterface::RegisterAlias` (short local name handle) | **NYI** | `kcdx.alias` | [alias.md](alias.md) |
| `require` (sibling-file loading) | Single-surface | `require` | [require.md](require.md) |

### Domains

| C++ surface | Status | Lua counterpart | File |
|---|---|---|---|
| `kcdxInterface::Log` + `kcdxLogger` + `kcdxLogLevel` | Built | `kcdx.log.*` | [log.md](log.md) |
| `kcdxMemoryInterface::ScanPattern`/`GetModuleBase`/`Read`/`WriteBytes` (+ NYI dynamic_call/dynamic_hook) | Built / partial | `kcdx.memory.*` | [memory.md](memory.md) |
| `kcdxInterface::ResolveAddress`/`ResolveAddressByName`/`ResolveSymbol` | Built | `kcdx.addr` | [addr.md](addr.md) |
| `kcdxConsoleInterface::Print` / `ExecuteString` (+ `kcdx::console::print` wrapper) | Built | `kcdx.console.*` | [console.md](console.md) |
| `kcdxConsoleInterface::GetCVarInt` / `GetCVarBool` / `GetCVarFloat` (read a game CVar by name; Version 3) | Built | `kcdx.cvar.*` | [cvar.md](cvar.md) |
| `kcdxInterface::ReportTestResult` | Built | `kcdx.test.*` | [test.md](test.md) |
| `kcdxSerializationInterface` (per-save plugin data; `OpenRecordNamed`/`GetRecordTagName`, Version 2) | Built (name-derived UID **NYI**) | `kcdx.cosave.*` | [cosave.md](cosave.md) |
| `kcdxPluginInfoInterface` (plugin introspection — is another plugin rejected) | **NYI** | `kcdx.plugin.*` | [plugin.md](plugin.md) |
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
