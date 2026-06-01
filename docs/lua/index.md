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
   engine primitive. The live set is `kcdx.hook`, `kcdx.bytes`, `kcdx.code`,
   `kcdx.on`, `kcdx.command`, `kcdx.publish`, `kcdx.scan`.

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
  `plugin.lua`), or both. Identified by the `<author>.<name>` pair in its
  manifest (`[plugin].author` + `[plugin].name`).

- **author (namespace)** — `[plugin].author`: the leading component of the
  qualified namespace your plugin exports. Charset `[a-z0-9_]`, 2–128 chars
  (the same shape as `[plugin].name`). Distinct from any *display* author
  string — this one stamps every shared name your plugin exports. The
  reserved value `kcdx` is the engine's own namespace and is rejected for
  user plugins.

- **manifest (`kcdx.toml`)** — the per-plugin config file declaring identity
  (`[plugin]`), entry points (`[entrypoints]`), and engine settings
  (`[kcdx]`). See [The plugin shell](plugin-shell.md).

- **entrypoint** — a script kcdx runs to set your plugin up. `lua` runs in the
  plugin's load-order slot (the *before* slot by default); `lua_after` runs in
  the after-game slot. The C++ mirror is `kcdxPlugin_Load` (before) and
  `kcdxPlugin_PostGameLoad` (after).

- **zone** — which side of the running game the plugin loads on:
  `before_game` (engine fixes and plugins that must be in place before the game
  starts) or `after_game` (most user plugins). Set with `[load_order].zone`.

- **zone gate** — the engine's plugin-init capability check. After load order
  resolves every plugin's zone, the gate cross-references each enabled plugin's
  declared zone against the engine's static capability table; if any
  *requireZone* API would be unreachable from that zone, the plugin is rejected
  (its `engineAccepted` flag flips false and it is skipped at every init site).
  Runs ONCE per session, BEFORE any plugin's `plugin.lua` or DLL `Load` runs —
  so no half-loaded state is possible. Query the result from your own plugin
  with `kcdx.plugin.is_rejected`. See [plugin.md](plugin.md).

- **requireZone** — the engine's per-API zone capability annotation. Each
  `kcdx.*` API the engine ships carries a `requireZone` value declaring which
  load-order zone it can legally be called from (`Either` / `Before` /
  `After`). Today every shipped API is `Either` — deferred registration handles
  the "called early but work must happen later" cases — so the zone gate only
  ever rejects against a synthetic test entry; a future API requiring a
  specific zone would reject a plugin whose declared zone makes that API
  unreachable. See [plugin.md](plugin.md).

- **load-order priority** — where a plugin sits within its zone, `0` (earliest)
  to `100` (latest), default `50`. Set with `[load_order].priority`; the engine's
  `load_order.toml` can override it. Cross-plugin ordering of hooks/bytes comes
  from this; ordering *within* one plugin is the order your `plugin.lua`
  registers them.

- **apply order / kind ordering** — the order the engine applies queued
  registrations: by load-order priority first, then by *kind* at the same
  priority. At a shared site a bytes-patch ([`kcdx.bytes`](bytes.md)) applies
  before a hook ([`kcdx.hook`](hook.md)) — the patch rewrites the bytes, then
  the hook detours the patched prologue — so a patch and a hook on the same
  function coexist.

- **hook mode / behaviour** — what your callback does to a hooked function:
  `before`, `after`, `around`, `replace` (function-wrapping behaviours) or
  `mid` (a register/memory capture mid-function). Attached under the behaviour
  key itself.

- **hook scope** — `mode = "callsite"` redirects a single call instruction
  instead of the function entry; omitting `mode` hooks the function entry
  (the default).

- **locator** — how a hook or byte patch finds its target. `target = "<name>"`
  is the common path (the engine resolves both address *and* verified
  signature). The name may be an engine [Address Library](addr.md) name or one
  you declared yourself (see *author-target* below). Advanced/expert locators
  (`address`, `address_id`, `pattern`, `target_symbol`) make you supply hex/ABI
  yourself.

- **author-target / `targets.toml`** — a code site you name yourself in a
  `targets.toml` sidecar (`[[target]]` rows: a bare `name` + one locator +
  optional `signature`), then refer to by name from `kcdx.hook` / `kcdx.bytes`.
  The author-declared peer of an engine name; shareable by name across plugins
  (an expert names an AOB once, non-experts hook it by name). See
  [targets.md](targets.md).

- **named target** — an entry in the unified named-target table the hook /
  bytes verbs consume by name. Two population sources: **curated targets**
  (engine-shipped, maintained by the kcdx maintainer, pre-checked for
  byte-survival across game versions) and **declared targets** (plugin-supplied
  via `kcdx.declare`, owned by the declaring plugin's `<author>.<plugin>`
  namespace, per-version mapping owned by the author). The consumer cannot
  tell which source backed a name — both reach the hook / bytes verbs through
  the same resolver and install identically. See [declare.md](declare.md).

- **curated target** — a named target supplied by the kcdx engine itself.
  Lives in the engine seed under the reserved `kcdx.` root (the 1-dot
  `kcdx.<seedname>` form); maintained by the kcdx maintainer, pre-checked for
  byte-survival across game versions. The other population source of the
  unified named-target table. See [addr.md](addr.md).

- **declared target** — a named target supplied by a plugin via
  `kcdx.declare`; owned by the declaring plugin's (author, plugin) namespace
  (stamped as `<author>.<plugin>.<bare>`). The other population source of the
  unified named-target table; the plugin-supplied peer of a curated target.
  See [declare.md](declare.md).

- **smart resolver** — the engine's name → address-and-verified-ABI lookup
  the hook / bytes verbs route through. The `__index`-driven Lua shape
  (`kcdx.hook.<name>.<mode>(fn)`, `kcdx.bytes.<name>{...}`) resolves a named
  target to its install function on demand: a typo fails fast at the name
  access (the `__index` returns nil); a kind-mismatch (e.g.
  `kcdx.hook.<value-only-name>.before`) fails at the mode access. Walks
  **self > engine > other-plugin** precedence: a bare name resolves to the
  calling plugin's own declarations first, then engine-shipped names, then
  other plugins' declarations. Explicit `"<author>.<plugin>.<bare>"` and
  `"kcdx.<seedname>"` forms bypass the precedence walk and resolve directly.

- **implicit namespace prefix** — the `<author>.<plugin>` the engine stamps on
  every shared name your plugin exports (a target, a `kcdx.code` export, a
  published event), derived from `[plugin].author` + `[plugin].name`. You write
  the bare `name`; the engine registers `<author>.<plugin>.<name>`. You never
  type your own prefix. The engine's own seed names live under the reserved
  `kcdx.` root (the 1-dot `<kcdx>.<seedname>` form — `kcdx` is the reserved
  author, with no plugin component). See [targets.md](targets.md).

- **name precedence (self > engine > other)** — how a *bare* shared name
  resolves: your own plugin's declaration first, then an engine seed name,
  then another plugin's. The explicit `"<author>.<plugin>.<name>"` form
  bypasses precedence and is unambiguous from anywhere; a bare-name collision
  warns once per session. See [targets.md](targets.md).

- **alias** — a short, plugin-scoped local handle for a long shared name,
  declared with `kcdx.alias(short, target)`. Only adds a handle — never shadows
  or displaces resolution. See [alias.md](alias.md).

- **signature** — the ABI string telling the engine a function's argument and
  return types, e.g. `"i32 (i32 seed)"`. The `target = "<name>"` path supplies
  it for you; the advanced locators require you to write it.

- **AOB pattern** — an *array-of-bytes* pattern: a hex byte string with optional
  `??` wildcards, e.g. `"48 8B 88 ?? ?? ?? ?? 48"`, that matches a code sequence
  in a module. It is the expert hex form behind the `pattern` locator and the
  input to the diagnostic scan — disassembler-tier, not the common path.

- **diagnostic scan (the workbench)** — the dev-time step, `kcdx.scan{...}`, where
  you resolve a hand-written AOB pattern against a module and check whether it
  matched and how many sites it hit, *before* committing it to a hook. The expert
  workbench you run to discover and validate an un-named site; once it resolves
  uniquely you name it and hook it by name. See [scan.md](scan.md).

- **handle** — the userdata `kcdx.hook` / `kcdx.bytes` return. Carries
  `:applied()`, `:reason()`, `:name()`. Its status is `nil` (pending) until the
  apply pass runs.

- **lifecycle event** — a named moment in the game's run (e.g. `input_loaded`,
  `save_game`) your plugin can subscribe to with `kcdx.on`.

- **co-save** — the per-save `.kcdx` sidecar file holding your plugin's
  persisted state, written and read through `kcdx.cosave.*` and tied to the
  specific game save it sits next to. Each save has its own co-save, so plugin
  state is remembered per-save. See [cosave.md](cosave.md).

- **cosave write/read window** — the brief moment, inside the body you register
  with `kcdx.cosave.on_save` (resp. `on_load`), when `kcdx.cosave.write`
  (resp. `records`) actually works because the engine is mid-writing (resp.
  mid-reading) the co-save file. Distinct from the `save_game` *lifecycle event*,
  which fires *after* the file is written and carries no write window — so cosave
  data must be written from `on_save`, never from `kcdx.on("save_game")`.

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
| `kcdx.hook` bootstrap targets | hook the engine's own boot/runtime sites (`lua_pcall`, `engine.savegame`, …) — they chain like any other target | [hook.md](hook.md#bootstrap-targets) |
| `kcdx.bytes` | rewrite bytes at a located site | [bytes.md](bytes.md) |
| `kcdx.on` | subscribe to a lifecycle or custom event | [on.md](on.md) |
| `kcdx.command` | register a console command | [command.md](command.md) |
| `kcdx.publish` | broadcast a custom event to subscribers | [publish.md](publish.md) |
| `kcdx.code` | allocate an executable code region you own (for a callback address the game calls, a cross-plugin extension point, or a shared helper). To intercept an existing game function, use `kcdx.hook` instead. | [code.md](code.md) |
| `kcdx.scan` | validate an AOB pattern + discover an address (dev-time workbench) | [scan.md](scan.md) |
| `kcdx.alias` | declare a short local handle for a long shared name | [alias.md](alias.md) |
| `kcdx.declare` / `kcdx.declared` | declare a per-version named target your plugin owns (the author-declared peer of a curated engine name; hook/byte verbs consume it by name); read a declared VALUE entry back | [declare.md](declare.md) |
| author-declared targets (`targets.toml`) | name a code site yourself, then hook/patch it by name | [targets.md](targets.md) |
| `require` | load a sibling Lua file (multi-file plugins) | [require.md](require.md) |

### Domains

| Call | What it does | File |
|---|---|---|
| `kcdx.log.*` | structured logging (info/warn/error/debug/trace) | [log.md](log.md) |
| `kcdx.memory.*` | direct memory access + runtime native interop | [memory.md](memory.md) |
| `kcdx.addr` | Address Library name → pointer snapshot table | [addr.md](addr.md) |
| `kcdx.console.*` | print a line to the `~` console / run a console command line from Lua | [console.md](console.md) |
| `kcdx.test.*` | record a test-suite result | [test.md](test.md) |
| `kcdx.cosave.*` | persist plugin state across saves (write on save, read on load) | [cosave.md](cosave.md) |
| `kcdx.plugin.*` | plugin introspection (is another plugin rejected/loaded) | [plugin.md](plugin.md) |
| `kcdx.dev.*` | dev-mode introspection sugar | [dev.md](dev.md) |
| the `kcdx.lua` domain | VM-introspection (cfunction_address, _probe_numbers) | [lua.md](lua.md) |

### Lifecycle & cross-cutting

| Call | What it does | File |
|---|---|---|
| `kcdx.on` lifecycle events | `"ready"`, the 9 game events, custom `"<author>.<plugin>.<event>"` | [lifecycle.md](lifecycle.md) |
| cross-cutting rules | main-thread, pointer-userdata, deferred-apply, error contracts | [cross-cutting.md](cross-cutting.md) |

### Planned

| Call | What it does | File |
|---|---|---|
| planned — not yet available | gameplay domains | [planned.md](planned.md) |
