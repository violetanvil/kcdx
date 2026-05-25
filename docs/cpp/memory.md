# kcdxMemoryInterface (↔ kcdx.memory)
> Part of the [kcdx C++ API](index.md).

AOB scan + byte read/write for C++ plugins. **Built** — `kcdxMemoryInterface`
in [`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h). Fetch via
`QueryInterface(kcdxInterface_Memory, kcdxMemoryInterface_Version)`. Mirrors a
subset of the Lua `kcdx.memory.*` surface. Pattern syntax (space-separated hex
pairs + `?`/`??` wildcards) matches the `[[patch]]`/`[[hook]]` schema exactly.

> **Note — advanced/expert surface.** Pattern scanning and raw byte writes ask
> you to do work the name-based hook path does for you. For function
> interception, prefer the built `kcdxHookInterface` ([hook.md](hook.md)); reach
> for these only when you already hold an address.

## Built methods

```cpp
uintptr_t (*ScanPattern)  (const char* moduleName, const char* pattern);
uintptr_t (*GetModuleBase)(const char* moduleName);
int       (*WriteBytes)   (uintptr_t addr, const void* bytes, size_t size);
int       (*ReadBytes)    (uintptr_t addr, void* out, size_t size);
```

| Method | Args | Returns |
|---|---|---|
| `ScanPattern` | `moduleName` (e.g. `"WHGame.dll"`; `null` = main module), `pattern` (AOB) | Absolute VA of the FIRST match, or `0` (no match, or ambiguous — multiple matches log a warn and return `0`; add context bytes). Mirror of Lua `scan_pattern`/`scan_pattern_from_module`. |
| `GetModuleBase` | `moduleName` | Module base VA, or `0` if not loaded. Mirror of Lua `get_module_base_address`. |
| `WriteBytes` | `addr`, `bytes`, `size` | `1` on success, `0` on failure (writes nothing on failure; page-protect rolls back automatically). Handles `VirtualProtect` for you. |
| `ReadBytes` | `addr`, `out`, `size` | `1` on success (uses `VirtualQuery` to confirm readable; no page-protect dance). |

**Pointer precision:** because C++ works with native `uintptr_t`, there is no
`LUA_NUMBER=float` rounding hazard here — the Lua surface needs a pointer
userdata to dodge that; C++ does not (`lua-precision.md`). This is a
**single-surface** difference, by design: the Lua pointer-userdata machinery
(`p:add`, `p:deref`, `p:get_dword`, …) has no C++ interface analogue because
C++ does pointer arithmetic and typed loads natively. `ReadBytes`/`WriteBytes`
cover the typed-access need.

**SAFETY:** for `WriteBytes`, the same-length single-instruction rewrite is the
documented contract (matches the `[[patch]]` rule). You own the byte-level
safety contract — no clobbering unrelated state.

**Lifecycle:** safe from `kcdxPlugin_Load` and from any messaging callback.

## NYI — runtime native interop

The Lua `kcdx.memory.dynamic_call` (JIT a callable for a native function) and
`kcdx.memory.dynamic_hook` (install a runtime hook on an address you hold) have
**no C++ mirror interface in the header**.

> **NYI** — mirror of [kcdx.memory](../lua/memory.md) `dynamic_call` /
> `dynamic_hook`; lands in the C++ parity backfill. Planned shape: the
> dynamic-call peer is an options-struct describing `target` + `return_type` +
> `param_types`; the dynamic-hook peer maps onto the same `kcdxHookInterface`
> ([hook.md](hook.md)) installing immediately against a held address rather than
> through the deferred apply pass. Exact C++ signatures are not yet defined —
> they land with the interface.

## Minimal snippet

```cpp
auto* mem = static_cast<kcdxMemoryInterface*>(
    api->QueryInterface(kcdxInterface_Memory, kcdxMemoryInterface_Version));
if (!mem) return false;

uintptr_t site = mem->ScanPattern("WHGame.dll", "44 8A F0");
if (site) {
    const unsigned char nops[3] = { 0x90, 0x90, 0x90 };
    mem->WriteBytes(site, nops, sizeof(nops));   // same-length rewrite
}
```

This is the C++ mirror of [kcdx.memory](../lua/memory.md). For byte rewrites by
locator see [bytes.md](bytes.md); for Address Library resolution see
[addr.md](addr.md).
