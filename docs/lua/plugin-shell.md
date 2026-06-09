# The plugin shell
> Part of the [kcdx Lua API](index.md).

A declarative/Lua plugin is a folder with a `kcdx.toml` and one or more Lua
files. The minimal working plugin:

`kcdx.toml`:

```toml
[plugin]
author  = "violetanvil"
name    = "my_first_plugin"
version = "0.1.0"

[entrypoints]
lua = "plugin.lua"
```

`plugin.lua`:

```lua
kcdx.log.info("MYMOD", "hello from my first plugin")
```

## `[plugin]` keys

| Key | Type | Meaning |
|---|---|---|
| `author` | string | **Required.** The author/publisher namespace component. Charset `[a-z0-9_]`, 2–128 chars. Combined with `name`, the engine stamps `<author>.<plugin>` as the namespace prefix on every shared name this plugin exports. The reserved `kcdx` author is rejected. |
| `name` | string | **Required.** The plugin name within its author namespace. Charset `[a-z0-9_]`, 2–128 chars. Stable plugin identity used for load order, attribution, dependency resolution, namespace prefix (second component). |
| `display_name` | string | Human-friendly name (defaults to `name`). Free-form; never used as a key. |
| `description` | string | Free text. |
| `url` | string | Project/support URL. |
| `support_email` | string | Contact email. |
| `version` | string | Semver, e.g. `"0.1.0"`. |
| `kcdx_min_version` | string | Minimum kcdx version this plugin needs (semver). |
| `supports` | array of strings | Game-version patterns this plugin targets, e.g. `supports = ["1.5*"]`. Each pattern is string-compared against the running KCD2 version: a trailing `*` is a prefix wildcard (`"1.5*"` matches `1.5`, `1.5.5`, `1.5.1164953`); no `*` = exact match. Empty or absent = compatible with any version (the plugin pins no specific build). This is the SAME version-compat model a vanilla pak mod's `mod.manifest` `<supports><version>1.5*</version></supports>` uses — one policy for plugins and pak mods. |
| `log_level` | string | Floor for the plugin's own log file: `trace`/`debug`/`info`/`warn`/`error`/`off` (default `info`). Warn/Error always pass. |
| `test_names` | array of strings | For test-suite plugins: the matrix row IDs this plugin promises to report. |

Load-order hints are NOT `[plugin]` keys — they live in a separate per-plugin
`[load_order]` table:

```toml
[load_order]
zone     = "before_game"   # or "after_game"; omit to derive from capabilities
priority = 30              # 0..100 within the zone (default 50)
```

| Key | Type | Meaning |
|---|---|---|
| `zone` | string | Load zone: `"before_game"` or `"after_game"`. Omit to let the engine derive it (engine builtins → before; user plugins → after). |
| `priority` | integer | `0`–`100` within the zone (default `50`). |

`[[plugin.dependencies]]` — zero or more dependency entries:

```toml
[[plugin.dependencies]]
name        = "some_other_plugin"
min_version = "0.2.0"
optional    = false
```

| Key | Type | Meaning |
|---|---|---|
| `name` | string | **Required.** The depended-on plugin's bare `[plugin].name`. |
| `min_version` | string | Minimum required version (semver). |
| `optional` | bool | `true` if the dependency is soft (default `false`). |

## `[entrypoints]` keys

| Key | Type | Meaning |
|---|---|---|
| `lua` | string or array | The before/default-slot Lua file(s), run in declared order at the plugin's load-order slot. A bare string is a one-element list. |
| `lua_after` | string or array | Optional after-game-slot Lua file(s). Run in the after_game phase at the plugin's priority, regardless of declared zone. |
| `dll` | string | The plugin DLL (a C++ plugin). |

## `[kcdx]` keys

| Key | Type | Meaning |
|---|---|---|
| `test_suite_only` | bool | `true` = the plugin runs only under dev mode (silent in production). Used by test-suite plugins. |

Engine settings (`dev_mode`, `dry_run`, `dev_log_*`) are **not** valid here —
they live in `<kcdx-engine>/engine.toml`. A plugin that sets them gets a
warning.

## The both-phase model

A plugin can run code before the game is up *and* after. In Lua, declare `lua`
(before/default slot) and/or `lua_after` (after-game slot); the C++ mirror is
the `kcdxPlugin_Load` export (before) and the optional `kcdxPlugin_PostGameLoad`
export (after) on the same plugin DLL. Both-phase work runs in load-order
priority within each phase.

## Cross-plugin ordering

A `priority` field on an individual `kcdx.hook` / `kcdx.bytes` call is **no
longer honoured** (kcdx logs a once-per-session notice if you set it).
Cross-plugin ordering comes from the plugin's `[load_order].priority` (or the
engine `load_order.toml`); intra-plugin ordering is the order your `plugin.lua`
registers entries.
