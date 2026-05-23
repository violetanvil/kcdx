# kcdx.memory
> Part of the [kcdx Lua API](index.md).

Direct memory access and runtime native interop. Grouped domain.

Pointer values cross the Lua boundary as a **pointer userdata**, never a raw
number — CryEngine's Lua 5.1 uses `LUA_NUMBER=float`, so a pointer-magnitude
integer silently corrupts (`lua-precision.md`). Pass pointer userdata between
these calls; only call `:get_address()` for a display/opaque integer.

## Table-level functions

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

## The pointer userdata

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

## kcdx.memory.dynamic_call

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

## kcdx.memory.dynamic_hook

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
