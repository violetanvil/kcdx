# kcdx.publish
> Part of the [kcdx Lua API](index.md).

Broadcast a custom event to subscribers in any plugin (the counterpart to
`kcdx.on("<publisher>:<event>", ...)`).

**Call shape:** positional `(event [, payload])`. Returns the number of
subscribers fired (an integer; `0` means nobody listened, not an error). Returns
`(nil, err)` on a bad `event`.

```lua
local n = kcdx.publish(event, payload)
```

## Arguments

| Arg | Type | Meaning |
|---|---|---|
| `event` | string | The **bare** custom event name. The engine stamps your plugin name in front, so subscribers hear it as `"<your_plugin>:<event>"`. |
| `payload` | any (optional) | Any Lua value, passed **by reference** to each subscriber (a table is shared, not copied). Omit to fire subscribers with no argument. |

## Behaviour notes

- The event is namespaced by your plugin: a subscriber uses
  `kcdx.on("<your_plugin>:<event>", fn)`.
- The payload is shared by reference — treat it as immutable by convention.
- An anonymous publisher (e.g. from the console) fires under `"<anon>:<event>"`
  and logs a warning.

## Minimal snippet

```lua
-- in plugin "violetanvil":
kcdx.publish("outfit_changed", { slot = 2, name = "Noble" })

-- in another plugin:
kcdx.on("violetanvil:outfit_changed", function(payload)
    kcdx.log.info("MOD", "outfit -> " .. payload.name)
end)
```
