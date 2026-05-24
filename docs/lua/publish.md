# kcdx.publish
> Part of the [kcdx Lua API](index.md).

Broadcast a custom event to subscribers in any plugin (the counterpart to
`kcdx.on("<author>.<plugin>.<event>", ...)`).

The custom-event separator is the canonical dot per
[`.claude/rules/naming-namespaces.md`](../../.claude/rules/naming-namespaces.md):
events stamp as `<author>.<plugin>.<event>`. The legacy
`<publisher>:<event>` colon form is rejected by `kcdx.on` with a teaching
error.

**Call shape:** positional `(event [, payload])`. Returns the number of
subscribers fired (an integer; `0` means nobody listened, not an error). Returns
`(nil, err)` on a bad `event`.

```lua
local n = kcdx.publish(event, payload)
```

## Arguments

| Arg | Type | Meaning |
|---|---|---|
| `event` | string | The **bare** custom event name. The engine stamps your plugin's qualified namespace in front, so subscribers hear it as `"<your_author>.<your_plugin>.<event>"` (dot-separated). |
| `payload` | any (optional) | Any Lua value, passed **by reference** to each subscriber (a table is shared, not copied). Omit to fire subscribers with no argument. |

## Behaviour notes

- The event is namespaced by your manifest: a subscriber uses
  `kcdx.on("<your_author>.<your_plugin>.<event>", fn)`.
- While a publishing plugin's `[plugin].author` is still empty during the
  corpus 2-dot transition, the engine falls back to the legacy 2-segment
  form `<plugin>.<event>` (same canonical dot separator, just author-less).
- The payload is shared by reference — treat it as immutable by convention.
- An anonymous publisher (e.g. from the console) fires under
  `"<anon>.<event>"` and logs a warning.

## Minimal snippet

```lua
-- in plugin "violetanvil" (author "walkabout"):
kcdx.publish("outfit_changed", { slot = 2, name = "Noble" })

-- in another plugin:
kcdx.on("walkabout.violetanvil.outfit_changed", function(payload)
    kcdx.log.info("MOD", "outfit -> " .. payload.name)
end)
```
