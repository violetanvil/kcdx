# kcdx Lua API — author reference

Reference documentation for the kcdx Lua authoring surface, as built. Every
accessor here is verified registered in the engine; if a call is not in this
document, it does not exist yet (see [Planned](#planned--not-yet-available) for
what is coming but not callable).

This is a reference, not a tutorial. Each entry states the call shape, the
arguments (type + meaning), the return value, the error behaviour, and a
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
  (`[kcdx]`). See [The plugin shell](#3-the-plugin-shell).

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

## 3. The plugin shell

A declarative/Lua plugin is a folder with a `kcdx.toml` and one or more Lua
files. The minimal working plugin:

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
kcdx.log.info("MYMOD", "hello from my first plugin")
```

### `[plugin]` keys

| Key | Type | Meaning |
|---|---|---|
| `name` | string | **Required.** Stable plugin identity used for load order, attribution, dependency resolution. |
| `display_name` | string | Human-friendly name (defaults to `name`). |
| `author` | string | Author name. |
| `description` | string | Free text. |
| `url` | string | Project/support URL. |
| `support_email` | string | Contact email. |
| `version` | string | Semver, e.g. `"0.1.0"`. |
| `kcdx_min_version` | string | Minimum kcdx version this plugin needs (semver). |
| `version_independent` | bool | `true` if the plugin does not bind to a specific KCD2 build (default `false`). |
| `compatible_game_versions` | array of strings | KCD2 build versions this plugin targets, e.g. `["1.5.1164953"]`. |
| `default_position` | string | Load zone: `"before_game"` or `"after_game"`. Omit to let the engine derive it (engine builtins → before; user plugins → after). |
| `default_priority` | integer | `0`–`100` within the zone (default `50`). |
| `log_level` | string | Floor for the plugin's own log file: `trace`/`debug`/`info`/`warn`/`error`/`off` (default `info`). Warn/Error always pass. |
| `test_names` | array of strings | For test-suite plugins: the matrix row IDs this plugin promises to report. |

`[[plugin.dependencies]]` — zero or more dependency entries:

```toml
[[plugin.dependencies]]
name        = "kcdx.some-other-plugin"
min_version = "0.2.0"
optional    = false
```

| Key | Type | Meaning |
|---|---|---|
| `name` | string | **Required.** The depended-on plugin's `name`. |
| `min_version` | string | Minimum required version (semver). |
| `optional` | bool | `true` if the dependency is soft (default `false`). |

### `[entrypoints]` keys

| Key | Type | Meaning |
|---|---|---|
| `lua` | string or array | The before/default-slot Lua file(s), run in declared order at the plugin's load-order slot. A bare string is a one-element list. |
| `lua_after` | string or array | Optional after-game-slot Lua file(s). Run in the after_game phase at the plugin's priority, regardless of declared zone. |
| `dll` | string | The plugin DLL (a C++ plugin). |

### `[kcdx]` keys

| Key | Type | Meaning |
|---|---|---|
| `test_suite_only` | bool | `true` = the plugin runs only under dev mode (silent in production). Used by test-suite plugins. |

Engine settings (`dev_mode`, `dry_run`, `dev_log_*`) are **not** valid here —
they live in `<kcdx-engine>/engine.toml`. A plugin that sets them gets a
warning.

### The both-phase model

A plugin can run code before the game is up *and* after. In Lua, declare `lua`
(before/default slot) and/or `lua_after` (after-game slot); the C++ mirror is
the `kcdxPlugin_Load` export (before) and the optional `kcdxPlugin_PostGameLoad`
export (after) on the same plugin DLL. Both-phase work runs in load-order
priority within each phase.

### Cross-plugin ordering

A `priority` field on an individual `kcdx.hook{}` / `kcdx.bytes{}` call is **no
longer honoured** (kcdx logs a once-per-session notice if you set it).
Cross-plugin ordering comes from the plugin's `default_priority` (or the
engine `load_order.toml`); intra-plugin ordering is the order your `plugin.lua`
registers entries.

---

## 4. Core verbs

### kcdx.hook

Intercept a game function: run your Lua callback when the game calls it, and
optionally change its arguments, its return value, or whether it runs at all.

**Call shape:** a single named-field table. Returns a **handle** on successful
registration, or `(nil, err)` on a bad call.

```lua
local h = kcdx.hook{ name = "...", target = "...", before = function(...) ... end }
```

#### Fields

| Field | Type | Meaning |
|---|---|---|
| `name` | string | Optional label for logs and conflict messages (default `"lua_hook"`). |
| `description` | string | Optional free text. |
| one behaviour key | function | **Required, exactly one** of `before` / `after` / `around` / `replace` / `mid` — see [Modes](#modes). |
| one locator | — | **Required, exactly one** function-entry locator — see [Locators](#locators). (For `mode = "callsite"`, use `target_callsite` instead.) |
| `signature` | string | The ABI string. Required for `before`/`after`/`around`/`replace` **unless** `target` carries a verified one. Not used by `mid`. See [Signature grammar](#signature-grammar). |
| `mode` | string | Scope selector. The only value is `"callsite"`; omit for the default function-entry scope. **`mode` never names a behaviour** — the behaviour is the key (`before=`, etc.). |
| `captures` | table | **Required for `mid` only** — the register/memory values to read/write. See [mid](#mid). |
| `target_callsite` | table | **Required for `mode = "callsite"` only** — locates the call instruction. See [callsite](#callsite-scope). |
| `module` | string | Module the locator resolves against (default `"WHGame.dll"`). |
| `offset` | integer | For `mid`: the offset inside the function of the captured instruction. |

#### Returns

A **handle** userdata (see [Handle methods](#handle-methods)). The hook is not
installed yet — installation is [deferred](#2-glossary) to the end-of-zone
apply pass. The handle's `:applied()` reads `nil` until then.

#### Errors

Registration validates immediately and returns `(nil, err)` (the second value
is a teaching string) when:
- the argument is not a table;
- you attach zero, or more than one, behaviour key;
- you set zero, or more than one, function-entry locator;
- you pass an unknown/echoed `mode`;
- a `signature` / `pattern` / `target_callsite` fails to parse;
- a `before`/`after`/`around`/`replace` hook has no signature and `target`
  carries none (the engine will not invent an ABI);
- `mid` has no `captures`.

A failure that only becomes knowable at apply time (the locator does not
resolve on the running build, or a conflict is lost) does **not** return
`(nil, err)` — registration succeeds with a real handle, and the handle goes
`:applied() == false` with a `:reason()`. Read those in a
`kcdx.on("ready", ...)` callback.

#### Minimal snippet

```lua
local h = kcdx.hook{
    name   = "double_ui_pump",
    target = "CGame_per_frame_ui_pump",   -- name supplies address AND signature
    after  = function(ret) return ret * 2 end,
}
```

#### Locators

The hook needs to find its target. The **common path** is by name:

- **`target = "<name>"`** — the Address Library name of the function. The name
  resolves **both** the address and the verified signature, so you write no
  hex and no ABI. This is the path the engine is built around (the disassembler
  test, `cornerstones.md`). Use it whenever the engine knows the name (see
  `kcdx.addr` for what is named on your build).

The remaining locators are an **advanced/expert escape hatch** for targets the
library cannot name yet. When you use one you must supply `signature = "..."`
yourself, because there is no name for the engine to carry the ABI from. Set
**exactly one**:

- **`address = <pointer|integer>`** — a raw VA. Pass a `kcdx.memory.pointer`
  userdata or lightuserdata (exact); an integer is accepted but is lossy at
  pointer magnitudes (`lua-precision.md`).
- **`address_id = <number>`** — a numeric Address Library id.
- **`address_id = "<name>"`** — a string here is the same name-based locator as
  `target` (the readable name you see in `kcdx.addr`). Do not set both `target`
  and a string `address_id` — they are one slot.
- **`pattern = "<AOB>"`** — a byte/wildcard pattern scanned in `module`.
  `context` and `anchor_string` refine it (same shape as `kcdx.bytes`).
- **`target_symbol = "<name>"`** — an exported symbol name.
- **`target_lua_cfunction = "<name>"`** — a Lua C-function target.

#### Signature grammar

The signature tells the engine the function's ABI. Form:

```
<return-type> ( <arg> , <arg> , ... )
```

Each `<arg>` is `<type>` optionally preceded by a register pin (`<reg>:<type>`)
and optionally followed by an argument name (`<type> name` or
`<reg>:<type> name`). The arg name is yours; the engine ignores it (it is for
your readability). Examples:

```
"void ()"
"i32 (i32 seed)"
"i32 (wstr s)"
"void (ptr self, wstr szApp)"
"f64 (xmm0:f64 x, xmm1:f64 y)"
```

**Types:** `void` (return only), `i8` `i16` `i32` `i64`, `u8` `u16` `u32`
`u64`, `f32` `f64`, `ptr`, `bool`, `wstr` (wide string), `cstr` (C string).
`int` is an accepted alias for `i32`.

**Register pins** (advanced): `rax rcx rdx rbx rsi rdi r8`–`r15` for
non-float types; `xmm0`–`xmm15` for `f32`/`f64`. A GPR cannot hold a float type
and an XMM cannot hold a non-float — the parser rejects the mismatch. The arg
names `hook_skip` and `hook_retval` are reserved by the engine.

Parse errors come back as `(nil, err)` with a 1-based column index pointing at
the offending token.

#### Modes

Attach **exactly one** behaviour, under its own key. The callback's parameters
arrive positionally, typed by the signature; you name them in your own
`function(...)` list. **Mutation is by return — what you return is what flows
forward.**

- **`before = function(...args) ... end`** — runs before the original, which
  **always** runs afterwards. Return nothing to leave the args unchanged;
  return N values to replace the args. Use it to massage inputs.

  ```lua
  kcdx.hook{ name="bump", target="Add", signature="i32 (i32 seed)",
             before = function(seed) return seed + 1 end }
  ```

- **`after = function(ret) ... end`** — receives the original's return value;
  returns the (possibly changed) value. Use it to transform a result.

  ```lua
  kcdx.hook{ name="boost", target="Score", signature="i32 (i32)",
             after = function(ret) return ret + 1000 end }
  ```

- **`replace = function(...args) ... end`** — the original never runs; your
  return is the result. An empty `replace = function() end` suppresses the call
  entirely. Use it to substitute behaviour.

  ```lua
  kcdx.hook{ name="const", target="Cost", signature="i32 (i32)",
             replace = function(seed) return 42 end }
  ```

- **`around = function(orig, ...args) ... end`** — receives the original as a
  callable first parameter; call it zero, one, or many times, and return the
  result. The full wrap — the only mode that can conditionally skip the
  original.

  ```lua
  kcdx.hook{ name="wrap", target="Calc", signature="i32 (i32)",
             around = function(orig, seed) return 2 * orig(seed) end }
  ```

- **`mid = function(c) ... end`** — see [mid](#mid).

##### mid

A mid-function hook intercepts a single instruction at `offset` inside the
function and reads/writes named register/memory captures. It does **not** take
a `signature` (it doesn't need the function's ABI) — it takes `captures`.

The callback receives one argument: a table of capture handles. Each handle has
`:get()` and `:set(v)`. Return nothing to run the captured instruction; return
`"skip"` to skip it.

`captures` accepts two forms:

- **positional list** — `captures = { "rax", "[rcx+0x10]:i32" }` → handles
  keyed `c[1]`, `c[2]`, …
- **name map** — `captures = { hp = "rax", x = "[rcx+0x10]:i32" }` → handles
  keyed `c.hp`, `c.x`.

Each capture entry is a register/memory expression with an optional `:type`
suffix (the type defaults to `i64`). The expression is written straight off a
disassembler; recognized type tokens are `i8`–`i64`, `u8`–`u64`, `ptr`, `f32`,
`f64`, `float`, `double`, `bool`.

```lua
kcdx.hook{
    name     = "clamp_hp",
    address  = some_capture_site_pointer,
    offset   = 0,
    captures = { hp = "rax" },
    mid      = function(c)
        if c.hp:get() > 1000 then c.hp:set(1000) end
        -- return nothing → the captured instruction runs
        -- return "skip"  → it is skipped
    end,
}
```

##### callsite scope

`mode = "callsite"` redirects **one call instruction** (rather than the function
entry). It accepts the function-wrapping behaviours (`before` / `after` /
`around` / `replace`) but **not** `mid`. The patch target is given by a
`target_callsite` table — set **exactly one** of its locators:

| `target_callsite` key | Type | Meaning |
|---|---|---|
| `pattern` | string | AOB pattern of the call site. |
| `address_id` | number | Address Library id of the call site. |
| `rva` | string | Raw RVA. |
| `offset` | integer | Optional offset from the located point. |

When you use `mode = "callsite"`, do **not** also set a function-entry locator
(`target`/`address`/`pattern`/…) — the `target_callsite` carries the target.

#### Chaining

Multiple hooks can coexist on one target — kcdx installs one detour and fires an
ordered chain of your callbacks in load order. Several `before`s, several
`after`s, `before` + `after`, etc. all stack. To put more than one behaviour on
a target, make **separate** `kcdx.hook` calls (one behaviour per call).

When two hooks genuinely cannot coexist (incompatible signature on the shared
target, or two `replace`/`around` which the engine treats as exclusive), the
**later in load order** loses: its handle goes `:applied() == false` with a
reason, and the earlier one wins.

#### Handle methods

The userdata returned by `kcdx.hook` and `kcdx.bytes`:

| Method | Returns | Meaning |
|---|---|---|
| `h:name()` | string | The entry's name. |
| `h:applied()` | `nil` / `true` / `false` | `nil` = pending (apply pass hasn't run); `true` = applied; `false` = failed. |
| `h:reason()` | string or `nil` | The failure reason when `:applied() == false`; `nil` otherwise. |
| `h:wait_applied()` | — | **Not available yet.** Raises a clear error directing you to use `kcdx.on("ready", ...)`. (Returns self if already resolved.) |
| `tostring(h)` | string | `kcdx.handle<id=… name=… status=…>`. |

Because `:applied()` is `nil` in straight-line `plugin.lua` code, read final
status from a `kcdx.on("ready", ...)` callback (it fires after the apply pass).

---

### kcdx.bytes

Rewrite bytes at a located site. Succeeds the v0.1 `[[patch]]` TOML schema. A
replacement must be the same length as the original it overwrites — adding code
goes through `kcdx.hook`.

**Call shape:** a single named-field table. Returns a **handle** on successful
registration, or `(nil, err)` on a bad call. Like `kcdx.hook`, the actual write
is [deferred](#2-glossary) to the apply pass.

#### Fields

| Field | Type | Meaning |
|---|---|---|
| `name` | string | Optional label (default `"lua_bytes"`). |
| `description` | string | Optional free text. |
| `replacement` | string | **Required.** The bytes to write, e.g. `"45 31 F6"`. |
| one locator | — | **Required, exactly one** of `pattern`, `address_id` (number), `target_symbol`. |
| `original` | string | Optional verification bytes; if set, must match what is at the site, and must be the same length as `replacement`. |
| `module` | string | Module to resolve against (default `"WHGame.dll"`). |
| `offset` | integer | Offset from the located point (default `0`). |
| `idempotent` | bool | Skip the write if already applied (default `true`). |
| `context` | string | Optional context pattern to disambiguate the locator. |
| `anchor_string` | string | Optional anchor-string locator refinement. |

#### Returns / Errors

A handle (same as `kcdx.hook`). Returns `(nil, err)` when: the argument is not
a table; zero or more than one locator is set; `replacement` is missing; a
pattern/bytes string fails to parse; or `original` length ≠ `replacement`
length.

#### Minimal snippet

```lua
local h = kcdx.bytes{
    name        = "nop_check",
    address_id  = 12345,
    original    = "44 8A F0",
    replacement = "90 90 90",
}
```

---

### kcdx.on

Subscribe to a lifecycle event or a custom cross-plugin event.

**Call shape:** positional `(event, fn)`. Returns nothing on success; returns
`(nil, err)` on a bad argument or an unknown event.

```lua
kcdx.on(event, fn)
```

#### Arguments

| Arg | Type | Meaning |
|---|---|---|
| `event` | string | `"ready"`, one of the 9 game lifecycle events, or a `"<publisher>:<event>"` custom event. See [Lifecycle events](#6-lifecycle-events). |
| `fn` | function | The callback. `"ready"` and the no-arg lifecycle events take no arguments; the three save/load events pass a save basename string; a custom event receives the publisher's payload. |

#### Errors

`(nil, err)` if `event` is not a string, `fn` is not a function, or `event` is
a bare name that is neither `"ready"` nor a known lifecycle event (the error
lists the valid names and explains the `"<publisher>:<event>"` form for custom
events).

#### Minimal snippet

```lua
kcdx.on("ready", function()
    -- fires once, after THIS plugin's hooks/bytes are applied
    assert(my_hook:applied() == true)
end)
```

---

### kcdx.command

Register a console command runnable from the in-game `~` console.

**Call shape:** a single named-field table. Returns `true` on success; `(nil,
err)` on failure. Unlike hooks/bytes, registration is **immediate** (not
deferred) — commands have no conflict semantics.

```lua
kcdx.command{ name = "...", callback = function(args) ... end }
```

#### Fields

| Field | Type | Meaning |
|---|---|---|
| `name` | string | **Required.** The command name, unique across the process. |
| `callback` | function | **Required.** Runs on the main thread when the command fires; receives one `args` table. |
| `description` | string | Optional help text shown for `help <name>`. |

#### The `args` table

The callback's single argument is a table that is **both** an array of the
user-supplied argument strings **and** carries a `raw` field:

- `args[1]`, `args[2]`, … — the typed arguments (excluding the command name
  itself).
- `#args` — the argument count.
- `args.raw` — the full command line string.

#### Returns / Errors

`true` on success. `(nil, err)` if `name`/`callback` are missing or mistyped, if
`description` is present but not a string, if the name is already registered, or
if the console refuses the registration (duplicate name, no free slots, or
IConsole not yet ready).

#### Minimal snippet

```lua
kcdx.command{
    name        = "outfit_dump",
    description = "Dump outfit state to the log.",
    callback    = function(args)
        kcdx.log.info("CMD", "argc=" .. #args)
        if args[1] then kcdx.log.info("CMD", "first=" .. args[1]) end
    end,
}
```

To run a command from Lua, see [`kcdx.console.execute`](#kcdxconsole).

---

### kcdx.publish

Broadcast a custom event to subscribers in any plugin (the counterpart to
`kcdx.on("<publisher>:<event>", ...)`).

**Call shape:** positional `(event [, payload])`. Returns the number of
subscribers fired (an integer; `0` means nobody listened, not an error). Returns
`(nil, err)` on a bad `event`.

```lua
local n = kcdx.publish(event, payload)
```

#### Arguments

| Arg | Type | Meaning |
|---|---|---|
| `event` | string | The **bare** custom event name. The engine stamps your plugin name in front, so subscribers hear it as `"<your_plugin>:<event>"`. |
| `payload` | any (optional) | Any Lua value, passed **by reference** to each subscriber (a table is shared, not copied). Omit to fire subscribers with no argument. |

#### Behaviour notes

- The event is namespaced by your plugin: a subscriber uses
  `kcdx.on("<your_plugin>:<event>", fn)`.
- The payload is shared by reference — treat it as immutable by convention.
- An anonymous publisher (e.g. from the console) fires under `"<anon>:<event>"`
  and logs a warning.

#### Minimal snippet

```lua
-- in plugin "violetanvil":
kcdx.publish("outfit_changed", { slot = 2, name = "Noble" })

-- in another plugin:
kcdx.on("violetanvil:outfit_changed", function(payload)
    kcdx.log.info("MOD", "outfit -> " .. payload.name)
end)
```

---

### kcdx.code

Allocate a region of executable memory and (optionally) fill it with machine
code and publish its address as a named symbol. Succeeds the v0.1
`[[trampoline]]` TOML schema. Use it to inject code (not just rewrite
equal-length bytes — that is `kcdx.bytes`): build a routine other hooks branch
to, or reserve a NOP region other plugins patch into by symbol.

**Call shape:** a single named-field table. Unlike `kcdx.hook`/`kcdx.bytes`,
`kcdx.code` is **not deferred** — it allocates *immediately at the call* and
returns a live [`kcdx.memory.pointer`](#kcdxmemory) userdata to the region (so
you can write code into it with `:set_byte`/`:set_dword`/… or pass it as a hook
target right away). Returns `(nil, err)` on a bad call.

#### Fields

| Field | Type | Meaning |
|---|---|---|
| `name` | string | **Required.** Label used in logs and export-collision diagnostics. |
| `bytes` | string | Optional initial machine code as a hex string, e.g. `"48 83 EC 28"`. Copied to the head of the region. |
| `size` | integer | Optional total bytes to allocate. Defaults to the length of `bytes`. If larger than `bytes`, the tail is **NOP-padded** (`0x90`) so other plugins can patch into the unused space. Must be ≥ the length of `bytes`. |
| `pool` | string | Optional. `"branch"` (default) places the region within ±2 GB of `WHGame.dll`'s code so a `rel32` branch can reach it; `"local"` places it anywhere (use when ±2 GB reachability is not required). |
| `export` | string | Optional. Publishes the allocated address under this symbol name; a later `kcdx.hook{ target_symbol = ... }` or `kcdx.bytes{ target_symbol = ... }` (in this or any plugin) resolves to it. |

You must declare `bytes` or `size` (or both).

#### Returns / Errors

A live `kcdx.memory.pointer` userdata to the region. Returns `(nil, err)` when:
the argument is not a table; `name` is missing; `bytes` fails to parse; `size`
is not a positive integer or is smaller than `bytes`; `pool` is not
`"branch"`/`"local"`; the pool cannot allocate (out of space, or no
`rel32`-reachable region for `"branch"`); or `export` collides with a symbol
another plugin already registered (the error names the prior owner — the region
is still allocated, but is unreachable by that symbol name).

#### Minimal snippet

```lua
-- reserve a 256-byte region, publish it, write a RET into it:
local region = kcdx.code{
    name   = "outfit_gate_logic",
    size   = 256,
    pool   = "branch",
    export = "violetanvil.outfit_gate_logic",
}
region:set_byte(0xC3)   -- write a RET into the base (the region is live now)
-- elsewhere: kcdx.hook{ target_symbol = "violetanvil.outfit_gate_logic", ... }
```

---

### require — multi-file plugins

`require("helper")` loads a sibling Lua file from **your plugin's own folder**.
This is the idiomatic Lua form, but kcdx owns the resolution for plugin chunks:

- A bare `require("helper")` from your `plugin.lua` resolves to *your*
  `helper.lua`, not the EXE directory or another plugin's file.
- The module cache is namespaced per plugin: plugin A's `require("helper")` and
  plugin B's `require("helper")` get **different** modules — no cross-plugin
  collision, even though there is one shared Lua state.
- A second `require("helper")` within the same plugin returns the cached module.
- A `kcdx.*` call made from inside a `require`'d helper attributes to your
  plugin (not `<anon>`) — so `kcdx.on`, `kcdx.publish`, `kcdx.hook` from a
  helper work exactly as they do from `plugin.lua`.

```lua
-- plugin.lua
require("helper")     -- runs sibling helper.lua under this plugin's identity
```

---

## 5. Domains

### kcdx.log

Structured logging. Grouped domain; positional `(category, message)`.

| Method | Args | Notes |
|---|---|---|
| `kcdx.log.info(category, message)` | strings | Info level. |
| `kcdx.log.warn(category, message)` | strings | Warn level. |
| `kcdx.log.error(category, message)` | strings | Error level. |
| `kcdx.log.debug(category, message)` | strings | Dev-mode only. |
| `kcdx.log.trace(category, message)` | strings | Dev-mode only. |

`category` is a stable tag for the feature (e.g. `"MYMOD"`); `message` is
pre-formatted text. The engine does not do printf-style marshaling across the
boundary — build your string with `string.format` yourself. A one-argument call
(`kcdx.log.info("just a message")`) treats the sole string as the message under
category `"LUA"`. These calls return nothing.

```lua
kcdx.log.info("DMG", string.format("hit for %.1f", dmg))
```

### kcdx.memory

Direct memory access and runtime native interop. Grouped domain.

Pointer values cross the Lua boundary as a **pointer userdata**, never a raw
number — CryEngine's Lua 5.1 uses `LUA_NUMBER=float`, so a pointer-magnitude
integer silently corrupts (`lua-precision.md`). Pass pointer userdata between
these calls; only call `:get_address()` for a display/opaque integer.

#### Table-level functions

| Call | Args | Returns |
|---|---|---|
| `kcdx.memory.pointer(address)` | integer address (optional, default 0) | A pointer userdata wrapping `address`. |
| `kcdx.memory.get_module_base_address([module])` | optional module name (string; default `"WHGame.dll"`) | Pointer userdata of the module base (null pointer if not found). |
| `kcdx.memory.scan_pattern(pattern)` | AOB string | Pointer userdata of the first match in `WHGame.dll` (null if no match). |
| `kcdx.memory.scan_pattern_from_module(module, pattern)` | module name, AOB string | Pointer userdata of the first match in `module` (null if no match). |
| `kcdx.memory.allocate(size)` | integer byte count | Pointer userdata of a fresh zeroed buffer (null on failure / size ≤ 0). |
| `kcdx.memory.free(ptr)` | a pointer userdata from `allocate` | Frees the buffer; nils the pointer. Returns nothing. |
| `kcdx.memory.dynamic_call(table)` | see below | A callable userdata, or `(nil, err)`. |
| `kcdx.memory.dynamic_hook(table)` | see below | A handle userdata, or `(nil, err)`. |

> **Note:** these `kcdx.memory` calls are an advanced/expert surface — pattern
> scanning, raw allocation, and runtime ABI declaration ask you to do work the
> name-based `kcdx.hook{ target = }` path does for you. For function
> interception prefer `kcdx.hook`; reach for `dynamic_hook`/`dynamic_call` only
> when you need runtime installation against an address you already hold.

#### The pointer userdata

A `kcdx.memory.pointer` carries typed read/write accessors, arithmetic, and
RIP-relative resolution. Methods (called with `:`):

Arithmetic / navigation (each returns a new pointer userdata):
- `p:add(offset)` — `p + offset`.
- `p:sub(offset)` — `p - offset`.
- `p:deref()` — read a pointer-width value at `p` and wrap it.
- `p:rip()` — resolve a RIP-relative rel32 (reads the disp32 at `p`, advances
  past it). For `CALL`/`JMP`/`LEA` rel32.
- `p:rip_cmp()` — like `rip()` but skips a 1-byte opcode first (5-byte CMP
  rel32).

Typed reads (return a Lua number; `get_qword` is lossy at pointer magnitudes —
use `:deref()` for pointers):
- `p:get_byte()`, `p:get_word()`, `p:get_dword()`, `p:get_qword()`
- `p:get_float()`, `p:get_double()`
- `p:get_string()` — read a C string at `p`.

Typed writes (return nothing):
- `p:set_byte(v)`, `p:set_word(v)`, `p:set_dword(v)`, `p:set_qword(v)`
- `p:set_float(v)`, `p:set_double(v)`
- `p:set_string(s, max_length)` — write a C string, bounded by `max_length`.

Predicates / accessors:
- `p:is_null()` → bool, `p:is_valid()` → bool.
- `p:get_address()` → integer VA. **Lossy** at pointer magnitudes — for display
  only; never pass it back into a kcdx API that wants an exact address (pass the
  pointer userdata itself).

A read/write through a null pointer raises a Lua error.

```lua
local base = kcdx.memory.get_module_base_address()
local flag = base:add(0x1234)
if flag:get_dword() ~= 0 then flag:set_dword(0) end
```

#### kcdx.memory.dynamic_call

JIT a callable for an arbitrary native function and invoke it from Lua.

**Argument table:**

| Field | Type | Meaning |
|---|---|---|
| `target` | pointer userdata / lightuserdata / integer VA | **Required.** The function to call. |
| `return_type` | string | A signature type token (default `"void"`). |
| `param_types` | array of strings | Type tokens for each parameter (default empty). |

Returns a **callable userdata** — call it like a function with your args, or
`(nil, err)` on failure. Numeric args/returns cross as `LUA_NUMBER=float`;
pointer-magnitude values lose precision through this path (use the pointer
userdata surface for pointer returns).

```lua
local memcpy = kcdx.memory.dynamic_call{
    target      = memcpy_addr,           -- a pointer userdata
    return_type = "ptr",
    param_types = {"ptr", "ptr", "i64"},
}
local result = memcpy(dst, src, length)
```

#### kcdx.memory.dynamic_hook

Install a runtime hook on a target address (a lower-level peer of `kcdx.hook`;
it does not participate in the deferred apply pass — it installs immediately and
lives for the session).

**Argument table:**

| Field | Type | Meaning |
|---|---|---|
| `name` | string | **Required.** Used for logs and first-wins conflict messages. |
| `target` | pointer userdata / integer VA | **Required.** The function to hook. |
| `return_type` | string | Signature type token (default `"void"`). |
| `param_types` | array of strings | Type tokens (default empty). |
| `pre_callback` | function | Optional. Runs before the original. |
| `post_callback` | function | Optional. Runs after. |

At least one of `pre_callback` / `post_callback` is required. Returns a **handle
userdata** (with `handle:get_target()` → pointer userdata), or `(nil, err)`.
Hooks installed this way are first-hook-wins per target. Keep the handle alive
(store it in a table) — if it is collected, the hook is disabled.

```lua
local h = kcdx.memory.dynamic_hook{
    name        = "log_intercept",
    target      = some_pointer,
    return_type = "void",
    param_types = {"ptr"},
    pre_callback = function(...) kcdx.log.info("HOOK", "called") end,
}
```

### kcdx.addr

A snapshot table of every Address Library name that resolves on the running
KCD2 build, each mapped to a `kcdx.memory.pointer` userdata. Built once at
startup. Names that do not resolve (wrong game version, unverified, or zero
RVA) are absent — indexing a missing name gives the normal Lua "attempt to index
nil" error, surfacing the unmet dependency immediately.

It is a plain table, so iterate it with `pairs`:

```lua
for name, ptr in pairs(kcdx.addr) do
    kcdx.log.info("ADDR", name .. " -> " .. tostring(ptr))
end

local p = kcdx.addr.lua_pcall    -- pointer userdata, or nil if unnamed here
```

There is no `kcdx.address(...)` function in the Lua surface — name lookup is
this table, and the `kcdx.hook{ target = "<name>" }` / `address_id = "<name>"`
locators resolve names directly.

### kcdx.console

| Call | Args | Returns |
|---|---|---|
| `kcdx.console.execute(commandLine)` | string command line | `true` on success; `false` if IConsole isn't ready; `(nil, err)` on a non-string argument. |

Runs a command line exactly as if typed into the `~` console, through the same
synchronous main-thread dispatch path. A command registered with `kcdx.command`
fires same-stack before `execute` returns.

```lua
local ok = kcdx.console.execute("cap26_cmd 42 hello")
```

### kcdx.test

| Call | Args | Returns |
|---|---|---|
| `kcdx.test.report(name, pass, reason)` | string name, boolean pass, optional string reason | nothing |

Records a test-suite result, rolled into the engine's `Test suite: X/Y passing`
summary. The mirror of the C++ `ReportTestResult`. Used by `test_suite_only`
plugins under dev mode.

```lua
kcdx.test.report("CAP-XX", got == expected, "round-trip ok")
```

### kcdx.dev

| Call | Args | Returns |
|---|---|---|
| `kcdx.dev.is_enabled()` | none | bool — whether engine dev mode is on. |
| `kcdx.dev.on_ready(fn)` | function | Invokes `fn()` immediately if `kcdx.*` is fully populated; returns `true` if it ran, `false` if not yet ready. (Sugar — by the time your Lua can call this, kcdx is ready.) |

```lua
if kcdx.dev.is_enabled() then
    kcdx.log.debug("MYMOD", "dev diagnostics on")
end
```

### kcdx.lua

VM-introspection helpers.

| Call | Args | Returns |
|---|---|---|
| `kcdx.lua.cfunction_address(fn)` | a Lua C function value | A `kcdx.memory.pointer` userdata of the backing C function pointer, or `(nil, err)` if the argument is not a C function. |
| `kcdx.lua._probe_numbers()` | none | nothing — a dev-mode numeric-precision diagnostic that logs to `kcdx-dev.log` under category `LUA / NUMBER_PROBE`. Diagnostic only. |

`cfunction_address` returns a pointer userdata (never an integer) so you can
hand it straight to `kcdx.memory.dynamic_hook` as `target`.

```lua
local addr = kcdx.lua.cfunction_address(System.LogAlways)
kcdx.memory.dynamic_hook{ name = "log_hook", target = addr,
                          pre_callback = function() end }
```

---

## 6. Lifecycle events

`kcdx.on(event, fn)` accepts the following.

### `"ready"`

Fires **once**, per plugin, after that plugin's zone apply pass completes — the
post-apply moment when every handle your plugin captured has a final
`:applied()` / `:reason()`. Takes no arguments. This is the place to assert
your hooks installed.

```lua
local h = kcdx.hook{ name="x", target="Add", signature="i32 (i32)",
                     before=function(s) return s end }
kcdx.on("ready", function()
    if h:applied() then kcdx.log.info("MYMOD", "hooked")
    else kcdx.log.warn("MYMOD", "hook failed: " .. tostring(h:reason())) end
end)
```

### The 9 game lifecycle events

Each is a bridge over an engine message and fires **every** time that message
fires. Six pass no arguments; three (the save/load events) pass the save
**basename** string.

| Event | Argument | Fires when |
|---|---|---|
| `post_load` | none | The game's post-load message. |
| `post_post_load` | none | The subsequent post-post-load message. |
| `input_loaded` | none | Input is loaded (fires every boot — the standard auto-pass trigger). |
| `new_game` | none | A new game starts. |
| `pre_load_game` | none | Before a save loads. |
| `post_load_game` | none | After a save finishes loading. |
| `save_game` | basename (string) | A save is written. |
| `load_game_selected` | basename (string) | A save is selected to load. |
| `delete_game` | basename (string) | A save is deleted. |

```lua
kcdx.on("input_loaded", function()
    kcdx.log.info("MYMOD", "world is up")
end)

kcdx.on("save_game", function(basename)
    kcdx.log.info("MYMOD", "saved to " .. basename)
end)
```

### Custom events — `"<publisher>:<event>"`

Any event name containing `:` subscribes to a `kcdx.publish` broadcast. The
form is the publishing plugin's name, a colon, then the bare event name. The
callback receives the published payload (by reference).

```lua
kcdx.on("violetanvil:outfit_changed", function(payload)
    kcdx.log.info("MYMOD", "slot " .. payload.slot)
end)
```

A bare event name that is neither `"ready"` nor a lifecycle event is rejected
with a teaching error — custom events are always heard via the `:` form.

---

## 7. Cross-cutting rules

- **Callbacks run on the main thread.** kcdx fires your Lua callbacks
  (hook callbacks, lifecycle/`ready` callbacks, command callbacks, publish
  subscribers) on the game's main thread. Only hook functions that the game
  itself runs on the main thread; hooking an audio/physics/streaming worker
  function is unsafe (`lua-callback-threading.md`).

- **Pointers are userdata, not numbers.** CryEngine's Lua 5.1 uses
  `LUA_NUMBER=float` (24-bit mantissa). A pointer-magnitude integer silently
  rounds to a 16 MB grid when it crosses the Lua boundary as a number. Always
  pass `kcdx.memory.pointer` userdata between kcdx calls; only use
  `:get_address()` for an opaque display value, never to feed another kcdx API
  (`lua-precision.md`).

- **Hooks and bytes apply later, not at the call.** `kcdx.hook` and
  `kcdx.bytes` validate immediately (so a malformed call returns `(nil, err)` in
  straight-line code) but **install** in the end-of-zone apply pass, after every
  plugin has registered. A handle's `:applied()` is `nil` until then. To act on
  the outcome, use `kcdx.on("ready", ...)` — it fires after the apply pass with
  final handle status. (`kcdx.command`, `kcdx.console.execute`, and the
  `kcdx.memory.*` runtime calls apply immediately.)

- **Errors teach.** A bad kcdx call returns `(nil, message)` (or raises, for the
  `kcdx.memory.pointer` null-pointer case) with a message in your terms naming
  the fix. Read the second return value.

- **Plugin errors go to your log.** An uncaught error in your `plugin.lua` is
  reported to your plugin's own log (file and, where the engine can recover it,
  line). (A current CryEngine limitation can surface plugin.lua errors without
  a line number — see the engine's outstanding work.)

- **`require` is plugin-scoped.** `require("helper")` resolves your own folder
  and a per-plugin cache; it never reaches another plugin's module (see
  [require](#require--multi-file-plugins)).

---

## 8. Planned — not yet available

The following appear in the kcdx authoring model and the restructure plan but
are **not callable today** — there is no binder for them. Do not write code
against them.

- **`kcdx.cosave.*`** — per-save persistence (read/write data tied to a save).
  Tracked in the restructure plan, Phase 2; not built yet.
- **`kcdx.scan{...}`** — diagnostic AOB scan as a top-level verb. Tracked in the
  restructure plan, Phase 2; not built yet. (For runtime scanning today, use the
  `kcdx.memory.scan_pattern*` domain calls.)

Gameplay domains (`kcdx.player.*`, `kcdx.world.*`, `kcdx.dialogue.*`,
`kcdx.quest.*`, `kcdx.inventory.*`, `kcdx.assets.*`) are roadmap items (Phase 9+)
and are not built.
