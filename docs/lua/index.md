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

3. **Call shape: required → positional, optional → trailing `{ table }`;
   behavioural variants → sub-verbs.**
   - Required arguments are positional, so you cannot forget one:
     `kcdx.hook.before(module, target, callback)`,
     `kcdx.log.info(category, msg)`, `kcdx.on(event, fn)`. Optional knobs go in
     a trailing table: `kcdx.hook.before(module, target, callback, { signature = ... })`.
   - A verb with discrete behavioural variants splits them into **sub-verbs**
     (one C function each, impossible to misspell into another):
     `kcdx.hook.before/after/around/replace`, `kcdx.log.info/warn/error`. You
     pick the mode by which sub-verb you call.
   - A configuring verb with "at least one of N" options still takes a single
     table: `kcdx.bytes{...}`, `kcdx.command{...}`.

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
  yourself. **A `kcdx.locator.*` value** is a finer-grained locator that names
  *where within a function* an op applies (a call, a return, a matching
  statement) — see [locator.md](locator.md).

- **author-target / `targets.toml`** — a code site you name yourself in a
  `targets.toml` sidecar (`[[target]]` rows: a bare `name` + one locator +
  optional `signature`), then refer to by name from `kcdx.hook` / `kcdx.bytes`.
  The author-declared peer of an engine name; shareable by name across plugins
  (an expert names an AOB once, non-experts hook it by name). See
  [targets.md](targets.md).

- **asset sidecar (`replaces.toml`)** — an opt-in metadata TOML co-located with
  an asset in your `assets/` tree, declaring what the asset replaces (and/or a
  published name) via `[[asset]]` rows. The no-code path to replace a vanilla
  (or another mod's) asset; mirrors the [`targets.toml`](targets.md) idiom for
  code sites, applied to assets. See [assets.md](assets.md).

- **declared replacement** — making the engine serve your asset where it would
  have served a vanilla (or another mod's) asset, stated by an explicit
  `replaces` declaration in a [`replaces.toml`](assets.md). Always declared,
  never inferred from a path that coincides with a vanilla path: a file with no
  declaration is referenceable but replaces nothing (existence is not
  replacement).

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

- **name resolution** — the engine's name → address-and-verified-ABI lookup
  the hook / bytes verbs route through. You name the target (the `target`
  argument of a `kcdx.hook` sub-verb, or `target = "<name>"` on `kcdx.bytes`)
  and the engine resolves both its address and its verified signature, so you
  write no hex and no ABI. An unknown name is rejected with a teaching error
  (no silent skip); a target with no verified ABI and no `signature` you
  supplied is rejected (the engine never invents an ABI). Walks
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

- **function reference** — a value naming a function (a game-engine function or
  a plugin's), carrying its address and (where known) its verified signature.
  Reached through `kcdx.functions.*` and handed to a hook/statement verb as the
  *what to hook* — you never write an address or an ABI. A dot-free stem
  (`kcdx.functions.WHGame.SaveGame`, or `kcdx.functions.by_id[N]`) names a
  game-engine function from the reference database; a dotted `<author>.<plugin>`
  stem (`kcdx.functions["redmoon.outfit_mod"].Fn`) names a plugin function the
  owning plugin declared. See [functions.md](functions.md).

- **declared DLL function** — a plugin function exposed via `kcdx.dll.declare`
  (a signature copied from the author's own source, no disassembly), reachable by
  name at `kcdx.functions["<author>.<plugin>"].<name>` so any plugin can hook it
  without a disassembler. Distinct from a *declared target* (`kcdx.declare`,
  which names a per-version game-binary site by AOB pattern). See [dll.md](dll.md).

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

- **CVar** — a console variable: a CryEngine engine setting addressable by
  name — the values you see and set in the in-game `~` console. kcdx reads them
  by their console name with [`kcdx.cvar.*`](cvar.md); you supply the name (from
  a modding wiki, the `~` console, or a config), the engine resolves the rest.

- **behavior** — a named, settable unit of intent: a value plus the declarer's
  `implementation` that reconfigures the game to match it, under the engine's
  apply contract — think a CVar whose setter is mod-authored. NOT a shared
  variable (plain cross-plugin data is `kcdx.publish`/`kcdx.on`). Two tiers
  register through one registry: the engine catalog (`kcdx.behavior.<bare>`)
  and plugin-declared behaviors (`<author>.<plugin>.<bare>`). See
  [behavior.md](behavior.md).

- **declarer (of a behavior)** — the plugin (or the engine catalog) that owns
  a behavior's name and implementation, declared via
  `kcdx.behavior.declare`. `kcdx.behavior.list()` names each entry's declarer.

- **consumer (of a behavior)** — a plugin that reads (and, once `set` ships,
  sets) a behavior it does not declare — the simple-modder role: a consumer
  names a behavior and a value, never a function name or an address.

- **apply boundary** — the single point, after all plugins have loaded, where
  the engine invokes each *set* behavior's implementation once with the final
  settled value. Ships with `kcdx.behavior.set` (its doc entry lands with that
  call); today nothing can be set, so no implementation is invoked and every
  behavior reads as its declared `default`.

- **catalog tier** — the engine-shipped behaviors under the reserved
  `kcdx.behavior.<bare>` root, declared by the engine's catalog pack ahead of
  every user plugin (so they are always declared before any user code runs).
  Not shipped yet; once it lands, `kcdx.behavior.list("kcdx.behavior.")`
  browses it.

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
| `kcdx.statement.*` | static-bytes modification at a located statement (`replace_with` writes an op's bytes natively — zero per-call cost; `insert_before`/`insert_after` run a callback at a statement) — the static-bytes sibling of `kcdx.hook` | [statement.md](statement.md) |
| `kcdx.on` | subscribe to a lifecycle or custom event | [on.md](on.md) |
| `kcdx.command` | register a console command | [command.md](command.md) |
| `kcdx.publish` | broadcast a custom event to subscribers | [publish.md](publish.md) |
| `kcdx.code` | allocate an executable code region you own (for a callback address the game calls, a cross-plugin extension point, or a shared helper). To intercept an existing game function, use `kcdx.hook` instead. | [code.md](code.md) |
| `kcdx.scan` | validate an AOB pattern + discover an address (dev-time workbench) | [scan.md](scan.md) |
| `kcdx.alias` | declare a short local handle for a long shared name | [alias.md](alias.md) |
| `kcdx.declare` / `kcdx.declared` | declare a per-version named target your plugin owns (the author-declared peer of a curated engine name; hook/byte verbs consume it by name); read a declared VALUE entry back | [declare.md](declare.md) |
| author-declared targets (`targets.toml`) | name a code site yourself, then hook/patch it by name | [targets.md](targets.md) |
| asset replacement (`replaces.toml`) | replace a vanilla (or another mod's) asset with your own, no code — declare it in a co-located sidecar | [assets.md](assets.md) |
| `require` | load a sibling Lua file (multi-file plugins) | [require.md](require.md) |

### Domains

| Call | What it does | File |
|---|---|---|
| `kcdx.log.*` | structured logging (info/warn/error/debug/trace) | [log.md](log.md) |
| `kcdx.memory.*` | direct memory access + runtime native interop | [memory.md](memory.md) |
| `kcdx.addr` | Address Library name → pointer snapshot table | [addr.md](addr.md) |
| `kcdx.console.*` | print a line to the `~` console / run a console command line from Lua | [console.md](console.md) |
| `kcdx.cvar.get_int` | read a game CVar's integer value by name | [cvar.md](cvar.md) |
| `kcdx.cvar.get_bool` | read a game CVar as on/off (its int value `!= 0`) | [cvar.md](cvar.md) |
| `kcdx.cvar.get_float` | read a game CVar's float value by name | [cvar.md](cvar.md) |
| `kcdx.locator.*` | locator values — where in a function a hook/statement op applies (`function_entry`/`first_call_to`/`first_return`/`matching{…}`/…); `:resolve(module, target)` inspects what one picks | [locator.md](locator.md) |
| `kcdx.op.*` | static-bytes op values — what static change a `kcdx.statement` op makes, named not hex (`replace_with_return`/`replace_with_noop`/`never_take_branch`/`skip_call_void`/…); `:emit_for(kind, byte_range_len)` inspects what one emits | [op.md](op.md) |
| `kcdx.functions.*` | function-reference values — name a function (game-engine or plugin) → a value carrying its address + verified signature, for a hook/statement verb. Game-DLL: dot-free stem (`kcdx.functions.WHGame.SaveGame`) + `by_id[N]`; plugin-DLL: dotted stem (`kcdx.functions["<author>.<plugin>"].Fn`). `:resolve()` inspects one | [functions.md](functions.md) |
| `kcdx.dll.declare` | declare your own DLL's functions (signature from your source, no disassembly) → exposed at `kcdx.functions["<your-namespace>"].*` for cross-plugin hooking by name | [dll.md](dll.md) |
| `kcdx.assets.get_by_path` | resolve your own asset by path → a loadable path you hand to a game asset API (cross-plugin via `kcdx.plugin.<a>.<p>.assets.get_by_path`) | [assets.md](assets.md) |
| `kcdx.behavior.declare` / `.get` / `.list` | named behaviors — declare a behavior your plugin owns (stamped `<author>.<plugin>.<bare>`; spec = description + default + implementation + optional revert); read a behavior's current-or-default value; browse the registered behaviors with an optional prefix filter. (`kcdx.behavior.set` + the apply boundary are not callable yet) | [behavior.md](behavior.md) |
| `kcdx.find` | discover a game function from what you know about it (a string/CVar it references, a function it calls/is-called-by, a name substring) — a **dev-mode-only** discovery workbench returning matching function headers (name, module, rva, statement_count); `{}` (not nil) on no-match or when the dev tool is unavailable. Inspect one with `kcdx_dev_inspect` | [find.md](find.md) |
| `kcdx.test.*` | record a test-suite result | [test.md](test.md) |
| `kcdx.cosave.*` | persist plugin state across saves (write on save, read on load) | [cosave.md](cosave.md) |
| `kcdx.plugin.*` | plugin introspection (is another plugin rejected/loaded) + the navigable cross-plugin namespace (`kcdx.plugin.<author>.<plugin>.*` reaches another mod's published surface by a native dotted path) | [plugin.md](plugin.md) |
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
