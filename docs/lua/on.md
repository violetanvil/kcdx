# kcdx.on
> Part of the [kcdx Lua API](index.md).

Subscribe to a lifecycle event or a custom cross-plugin event.

Custom-event names use the canonical dot separator:
`<author>.<plugin>.<event>`. The legacy `<publisher>:<event>` colon form
is rejected with a teaching error pointing at the dot form.

**Call shape:** positional `(event, fn)`. Returns nothing on success; returns
`(nil, err)` on a bad argument or an unknown event.

```lua
kcdx.on(event, fn)
```

## Arguments

| Arg | Type | Meaning |
|---|---|---|
| `event` | string | `"ready"`, one of the 9 game lifecycle events, or a `"<author>.<plugin>.<event>"` custom event (dot-separated; the legacy colon form is rejected). See [Lifecycle events](lifecycle.md). |
| `fn` | function | The callback. `"ready"` and the no-arg lifecycle events take no arguments; the three save/load events pass a save basename string; a custom event receives the publisher's payload. |

## Errors

`(nil, err)` if `event` is not a string, `fn` is not a function, `event`
uses the legacy `<publisher>:<event>` colon form (the error names the
canonical dot form), or `event` is a bare name that is neither `"ready"`
nor a known lifecycle event (the error lists the valid names and explains
the `"<author>.<plugin>.<event>"` form for custom events).

## Minimal snippet

```lua
kcdx.on("ready", function()
    -- fires once, after THIS plugin's hooks/bytes are applied
    assert(my_hook:applied() == true)
end)
```
