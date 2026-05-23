# kcdx.command
> Part of the [kcdx Lua API](index.md).

Register a console command runnable from the in-game `~` console.

**Call shape:** a single named-field table. Returns `true` on success; `(nil,
err)` on failure. Unlike hooks/bytes, registration is **immediate** (not
deferred) — commands have no conflict semantics.

```lua
kcdx.command{ name = "...", callback = function(args) ... end }
```

## Fields

| Field | Type | Meaning |
|---|---|---|
| `name` | string | **Required.** The command name, unique across the process. |
| `callback` | function | **Required.** Runs on the main thread when the command fires; receives one `args` table. |
| `description` | string | Optional help text shown for `help <name>`. |

## The `args` table

The callback's single argument is a table that is **both** an array of the
user-supplied argument strings **and** carries a `raw` field:

- `args[1]`, `args[2]`, … — the typed arguments (excluding the command name
  itself).
- `#args` — the argument count.
- `args.raw` — the full command line string.

## Returns / Errors

`true` on success. `(nil, err)` if `name`/`callback` are missing or mistyped, if
`description` is present but not a string, if the name is already registered, or
if the console refuses the registration (duplicate name, no free slots, or
IConsole not yet ready).

## Minimal snippet

```lua
kcdx.command{
    name        = "outfit_dump",
    description = "Dump outfit state to the log.",
    callback    = function(args)
        kcdx.log.info("CMD", "argc=" .. #args)
        if args[1] then kcdx.log.info("CMD", "first=" .. args[1]) end
    end,
}
```

To run a command from Lua, see [`kcdx.console.execute`](console.md).
