# kcdx.locator
> Part of the [kcdx Lua API](index.md).

A **locator** says *where in a function* a hook or statement op applies. You
name what you already understand — a call to a function, a return, a statement
matching a condition — and the engine resolves it to the exact statement. No
address, offset, or instruction length ever crosses to your plugin: the name of
the thing is the whole input.

A `kcdx.locator.*` call returns a **locator value** you pass to a verb that
takes one (the `kcdx.hook.*` / `kcdx.statement.*` verbs). When a verb accepts a
default locator and you omit it, the verb uses `kcdx.locator.function_entry()`.

```lua
-- "insert my callback right after the first call to IsInCombat in this function"
kcdx.hook.insert_after("WHGame.dll", "CheckOutfitSwap",
    kcdx.locator.first_call_to("IsInCombat"),
    function(captures) ... end)
```

The common-path locators below name what you understand. `matching_pattern` is
the labeled **expert** escape hatch for a raw byte pattern — use it only when no
named form fits (see [the expert hatch](#the-expert-hatch)).

## The locator forms

### Function-level

| Call | Resolves to |
|---|---|
| `kcdx.locator.function_entry()` | the function's first statement |
| `kcdx.locator.function_exit()` | the function's last statement |

`function_entry()` is the default when a verb's locator is omitted.

### Statement-content shortcuts (the common path)

| Call | Arg | Resolves to |
|---|---|---|
| `kcdx.locator.first_call_to(fn)` | string callee name | the first statement that calls `fn` |
| `kcdx.locator.last_call_to(fn)` | string callee name | the last statement that calls `fn` |
| `kcdx.locator.call_to(fn)` | string callee name | the UNIQUE statement that calls `fn` — **errors if the function calls `fn` more than once** |
| `kcdx.locator.first_return()` | — | the first return statement |
| `kcdx.locator.last_return()` | — | the last return statement |
| `kcdx.locator.return_value(v)` | string operand | the first return statement whose text references `v` |
| `kcdx.locator.references_string(s)` | string | the first statement that references string `s` |
| `kcdx.locator.first_read_of_cvar(name)` | string CVar name | the first statement that reads CVar `name` |

`call_to(fn)` is the "there is exactly one of these" form — if you are not sure
the function calls `fn` exactly once, use `first_call_to` / `last_call_to`, which
never error on a duplicate. A `call_to` against a callee that appears twice
resolves to a not-found result whose `reason` is `call_to_ambiguous`.

### The general matcher

```lua
kcdx.locator.matching{
    kind               = "call",     -- statement kind ("call"/"return"/"branch"/"assign"/"store"/…)
    callee             = "IsInCombat",
    condition_contains = "health",   -- substring of the statement's condition text
    reads_cvar         = "g_difficulty",
    references_string  = "autoexec.cfg",
}
```

`matching{…}` takes any **subset** of the keys; the provided keys are ANDed
(every one must hold). It resolves to the first statement that satisfies all of
them. An empty `matching{}` matches the first statement (no constraint). A
mistyped key fails loud at the call — `kcdx.locator.matching` rejects an
unrecognized key with a teaching error rather than silently dropping your
constraint.

### The expert hatch

```lua
kcdx.locator.matching_pattern("48 8B C1 ?? ?? ?? ??")   -- advanced/expert form
```

`matching_pattern` is the **labeled expert escape hatch** for a raw AOB byte
pattern, for the rare site no named locator can describe. It is NOT a
statement-metadata locator — it carries the pattern for the byte path, and the
common path is always a named form (`first_call_to`, `first_return`, …). Reach
for it only when nothing else fits; a `matching_pattern` value handed to
`:resolve` (below) reports `found=false` with reason
`matching_pattern_not_statement_locator`, because the AOB resolves against the
binary's bytes elsewhere, not against statement metadata.

## `value:resolve(module, target)` — inspect what a locator resolves to

Every locator value carries a `:resolve(module, target)` method that resolves it
against a named curated function and returns a result table. Use it to inspect
what a locator picks before you wire it into a hook, or to confirm a site still
resolves after a game update.

```lua
local stmt = kcdx.locator.first_return():resolve("WHGame.dll", "SaveGame")
if stmt.found then
    kcdx.log.info("MYMOD", "first return is statement " .. stmt.statement_idx
        .. " (kind " .. stmt.kind .. ")")
else
    kcdx.log.warn("MYMOD", "locator did not resolve: " .. stmt.reason)
end
```

| Arg | Type | Meaning |
|---|---|---|
| `module` | string | the module the function lives in (e.g. `"WHGame.dll"`) |
| `target` | string | the curated function name to resolve the locator within (e.g. `"SaveGame"`) |

**Returns** a result table:

| Field | Type | Meaning |
|---|---|---|
| `found` | boolean | `true` when the locator resolved to a statement |
| `statement_idx` | number | the resolved statement's index within the function (present when `found`) |
| `kind` | string | the statement's kind (`"call"` / `"return"` / `"branch"` / `"assign"` / `"store"` / …) |
| `byte_range_len` | number \| nil | the statement's byte span; `nil` when the statement carries no span |
| `callee` | string | the call target name (empty when the statement is not a call) |
| `string_ref` | string | the referenced string / CVar name (empty when none) |
| `captures` | table | the per-statement captured variables — an array of `{ name, storage_kind, storage_detail, data_type, size_bytes }` (empty when the statement has none) |
| `reason` | string | present only when `found == false` — the reason token (`locator_no_match`, `call_to_ambiguous`, `matching_pattern_not_statement_locator`, `function_no_statements`, `name_unknown`, `db_not_loaded`) |

**Bad argument:** a non-string `module` or `target` returns `(nil, err)` — `err`
names the expected argument so you can find and fix the call. A valid call whose
locator simply doesn't match returns the result table with `found = false` and a
`reason`, never a silent `nil`.

`:resolve` reads the curated reference database, which is open after the engine
finishes loading — call it from a `kcdx.on("ready", …)` callback (or later) when
you need it at a defined point.

This is the Lua surface; the C++ mirror is [`kcdxLocatorInterface`](../cpp/locator.md)
(not yet implemented).

## Glossary

- **locator** — a value naming *where in a function* a hook or statement op
  applies (a call to a function, a return, a statement matching a condition).
  Produced by a `kcdx.locator.*` call; consumed by the `kcdx.hook.*` /
  `kcdx.statement.*` verbs. The common-path forms name what you understand;
  `matching_pattern` is the labeled expert raw-AOB hatch.
- **statement** — one analyzed step inside a curated function (a call, a return,
  a branch, an assignment, a store). The reference database carries a function's
  statements in index order; a locator resolves to one of them by index.
- **capture** — a variable live at a statement (a register or stack slot), with
  its storage and type. An insert-callback receives the statement's captures by
  name; `:resolve(...).captures` lists them.
