# Lifecycle events
> Part of the [kcdx Lua API](index.md).

`kcdx.on(event, fn)` accepts the following.

## `"ready"`

Fires **once**, per plugin, after that plugin's zone apply pass completes — the
post-apply moment when every handle your plugin captured has a final
`:applied()` / `:reason()`. Takes no arguments. This is the place to assert
your hooks installed.

```lua
local h = kcdx.hook{ name="x", target="Add", signature="i32 (i32)",
                     before=function(s) return s end }
kcdx.on("ready", function()
    if h:applied() then kcdx.log.info("MYMOD", "hooked")
    else kcdx.log.warn("MYMOD", "hook failed: " .. tostring(h:reason())) end
end)
```

## The 9 game lifecycle events

Each is a bridge over an engine message and fires **every** time that message
fires. Six pass no arguments; three (the save/load events) pass the save
**basename** string.

| Event | Argument | Fires when |
|---|---|---|
| `post_load` | none | The game's post-load message. |
| `post_post_load` | none | The subsequent post-post-load message. |
| `input_loaded` | none | Input is loaded (fires every boot — the standard auto-pass trigger). |
| `new_game` | none | A new game starts. |
| `pre_load_game` | none | Before a save loads. |
| `post_load_game` | none | After a save finishes loading. |
| `save_game` | basename (string) | A save is written. |
| `load_game_selected` | basename (string) | A save is selected to load. |
| `delete_game` | basename (string) | A save is deleted. |

```lua
kcdx.on("input_loaded", function()
    kcdx.log.info("MYMOD", "world is up")
end)

kcdx.on("save_game", function(basename)
    kcdx.log.info("MYMOD", "saved to " .. basename)
end)
```

## Custom events — `"<author>.<plugin>.<event>"`

Any event name with two dots subscribes to a `kcdx.publish` broadcast. The form
is the publishing plugin's `<author>.<plugin>` namespace + the bare event name,
joined by dots (the same canonical separator every other shared name in the
namespace model uses). The callback receives the published payload (by
reference).

```lua
kcdx.on("redmoon.outfit.outfit_changed", function(payload)
    kcdx.log.info("MYMOD", "slot " .. payload.slot)
end)
```

A bare event name that is neither `"ready"` nor a lifecycle event is rejected
with a teaching error — custom events are always heard via the 3-segment
`<author>.<plugin>.<event>` dot form. The legacy `<publisher>:<event>` colon
form is rejected with a teaching error pointing at the dot form (see
[`publish.md`](publish.md) for the publisher side).
