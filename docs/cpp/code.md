# kcdxTrampolineInterface (↔ kcdx.code)
> Part of the [kcdx C++ API](index.md).

Allocate executable memory the plugin owns, fill it with machine code, and
publish its address as a named symbol. The C++ mirror of the core Lua verb
`kcdx.code{...}` ([../lua/code.md](../lua/code.md)).

**Use `kcdxTrampolineInterface` for three cases:**

1. **The game calls your code.** A game function pointer (vtable slot,
   callback registration, fn-pointer table) needs to point at *your* code.
   There is no game function to hook — you are producing the address itself.
   Allocate the code via `Allocate(...)`, then write the returned address
   into the slot via [`kcdxBytesInterface::Register`](bytes.md) (or the
   memory-write helpers on `kcdxInterface`).
2. **A cross-plugin extension point.** Reserve a NOP region published as a
   symbol (via `kcdxCodeOptions::exportName`) so other plugins can hook the
   symbol via [`kcdxBytesInterface`](bytes.md)'s `target_symbol` locator —
   their own behaviour fills your reserved region. You author the
   convention; other plugins fill it in.
3. **A shared helper several of your own hooks branch to.** Lay out a
   common helper as one block of code your hooks call into, rather than as
   N independent trampolines.

**To run your C++ at an existing game function, use
[`kcdxHookInterface`](hook.md) instead** — it allocates the trampoline and
wires the ABI for you; you never see the address. `kcdxHookInterface` covers
function-entry, mid-instruction, and callsite interception with full
register/memory capture and run-or-skip control. **To rewrite bytes at an
existing site (length-preserving), use [`kcdxBytesInterface`](bytes.md).**
Reach for `kcdxTrampolineInterface` only when you are producing a new
addressable site rather than acting on an existing one.

This page documents `kcdxTrampolineInterface` **v2** as built and verified
(`kcdxTrampolineInterface_Version == 2`,
[`Interfaces.h`](../../include/kcdx/Interfaces.h)). The `cap-40-cpp-code`
regression plugin exercises it end-to-end (the C++ peer of the Lua `kcdx.code`
coverage — parity is tested, not assumed).

The C++ surface for code allocation has **two tiers, and they coexist by
design**:

- **`Allocate(const kcdxCodeOptions*)`** — the **all-in-one**
  allocate+fill+pad+export call: the high-level peer, and the direct mirror of
  Lua's `kcdx.code{...}`. The author declares intent in one struct (name +
  bytes/size + pool + optional export) and the engine allocates from the chosen
  pool, copies the initial bytes, NOP-pads the tail, and registers the export.
  **Reach for this for the everyday case.**
- **`AllocateFromBranchPool` / `AllocateFromLocalPool`** — the **raw pooled
  floor**: hand back a zero-filled region of a given size, nothing more. You
  `memcpy` your own bytes and publish a symbol yourself (via `Export`, below, or
  the `kcdx.code{ export = }` Lua surface). Reach for these when you want only the
  raw allocation with no fill/pad/export step.

`Allocate` is built on top of the raw pool methods — it is the convenience layer,
not a different allocator. Both draw from the same branch/local pools.

A standalone **`Export(owner, bareName, addr)`** publishes the symbol-table entry
for an address you already hold (no allocation needed) — the C++ mirror of
`kcdx.code`'s `export=` for the no-alloc case.

## Fetching the interface

Like every capability interface, fetch it once via `QueryInterface` and cache
the pointer (its lifetime is the engine's). With the [`Kcdx.h`](wrapper.md)
wrapper it is the pre-fetched `K.code` field:

```cpp
auto* code = static_cast<kcdxTrampolineInterface*>(
    api->QueryInterface(kcdxInterface_Trampoline, kcdxTrampolineInterface_Version));
if (!code) { /* engine older than v2 — fail loud, do not skip silently */ }
```

A null return means the running engine does not implement that
interface/version.

## The common path — `Allocate`

```cpp
void* (*Allocate)(const kcdxCodeOptions* opts);
```

Allocates from `opts->pool`, copies `opts->bytes` (length `opts->bytesSize`) to
the head of the region, NOP-pads the tail out to `opts->size`, registers
`opts->exportName` if set, and returns the region pointer.

### Options (`kcdxCodeOptions`)

POD, C-ABI struct ([`Interfaces.h`](../../include/kcdx/Interfaces.h)).
Default-zero every field (`kcdxCodeOptions opts = {};`), then set only the
fields you use. Sentinel for unset: `null` for strings, `0` for numerics.
Because `kcdxCodePool_Branch == 0`, a zero-initialized struct allocates from the
branch pool by default.

| Field | Type | Meaning |
|---|---|---|
| `owningPlugin` | `kcdxPluginHandle` | **Required.** Your plugin handle (from `api->GetPluginHandle("<name>")`). Drives the `<author>.<plugin>` export prefix and the pool ownership attribution. The [wrapper](wrapper.md) threads this for you (`K.self`); raw-interface callers set it themselves. Pass `kcdxInvalidPluginHandle`/`0` for the anonymous path. |
| `name` | `const char*` | **Required.** The label used in logs and export diagnostics (e.g. `"outfit_gate_logic"`). Not itself a shared name — see `exportName` to publish the region's address. |
| `bytes` | `const void*` | Optional initial machine code. The engine copies `bytesSize` bytes from here to the head of the region. `null` = no initial code (a bare NOP region sized by `size`). |
| `bytesSize` | `size_t` | Length in bytes of `bytes`. The C-pointer+length idiom (matching `kcdxMemoryInterface::WriteBytes`). |
| `size` | `size_t` | Optional total bytes to allocate. If `> bytesSize`, the tail beyond the copied bytes is **NOP-padded** (`0x90`) so another plugin can patch into the unused space. `0` = default to `bytesSize` (allocate exactly the initial code). Must be `>= bytesSize`. |
| `pool` | `kcdxCodePool` | `kcdxCodePool_Branch` (default, `0`) places the region within ±2 GB of `WHGame.dll`'s `.text` so a `rel32` branch reaches it; `kcdxCodePool_Local` (`1`) places it anywhere (use when ±2 GB reachability is not required — callers branch in via abs-64 or register-indirect). |
| `exportName` | `const char*` | Optional. A **BARE** symbol name to publish the region's address under. The engine derives the `<author>.<plugin>` prefix from `owningPlugin` and registers `<author>.<plugin>.<exportName>` — you never type your own prefix. A **dotted** `exportName` is an author error and is rejected. `null` = no export. |

**The `bytes` OR `size` rule.** You must set `bytes` (with `bytesSize > 0`) OR
`size` (or both) — the same rule as Lua's "declare `bytes` or `size`". `name`
and `owningPlugin` are always required; `pool` defaults to branch.

### Returns / errors

**Returns** the region pointer (memory marked `PAGE_EXECUTE_READWRITE`). The
plugin owns the pointer for the process lifetime — there is **no Free** function
(matches SKSE's model). **Returns `null`** when: `opts` is null; `name` is
missing; neither `bytes` nor `size` is set; `size < bytesSize`; `exportName` is
dotted; the pool cannot allocate (out of space, or no `rel32`-reachable region
for the branch pool); or `exportName` collides with a symbol already registered
under the same fully-qualified name (the region is still allocated but is
unreachable by that symbol). The teaching reason is auto-logged at Error level
to the engine log on every null return.

## The standalone publish — `Export`

```cpp
bool (*Export)(kcdxPluginHandle owner, const char* bareName, uintptr_t addr);
```

Registers `addr` under `<owner-author>.<owner-plugin>.<bareName>` via the
cross-plugin symbol table — for an address the plugin **already holds** without
allocating (e.g. a static function or buffer in your DLL). `bareName` is a BARE
name; the engine derives the `<author>.<plugin>` prefix from `owner` (a dotted
`bareName` is rejected).

**Returns** `true` on success; `false` on bad args (null/empty `bareName`,
dotted `bareName`, `addr == 0`) or a collision (the same plugin re-exporting the
same bare name with a different address — names are per-namespace, so
cross-plugin clashes cannot happen). The teaching reason is auto-logged.

## Consuming an export

An export published by `Allocate(exportName=)` or `Export` is resolved by name
through the root `kcdxInterface`:

- From **your own** plugin, resolve the **bare** name with your handle as the
  owner so the self-tier finds it: `K.api->ResolveSymbolAs(K.self,
  "outfit_gate_logic")`. (A bare `ResolveSymbol("outfit_gate_logic")` carries no
  caller identity and resolves on the other-only path — it **misses** your own
  export, which is stored under your `<author>.<plugin>` prefix. Thread the owner
  with `ResolveSymbolAs`.)
- From **another** plugin, resolve the explicit
  `"<author>.<plugin>.outfit_gate_logic"` form, or pass that to a hook/byte
  `targetSymbol` locator.

## The raw pooled floor — `AllocateFromBranchPool` / `AllocateFromLocalPool`

```cpp
void* (*AllocateFromBranchPool)(kcdxPluginHandle owner, size_t size);
void* (*AllocateFromLocalPool) (kcdxPluginHandle owner, size_t size);
```

| Method | Proximity guarantee | Use when |
|---|---|---|
| `AllocateFromBranchPool` | Within ±2 GB of `WHGame.dll`'s `.text`, so a 5-byte `E9` `rel32` jump reaches it. Limited budget (default 64 KB across all plugins). | The game branches into the region via a 5-byte rel32 jump (the `kcdxCodePool_Branch` case). |
| `AllocateFromLocalPool` | Anywhere `VirtualAlloc` places it; effectively unlimited. Callers must use abs-64 (`FF 25` + 8-byte target) or register-indirect calls. | Proximity does not matter (the `kcdxCodePool_Local` case). |

Both return memory marked `PAGE_EXECUTE_READWRITE` and zero-filled (`null` on
exhaustion / no rel32-reachable region). Tag with `owner` so kcdx's conflict
detector knows which plugin owns the byte range. You fill the bytes yourself
(`memcpy`) and publish a symbol via `Export` (or the `kcdx.code{ export = }`
Lua surface). These are the floor `Allocate` is built on; reach for them only when
you want the raw region with no fill/pad/export step.

## Minimal snippet (the common path)

A copy-paste-runnable region allocation + export by **bare name**, using the
[`Kcdx.h`](wrapper.md) wrapper's pre-fetched `K.code`:

```cpp
#include "kcdx/Interfaces.h"
#include "kcdx/Kcdx.h"

static Kcdx K;

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    if (!K.Init(api, "walkabout", "violetanvil")) return true;  // logs why
    if (!K.code) return true;  // engine older than v2 — fail loud upstream

    // "mov eax, 42; ret" — a tiny self-contained int() routine.
    static const unsigned char kCode[6] = { 0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3 };

    kcdxCodeOptions opts = {};
    opts.owningPlugin = K.self;                 // self-tier resolution + ownership
    opts.name         = "outfit_gate_logic";
    opts.bytes        = kCode;
    opts.bytesSize    = sizeof(kCode);
    opts.size         = 256;                    // tail [6,256) is NOP-padded (0x90)
    opts.pool         = kcdxCodePool_Branch;    // rel32-reachable from a hook site
    opts.exportName   = "outfit_gate_logic";    // BARE; engine stamps the prefix

    void* region = K.code->Allocate(&opts);
    if (!region) return true;  // engine already logged the teaching reason

    // Consume your own export by bare name (self-tier resolver):
    uintptr_t a = K.api->ResolveSymbolAs(K.self, "outfit_gate_logic");  // == region
    // Elsewhere (another plugin): resolve "walkabout.violetanvil.outfit_gate_logic",
    // or point a hook/byte targetSymbol at it.
    (void)a;
    return true;
}
```

For an address you **already hold** (no allocation), publish it directly:

```cpp
static int my_routine(int x) { return x + 1; }

bool ok = K.code->Export(K.self, "my_routine",
                         reinterpret_cast<uintptr_t>(&my_routine));
// resolvable as "walkabout.violetanvil.my_routine" from anywhere,
// or bare via ResolveSymbolAs(K.self, "my_routine") in this plugin.
```

---

See also: [../lua/code.md](../lua/code.md) (the Lua peer), [bytes.md](bytes.md)
(equal-length rewrite of existing bytes), [hook.md](hook.md) (branch into an
allocated region), [addr.md](addr.md) (name → address resolution), and
[cross-cutting.md](cross-cutting.md) (the symbol table / ABI append-only
discipline).
