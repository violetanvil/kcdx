# kcdxConsoleInterface — GetCVarInt / GetCVarBool / GetCVarFloat (↔ kcdx.cvar)
> Part of the [kcdx C++ API](index.md).

Read a game CVar's value by name. **Built** —
`kcdxConsoleInterface::GetCVarInt`, `GetCVarBool`, and `GetCVarFloat` in
[`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h)
(`kcdxConsoleInterface_Version == 3` — the version that added the CVar readers).

You pass the CVar string you already hold — the name you'd type after `~`, or
found on a modding wiki — and the engine resolves the console and the value
accessor for you. No address, offset, or signature crosses to your plugin.

> **Note — shared interface.** The CVar readers live on the same
> `kcdxConsoleInterface` that carries `Print` / `ExecuteString` and the command
> registration/argument calls; this file documents the three readers to keep
> the C++ and Lua folders structurally parallel with [the Lua side](../lua/cvar.md).
> Fetch the interface once via
> `QueryInterface(kcdxInterface_Console, kcdxConsoleInterface_Version)` — or use
> the pre-fetched `K.console` field on the [`Kcdx.h`](wrapper.md) handle. A
> plugin built against an older header (version 2) will not see these slots; a
> plugin built against version 3 running on an older engine gets a null
> interface back from `QueryInterface` for the higher version.

All three share one shape: an out-param plus a `bool` return. The return tells
you whether the read landed; the out-param is written ONLY on success and left
**untouched** on any miss. That split makes a missing CVar distinguishable from
a real value of `0` — a failed read never writes garbage into your variable, so
initialize it to your fallback and let a miss leave it alone:

```cpp
int priority = 0;                              // fallback
K.console->GetCVarInt("sys_pakPriority", &priority);   // untouched on a miss
```

A miss happens when there is no such CVar, when the console surface isn't ready
(before `kcdxMessage_InputLoaded` fires), or for a null/empty name. A refusal is
logged — it is never a silent no-op.

## `GetCVarInt` — read an integer CVar

```cpp
bool (*GetCVarInt)(const char* name, int* out);
```

| Arg | Type | Meaning |
|---|---|---|
| `name` | `const char*` | The CVar's console name. |
| `out` | `int*` | Receives the CVar's integer value on success; untouched on a miss. |

**Returns:** `bool` — `true` and writes `*out` on success; `false` and leaves
`*out` untouched if the CVar does not exist, the console surface isn't ready, or
`name` is null/empty.

```cpp
int difficulty = 0;
if (K.console->GetCVarInt("g_difficulty", &difficulty)) {
    // difficulty now holds the live value
}
```

## `GetCVarBool` — read a CVar as on/off

```cpp
bool (*GetCVarBool)(const char* name, bool* out);
```

Reports the CVar's integer value tested against zero — `*out = (int value != 0)`.
There is no separate boolean CVar type; this is the everyday "is this toggle
on?" reading, derived from the same integer accessor as `GetCVarInt`.

| Arg | Type | Meaning |
|---|---|---|
| `name` | `const char*` | The CVar's console name. |
| `out` | `bool*` | Receives `true` if the CVar's int value is non-zero, `false` if zero; untouched on a miss. |

**Returns:** `bool` — `true` and writes `*out` on success; `false` and leaves
`*out` untouched on a miss (same contract as `GetCVarInt`).

```cpp
bool shadows = false;
K.console->GetCVarBool("e_shadows", &shadows);
```

## `GetCVarFloat` — read a floating-point CVar

```cpp
bool (*GetCVarFloat)(const char* name, float* out);
```

| Arg | Type | Meaning |
|---|---|---|
| `name` | `const char*` | The CVar's console name. |
| `out` | `float*` | Receives the CVar's float value on success; untouched on a miss. |

**Returns:** `bool` — `true` and writes `*out` on success; `false` and leaves
`*out` untouched on a miss (same contract as `GetCVarInt`).

```cpp
float fov = 60.0f;
K.console->GetCVarFloat("cl_fov", &fov);
```

---

This is the C++ mirror of [kcdx.cvar](../lua/cvar.md)
(`GetCVarInt` ↔ `get_int`, `GetCVarBool` ↔ `get_bool`, `GetCVarFloat` ↔
`get_float`). The C++ out-param + `bool` return is the idiomatic spelling of
Lua's value-or-`nil` miss: in Lua a miss is a `nil` return; in C++ it is a
`false` return that leaves your out-param untouched.
