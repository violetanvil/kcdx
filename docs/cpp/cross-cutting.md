# Cross-cutting rules (↔ cross-cutting)
> Part of the [kcdx C++ API](index.md).

The C++ spelling of the Lua [cross-cutting rules](../lua/cross-cutting.md).
Grounded in [`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h) where
the header documents the rule.

## Threading — defer game-state work to the main thread (`kcdxTaskInterface`)

Most CryEngine state is **not** thread-safe. Engine lifecycle messages
([lifecycle.md](lifecycle.md)) and console-command callbacks
([command.md](command.md)) fire on the main thread. But if your code runs from a
non-main thread (e.g. inside a MinHook detour on a function called from the
game's worker pool), defer any game-state mutation onto the next update tick via
the **built** `kcdxTaskInterface` (fetch via
`QueryInterface(kcdxInterface_Task, kcdxTaskInterface_Version)`):

```cpp
struct kcdxTask {                 // derive from this; share MSVC vtable ABI
    virtual ~kcdxTask() = default;
    virtual void Run()     = 0;   // called on the main thread next update tick
    virtual void Dispose() = 0;   // called after Run(); typically `delete this;`
};

void (*AddTask)(kcdxTask* task);  // safe to call from any thread
```

This is the C++ mechanism behind the Lua "callbacks run on the main thread"
guarantee. The Lua VM is single-threaded — a Lua callback firing off-thread
races the main thread (`.claude/rules/lua-callback-threading.md`); on the C++
side, only hook functions the game runs on the main thread are safe to fire work
from directly, otherwise marshal through `AddTask`.

## Numeric precision — pointers through the Lua boundary

C++ pointer arithmetic is native and exact — the `LUA_NUMBER=float` rounding
hazard only bites when a value crosses into the Lua VM. If your C++ plugin pushes
values into pak Lua via `kcdxScriptingInterface`'s `kcdxLuaApi`, the
`PushInteger`/`PushNumber` (and `LCheckNumber`/`LCheckInteger`) caveat applies:
above 2^24 the value loses low bits; at pointer magnitudes it rounds to a 16 MB
grid. Use `PushLightUserdata` for pointers. The caveat is documented inline in
the header on those members; see [lua.md](lua.md) and
`.claude/rules/lua-precision.md`.

## One shared Lua state — use `kcdxLuaApi`, don't call the game's `lua_*` copy directly

kcdx and WHGame.dll each statically link their own copy of Lua 5.1, but both
drive **one shared `lua_State`** — the same globals, tables, and stack. So the
`kcdxLuaApi` you fetch from `kcdxScriptingInterface` already reaches everything
the game's Lua sees; it is the full, safe path to the live VM, and a plugin
never needs the game's own `lua_*` exports.

*Hooking* a game Lua function is fully supported — install a `kcdxHookInterface`
hook on it and your callback fires when the game calls it. What is **not** safe
is a plugin *calling* the game's compiled `lua_*` copy directly by address (e.g.
`ResolveAddressByName("kcdx.lua_settable")` then invoking it on a stack you
built with `kcdxLuaApi`): the two copies have separate internal `static const`
sentinels, so entering the game's copy with the other copy's stack raises a Lua
error that `longjmp`s out of your call (it never returns; it is uninstrumentable
from the plugin side). There is no reason to do this — build and act on the
shared state through `kcdxLuaApi`, which crosses no boundary. The underlying
hazard (the dual-Lua GC-sentinel problem) is `.claude/rules/lua-bridge.md`;
PROBE A in `docs/known-issues/cap-38 cpp before-observer never fires on a named
game target.md` is the worked demonstration.

## Error conventions

- **`QueryInterface` returns `null`** if the interface ID is unknown or the
  requested version is newer than the engine supports — always null-check.
- **`Resolve*` / `ScanPattern` / `GetModuleBase` return `0`** on a miss (unknown
  ID, wrong game version, no/ambiguous match). `0` is the not-found sentinel,
  not an exception.
- **`bool` / `int` returns** signal success: `RegisterListener`/`Dispatch`/
  `RegisterCommand`/`ExecuteString` return `bool`; `RegisterFunction`/
  `WriteBytes`/`ReadBytes` return `int` (`1`/`0`).
- **`GetPluginHandle` returns `kcdxInvalidPluginHandle`** on a name miss.
- A `false` return from `kcdxPlugin_Load` is logged but the DLL is not unloaded.

## ABI is append-only

`kcdxInterface` and every `kcdx*Interface` struct are **append-only**: new
function pointers go at the END, never inserted mid-struct. Inserting shifts
every later pointer's offset, so a plugin DLL compiled against the older header
calls through the wrong offset → ACCESS_VIOLATION. The header carries an
explicit `--- APPEND-ONLY BELOW THIS LINE ---` marker (e.g. `ResolveAddressByName`
sits after it). A genuine layout change bumps `kcdx<Name>Interface_Version` and
gates the new shape. This is `.claude/rules/anti-patterns.md` AP11 — there is no
Lua-side analogue (the Lua surface is name-keyed, not offset-keyed), so this rule
is C++-specific.

This is the C++ mirror of [the Lua cross-cutting rules](../lua/cross-cutting.md).
