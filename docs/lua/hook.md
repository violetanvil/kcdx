# kcdx.hook
> Part of the [kcdx Lua API](index.md).

Intercept a game function: run your Lua callback when the game calls it, and
optionally change its arguments, its return value, or whether it runs at all.

`kcdx.hook` is a table of **sub-verbs**, one per interception mode. You pick the
mode by the sub-verb you call:

```lua
kcdx.hook.before("WHGame.dll", "IsInCombat", function(self) ... end)
```

Every sub-verb takes the **module** name as its required first argument, the
**target** as its second, then the callback (and, for some, a locator). The name
resolves the address AND the verified ABI — you write no hex and no signature on
the common path.

## The call shape (every sub-verb)

```
kcdx.hook.before (module, target, [locator], callback, [opts])
kcdx.hook.after  (module, target, [locator], callback, [opts])
kcdx.hook.around (module, target, [locator], callback, [opts])
kcdx.hook.replace(module, target, [locator], callback, [opts])
kcdx.hook.insert_before(module, target, locator, callback, [opts])  -- locator REQUIRED
kcdx.hook.insert_after (module, target, locator, callback, [opts])  -- locator REQUIRED
```

- **`module`** (required, 1st) — the DLL the target lives in, e.g.
  `"WHGame.dll"`. There is **no default** — you type it every time. This is
  honest about multi-DLL coverage: copy an example and you cannot accidentally
  hook the wrong module via a hidden default.
- **`target`** (required, 2nd) — the function to hook, as **either**:
    - a **name string** — a curated engine name, your own
      [author-declared target](targets.md), or a name you exposed via
      `kcdx.declare`. The name resolves both the address and the verified ABI.
    - a **[`kcdx.functions.*` reference value](functions.md)** — e.g.
      `kcdx.functions.WHGame.IsInCombat`. The engine dispatches by type: a
      reference carries its resolved name and signature, so you hook another
      author's function BY NAME with no disassembly.
- **`[locator]`** (optional 3rd for before/after/around/replace; **required**
  for the insert verbs) — a [`kcdx.locator.*`](locator.md) value naming a point
  inside the function. Omit it on before/after/around/replace to hook the
  **function entry** (the everyday case).
- **`callback`** (required) — your Lua function. Its parameters arrive
  positionally, typed by the signature; you name them in your own `function(...)`
  list. **Mutation is by return — what you return is what flows forward.**
- **`[opts]`** (optional, trailing table) — the non-positional knobs:
  `name`, `description`, `signature` (only when you use an advanced raw locator
  the engine cannot carry an ABI for), `off_thread`, and the advanced/expert
  locators (`address` / `pattern` / `address_id` / `target_symbol` /
  `target_lua_cfunction`). Required → positional; optional → this table.

Each sub-verb returns a **handle** on successful registration, or `(nil, err)`
(a teaching string) on a bad call. The hook is not installed yet — installation
is [deferred](index.md#2-glossary) to the end-of-zone apply pass, so `:applied()`
reads `nil` until then.

## Minimal snippet

```lua
-- Double a return value. The name supplies address AND signature — no hex,
-- no signature string.
local h = kcdx.hook.after("WHGame.dll", "CGame_per_frame_ui_pump",
    function(ret) return ret * 2 end)
```

## Modes (the sub-verbs)

- **`before(module, target, [locator], fn)`** — runs before the original, which
  **always** runs afterwards. Return nothing to leave the args unchanged; return
  N values to replace the args. Use it to massage inputs.

  ```lua
  kcdx.hook.before("WHGame.dll", "Add", function(seed) return seed + 1 end,
      { signature = "i32 (i32 seed)" })
  ```

- **`after(module, target, [locator], fn)`** — receives the original's return
  value; returns the (possibly changed) value. Use it to transform a result.

  ```lua
  kcdx.hook.after("WHGame.dll", "Score", function(ret) return ret + 1000 end,
      { signature = "i32 (i32)" })
  ```

- **`replace(module, target, [locator], fn)`** — the original never runs; your
  return is the result. An empty `function() end` suppresses the call entirely.
  Use it to substitute behaviour.

  ```lua
  kcdx.hook.replace("WHGame.dll", "Cost", function(seed) return 42 end,
      { signature = "i32 (i32)" })
  ```

- **`around(module, target, [locator], fn)`** — receives the original as a
  callable first parameter; call it zero, one, or many times, and return the
  result. The full wrap — the only mode that can conditionally skip the original.

  ```lua
  kcdx.hook.around("WHGame.dll", "Calc", function(orig, seed) return 2 * orig(seed) end,
      { signature = "i32 (i32)" })
  ```

### insert_before / insert_after

`insert_before` / `insert_after` run your callback at a **statement inside** the
function (not at the entry), located by a required [`kcdx.locator.*`](locator.md)
value. The callback receives a **named table of captures** — the live
register/memory values at that point:

```lua
kcdx.hook.insert_after("WHGame.dll", "CheckOutfitSwap",
    kcdx.locator.first_call_to("IsInCombat"),
    function(captures)
        kcdx.log.info("OUTFIT", "rax=%d", captures.rax)
        -- return nothing → captures unchanged, execution continues
        -- return a table with capture-name keys → those captures are written
        --   back to the registers/memory after the callback returns
    end)
```

Which captures are available depends on the located statement (the curated
statement metadata determines what is live there). Returning a table writes the
named captures back — the same return-flow shape as `before`.

> **Engine support.** The `insert_before` / `insert_after` sub-verbs and their
> registration validate today; the engine's curated-statement capture-thunk
> apply path lands in a later step, so an insert is currently deferred at apply
> with a teaching reason (`:applied() == false`). For an **offset-based**
> capture today, use [`mid`](#mid).

## Advanced sub-verbs

These two are beyond the six common verbs, for cases the statement locators do
not yet cover.

### mid

`kcdx.hook.mid(module, target, offset, captures, callback, [opts])` intercepts a
**single instruction at a byte offset** inside the function (or a raw code
region) and reads/writes named register/memory captures. It does **not** take a
signature (it does not need the function's ABI) — it takes a `captures` table.

- `target` — typically a raw address (a [`kcdx.memory.pointer`](memory.md), e.g.
  a `kcdx.code` region) pointing at or near the captured instruction.
- `offset` — the byte offset of the captured instruction from `target`.
- `captures` — the register/memory values to read/write. Two forms:
    - **positional list** — `{ "rax", "[rcx+0x10]:i32" }` → handles keyed `c[1]`, `c[2]`, …
    - **name map** — `{ hp = "rax", x = "[rcx+0x10]:i32" }` → handles keyed `c.hp`, `c.x`.
  Each entry is a register/memory expression with an optional `:type` suffix
  (default `i64`), written straight off a disassembler. Recognized type tokens:
  `i8`–`i64`, `u8`–`u64`, `ptr`, `f32`, `f64`, `float`, `double`, `bool`.

Each capture handle has `:get()` and `:set(v)`. Return nothing to run the
captured instruction; return `"skip"` to skip it.

```lua
kcdx.hook.mid("WHGame.dll", some_capture_site_pointer, 0, { hp = "rax" },
    function(c)
        if c.hp:get() > 1000 then c.hp:set(1000) end
        -- return nothing → the captured instruction runs
        -- return "skip"  → it is skipped
    end)
```

> **C++ mirror.** The C++ peer is `kcdxHookInterface::Mid`
> ([../cpp/hook.md#mid](../cpp/hook.md#mid)); its callback returns an `int`
> `kcdxMidResult` (`Run = 0` / `Skip = 1`) where Lua returns nothing / `"skip"`.

### callsite

`kcdx.hook.callsite(module, callsite, mode, callback, [opts])` redirects **one
call instruction** (rather than the function entry) — only that caller is
affected; every other caller of the same callee is untouched.

- `callsite` — a `target_callsite` table locating the call instruction; set
  **exactly one** of `rva` (a `"<module> @ rva 0x…"` string), `pattern` (an AOB),
  or `address_id` (an Address Library id). An optional `offset` refines the match.
- `mode` — the wrapping behaviour, one of `"before"` / `"after"` / `"around"` /
  `"replace"`. It operates on the **called** function's ABI, so pass its
  `signature` in `[opts]`.

```lua
kcdx.hook.callsite("WHGame.dll", { rva = some_call_site_rva }, "before",
    function(x) return x + 1 end,
    { signature = "i32 (i32 x)" })
```

## Locators (the common path and the escape hatch)

The **common path is the target name** — it resolves both the address and the
verified signature, so you write no hex and no ABI. The name resolves three ways,
by [precedence](targets.md#resolving-a-name--self--engine--other) (self > engine
> other):

- an **engine** [Address Library](addr.md) name (`kcdx.addr` lists what is named
  on your build);
- one of **your own** [author-declared targets](targets.md) — including a
  `pattern`- or `target_symbol`-located target, resolved by name end-to-end (the
  engine carries the hex and ABI you declared once);
- another plugin's target, by its explicit `"<author>.<plugin>.<name>"` form
  (e.g. `"redmoon.outfit.open_inventory"`).

When a `pattern`/`rva` author-target supplies the address, its `signature`
carries the ABI — so a named pattern site needs no `signature` in `[opts]`.

The remaining locators are an **advanced/expert escape hatch** for targets the
library cannot name yet. When you use one you must supply `signature = "..."` in
`[opts]`, because there is no name for the engine to carry the ABI from. Pass
**exactly one**, either as the `target` positional (a raw address) or an `[opts]`
key:

- **a raw VA as `target`, or `address = <pointer|integer>` in `[opts]`** — a raw
  virtual address. Pass a `kcdx.memory.pointer` userdata or lightuserdata
  (exact); an integer is accepted but is lossy at pointer magnitudes (Lua's
  `LUA_NUMBER` is float — integers beyond 2^24 round).
- **`address_id = <number>`** — a numeric Address Library id.
- **`address_id = "<name>"`** — a string here is the same name-based locator as
  the target name. Do not set both the target name and a string `address_id`.
- **`pattern = "<AOB>"`** — a byte/wildcard pattern scanned in `module`.
  `context` and `anchor_string` refine it (same shape as `kcdx.bytes`).
- **`target_symbol = "<name>"`** — an exported symbol name.
- **`target_lua_cfunction = "<name>"`** — a Lua C-function target.

> **Named target + explicit `signature` — conflict contract.** If you name a
> target that carries a verified ABI **and** also pass `signature = "..."` in
> `[opts]`, the **explicit signature wins** (the deliberate-override case — you
> may know better than the seed, or be overriding a stale row). The engine
> consults the verified ABI only to **detect** a conflict, not to override
> yours: when your explicit signature is **not** compatible with the verified
> ABI, the engine emits a teaching diagnostic (`HOOK_SIG_GATE`, naming the
> target, both signatures, and that the explicit one is used as authored) and
> then **proceeds with your explicit signature**. The severity tells you how
> serious the disagreement is:
>
> - **Hard conflict → `ERROR`** (action `explicit_overrides_verified_hard`,
>   `severity=hard`, `crash_risk=true`). A different **argument count** or
>   **return-register width** — your signature mis-describes the call frame of a
>   **live engine function**, a known crash risk. The hook still installs (the
>   override is honored), but if the game crashes in or after this hook, this is
>   the cause.
> - **Soft conflict → `WARN`** (action `explicit_overrides_verified`,
>   `severity=soft`). Same arg count and return width — only a per-slot type
>   nuance differs (e.g. `ptr` vs `i64`).
>
> To silence the diagnostic, drop `signature` (let the name carry the verified
> ABI) or correct it to match.

## Signature grammar

When you use an advanced raw locator (so the engine has no name to carry the
ABI), the signature in `[opts]` tells the engine the function's ABI. Form:

```
<return-type> ( <arg> , <arg> , ... )
```

Each `<arg>` is `<type>` optionally preceded by a register pin (`<reg>:<type>`)
and optionally followed by an argument name. The arg name is yours; the engine
ignores it. Examples:

```
"void ()"
"i32 (i32 seed)"
"i32 (wstr s)"
"void (ptr self, wstr szApp)"
"f64 (xmm0:f64 x, xmm1:f64 y)"
```

**Types:** `void` (return only), `i8` `i16` `i32` `i64`, `u8` `u16` `u32` `u64`,
`f32` `f64`, `ptr`, `bool`, `wstr` (wide string), `cstr` (C string). `int` is an
accepted alias for `i32`.

**Register pins** (advanced): `rax rcx rdx rbx rsi rdi r8`–`r15` for non-float
types; `xmm0`–`xmm15` for `f32`/`f64`. A GPR cannot hold a float type and an XMM
cannot hold a non-float — the parser rejects the mismatch. The arg names
`hook_skip` and `hook_retval` are reserved by the engine. Parse errors come back
as `(nil, err)` with a 1-based column index pointing at the offending token.

## Errors

Registration validates immediately and returns `(nil, err)` when:
- `module` (the 1st positional) is missing or not a string;
- the callback is missing or not a function;
- you set zero, or more than one, function-entry locator;
- a `signature` / `pattern` / `target_callsite` fails to parse;
- a `before`/`after`/`around`/`replace` hook has no signature and the target
  carries none (the engine will not invent an ABI);
- `mid` has no `captures`;
- an `insert_before`/`insert_after` has no required locator;
- an `[opts]` table carries an unrecognized key (a typo fails loud).

A failure that only becomes knowable at apply time (the locator does not resolve
on the running build, or a conflict is lost) does **not** return `(nil, err)` —
registration succeeds with a real handle, and the handle goes `:applied() ==
false` with a `:reason()`. Read those in a `kcdx.on("ready", ...)` callback.

## Chaining

Multiple hooks can coexist on one target — kcdx installs one detour and fires an
ordered chain of your callbacks in load order. Several `before`s, several
`after`s, `before` + `after`, etc. all stack. To put more than one behaviour on a
target, make **separate** sub-verb calls (one behaviour per call).

When two hooks genuinely cannot coexist (incompatible signature on the shared
target, or two `replace`/`around` which the engine treats as exclusive), the
**later in load order** loses: its handle goes `:applied() == false` with a
reason, and the earlier one wins.

### A patch and a hook on the same site coexist

At a shared site, a bytes-patch (`kcdx.bytes`) applies **before** a hook
(`kcdx.hook`): the patch rewrites the bytes first, then the hook detours the
patched prologue. So a `kcdx.bytes` patch and a `kcdx.hook` on the **same**
function coexist — both apply. Ordering is by *kind* — a patch always precedes a
hook at the same site regardless of declaration or load order.

## Bootstrap targets

kcdx itself hooks a handful of game and runtime functions during engine boot.
Every engine-internal hook registers through the same `hook_chain` machinery your
plugin uses, so a plugin hook on the same target chains alongside the engine's by
load order — no "already hooked" rejection. The engine's chain entries always
sort to the FRONT of the chain regardless of declared priority.

### Hookable engine targets

These six targets are first-class hook targets — install on them the same way as
any other named target. Each carries a verified ABI, so the name alone supplies
address + signature:

| Target name | Mode contract | What the engine itself does at this site |
|---|---|---|
| `lua_pcall` | shareable (before/after/around) | Captures the live `lua_State*` so the chain dispatchers can bind to the VM. |
| `engine.frealloc_canary` | shareable (before/after/around) | Read-only image-range fingerprint of every `frealloc` call (the dual-Lua sentinel canary). |
| `engine.modmanager_ctor` | **rejects all plugin coexistence** (engine `replace` contract) | kcdx fully replaces the game's mod-manager constructor; a plugin install on this target is rejected with `:applied() == false` and `:reason()` containing `"engine bootstrap point"`. |
| `engine.bugsplat_ctor` | shareable | Hook on the BugSplat crash-reporter constructor (when present). |
| `engine.savegame` | shareable | Fires `kcdx.on("save_game", ...)`; your `before` runs alongside. |
| `engine.loadgame` | shareable | Fires `kcdx.on("pre_load_game", ...)`; your `before` runs alongside. |

### One un-hookable target

`update` (the per-frame loop that drives every other hook's dispatch) **cannot
be hooked** from authors — the chain's per-frame dispatcher would be the function
the chain dispatches through. If you need a per-tick hook, subscribe to
`kcdx.on("input_loaded", ...)` and schedule work via a `kcdx.task.*` API.

### Minimal snippet (a hook on `lua_pcall`)

```lua
kcdx.hook.before("WHGame.dll", "lua_pcall",
    function(L, nargs, nresults, errfunc)
        kcdx.log.info("PCALL", "fired")
    end)
```

The name `"lua_pcall"` resolves to the same target the engine's own chain entry
sits on — your `before` callback runs after the engine's `before` for every
`lua_pcall` the game makes.

## Handle methods

The userdata returned by a `kcdx.hook` sub-verb (and `kcdx.bytes`):

| Method | Returns | Meaning |
|---|---|---|
| `h:name()` | string | The entry's name. |
| `h:applied()` | `nil` / `true` / `false` | `nil` = pending (apply pass hasn't run); `true` = applied; `false` = failed OR removed (after `:uninstall()`). |
| `h:reason()` | string or `nil` | The failure reason when `:applied() == false`; `nil` otherwise. |
| `h:wait_applied()` | — | **Not available yet.** Raises a clear error directing you to use `kcdx.on("ready", ...)`. |
| `h:uninstall()` | self | **`kcdx.hook` handles only.** Logically removes the hook. After this returns, `:applied() == false` and the callback no longer fires. Idempotent. The underlying detour stays installed for the session. Calling `:uninstall()` on a `kcdx.bytes` handle raises a teaching error today (the patch engine has no revert path yet). |
| `tostring(h)` | string | `kcdx.handle<id=… name=… status=…>`. |

Because `:applied()` is `nil` in straight-line `plugin.lua` code, read final
status from a `kcdx.on("ready", ...)` callback (it fires after the apply pass).

## Glossary

- **sub-verb** — a mode-named function on `kcdx.hook` (`before` / `after` /
  `around` / `replace` / `insert_before` / `insert_after`, plus the advanced
  `mid` / `callsite`). You pick the mode by which sub-verb you call.
- **module** — the DLL the target lives in, the required first argument. No
  default.
- **target** — the function to hook: a name string OR a `kcdx.functions.*`
  reference value. The name carries the address and verified ABI.
- **locator** — a [`kcdx.locator.*`](locator.md) value naming a point inside the
  function. Omitted on before/after/around/replace → the function entry;
  required for the insert verbs.
- **opts** — the trailing optional table for non-positional knobs (`name`,
  `signature`, `off_thread`, the advanced raw locators).
- **captures** — for `mid` / the insert verbs: the named register/memory values
  live at the located point, each a `:get()`/`:set()` handle.
- **handle** — the userdata a sub-verb returns; read `:applied()` / `:reason()`
  at `kcdx.on("ready")`.
