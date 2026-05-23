# kcdx Lua API — author reference

Reference documentation for the kcdx Lua authoring surface, as built. Every
accessor here is verified registered in the engine; if a call is not in this
map, it does not exist yet (see [Planned](planned.md) for what is coming but
not callable).

These files are a reference, not a tutorial. Each entry states the call shape,
the arguments (type + meaning), the return value, the error behaviour, and a
minimal correct snippet.

---

## 1. The model

Everything an author calls hangs off **one global: `kcdx`**. There are no
other globals — no `KCDX` for your own use, no per-plugin globals. Hold these
three rules in your head and you can predict where any call lives and what
shape it takes:

1. **Core authoring verbs are top-level: `kcdx.<verb>`.** These are the
   actions every plugin performs to register intent with the engine — one per
   engine primitive. The live set is `kcdx.hook`, `kcdx.bytes`, `kcdx.on`,
   `kcdx.command`, `kcdx.publish`.

2. **Everything else is a grouped domain: `kcdx.<domain>.<verb>`.** Capability
   areas are sub-tables — `kcdx.log.*`, `kcdx.memory.*`, `kcdx.addr.*`,
   `kcdx.console.*`, `kcdx.test.*`, `kcdx.dev.*`, `kcdx.lua.*`. Typing
   `kcdx.log.` in your editor shows only logging calls; the grouping scales
   without a flat wall of names. It mirrors Lua's own idiom (`string.`,
   `table.`, `os.`).

3. **Call shape: configuring → `{ named table }`, doing → positional.**
   - When you *configure* a thing with several options, you pass a single
     named-field table: `kcdx.hook{ name=, target=, before= }`,
     `kcdx.bytes{...}`, `kcdx.command{...}`. Named fields are self-documenting,
     order-free, and omit optionals cleanly.
   - When you just *do* a thing with one to three obvious arguments, you pass
     them positionally: `kcdx.log.info(category, msg)`, `kcdx.on(event, fn)`,
     `kcdx.test.report(name, pass, reason)`.

That is the whole surface model. A call you have never seen still resolves to
the right place by these rules.

### kcdx.version

`kcdx.version` is a string field (not a function) carrying the engine version,
e.g. `"0.1.0-phase5c"`. Read it to gate on engine capabilities.

```lua
kcdx.log.info("MYMOD", "running on kcdx " .. kcdx.version)
```

---

## 2. Glossary

- **plugin** — a unit of mod content kcdx loads. Either a C++ DLL exporting
  `kcdxPlugin_Load`, a declarative/Lua plugin (a `kcdx.toml` plus a
  `plugin.lua`), or both. Identified by the `name` in its manifest.

- **manifest (`kcdx.toml`)** — the per-plugin config file declaring identity
  (`[plugin]`), entry points (`[entrypoints]`), and engine settings
  (`[kcdx]`). See [The plugin shell](plugin-shell.md).

- **entrypoint** — a script kcdx runs to set your plugin up. `lua` runs in the
  plugin's load-order slot (the *before* slot by default); `lua_after` runs in
  the after-game slot. The C++ mirror is `kcdxPlugin_Load` (before) and
  `kcdxPlugin_PostGameLoad` (after).

- **zone** — which side of the running game the plugin loads on:
  `before_game` (engine fixes and plugins that must be in place before the game
  starts) or `after_game` (most user plugins). Set with `default_position`.

- **load-order priority** — where a plugin sits within its zone, `0` (earliest)
  to `100` (latest), default `50`. Set with `default_priority`; the engine's
  `load_order.toml` can override it. Cross-plugin ordering of hooks/bytes comes
  from this; ordering *within* one plugin is the order your `plugin.lua`
  registers them.

- **hook mode / behaviour** — what your callback does to a hooked function:
  `before`, `after`, `around`, `replace` (function-wrapping behaviours) or
  `mid` (a register/memory capture mid-function). Attached under the behaviour
  key itself.

- **hook scope** — `mode = "callsite"` redirects a single call instruction
  instead of the function entry; omitting `mode` hooks the function entry
  (the default).

- **locator** — how a hook or byte patch finds its target. `target = "<name>"`
  is the common path (the engine resolves both address *and* verified
  signature). Advanced/expert locators (`address`, `address_id`, `pattern`,
  `target_symbol`) make you supply hex/ABI yourself.

- **signature** — the ABI string telling the engine a function's argument and
  return types, e.g. `"i32 (i32 seed)"`. The `target = "<name>"` path supplies
  it for you; the advanced locators require you to write it.

- **handle** — the userdata `kcdx.hook` / `kcdx.bytes` return. Carries
  `:applied()`, `:reason()`, `:name()`. Its status is `nil` (pending) until the
  apply pass runs.

- **lifecycle event** — a named moment in the game's run (e.g. `input_loaded`,
  `save_game`) your plugin can subscribe to with `kcdx.on`.

- **dev mode** — an engine setting (`engine.toml`, `dev_mode = true`) that
  enables the test suite, debug/trace logging, and dev-only diagnostics.

- **deferred-apply model** — `kcdx.hook` and `kcdx.bytes` do not change game
  memory at the moment you call them. They validate immediately (returning
  `(nil, err)` on a bad call) and *queue* the intent. After every plugin in the
  zone has registered, the engine runs one apply pass that resolves conflicts
  in load order and installs everything. Your handle's `:applied()` flips from
  `nil` to `true`/`false` then, and your `kcdx.on("ready", ...)` callback fires.

- **trampoline pool** — where `kcdx.code` allocates an executable region. The
  `"branch"` pool keeps the region within ±2 GB of `WHGame.dll`'s code, so a
  32-bit relative (`rel32`) branch from a hooked site can reach it; the
  `"local"` pool places it anywhere (no reachability guarantee). Unlike a hook
  or byte patch, a code allocation has no conflict to resolve, so `kcdx.code`
  allocates *immediately* rather than through the deferred-apply model.

- **symbol export** — a name `kcdx.code{ export = "..." }` publishes for the
  region it allocated. Other plugins (or yours) reach that address by name via
  the `target_symbol` locator on `kcdx.hook` / `kcdx.bytes`. Names are
  process-global and unique: a second plugin exporting the same name gets a
  loud collision error.

---

## 3. The map

Every call in the kcdx Lua surface, mapped to the file that documents it. Scan
for the call you want and click straight through. If a call is not in this map,
it does not exist yet.

### Core verbs

| Call | What it does | File |
|---|---|---|
| the plugin shell / manifest | `kcdx.toml` keys — identity, entrypoints, engine settings | [plugin-shell.md](plugin-shell.md) |
| `kcdx.hook` | intercept a game function (before/after/around/replace/mid) | [hook.md](hook.md) |
| `kcdx.bytes` | rewrite bytes at a located site | [bytes.md](bytes.md) |
| `kcdx.on` | subscribe to a lifecycle or custom event | [on.md](on.md) |
| `kcdx.command` | register a console command | [command.md](command.md) |
| `kcdx.publish` | broadcast a custom event to subscribers | [publish.md](publish.md) |
| `kcdx.code` | allocate executable memory, fill + export it | [code.md](code.md) |
| `require` | load a sibling Lua file (multi-file plugins) | [require.md](require.md) |

### Domains

| Call | What it does | File |
|---|---|---|
| `kcdx.log.*` | structured logging (info/warn/error/debug/trace) | [log.md](log.md) |
| `kcdx.memory.*` | direct memory access + runtime native interop | [memory.md](memory.md) |
| `kcdx.addr` | Address Library name → pointer snapshot table | [addr.md](addr.md) |
| `kcdx.console.*` | run a console command line from Lua | [console.md](console.md) |
| `kcdx.test.*` | record a test-suite result | [test.md](test.md) |
| `kcdx.dev.*` | dev-mode introspection sugar | [dev.md](dev.md) |
| the `kcdx.lua` domain | VM-introspection (cfunction_address, _probe_numbers) | [lua.md](lua.md) |

### Lifecycle & cross-cutting

| Call | What it does | File |
|---|---|---|
| `kcdx.on` lifecycle events | `"ready"`, the 9 game events, custom `"<publisher>:<event>"` | [lifecycle.md](lifecycle.md) |
| cross-cutting rules | main-thread, pointer-userdata, deferred-apply, error contracts | [cross-cutting.md](cross-cutting.md) |

### Planned

| Call | What it does | File |
|---|---|---|
| planned — not yet available | `kcdx.cosave.*`, `kcdx.scan{...}`, gameplay domains | [planned.md](planned.md) |
