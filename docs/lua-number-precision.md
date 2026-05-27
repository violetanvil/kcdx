# Lua number precision on KCD2 — what every kcdx binding must know

CryEngine's bundled Lua 5.1 is **not stock Lua 5.1**. The single most
important deviation, for anyone writing a Lua binding that hands
addresses or large integers to pak Lua, is:

> `LUA_NUMBER` is `float` (single-precision, 24-bit mantissa),
> not the stock `double`.

This document explains how we found that out, what it does to your
bindings if you ignore it, and what the rule is.

## TL;DR (the rule)

**Never push a pointer (or any value greater than 2²⁴) onto the
Lua stack as a number.** Use `kcdx.memory.pointer` userdata or
`lua_pushlightuserdata`. If you have to expose an integer-returning
helper, document the precision limit in the binding's docstring.

This is a hard rule for every kcdx binding: `LUA_NUMBER` is float, so integers
beyond 2^24 lose precision and pointers must be pushed as light userdata.

## How we pinned it

In Phase 5c.7d we shipped `kcdx.lua.cfunction_address(fn)`, which
returns the address of the C function backing a registered Lua
callable. The intent: combine it with `kcdx.memory.dynamic_hook`
so pak Lua can detour any registered C function.

It didn't work. `MH_CreateHook` failed with `MH_ERROR_NOT_EXECUTABLE`
on the address pak Lua handed back.

Dev-mode tracing showed that on the kcdx side, the real value never
moved — the `LuaDispatchShim`'s cfunction address (e.g.
`0x7FFD46781D00`) was correctly observed at every step. But pak Lua
saw `0x7FFD46800000`. Same low 20 bits zeroed, every time.

We built `kcdx.lua._probe_numbers()` to characterize the round-trip
across magnitude tiers. Dev-mode session 2026-05-19 12:26:44
captured:

```
LUA.NUMBER_PROBE/sizes
  sizeof_lua_Number=4 sizeof_lua_Integer=8
  sizeof_void_ptr=8 sizeof_long_long=8
```

So `lua_Integer` is still 8 bytes (`ptrdiff_t`), but `lua_Number` is
**4 bytes** — that's `float`, not `double`. Stock Lua 5.1 is double.
CryEngine modified the build.

In Lua 5.1 there is no integer subtype; every value is a `lua_Number`
internally. `lua_pushinteger(L, x)` is implemented as
`lua_pushnumber(L, (lua_Number)x)`. So an 8-byte `lua_Integer`
pointer cast to a 4-byte `lua_Number` loses 24+ bits of precision on
the very first push.

## What the probe showed at each magnitude

| Input | Output | Loss |
|---|---|---|
| `0x00000000` | `0x00000000` | exact |
| `0x00000001` | `0x00000001` | exact |
| `0x01000000` (2²⁴) | `0x01000000` | exact (last exact integer) |
| `0x01000001` (2²⁴+1) | `0x01000000` | rounded down 1 |
| `0xDEADBEEF` (2³¹·7) | `0xDEADBF00` | rounded up 17 (step ≈ 256) |
| `0x7FFFFFFF` (2³¹−1) | `0x80000000` | rounded up 1 (step ≈ 256) |
| `0x80000000` (2³¹) | `0x80000000` | exact (power of two) |
| `0xFFFFFFFF` (2³²−1) | `0x100000000` | rounded up 1 |
| `0x7FFD00000000` | `0x7FFD00000000` | exact (high bits only) |
| `0x7FFD46781D00` (real shim VA) | `0x7FFD46800000` | rounded up 516,864 (step ≈ 16 MB) |
| `0x7FFD46781D01` | `0x7FFD46800000` | same target |
| `0x7FFD46800000` | `0x7FFD46800000` | exact (already on 16 MB grid) |
| `2⁵³` | `2⁵³` | exact (in float because power of 2) |
| `2⁵³+1` | `2⁵³` | rounded down 1 |

The pattern matches IEEE 754 single-precision exactly. At magnitude
2ⁿ, the rounding step is 2ⁿ⁻²³. For n=47 (pointer magnitude), step
is 2²⁴ = 16,777,216 = 16 MB.

The `lua_pushnumber` direct path showed the same losses, ruling out
the `pushinteger→pushnumber` adapter as the cause. It's `lua_Number`
itself.

`lua_pushlightuserdata` followed by `lua_touserdata` round-tripped
the exact pointer — `lightuserdata` stores `void*` natively and
sidesteps the numeric encoding entirely.

## What this breaks

Any kcdx binding that returns a 64-bit value to pak Lua as a
number, where the value's magnitude exceeds 2²⁴. In practice:

- Anything that returns a pointer or VA (function addresses,
  module bases, allocated buffers, scan results).
- `pointer:get_qword` reading a 64-bit field whose value is itself
  a pointer.
- `value_wrapper_t::push_value`'s `integer_` case when the wrapped
  64-bit integer is actually a pointer (very common in
  dynamic_hook register captures).
- Anything that returns a `size_t` for a buffer ≥ 16 MB (rare but
  possible).

## The fix pattern

The "safe channel" is the `kcdx.memory.pointer` userdata. It stores
a `void*` natively, exposes arithmetic and dereference methods, and
is accepted by every kcdx binding that takes a `target` parameter.

```cpp
// WRONG — silently corrupts on KCD2:
lua_pushinteger(L, (lua_Integer)addr);

// RIGHT — exact:
kcdx::lua_bind_helpers::PushPointer(L, kcdx::lua_memory::pointer(addr));
```

If you want raw `void*` semantics with no methods (e.g., to pass
through some opaque-handle API):

```cpp
lua_pushlightuserdata(L, (void*)addr);  // exact
```

If you have to return an integer (e.g., for a "size" field in a
struct, or for backwards compatibility), document the precision
limit in the binding's docstring and ideally only use the integer
path for values you know are bounded.

## Alternatives we considered and rejected

1. **Return as a hex string.** Avoids the precision loss but
   forces pak Lua to parse on every use. Asymmetric: kcdx pushes
   a string, plugins call `tonumber("0x...", 16)` to get back —
   and *that* number is float-precision again, defeating the
   point. Strings are only useful if the pak Lua side never
   re-converts to number.
2. **NaN-tagged 64-bit encoding.** Pack 51-bit pointers into the
   spare bits of a NaN double. CryEngine compiled `lua_Number=float`
   so there are no spare bits — and even if it were double, plugins
   would need a custom unpacker we'd have to ship as a Lua-side
   helper. Way too clever.
3. **Patch CryEngine's Lua build at runtime to use double.**
   `lua_Number` is a typedef baked into every Lua C function's
   ABI (`lua_pushnumber` takes a `lua_Number` by value); changing
   it would require re-writing every Lua C function inside KCD2.
   Not feasible.
4. **Always pass integers as a 2-int hi/lo pair `{ high, low }`
   table.** Works for the data, but every binding signature
   becomes awkward and plugins have to remember the convention.
   Userdata is the same semantically (an opaque handle) with
   first-class type identity.

Pointer userdata wins on all axes: exact, has methods, accepted
by the rest of the kcdx surface, matches existing memory-pointer
plumbing, no plugin-side parsing.

## How to keep this from biting again

Whenever you add a new binding that returns a value to pak Lua:

1. If the value is a pointer/VA: `PushPointer` or
   `lua_pushlightuserdata`. Done.
2. If the value is a "definitely small" integer (count, index,
   error code, type tag): `lua_pushinteger` is fine.
3. If the value is a 64-bit integer that **could** be > 2²⁴:
   document the precision limit. Better: split the API so the
   common case returns userdata, and there's a separate
   `:as_integer_lossy()` helper for the rare cases where the
   plugin author has accepted the loss.

The `kcdx.lua._probe_numbers()` function (in
[`src/lua_bind_lua.cpp`](../src/lua_bind_lua.cpp)) stays in the
build as a permanent regression probe — run it once per session
when characterizing a new KCD2 patch, and the dev-mode log will
tell you whether the rules above still apply. (If a future
CryEngine update flips Lua back to double, the rules can relax.)
