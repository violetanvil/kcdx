# kcdx.on
> Part of the [kcdx Lua API](index.md).

Subscribe to a lifecycle event or a custom cross-plugin event.

**Call shape:** positional `(event, fn)`. Returns nothing on success; returns
`(nil, err)` on a bad argument or an unknown event.

```lua
kcdx.on(event, fn)
```

## Arguments

| Arg | Type | Meaning |
|---|---|---|
| `event` | string | `"ready"`, one of the 9 game lifecycle events, or a `"<publisher>:<event>"` custom event. See [Lifecycle events](lifecycle.md). |
| `fn` | function | The callback. `"ready"` and the no-arg lifecycle events take no arguments; the three save/load events pass a save basename string; a custom event receives the publisher's payload. |

## Errors

`(nil, err)` if `event` is not a string, `fn` is not a function, or `event` is
a bare name that is neither `"ready"` nor a known lifecycle event (the error
lists the valid names and explains the `"<publisher>:<event>"` form for custom
events).

## Minimal snippet

```lua
kcdx.on("ready", function()
    -- fires once, after THIS plugin's hooks/bytes are applied
    assert(my_hook:applied() == true)
end)
```
