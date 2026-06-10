# kcdx.statement
> Part of the [kcdx Lua API](index.md).

`kcdx.statement.*` modifies the game's **bytes statically**. You name a target
statement and say what to do to it (an [op](op.md)); the engine writes the new
bytes in place, and from then on the modified bytes execute **natively** — there
is **zero per-call cost**, no Lua dispatch.

This is the static-bytes sibling of [`kcdx.hook`](hook.md):

| | runs | cost per call | use when |
|---|---|---|---|
| `kcdx.statement.replace_with` | nothing — the bytes themselves changed | **zero** (native bytes) | the change is static and you want native-speed execution |
| `kcdx.hook.*` | your Lua callback, every call | a dispatch (trampoline + Lua call) | you need per-call logic in Lua |

If the behaviour you want is fixed — always return 0, never take this branch,
skip this call — `kcdx.statement` is the zero-overhead way to do it. If you need
to *decide* something per call in Lua, use a [hook](hook.md).

## `kcdx.statement.replace_with(module, target, [locator], op, [opts])`

Replace a located statement with the bytes an [op](op.md) names.

```lua
-- "make DamageMultiplier always return 0" — no callback, no per-call cost
kcdx.statement.replace_with("WHGame.dll", "DamageMultiplier",
    kcdx.locator.first_return(),
    kcdx.op.replace_with_return(0))
```

| Arg | Type | Required | Meaning |
|---|---|---|---|
| `module` | string | yes | the DLL the target lives in (e.g. `"WHGame.dll"`). No default. |
| `target` | string \| [function reference](functions.md) | yes | the function the statement is in — a curated name (the engine carries its address) or a `kcdx.functions.*` value. No hand-written address. |
| `locator` | [locator value](locator.md) | no | *where in the function* the op applies (`first_call_to` / `first_return` / `matching{…}` / …). Omitted → `kcdx.locator.function_entry()` (the function's first statement). |
| `op` | [op value](op.md) | yes | a **static** op (`kcdx.op.*`). **Not** a callback — `replace_with` is static-bytes only; a per-call callback is [`kcdx.hook`](hook.md). |
| `opts` | table | no | `{ name = "...", description = "..." }`. |

**Returns** a [handle](index.md) (`:applied()` / `:reason()` / `:name()`), or
`(nil, err)` on a bad call (a missing op, a callback passed where the op must
be, an unknown opts key). Like [`kcdx.bytes`](bytes.md) / [`kcdx.hook`](hook.md),
the write is **deferred** to the engine's apply pass — `:applied()` is `nil`
(pending) in straight-line code and resolves to `true` / `false` at
[`kcdx.on("ready")`](on.md).

### The engine owns the bytes, the fit, and the trampoline

You name a target and an op — never an address, an offset, an instruction
length, or a byte. The engine:

- resolves the [locator](locator.md) to the exact statement,
- emits the [op](op.md)'s bytes,
- and chooses a **same-size byte rewrite** (the op's bytes fit the statement's
  span) or an automatic **trampoline** (lift the statement to a private code
  region + redirect) when they don't. **You never see a "doesn't fit"
  failure.**

### The kind check teaches, but never second-guesses your intent

Each [op](op.md) applies to a specific statement kind (a branch op needs a
branch statement, a call op a call, …). Aim an op at the wrong kind and the
engine refuses with a teaching error naming the actual and required kinds:

> `the op 'always_take_branch' requires a conditional jump (branch) statement;
> the resolved statement is a `call`. Pick an op that applies to a `call`
> statement, or a locator that resolves to a conditional jump (branch)
> statement.`

The engine checks the **kind**, not your *purpose* — replacing a damage
calculation with `replace_with_return(0)` is your call; it only stops you from
putting a branch op on a non-branch statement.

## `kcdx.statement.insert_before(module, target, locator, callback, [opts])`
## `kcdx.statement.insert_after(module, target, locator, callback, [opts])`

Run a callback **at a located statement** (not at the function entry). These are
callback-based (the callback receives the statement's captures as a named
table), so unlike `replace_with` they are **not** zero-cost — they exist for the
case where you need to read or adjust the live state at a specific statement.
The `locator` is **required** ("insert before *what*?" has no default).

```lua
kcdx.statement.insert_after("WHGame.dll", "CheckOutfitSwap",
    kcdx.locator.first_call_to("IsInCombat"),
    function(captures)
        kcdx.log.info("OUTFIT", "rax=%d", captures.rax)
        -- return nothing → captures unchanged; return a table → those captures
        -- are written back
    end)
```

> **Not yet wired.** The surface accepts a [locator](locator.md) and a callback
> and returns a handle, but the engine's statement-capture apply path lands in a
> later step — so an `insert_before` / `insert_after` registers and then fails
> **loud** at apply (`:applied()` is `false`, `:reason()` says the path is not
> yet wired). It never silently does nothing. Use `replace_with` for a
> static-bytes change, or [`kcdx.hook.mid`](hook.md) for an offset-based capture,
> until then.

There are **no** `before` / `after` / `around` / `replace` sub-verbs on
`kcdx.statement` — those describe callback ordering relative to an original call,
which has no static-bytes analogue. Use [`kcdx.hook`](hook.md) for those.

## Copy-paste starter

```lua
-- A complete plugin.lua: neutralize a check by name, no hex, no callback.
kcdx.on("ready", function()
    local h = kcdx.statement.replace_with("WHGame.dll", "IsInCombat",
        kcdx.locator.function_entry(),
        kcdx.op.replace_with_return(0),
        { name = "force_not_in_combat" })

    if h:applied() then
        kcdx.log.info("MYMOD", "IsInCombat now returns 0 natively (no per-call cost)")
    else
        kcdx.log.warn("MYMOD", "replace_with did not apply: " .. tostring(h:reason()))
    end
end)
```

This is the Lua surface; the C++ mirror is
[`kcdxStatementInterface`](../cpp/statement.md) (not yet implemented).

## Glossary

- **static-bytes modification** — changing the game's machine code in place so
  the new behaviour executes natively, with no per-call Lua dispatch. The
  zero-cost alternative to a [hook](hook.md) when the behaviour is fixed.
- **statement** — one decompiled operation inside a function (a call, a return,
  a branch, an assignment, a compare). A [locator](locator.md) picks the
  statement; an [op](op.md) says what to do to it.
- **same-size vs trampoline** — how the engine fits an op's bytes at the
  statement: a same-size rewrite when they fit the statement's byte span, an
  automatic trampoline (a redirect to a private code region) when they exceed
  it. The engine picks; you never handle a "doesn't fit".
- **insert (`insert_before` / `insert_after`)** — run a callback at a specific
  statement (not the function entry), receiving the statement's captures. Unlike
  `replace_with` it is callback-based (it has a per-call cost). Not yet wired —
  it registers and fails loud at apply until the statement-capture path lands.
