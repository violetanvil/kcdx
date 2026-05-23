# kcdx.log
> Part of the [kcdx Lua API](index.md).

Structured logging. Grouped domain; positional `(category, message)`.

| Method | Args | Notes |
|---|---|---|
| `kcdx.log.info(category, message)` | strings | Info level. |
| `kcdx.log.warn(category, message)` | strings | Warn level. |
| `kcdx.log.error(category, message)` | strings | Error level. |
| `kcdx.log.debug(category, message)` | strings | Dev-mode only. |
| `kcdx.log.trace(category, message)` | strings | Dev-mode only. |

`category` is a stable tag for the feature (e.g. `"MYMOD"`); `message` is
pre-formatted text. The engine does not do printf-style marshaling across the
boundary — build your string with `string.format` yourself. A one-argument call
(`kcdx.log.info("just a message")`) treats the sole string as the message under
category `"LUA"`. These calls return nothing.

```lua
kcdx.log.info("DMG", string.format("hit for %.1f", dmg))
```
