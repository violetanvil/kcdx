# Finding a game function

> Part of the [kcdx Lua API](index.md).

`kcdx.find{...}` discovers a game function from **what you already know about it**
— a string it references, a CVar it reads, a function it calls (or is called by),
a substring of its name. You hand kcdx the clue; it hands you back the matching
functions, with their statements and the ops you can apply to each. It is the
dev-time **function-discovery workbench** — the step that turns "the function
that mentions `test_marker`" into a concrete function name, address, and
statement list you then write `kcdx.statement.*` / `kcdx.locator.*` code against.

`kcdx.find` is a **dev tool**, and a **dev-mode-only** one (below). It searches a
separate, large dev reference database — the full game corpus — which the shipped
product does not carry. A shipped mod that calls `kcdx.find` in a player's
ordinary (non-dev) install does not break: the call returns an empty table and
logs a one-time teaching message, so your `if #r == 0` path runs harmlessly.

## Dev-mode + dev-DB gate (read this first)

`kcdx.find` needs **both** of these, or it returns `{}` (an empty table) and logs
the teaching message below — it never errors and never crashes:

1. **dev mode on** — `dev_mode = true` in `<game-bin>/kcdx-engine/engine.toml`.
2. **the dev reference DB present** — `reference-dev.sqlite` (a separate
   download, **not** shipped in the release zip) placed at
   `<game-bin>/kcdx-engine/data/reference-dev.sqlite`.

When either is missing, the logged message is:

```
[kcdx.find] dev tool unavailable. kcdx.find / kcdx_dev_inspect need dev mode
AND the dev reference DB:
  1. set dev_mode = true in <game-bin>/kcdx-engine/engine.toml
  2. place reference-dev.sqlite (a separate download, NOT in the release zip)
     at <game-bin>/kcdx-engine/data/reference-dev.sqlite
These are authoring tools — discover a function here, then write your
kcdx.statement.* / kcdx.locator.* code against it.
```

The **empty-table result is the same** whether the gate failed or the search
genuinely matched nothing — the log line is what tells a dev author which case
they hit. This is deliberate: a shipped mod's no-match branch and its
dev-tool-unavailable branch are one and the same harmless `if #r == 0`.

## Call shape

```lua
kcdx.find{ <criterion> = "<value>" [, <criterion> = "<value>", ...] }
```

A single table is **required**, and it must carry **at least one** criterion (the
"at least one of N" form). An empty table, or a non-table argument, is a
teaching error — not a silent no-op. Multiple criteria are **AND**-ed (a function
must satisfy all of them to match).

## Criteria

Each criterion is an optional string field; pass at least one.

| Criterion | Type | Finds functions that… |
|---|---|---|
| `string` | string | reference this exact string literal |
| `cvar` | string | read this CVar (by its console name) |
| `callers_of` | string | call the named function (the callers of it) |
| `callee` | string | are called by — i.e. contain a call to — the named function |
| `name_contains` | string | have this substring in their name |
| `callee_in_subsystem` | string | call into a subsystem with this name prefix |

An unrecognized key (a typo'd `strng = ...`) is rejected with a teaching error —
it is never silently ignored.

## Returns

`kcdx.find` **always returns a table** — never `nil`, never an error on a normal
search. The table is an **array of record tables**, one per matched function:

| Record key | Type | Meaning |
|---|---|---|
| `function` | string | the function's name (its curated name, or its auto-derived name) |
| `module` | string | the module the function lives in (e.g. `WHGame.dll`) |
| `rva` | pointer | the function's module-relative address, as a [`kcdx.memory.pointer`](memory.md) userdata (exact — a real address is never a lossy number) |
| `decompile_quality` | integer | a small quality code for the decompile (0 = unknown) |
| `statements` | array | the function's statements, in order — each a sub-table (below) |

Each entry in `statements` is:

| Statement key | Type | Meaning |
|---|---|---|
| `idx` | integer | the statement's position within the function |
| `kind` | string | the statement kind (`call` / `return` / `assign` / `branch` / …) |
| `pseudo_text` | string | the decompiled pseudo-code text of the statement |
| `callee` | string | the call target, when the statement is a call (empty otherwise) |
| `string_ref` | string | the string the statement references, when it has one (empty otherwise) |
| `captures` | array | the statement's captured variables — each `{ name, storage_kind, storage_detail, data_type, size_bytes }` (empty when the statement has none); the same shape [`kcdx.locator`](locator.md) `:resolve(...).captures` returns |
| `applicable_ops` | array | the [`kcdx.op.*`](op.md) op **names** that fit this statement (strings) — use one verbatim in `kcdx.statement.replace_with(...)` |

### No matches

A search that matches nothing returns an **empty table** `{}` — so the idiomatic
check is `if #r == 0`. Never `nil`, never an error.

### Result cap and loud truncation

A search is capped at **500 records**. When more than 500 functions match, the
result carries the first 500 (best-decompiled first) **plus** two marker fields,
so the truncation is never silent:

| Marker key | Type | Meaning |
|---|---|---|
| `_truncated` | boolean | `true` — the result is a capped prefix, not the full set |
| `_total_matches` | integer | the full match count (how many you would have gotten uncapped) |

Both are absent on an uncapped result. Tighten your criteria (add another AND
criterion) to bring the count under the cap.

## Minimal snippet

```lua
-- Discovery idiom: find the function that references a known string literal,
-- then inspect its statements to decide what to hook / patch.
local r = kcdx.find{ string = "test_marker" }

if #r == 0 then
    -- No match, OR dev mode / the dev DB is unavailable (check the log).
    kcdx.log.info("MYMOD", "no function found (or dev tool unavailable)")
else
    local fn = r[1]
    kcdx.log.info("MYMOD",
        fn["function"] .. " in " .. fn.module ..
        " @ " .. tostring(fn.rva) ..
        " (" .. #fn.statements .. " statements)")
end
```

> `function` is a Lua keyword-adjacent field name; index it as `fn["function"]`,
> not `fn.function`, in your own code.

## The in-game console peer

`kcdx_find` is the `~`-console command with the same job — discover a function in
seconds from the console, no plugin to write:

```
kcdx_find <module> --<criterion> "<value>" [...]
```

e.g. `kcdx_find WHGame.dll --string "test_marker"`. It runs the same dev-DB
search, the same dev-mode/dev-DB gate, and prints the same teaching message on
the gated-off path. It prints one row per matched function (name, address,
decompile-quality, statement count), capped so an over-broad query can't flood
the overlay. Like every console command it is not part of the
[Lua API call map](index.md#3-the-map) — you type it at the `~` console, you do
not call it from `plugin.lua`.

## Inspecting one function — `kcdx_dev_inspect`

Once `kcdx_find` (or your own knowledge) hands you a function name, the
`~`-console command `kcdx_dev_inspect` prints that function's **full statement
list** — the same `statements` array a `kcdx.find` record carries, rendered as a
table you read at a glance:

```
kcdx_dev_inspect <module> <function>
```

e.g. `kcdx_dev_inspect WHGame.dll IsInCombat`. Two positional arguments — the
module and the function name. It uses the same dev-DB layer, the same
dev-mode/dev-DB gate, and prints the same dev-tool-unavailable teaching message
on the gated-off path. Like every console command it is not part of the
[Lua API call map](index.md#3-the-map) — you type it at the `~` console.

### What it prints

A header line, then one row per statement:

```
[dev_inspect] IsInCombat  WHGame.dll+0x1A2B3C0  [clean]  (5 stmts)
  [0] call      caps:-                ops:skip_call_void replace_with_noop  combat = GetCombatState(actor)
  [1] branch    caps:flag:int         ops:always_take_branch never_take_branch invert_branch_condition replace_with_noop  if (flag != 0)
  [2] return    caps:-                ops:replace_with_return replace_return_value replace_with_noop  return 1
  ...
```

- **Header** — the function's name, its module + address, the decompile-quality
  label (`?` when unknown), and the statement count.
- **Each row** — the statement's `idx`, its `kind`, its captured variables
  (`caps:` — `name:type` pairs, or `-` when none), the [`kcdx.op.*`](op.md) op
  names that fit it (`ops:` — copy one verbatim into
  `kcdx.statement.replace_with(...)`, or `-` when none), then the decompiled
  pseudo-text. A long function's rows are capped with a loud `... and N more
  statements elided` line.

### The not-found teaching error

When the function name is unknown (the dev DB is present but no curated name or
auto-name matches), `kcdx_dev_inspect` does not fail silently — it prints a
teaching error with the **nearest curated name** by edit-distance, so a one-char
typo resolves to its intended target:

```
[dev_inspect] no function 'IsInCombatt' in WHGame.dll.
  Did you mean: IsInCombat? Try:
      kcdx_dev_inspect WHGame.dll IsInCombat
  Or search by content:
      kcdx_find WHGame.dll --name_contains IsInCombatt
```

The suggestion is the closest name by Levenshtein edit-distance — the dev DB
ranks it for you. If even the nearest name is not what you wanted, the
`kcdx_find --name_contains` line searches by substring instead.
