# Cross-cutting rules
> Part of the [kcdx Lua API](index.md).

- **Callbacks run on the main thread.** kcdx fires your Lua callbacks
  (hook callbacks, lifecycle/`ready` callbacks, command callbacks, publish
  subscribers) on the game's main thread. Only hook functions that the game
  itself runs on the main thread; hooking an audio/physics/streaming worker
  function is unsafe (`lua-callback-threading.md`).

- **Pointers are userdata, not numbers.** CryEngine's Lua 5.1 uses
  `LUA_NUMBER=float` (24-bit mantissa). A pointer-magnitude integer silently
  rounds to a 16 MB grid when it crosses the Lua boundary as a number. Always
  pass `kcdx.memory.pointer` userdata between kcdx calls; only use
  `:get_address()` for an opaque display value, never to feed another kcdx API
  (`lua-precision.md`).

- **Hooks and bytes apply later, not at the call.** `kcdx.hook` and
  `kcdx.bytes` validate immediately (so a malformed call returns `(nil, err)` in
  straight-line code) but **install** in the end-of-zone apply pass, after every
  plugin has registered. A handle's `:applied()` is `nil` until then. To act on
  the outcome, use `kcdx.on("ready", ...)` — it fires after the apply pass with
  final handle status. (`kcdx.command`, `kcdx.console.execute`, and the
  `kcdx.memory.*` runtime calls apply immediately.)

- **Errors teach.** A bad kcdx call returns `(nil, message)` (or raises, for the
  `kcdx.memory.pointer` null-pointer case) with a message in your terms naming
  the fix. Read the second return value.

- **Plugin errors go to your log.** An uncaught error in your `plugin.lua` is
  reported to your plugin's own log (file and, where the engine can recover it,
  line). (A current CryEngine limitation can surface plugin.lua errors without
  a line number — see the engine's outstanding work.)

- **`require` is plugin-scoped.** `require("helper")` resolves your own folder
  and a per-plugin cache; it never reaches another plugin's module (see
  [require](require.md)).
