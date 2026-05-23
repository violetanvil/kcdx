# kcdxScriptingInterface (↔ the kcdx.lua domain)
> Part of the [kcdx C++ API](index.md).

Register native C functions callable from pak Lua, and reach the full Lua 5.1 C
API. **Built** — `kcdxScriptingInterface` (with the embedded `kcdxLuaApi`
surface) in [`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h).
Fetch via
`QueryInterface(kcdxInterface_Scripting, kcdxScriptingInterface_Version)`.

This is the C++ side of VM interop. The Lua `kcdx.lua` domain
([../lua/lua.md](../lua/lua.md)) is VM-*introspection* sugar
(`cfunction_address`, `_probe_numbers`); the C++ surface goes the other way —
it lets a C++ plugin *author* new Lua-callable functions and drive the live
`lua_State` directly. The two are the same model (the C++/Lua VM bridge) seen
from each language's side.

## RegisterFunction — author a Lua-callable native function

```cpp
int (*RegisterFunction)(kcdxPluginHandle owner,
                        const char*      table_name,
                        const char*      fn_name,
                        kcdxLuaCFunction fn,
                        void*            user_data);
```

| Arg | Type | Meaning |
|---|---|---|
| `owner` | `kcdxPluginHandle` | Your handle. |
| `table_name` | `const char*` | Valid Lua identifier; the function lands at `kcdx.<table_name>.<fn_name>` in pak Lua. Copied. |
| `fn_name` | `const char*` | Valid Lua identifier. Copied. |
| `fn` | `kcdxLuaCFunction` | `int (*)(struct lua_State* L, void* user_data)` — returns the number of values pushed onto the Lua stack. |
| `user_data` | `void*` | Passed through to every invocation; commonly `scripting->lua` so the function reaches the C API without a global. Plugin owns its lifetime. |

**Returns:** `int` — `1` on success, `0` on failure (invalid args, name
collision, OOM; failures also logged). **Threading:** main thread only (from
`kcdxPlugin_Load` or a `Task::Run` callback). Registrations made before kcdx's
first update tick are queued and applied once the `kcdx` global table is
created; functions persist for the session (no Unregister).

## The lua C API surface

```cpp
const kcdxLuaApi* lua;   // member of kcdxScriptingInterface
```

`kcdxLuaApi` exposes all 117 Lua 5.1 `LUA_API` + `LUALIB_API` functions as
function pointers (KCD2's Lua VM is statically linked inside kcdx with no
exported symbols, so a plugin DLL cannot link them directly). Naming: `lua_X`
→ `X` (PascalCase), `luaL_X` → `LX`. Stash `scripting->lua` and call
`lua->PushString(L, ...)` etc.

> **Precision caveat (single-surface, intrinsic to CryEngine's Lua).** KCD2's
> Lua 5.1 is built with `LUA_NUMBER=float` (24-bit mantissa). `PushInteger` /
> `PushNumber` (and `LCheckNumber` / `LCheckInteger`) lose low bits above 2^24;
> at pointer magnitudes (~2^47) values round to a 16 MB grid. **Never push a
> pointer through `PushInteger`/`PushNumber`** — use `PushLightUserdata` (exact)
> or the `kcdx.memory.pointer` userdata channel. The header docstrings carry this
> warning; see `.claude/rules/lua-precision.md`. (This caveat cannot be fixed
> inside kcdx — it is the engine's Lua build.)

## Minimal snippet

```cpp
static int Lua_Greet(struct lua_State* L, void* ud) {
    auto* lua = static_cast<const kcdxLuaApi*>(ud);
    lua->PushString(L, "hello!");
    return 1;                                   // one value pushed
}

bool kcdxPlugin_Load(const kcdxInterface* api) {
    auto* scripting = static_cast<kcdxScriptingInterface*>(
        api->QueryInterface(kcdxInterface_Scripting, kcdxScriptingInterface_Version));
    if (!scripting) return false;
    scripting->RegisterFunction(api->GetPluginHandle("my.plugin"),
                                "hello", "greet",
                                Lua_Greet, (void*)scripting->lua);
    return true;   // pak Lua can now call kcdx.hello.greet()
}
```

This is the C++ counterpart of [the kcdx.lua domain](../lua/lua.md). The Lua
`kcdx.lua.cfunction_address` introspection helper has no C++ analogue interface
(C++ plugins hold their own function pointers natively — single-surface).
