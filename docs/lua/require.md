# require — multi-file plugins
> Part of the [kcdx Lua API](index.md).

`require("helper")` loads a sibling Lua file from **your plugin's own folder**.
This is the idiomatic Lua form, but kcdx owns the resolution for plugin chunks:

- A bare `require("helper")` from your `plugin.lua` resolves to *your*
  `helper.lua`, not the EXE directory or another plugin's file.
- The module cache is namespaced per plugin: plugin A's `require("helper")` and
  plugin B's `require("helper")` get **different** modules — no cross-plugin
  collision, even though there is one shared Lua state.
- A second `require("helper")` within the same plugin returns the cached module.
- A `kcdx.*` call made from inside a `require`'d helper attributes to your
  plugin (not `<anon>`) — so `kcdx.on`, `kcdx.publish`, `kcdx.hook` from a
  helper work exactly as they do from `plugin.lua`.

```lua
-- plugin.lua
require("helper")     -- runs sibling helper.lua under this plugin's identity
```
