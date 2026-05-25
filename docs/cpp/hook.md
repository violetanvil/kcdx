# kcdxHookInterface (↔ kcdx.hook)
> Part of the [kcdx C++ API](index.md).

Intercept a game function: run your C++ callback when the game calls it, and
optionally change its arguments, return value, or whether it runs at all. The
C++ mirror of the core Lua verb `kcdx.hook{...}` ([../lua/hook.md](../lua/hook.md)).

> For the everyday path, prefer the **empowered wrapper** in
> [`include/kcdx/Kcdx.h`](../../include/kcdx/Kcdx.h) —
> `kcdx::hook::Before/After/Around/Replace<Sig, &fn>(K, target)` lets you write
> a natural typed callback instead of the mangled per-mode `cFn` ABI shown
> below. See [wrapper.md](wrapper.md). This raw interface is the
> always-available **floor** underneath it (and the only path for the Mid /
> Callsite sub-verbs, which the wrapper does not wrap). Read this page when you
> need the raw floor: a callback whose ABI doesn't templatize, or the Mid /
> Callsite sub-verbs.

This page documents `kcdxHookInterface` **v1** as built and verified
(`kcdxHookInterface_Version == 1`, [`Interfaces.h:1346`](../../include/kcdx/Interfaces.h)).
The `cap-36-cpp-hook-interface` regression plugin exercises every method
end-to-end (7/7 PASS).

## Fetching the interface

Like every capability interface, fetch it once via `QueryInterface` and cache
the pointer (its lifetime is the engine's):

```cpp
auto* hook = static_cast<const kcdxHookInterface*>(
    api->QueryInterface(kcdxInterface_Hook, kcdxHookInterface_Version));
if (!hook) { /* engine version mismatch — fail loud, do not skip silently */ }
```

A null return means the running engine does not implement that
interface/version.

## The surface — six install sub-verbs + four query methods

`kcdxHookInterface` mirrors the Lua `kcdx.hook.before / .after / .around /
.replace / .mid / .callsite` sub-verbs **one-to-one** as six method pointers —
the variant IS the method name, there is no shared `Install` with a `mode`
enum (`lua-api-surface.md` rule 4a). Four query/control methods operate on a
returned handle.

Every install method has the **same shape** ([`Interfaces.h:1571-1596`](../../include/kcdx/Interfaces.h)):

```cpp
kcdxHookHandle (*Before)  (const char* target, void* callback,
                           const kcdxHookOptions* opts /* nullable */);
kcdxHookHandle (*After)   (const char* target, void* callback, const kcdxHookOptions* opts);
kcdxHookHandle (*Around)  (const char* target, void* callback, const kcdxHookOptions* opts);
kcdxHookHandle (*Replace) (const char* target, void* callback, const kcdxHookOptions* opts);
kcdxHookHandle (*Mid)     (const char* target, void* callback, const kcdxHookOptions* opts);
kcdxHookHandle (*Callsite)(const char* target, void* callback, const kcdxHookOptions* opts);
```

### Arguments (shared by all six install methods)

| Arg | Type | Meaning |
|---|---|---|
| `target` | `const char*` | **The common path.** A *name* the engine resolves to address AND verified ABI — see [Locators](#locators). Pass `null`/`""` only when using an `[advanced]` locator in `opts`. For `Callsite`, this is the FUNCTION containing the call instruction. |
| `callback` | `void*` | Your C callback, a function pointer cast to `void*` for C-ABI portability. The per-mode ABI it must have is the [mangled `cFn` floor](#the-mangled-cfn-abi-the-raw-floor). Must be a free function or static member — **capturing lambdas are NOT directly callable**; pass a free function that reaches into your own statics. |
| `opts` | `const kcdxHookOptions*` | Optional knobs container; pass `nullptr` for the simple case. See [Options](#options-kcdxhookoptions). |

### Return value

Every install method returns a `kcdxHookHandle`
([`Interfaces.h:1355`](../../include/kcdx/Interfaces.h) — an opaque
`uint64_t`):

- **`0`** = registration **FAILED** at install time (locator mismatch,
  signature parse failure, unknown owning-plugin handle, etc.). The teaching
  reason is auto-logged at Error level (see [Errors](#errors)).
- **A non-zero handle does NOT yet mean applied.** kcdx defers apply to a later
  pass (the deferred-apply model). Query `IsApplied(h)` *after* the apply pass
  to confirm. The handle is stable for the process lifetime, never reused, safe
  to copy by value.

### Errors

On a **zero** (failed) handle the engine auto-logs the teaching reason at Error
level to **both** the engine log AND the calling plugin's log — raw-interface
callers get the same loud-on-failure behavior as wrapper users
([`Interfaces.h:1559-1568`](../../include/kcdx/Interfaces.h)). Example reason:

> `target 'IsInCombat' did not resolve in WHGame.dll @ runtime build
> 1.5.1164953 — confirm the name in your Address Library, or use the expert
> 'pattern' locator`

A failure that only becomes knowable at apply time (the locator does not
resolve on the running build, or a conflict is lost) does NOT zero the handle —
registration returns a real handle, then `IsApplied(h)` reads false and
`GetReason(h)` carries the apply-time reason.

## Locators

The hook needs to find its target. The **common path is by name** — the
`target` positional argument:

- **`target = "<name>"`** — a named function. The name resolves **both** the
  address and the verified signature, so you write no hex and no ABI (the
  disassembler test, `cornerstones.md`). The engine resolves via
  `address_library::ResolveByName(target, opts->owningPlugin)` with
  **self > engine > other** precedence (`naming-namespaces.md`). Three name
  forms:
    - an **engine** [Address Library](addr.md) name — `"IsInCombat"`;
    - one of **your own** [author-declared targets](targets.md), bare —
      `"open_inventory"` (the engine stamps `<author>.<plugin>.open_inventory`);
    - another plugin's target, explicit — `"redmoon.outfit.open_inventory"`;
    - the engine-seed form — `"kcdx.luaL_loadfile"`.

  A named target that carries a verified signature leaves `opts->signature`
  null — the engine substitutes its known ABI; you never re-type it.

### Advanced locators (expert-only escape hatch)

The fields below in `kcdxHookOptions` are the `[advanced]` escape hatch for
targets the library cannot yet name. Pass an empty `target` (`null`/`""`) AND
set **exactly one**. On a locator that carries no signature (e.g. a raw
`address`), you MUST set `opts->signature` yourself — there is no name for the
engine to carry the ABI from, and the install fails when it is null.

Identify an un-named target **once** via an advanced locator, name it (publish
via `[[address]]` / `kcdx.address` or a cross-plugin export), and refer to it
by name thereafter (`cornerstones.md` "declare once / share / coexist").

## Options (`kcdxHookOptions`)

POD, C-ABI struct ([`Interfaces.h:1442-1529`](../../include/kcdx/Interfaces.h)).
Default-zero every field (`kcdxHookOptions opts = {};`), then set only the
fields you use. Sentinel for unset: `null` for strings, `0` for numerics.

| Field | Type | Line | Meaning |
|---|---|---|---|
| `name` | `const char*` | 1448 | Optional log/conflict label; null = engine synthesizes `"<handleId>:<target>"`. |
| `description` | `const char*` | 1449 | Optional free text; may be null. |
| `pattern` | `const char*` | 1457 | `[advanced]` AOB hex at function entry; null = unset. |
| `addressId` | `uint64_t` | 1458 | `[advanced]` Address-Library numeric ID; 0 = unset. |
| `targetSymbol` | `const char*` | 1459 | `[advanced]` cross-plugin symbol-table lookup; null = unset. |
| `targetLuaCfunction` | `const char*` | 1460 | `[advanced]` Lua C-function target (e.g. `"System.LogAlways"`); null = unset. |
| `address` | `uintptr_t` | 1461 | `[advanced]` raw absolute VA; 0 = unset. |
| `offset` | `int32_t` | 1462 | Applied after resolution (Mid uses this too). |
| `context` | `const char*` | 1463 | `[advanced]` AOB disambiguation; null = none. |
| `anchorString` | `const char*` | 1464 | `[advanced]` string anchor; null = none. |
| `maxAnchorDistance` | `uint32_t` | 1465 | Default 4096 (engine substitutes if 0). |
| `module` | `const char*` | 1466 | Default `"WHGame.dll"` (engine substitutes if null). |
| `callsitePattern` | `const char*` | 1474 | `[advanced]` (Callsite) AOB at the CALL instr; null = unset. |
| `callsiteOffset` | `int32_t` | 1475 | (Callsite) offset to the CALL opcode in the pattern match. |
| `callsiteAddressId` | `uint64_t` | 1476 | `[advanced]` (Callsite) Address-Library ID of the callsite; 0 = unset. |
| `callsiteRva` | `const char*` | 1477 | `[advanced]` (Callsite) `"WHGame.dll @ rva 0x12345a"`; null = unset. |
| `signature` | `const char*` | 1489 | The ABI DSL string (`"i32 (i32 seed)"`, `"void ()"`). Null on a named-target path (engine substitutes its verified ABI) and for Mid; **required** on a no-name locator that carries no signature. Grammar mirrors the Lua [signature grammar](../lua/hook.md#signature-grammar). |
| `captures` | `const kcdxHookCapture*` | 1496 | (Mid only) pointer to the capture-descriptor array; null otherwise. |
| `captureCount` | `uint32_t` | 1497 | (Mid only) length of `captures`; 0 otherwise. |
| `offThread` | `kcdxHookOffThread` | 1503 | Off-thread routing policy. Default 0 = `Marshal`. See [Threading](#threading). |
| `owningPlugin` | `kcdxPluginHandle` | 1513 | Your plugin handle (from `api->GetPluginHandle("<name>")`). Drives the self-tier of self > engine > other for a bare-name `target`. Pass `kcdxInvalidPluginHandle` (or 0) for the anonymous path. The wrapper threads this for you; **raw-interface callers set it themselves**. |
| `callsiteBehavior` | `kcdxHookCallsiteBehavior` | 1528 | (Callsite only) which behavior the callsite redirect uses. Default 0 = `Before`. See [Callsite](#callsite). |

## The mangled `cFn` ABI (the raw floor)

The engine's JIT thunk casts your `void* callback` to a per-mode signature
derived from the resolved target's (or `opts->signature`'s) ABI. **This is the
raw floor** — the wrapper ([wrapper.md](wrapper.md)) hides it for
Before/After/Around/Replace, but it is documented here because (a) it is always
reachable through the raw interface, and (b) Mid and Callsite have no wrapper.
A wrong-shape callback is undefined behavior; match it exactly. For
`Sig = R(Args...)`:

- **Before** — `void cFn(uintptr_t args[], int* outCount, /* typed Args... */)`.
  `args[]` is the mutation back-channel: write a new value into `args[i]`, then
  set `*outCount = N` to commit the first N slots. Set `*outCount = 0` (or leave
  it) to leave args unchanged. The original always runs afterward.

  ```cpp
  // Sig = int(int seed): bump the arg by 1.
  extern "C" void cb(uintptr_t args[], int* outCount, int seed) {
      (void)seed;
      args[0]   = static_cast<uintptr_t>(static_cast<int32_t>(args[0]) + 1);
      *outCount = 1;
  }
  ```

- **After (non-void return)** — `R cFn(R origReturn, /* typed Args... */)`.
  Receive the original's return; return the (possibly changed) one. Args are
  observe-only (by value — the original already ran with them).

  ```cpp
  // Sig = int(int seed): add 1000 to the original return.
  int cb(int origReturn, int seed) { (void)seed; return origReturn + 1000; }
  ```

- **After (void return)** — `void cFn(/* typed Args... */)`. Observe-only.

- **Around** — `R cFn(R(*call_original)(/* typed Args... */), /* typed Args... */)`.
  `call_original` is a typed function pointer; call it zero, one, or many times
  and return any `R`. The only mode that can conditionally skip the original.

  ```cpp
  // Sig = int(int seed): double the original's result.
  int cb(int (*call_original)(int), int seed) { return 2 * call_original(seed); }
  ```

- **Replace** — `R cFn(/* typed Args... */)`. The original never runs; your
  return is the result.

  ```cpp
  // Sig = int(int seed): constant 42.
  int cb(int seed) { (void)seed; return 42; }
  ```

- **Mid** — `void cFn(kcdxHookCaptureValue* values, int count)`. See
  [Mid](#mid).

- **Callsite** — the `cFn` ABI matches the shape of the behavior selected by
  `opts->callsiteBehavior` (the four shapes above). See [Callsite](#callsite).

## Mid

`Mid` intercepts a single instruction at `opts->offset` inside the target and
reads/writes register/memory captures. It takes **no function signature** (it
doesn't need the function's ABI) — instead it takes `captures` via `opts`.
`Mid` is **raw-floor-only**: the wrapper has no empowered helper for it (its
register/memory captures don't templatize), so this is the only doc for its
ABI.

**Declaring captures.** Author-owned array of `kcdxHookCapture`
([`Interfaces.h:1384-1388`](../../include/kcdx/Interfaces.h)), passed by pointer
+ count. The engine reads-only at install time:

```cpp
typedef struct kcdxHookCapture {
    const char* expr;   // register / memory expr ("rcx", "[rcx+0x10]")
    const char* type;   // type string ("i32", "i64", "ptr", …)
    const char* name;   // optional — author's name; null = positional
} kcdxHookCapture;
```

**The callback ABI** is `void cFn(kcdxHookCaptureValue* values, int count)`. The
engine fills one `kcdxHookCaptureValue`
([`Interfaces.h:1410-1417`](../../include/kcdx/Interfaces.h)) per capture
pre-call, reads the SAME field back post-call, and writes the bytes back. Read
and write `values[i].value_<type>` per the capture's declared type:

```cpp
typedef struct kcdxHookCaptureValue {
    const char* name;          // capture name, or null for positional
    const char* type;          // capture type string ("i32", "ptr", …)
    int64_t     value_int64;   // integer/bool types
    double      value_double;  // f32/f64 types
    void*       value_ptr;     // ptr type
} kcdxHookCaptureValue;
```

Type → field mapping ([`Interfaces.h:1406-1409`](../../include/kcdx/Interfaces.h)):

- `i8/i16/i32/i64/u8/u16/u32/u64/bool` → `value_int64`
- `f32/f64/float/double` → `value_double`
- `ptr` → `value_ptr`

Touching the wrong field is silently dropped (e.g. setting `value_double` on an
`i32` capture).

```cpp
// Clamp a captured HP register to 1000.
static const kcdxHookCapture kCaps[] = { { "rax", "i32", "hp" } };

void mid_cb(kcdxHookCaptureValue* values, int count) {
    (void)count;
    if (values[0].value_int64 > 1000) values[0].value_int64 = 1000;  // write-back
}

void install(const kcdxHookInterface* hook, kcdxPluginHandle self) {
    kcdxHookOptions opts = {};
    opts.owningPlugin = self;
    opts.address      = /* [advanced] raw VA of the capture site */ 0;
    opts.offset       = 0;
    opts.captures     = kCaps;
    opts.captureCount = 1;
    hook->Mid(/*target=*/nullptr, (void*)&mid_cb, &opts);
}
```

## Callsite

`Callsite` redirects **one CALL instruction** rather than the function entry.
The positional `target` is the FUNCTION whose body contains the call; the call
instruction is located by exactly one of `opts->callsitePattern` /
`opts->callsiteAddressId` / `opts->callsiteRva` (with optional
`opts->callsiteOffset`). The behavior at that callsite is selected by
`opts->callsiteBehavior` ([`Interfaces.h:1372-1376`](../../include/kcdx/Interfaces.h)):

- `kcdxHookCallsiteBehavior_Before` (0, default)
- `kcdxHookCallsiteBehavior_After` (1)
- `kcdxHookCallsiteBehavior_Around` (2)
- `kcdxHookCallsiteBehavior_Replace` (3)

The `cFn` ABI matches the selected behavior's shape (the four under
[the mangled cFn ABI](#the-mangled-cfn-abi-the-raw-floor)). Like Mid, `Callsite`
is **raw-floor-only** — its call-instruction sub-locators don't templatize, so
the wrapper does not wrap it and this is its only doc.

## Query / control methods

Operate on a handle returned by an install method
([`Interfaces.h:1606-1630`](../../include/kcdx/Interfaces.h)):

```cpp
bool        (*IsApplied)(kcdxHookHandle h);
const char* (*GetReason)(kcdxHookHandle h);
const char* (*GetName)  (kcdxHookHandle h);
bool        (*Uninstall)(kcdxHookHandle h);
```

- **`IsApplied(h)`** — true iff the apply pass has installed the hook. False for
  an unknown, still-pending, uninstalled, or failed-to-apply handle. Mirrors Lua
  `h:applied()`.
- **`GetReason(h)`** — the teaching failure string for a hook that did not apply
  (or was uninstalled); null when `h` is valid AND applied. Engine-owned,
  process-lifetime. Mirrors Lua `h:reason()`.
- **`GetName(h)`** — the author-supplied or engine-synthesized name; null for an
  unknown handle. Engine-owned. Mirrors Lua `h:name()`.
- **`Uninstall(h)`** — logically remove the hook; returns true on success.
  Idempotent (already-uninstalled / never-applied → true, no-op). Safe from
  `kcdxPlugin_Load` or any messaging callback. After return, `IsApplied(h)` is
  false and the callback no longer fires. The underlying MinHook detour stays
  installed for the session (reused if another install lands on the same target
  later). Mirrors Lua `h:uninstall()`.

## Minimal snippet (raw floor)

A copy-paste-runnable `Before` on a no-name target, threading
`GetPluginHandle → owningPlugin` by hand (the idiom the `cap-36` raw-floor row
uses). For a named game target, the everyday path is the
[`Kcdx.h` wrapper](wrapper.md) — reach for this raw shape when you need a
callback that doesn't templatize, or the Mid / Callsite sub-verbs.

```cpp
#include "kcdx/Interfaces.h"

// A target inside our own DLL (no engine name carries its ABI).
extern "C" __declspec(noinline) int my_add(int seed) { return seed + 100; }

// Before callback — RAW mangled ABI: bump the arg by 1.
extern "C" void before_cb(uintptr_t args[], int* outCount, int seed) {
    (void)seed;
    args[0]   = static_cast<uintptr_t>(static_cast<int32_t>(args[0]) + 1);
    *outCount = 1;
}

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    auto* hook = static_cast<const kcdxHookInterface*>(
        api->QueryInterface(kcdxInterface_Hook, kcdxHookInterface_Version));
    if (!hook) return true;  // engine version mismatch — fail loud upstream

    kcdxHookOptions opts = {};
    opts.owningPlugin = api->GetPluginHandle("my_plugin");  // [plugin].name
    opts.address      = reinterpret_cast<uintptr_t>(&my_add);  // [advanced] no-name path
    opts.signature    = "i32 (i32 seed)";  // required — no name to carry the ABI
    opts.name         = "bump_seed";

    kcdxHookHandle h = hook->Before(/*target=*/nullptr, (void*)&before_cb, &opts);
    if (h == 0) return true;  // engine already logged the teaching reason

    // Apply is deferred; query IsApplied(h) after the apply pass (e.g. in
    // kcdxPlugin_PostGameLoad or a kcdxMessage_InputLoaded listener).
    return true;
}
```

## Threading

C++ callbacks behave identically to Lua callbacks on off-thread fires
(`lua-callback-threading.md`). The engine compares the dispatch thread to the
recorded game main thread; off-thread fires route per `opts->offThread`
([`Interfaces.h:1362-1365`](../../include/kcdx/Interfaces.h)):

- `kcdxHookOffThread_Marshal` (0, default) — engine queues the callback onto the
  main thread; the original returns synchronously with its pre-hook default
  behavior. The right answer for almost every site.
- `kcdxHookOffThread_Skip` (1) — silently drop off-thread fires; warn-once-per-hook.
- `kcdxHookOffThread_Error` (2) — log an error and drop; the author asserts this
  site is main-thread-only.

## Chaining

Multiple hooks coexist on one target — kcdx installs one detour and fires an
ordered chain of callbacks in load order; C and Lua entries share the same
chain at the same site (the `cap-36` crosslang row proves it). When two hooks
genuinely cannot coexist (incompatible signature, or two exclusive
`Replace`/`Around`), the later in load order loses: its handle goes
`IsApplied == false` with a `GetReason`, the earlier one wins. To put more than
one behavior on a target, make separate install calls (one behavior per call).

---

See also: [wrapper.md](wrapper.md) (the empowered floor on top of this
interface), [../lua/hook.md](../lua/hook.md) (the Lua peer), [memory.md](memory.md)
+ [addr.md](addr.md) (the byte-write and address-resolution pieces a hook builds
on), and [cross-cutting.md](cross-cutting.md) (threading / precision / ABI
append-only discipline).
