# kcdxTrampolineInterface (↔ kcdx.code)
> Part of the [kcdx C++ API](index.md).

Allocate executable memory the plugin owns and fills with machine code.
**Built** — `kcdxTrampolineInterface` in
[`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h). Fetch via
`QueryInterface(kcdxInterface_Trampoline, kcdxTrampolineInterface_Version)`.

This is the C++ spelling of Lua's `kcdx.code{...}`. Where Lua takes a
`{ pool = "branch"/"local" }` field, C++ exposes the two pools as two methods.

## Call shape

```cpp
void* (*AllocateFromBranchPool)(kcdxPluginHandle owner, size_t size);
void* (*AllocateFromLocalPool) (kcdxPluginHandle owner, size_t size);
```

| Method | Proximity guarantee | Use when |
|---|---|---|
| `AllocateFromBranchPool` | Within ±2 GB of `WHGame.dll`'s `.text`, so a 5-byte `E9` `rel32` jump reaches it. Limited budget (default 64 KB across all plugins). | The game branches into the region via a 5-byte rel32 jump (the Lua `pool = "branch"` default). |
| `AllocateFromLocalPool` | Anywhere `VirtualAlloc` places it; effectively unlimited. Callers must use abs-64 (`FF 25` + 8-byte target) or register-indirect calls. | Proximity does not matter (the Lua `pool = "local"`). |

| Arg | Type | Meaning |
|---|---|---|
| `owner` | `kcdxPluginHandle` | Tags the byte range so the conflict detector knows which plugin owns it (the mirror of Lua's `name`/`export` ownership). |
| `size` | `size_t` | Bytes to allocate. |

**Returns:** a `void*` to memory marked `PAGE_EXECUTE_READWRITE` and
zero-filled. `null` if `size` is zero, the pool is exhausted, or (branch pool)
no rel32-reachable free region exists. The plugin owns the pointer for the
process lifetime — there is **no Free** function (matches SKSE's model).

> **Parity note (NYI portion).** The Lua `kcdx.code` returns a
> `kcdx.memory.pointer` userdata and accepts `bytes`/`size`/`export`/`name`
> fields — the initial-machine-code fill and the named `export` (symbol
> publication, consumed via the hook `target_symbol` locator) are conveniences
> layered on top of the raw allocation. The C++ interface today exposes only the
> raw pooled allocation; you fill bytes yourself (e.g. `memcpy` into the returned
> pointer) and publish a symbol through the `[[trampoline]]` TOML `export` field.
> A struct-shaped `Allocate(const kcdxCodeOptions&)` mirroring the full Lua field
> set is **NYI** — it lands in the C++ parity backfill.

## Minimal snippet

```cpp
auto* tramp = static_cast<kcdxTrampolineInterface*>(
    api->QueryInterface(kcdxInterface_Trampoline, kcdxTrampolineInterface_Version));
if (!tramp) return false;

void* region = tramp->AllocateFromBranchPool(api->GetPluginHandle("my.plugin"), 256);
if (!region) { gLog.Error("CODE", "branch pool exhausted"); return false; }

static const unsigned char kRet = 0xC3;       // RET
memcpy(region, &kRet, 1);                       // fill it yourself
```

This is the C++ mirror of [kcdx.code](../lua/code.md).
