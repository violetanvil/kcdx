# kcdx.hook
> Part of the [kcdx Lua API](index.md).

Intercept a game function: run your Lua callback when the game calls it, and
optionally change its arguments, its return value, or whether it runs at all.

**Call shape:** a single named-field table. Returns a **handle** on successful
registration, or `(nil, err)` on a bad call.

```lua
local h = kcdx.hook{ name = "...", target = "...", before = function(...) ... end }
```

## Fields

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

## Returns

A **handle** userdata (see [Handle methods](#handle-methods)). The hook is not
installed yet — installation is [deferred](index.md#2-glossary) to the end-of-zone
apply pass. The handle's `:applied()` reads `nil` until then.

## Errors

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

## Minimal snippet

```lua
local h = kcdx.hook{
    name   = "double_ui_pump",
    target = "CGame_per_frame_ui_pump",   -- name supplies address AND signature
    after  = function(ret) return ret * 2 end,
}
```

## Locators

The hook needs to find its target. The **common path** is by name:

- **`target = "<name>"`** — a named function. The name resolves **both** the
  address and the verified signature, so you write no hex and no ABI. This is
  the path the engine is built around (the disassembler test, `cornerstones.md`).
  The name resolves three ways, by [precedence](targets.md#resolving-a-name--self--engine--other)
  (self > engine > other):
    - an **engine** [Address Library](addr.md) name (`kcdx.addr` lists what is
      named on your build);
    - one of **your own** [author-declared targets](targets.md) — including a
      `pattern`- or `target_symbol`-located target, resolved by name
      end-to-end (the engine carries the hex and ABI you declared once);
    - another plugin's target, by its explicit `"<author>.<plugin>.<name>"`
      form (e.g. `target = "redmoon.outfit.open_inventory"`).

  When a `pattern`/`rva` author-target supplies the address, its `signature`
  carries the ABI — so a named pattern site needs no `signature =` on the hook.

> **Named target + explicit `signature` — conflict contract.** If you name a
> target that carries a verified ABI **and** also pass `signature = "..."`, the
> **explicit signature wins** (the deliberate-override case — you may know
> better than the seed, or be overriding a stale row). The engine consults the
> verified ABI only to **detect** a conflict, not to override yours: when your
> explicit signature is **not** compatible with the verified ABI, the engine
> emits a teaching diagnostic (`HOOK_SIG_GATE`, naming the target, both
> signatures, and that the explicit one is used as authored) and then
> **proceeds with your explicit signature**. The diagnostic **severity** tells
> you how serious the disagreement is:
>
> - **Hard conflict → `ERROR`** (action `explicit_overrides_verified_hard`,
>   `severity=hard`, `crash_risk=true`). A different **argument count** or a
>   different **return-register width** — your signature mis-describes the call
>   frame of a **live engine function**, a known crash risk. The hook still
>   installs (the override is honored), but if the game crashes in or after this
>   hook, this is the cause. Double-check it is a deliberate override.
> - **Soft conflict → `WARN`** (action `explicit_overrides_verified`,
>   `severity=soft`). Same arg count and same return width — only a per-slot
>   type nuance differs (e.g. `ptr` vs `i64`). A value-level heads-up, not a
>   frame mis-description.
>
> The hook still installs either way. To silence the diagnostic, drop
> `signature =` (let the name carry the verified ABI) or correct it to match.

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

## Signature grammar

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

## Modes

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

### mid

A mid-function hook intercepts a single instruction at `offset` inside the
function and reads/writes named register/memory captures. It does **not** take
a `signature` (it doesn't need the function's ABI) — it takes `captures`.

The callback receives one argument: a table of capture handles. Each handle has
`:get()` and `:set(v)`. Return nothing to run the captured instruction; return
`"skip"` to skip it.

> **C++ mirror.** The C++ peer is `kcdxHookInterface::Mid`
> ([../cpp/hook.md#mid](../cpp/hook.md#mid)); its callback returns an `int`
> `kcdxMidResult` (`Run = 0` / `Skip = 1`) where Lua returns nothing / `"skip"`.
> Both surfaces have the run/skip channel (full parity).

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

### callsite scope

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

## Chaining

Multiple hooks can coexist on one target — kcdx installs one detour and fires an
ordered chain of your callbacks in load order. Several `before`s, several
`after`s, `before` + `after`, etc. all stack. To put more than one behaviour on
a target, make **separate** `kcdx.hook` calls (one behaviour per call).

When two hooks genuinely cannot coexist (incompatible signature on the shared
target, or two `replace`/`around` which the engine treats as exclusive), the
**later in load order** loses: its handle goes `:applied() == false` with a
reason, and the earlier one wins.

### A patch and a hook on the same site coexist

At a shared site, a bytes-patch (`kcdx.bytes`) applies **before** a hook
(`kcdx.hook`): the patch rewrites the bytes first, then the hook detours the
patched prologue. So a `kcdx.bytes` patch and a `kcdx.hook` on the **same**
function coexist — both apply. (The patch's optional `original` byte-verify
sees the pristine bytes because it runs first; if the order were reversed it
would see the hook's detour and abort.) Ordering is by *kind* — a patch always
precedes a hook at the same site regardless of declaration or load order.

## Handle methods

The userdata returned by `kcdx.hook` and `kcdx.bytes`:

| Method | Returns | Meaning |
|---|---|---|
| `h:name()` | string | The entry's name. |
| `h:applied()` | `nil` / `true` / `false` | `nil` = pending (apply pass hasn't run); `true` = applied; `false` = failed OR removed (after `:uninstall()`). |
| `h:reason()` | string or `nil` | The failure reason when `:applied() == false`; `nil` otherwise. |
| `h:wait_applied()` | — | **Not available yet.** Raises a clear error directing you to use `kcdx.on("ready", ...)`. (Returns self if already resolved — including after `:uninstall()`.) |
| `h:uninstall()` | self | **`kcdx.hook` handles only.** Logically removes the hook. After this returns, `:applied() == false` and the callback no longer fires. Idempotent (calling on an already-uninstalled handle is a no-op). The underlying MinHook detour stays installed for the session — engine reuses it if another `kcdx.hook` lands on the same target later. Returns self for chaining. Calling `:uninstall()` on a `kcdx.bytes` handle raises a teaching error today (the patch engine has no revert path yet — a future feature ships per-kind uninstall). |
| `tostring(h)` | string | `kcdx.handle<id=… name=… status=…>`. |

Because `:applied()` is `nil` in straight-line `plugin.lua` code, read final
status from a `kcdx.on("ready", ...)` callback (it fires after the apply pass).
