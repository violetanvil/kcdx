# kcdx.cvar
> Part of the [kcdx Lua API](index.md).

Read a game [CVar](index.md#2-glossary)'s value by name. You write the CVar
string you already hold — the name you found on a modding wiki, typed into the
in-game `~` console, or set in a config — and the engine resolves the console
and the value accessor for you. No address, offset, or signature ever crosses
to your plugin; the name is the whole input.

| Call | Args | Returns |
|---|---|---|
| `kcdx.cvar.get_int(name)` | string CVar name | the CVar's integer value as a number; `nil` if no such CVar exists yet or the console surface isn't ready; `(nil, err)` on a non-string argument. |
| `kcdx.cvar.get_bool(name)` | string CVar name | `true` if the CVar's integer value is non-zero, `false` if zero; `nil` on a miss; `(nil, err)` on a non-string argument. |
| `kcdx.cvar.get_float(name)` | string CVar name | the CVar's float value as a number; `nil` on a miss; `(nil, err)` on a non-string argument. |

The three readers share one shape: pass the name, get the value or `nil`. A
`nil` is an observable miss — never a fabricated `0` — so the idiomatic call
nil-coalesces a default:

```lua
local priority = kcdx.cvar.get_int("sys_pakPriority") or 0
```

## `kcdx.cvar.get_int(name)` — read an integer CVar

Reads a game CVar's integer value by name.

```lua
local p = kcdx.cvar.get_int("sys_pakPriority")
```

| Arg | Type | Meaning |
|---|---|---|
| `name` | string | The CVar's console name — the same name you'd type after `~`. |

**Returns:** the CVar's integer value as a Lua number on success. Returns `nil`
when there is no such CVar, or when the console surface isn't ready yet (before
`kcdx.on("input_loaded")` fires). The `nil` distinguishes a miss from a real
value of `0`, so coalesce a default when you need one:

```lua
local difficulty = kcdx.cvar.get_int("g_difficulty") or 0
```

**Bad argument:** a non-string `name` returns `(nil, err)` — `err` is a string
naming the expected `string` argument so you can find and fix the call. This is
the standard kcdx-binder error shape: a wrong call shape returns `(nil, err)`, a
valid call shape whose CVar simply has no value returns a single `nil`.

CVars become readable once the console surface is armed at `input_loaded`, so
read from a `kcdx.on("input_loaded", ...)` callback (or later) when you need the
value at a defined point:

```lua
kcdx.on("input_loaded", function()
    local p = kcdx.cvar.get_int("sys_pakPriority") or 0
    kcdx.log.info("MYMOD", "pak priority is " .. p)
end)
```

## `kcdx.cvar.get_bool(name)` — read a CVar as on/off

Reads a game CVar and reports it as a boolean: `true` if the CVar's integer
value is non-zero, `false` if it is zero. A CVar has no separate boolean type —
this is its integer reading tested against zero, the everyday "is this toggle
on?" call.

```lua
if kcdx.cvar.get_bool("e_shadows") then
    -- shadows are enabled
end
```

| Arg | Type | Meaning |
|---|---|---|
| `name` | string | The CVar's console name. |

**Returns:** `true` if the CVar's integer value is non-zero, `false` if zero.
Returns `nil` on a miss (no such CVar, or the console surface isn't ready) — so
`get_bool` is three-valued: `true` / `false` / `nil`. Coalesce when you want a
plain on/off:

```lua
local shadows_on = kcdx.cvar.get_bool("e_shadows") or false
```

**Bad argument:** a non-string `name` returns `(nil, err)`.

## `kcdx.cvar.get_float(name)` — read a floating-point CVar

Reads a game CVar's float value by name.

```lua
local fov = kcdx.cvar.get_float("cl_fov")
```

| Arg | Type | Meaning |
|---|---|---|
| `name` | string | The CVar's console name. |

**Returns:** the CVar's float value as a Lua number on success; `nil` on a miss
(no such CVar, or the console surface isn't ready). Coalesce a default as with
the other readers:

```lua
local fov = kcdx.cvar.get_float("cl_fov") or 60.0
```

**Bad argument:** a non-string `name` returns `(nil, err)`.

This is the Lua mirror of C++'s [kcdxConsoleInterface](../cpp/cvar.md)
`GetCVarInt` / `GetCVarBool` / `GetCVarFloat`.
