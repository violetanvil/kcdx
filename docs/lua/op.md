# kcdx.op
> Part of the [kcdx Lua API](index.md).

An **op** says *what static change* to make at a code site — by name. You pick
`kcdx.op.never_take_branch`, not a byte sequence; the engine produces the bytes.
No opcode, displacement, or instruction length ever crosses to your plugin: the
name of the behaviour is the whole input.

A `kcdx.op.*` call returns an **op value** you pass to `kcdx.statement.*`
(alongside a [locator](locator.md) that says *where*). The locator picks the
statement; the op says what to do to it.

```lua
-- "make this function always return 0" — name the behaviour, not the bytes
kcdx.statement.replace_with("WHGame.dll", "DamageMultiplier",
    kcdx.locator.first_return(),
    kcdx.op.replace_with_return(0))
```

The engine chooses a same-size byte rewrite or a trampoline automatically, from
the op's bytes versus the located statement's byte span — you never see a
"doesn't fit" failure.

## The op catalog

Each op names a behaviour and applies to a specific kind of statement (a return,
a call, a branch, an assignment, a compare). Hand an op a statement of the wrong
kind and it fails loud with a teaching error naming what the statement actually
is — it never silently does nothing.

### Return / function-level

| Call | Arg | Does |
|---|---|---|
| `kcdx.op.replace_with_return(value)` | int constant | replace the statement with `return value` (sets the return register, returns) |
| `kcdx.op.return_const(value)` | int constant | alias of `replace_with_return` |
| `kcdx.op.replace_return_value(value)` | int constant | at a return statement, force the returned value to `value` (the function still returns; only the value changes) |

### Whole-statement neutralize

| Call | Does |
|---|---|
| `kcdx.op.replace_with_noop()` | replace the statement with no-ops (it does nothing) — applies to any statement kind |
| `kcdx.op.noop()` | alias of `replace_with_noop` |

### Call statements

| Call | Arg | Does |
|---|---|---|
| `kcdx.op.skip_call_void()` | — | skip the call entirely (its result is unused) |
| `kcdx.op.skip_call_return_value(value)` | int constant | skip the call but set its result to `value` |
| `kcdx.op.replace_call_target(new_fn_name)` | string name | redirect the call to `new_fn_name` instead |

### Branch (conditional-jump) statements

| Call | Does |
|---|---|
| `kcdx.op.always_take_branch()` | make the conditional jump always taken |
| `kcdx.op.never_take_branch()` | make the conditional jump never taken (fall through) |
| `kcdx.op.invert_branch_condition()` | flip the branch condition (taken ⇄ not-taken) |

### Assignment / compare statements

| Call | Arg | Does |
|---|---|---|
| `kcdx.op.replace_assignment_value(value)` | int constant | force an assignment's right-hand side to `value` |
| `kcdx.op.replace_compare_constant(value)` | int constant | replace the constant a compare tests against with `value` |

Every op is minted by a **call** — `kcdx.op.noop()`, not a bare
`kcdx.op.noop` — so the surface is consistent (an op is always the result of a
call, the same as a [locator](locator.md)).

### Wrong-kind error (teaching, not silent)

An op applied to the wrong statement kind reports the mismatch naming both the
required kind and what the statement actually is:

> `this op requires a conditional jump (branch) statement; this statement is a
> `call`. Use an op that applies to a call statement, or pick a locator that
> resolves to a conditional jump (branch) statement.`

The engine checks the **kind**, not your intent — replacing a damage calculation
with `replace_with_return(0)` is your call, not the engine's; it only stops you
from putting a branch op on a call.

## `value:emit_for(kind, byte_range_len)` — inspect what an op emits

Every op value carries an `:emit_for(kind, byte_range_len)` method that reports
what the op produces for a statement of a given kind and byte span. Use it to
confirm an op applies to a kind before you wire it into a `kcdx.statement`, or to
see the exact bytes it will write.

```lua
local r = kcdx.op.replace_with_noop():emit_for("call", 5)
if r.kind_ok then
    -- r.bytes is { 0x90, 0x90, 0x90, 0x90, 0x90 }, r.fit is "same_size"
    kcdx.log.info("MYMOD", "noop over 5 bytes emits " .. #r.bytes .. " bytes")
else
    kcdx.log.warn("MYMOD", "op rejected this statement: " .. r.reason)
end
```

| Arg | Type | Meaning |
|---|---|---|
| `kind` | string | the statement's kind (`"call"` / `"return"` / `"branch"` / `"assign"` / `"compare"` / …) |
| `byte_range_len` | number | the statement's byte span (its `byte_range_len` from a resolved [locator](locator.md)) |

**Returns** a result table:

| Field | Type | Meaning |
|---|---|---|
| `kind_ok` | boolean | `true` when the op applies to this statement kind |
| `reason` | string | present only when `kind_ok == false` — the teaching error naming the actual and required kinds |
| `deferred` | boolean | `true` when the op's final bytes need the live statement's bytes (a branch displacement, a call target, an operand encoding) — those are produced when the op is applied, not here |
| `bytes` | table \| nil | the emitted byte sequence (an array of `0..255`), present only when `deferred == false`; `nil` when deferred (the bytes are not knowable without the live statement) |
| `fit` | string \| nil | `"same_size"` or `"trampoline"` — whether the emitted bytes fit the statement's span or the engine will trampoline; present when `bytes` is |

**Bad argument:** a non-string `kind` or non-number `byte_range_len` returns
`(nil, err)` — `err` names the expected argument. A wrong-kind statement returns
the result table with `kind_ok = false` and a `reason`, never a silent `nil`.

Five ops report `deferred = true` (`always_take_branch`,
`invert_branch_condition`, `replace_call_target`, `replace_assignment_value`,
`replace_compare_constant`) — their final bytes depend on the live statement's
original bytes (a jump's displacement, a call site, where a constant sits in an
instruction), so the engine produces them when the op is applied to a resolved
statement, not from `:emit_for` alone. The other ops emit a fixed byte sequence
`:emit_for` returns exactly.

This is the Lua surface; the C++ mirror is [`kcdxOpInterface`](../cpp/op.md) (not
yet implemented).

## Glossary

- **op** — a value naming *what static change* to make at a code site (return a
  constant, no-op a statement, skip a call, flip a branch). Produced by a
  `kcdx.op.*` call; consumed by `kcdx.statement.*` alongside a
  [locator](locator.md). The op names the behaviour; the engine produces the
  bytes — you never write hex.
- **statement kind** — what a statement is (a call, a return, a branch, an
  assignment, a compare). An op applies to one or more kinds; applied to a wrong
  kind it reports a teaching error rather than acting.
- **same-size vs trampoline** — how the engine fits an op's bytes into the
  located statement: a same-size rewrite when the bytes fit the statement's
  byte span, a trampoline (a redirect to a private code region) when they
  exceed it. The engine picks automatically; you never handle a "doesn't fit".
