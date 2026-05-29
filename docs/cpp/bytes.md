# kcdxBytesInterface (↔ kcdx.bytes)
> Part of the [kcdx C++ API](index.md).

Rewrite bytes at a located site. The C++ mirror of the core Lua verb
`kcdx.bytes{...}` ([../lua/bytes.md](../lua/bytes.md)). A replacement must be the
same length as the original it overwrites — adding code goes through
[`kcdxHookInterface`](hook.md).

The C++ surface for byte work has **two pieces**, and they coexist by design:

- **`kcdxBytesInterface`** (this page) — the **deferred, locator-based,
  conflict-resolved registration** surface: you name a site and a replacement,
  the engine resolves the address, arbitrates conflicts, and writes at the
  end-of-zone apply pass. This is the parity mirror of Lua's `kcdx.bytes{...}`.
- **`kcdxMemoryInterface::WriteBytes` / `ReadBytes`** ([memory.md](memory.md)) —
  the **immediate raw write/read** at an address you already hold: no locator,
  no conflict arbitration, no deferral. The runtime floor.

Reach for the deferred registration (this interface) for the everyday "patch
this named site" case; reach for `WriteBytes` when you already hold an address
and want the write to land *now*.

This page documents `kcdxBytesInterface` **v1** as built and verified
(`kcdxBytesInterface_Version == 1`,
[`Interfaces.h:1689`](../../include/kcdx/Interfaces.h)). The
`cap-39-cpp-bytes` regression plugin exercises it end-to-end (the C++ peer of
`cap-01`'s Lua `kcdx.bytes` coverage — parity is tested, not assumed).

## Fetching the interface

Like every capability interface, fetch it once via `QueryInterface` and cache
the pointer (its lifetime is the engine's). With the [`Kcdx.h`](wrapper.md)
wrapper it is the pre-fetched `K.bytes` field:

```cpp
auto* bytes = static_cast<const kcdxBytesInterface*>(
    api->QueryInterface(kcdxInterface_Bytes, kcdxBytesInterface_Version));
if (!bytes) { /* engine version mismatch — fail loud, do not skip silently */ }
```

A null return means the running engine does not implement that
interface/version.

## Named-target sub-verb shape — `kcdxBytesInterface::<Name>{...}` (NYI)

The C++ peer of Lua's `kcdx.bytes.<name>{...}` smart-resolver shape
([../lua/bytes.md](../lua/bytes.md)). **Not yet implemented (NYI)** — the
parity mirror lands in a follow-up step that exposes a typed name-keyed
sub-verb (the planned form is a template specialization keyed by the resolved
name, e.g. `kcdxBytesInterface::outfit_swap_callsite_aob{...}` where the
locator is fixed by the name and the options struct carries only the rewrite
payload — no `target` string, no other locator fields). The NYI marker is
removed when this surface ships and the cross-language parity coverage
exercises both sides. Until then, the raw `Register` method below (which
takes a full `kcdxBytesOptions` including a `target` string) is the only C++
install path; it IS at full parity with the flat-table `kcdx.bytes{...}` form
on the Lua side.

## The surface — one `Register` + four query methods

Unlike [`kcdxHookInterface`](hook.md) (six install sub-verbs, one per hook
mode), a byte rewrite is a **single operation**: write `replacement` at a
located site. So there is **one** `Register` method taking an options struct,
plus the same query quartet `kcdxHookInterface` exposes
([`Interfaces.h:1767-1816`](../../include/kcdx/Interfaces.h)):

```cpp
kcdxBytesHandle (*Register) (const kcdxBytesOptions* opts);
bool            (*IsApplied)(kcdxBytesHandle h);
const char*     (*GetReason)(kcdxBytesHandle h);
const char*     (*GetName)  (kcdxBytesHandle h);
bool            (*Uninstall)(kcdxBytesHandle h);
```

This is the one-to-one mirror of the Lua `kcdx.bytes{...}` call returning a
handle with `:applied()` / `:reason()` / `:name()` / `:uninstall()`.

### Return value

`Register` returns a `kcdxBytesHandle`
([`Interfaces.h:1698`](../../include/kcdx/Interfaces.h) — an opaque
`uint64_t`):

- **`0`** = registration **FAILED** at `Register` time (zero or more than one
  locator set, `replacement` missing, a pattern/bytes string that fails to
  parse, `original` length ≠ `replacement` length, or a `target` name that does
  not resolve). The teaching reason is auto-logged at Error level (see
  [Errors](#errors)).
- **A non-zero handle does NOT yet mean applied.** kcdx defers the actual
  `VirtualProtect` + `memcpy` to the end-of-zone apply pass, so the conflict
  engine sees every plugin's intent before any byte is written (the
  **deferred-apply model**). Query `IsApplied(h)` *after* the apply pass to
  confirm. The handle is stable for the process lifetime, never reused, safe to
  copy by value.

### Errors

On a **zero** (failed) handle the engine auto-logs the teaching reason at Error
level to **both** the engine log AND the calling plugin's log — raw-interface
callers get the same loud-on-failure behavior as wrapper users
([`Interfaces.h:1774-1780`](../../include/kcdx/Interfaces.h)). Example reason:

> `bytes 'nop_check': target 'outfit_swap_callsite_aob' did not resolve
> (unknown name, wrong game version, unverified row, or a typo). Check the
> name against kcdx.addr.* or your declared [[target]] rows.`

A failure that only becomes knowable at apply time (the locator resolves but the
site's current bytes don't match `original`, a conflict is lost) does NOT zero
the handle — `Register` returns a real handle, then `IsApplied(h)` reads false
and `GetReason(h)` carries the apply-time reason.

## Locators

The byte rewrite needs to find its site. The **common path is by name** — the
`target` field:

- **`target = "<name>"`** — a named site. The name resolves the **address** (a
  byte rewrite is untyped, so unlike [`kcdxHookInterface`](hook.md) no signature
  is involved). The engine resolves via
  `address_library::ResolveByName(target, owningPlugin)` with
  **self > engine > other** precedence (a bare name resolves to your own
  declaration first, then an engine name, then another plugin's): an engine
  [Address Library](addr.md) name, one of your own
  [author-declared targets](targets.md) (bare — the engine stamps
  `<author>.<plugin>.<bare>`), another plugin's by the explicit
  `"<author>.<plugin>.<bare>"` form, or the engine-seed form
  `"kcdx.<seedname>"`. You write a name and never hand-write hex — the engine
  does the heavy lifting of resolving the address from the name.

### Advanced locators (expert-only escape hatch)

The fields below in `kcdxBytesOptions` are the `[advanced]` escape hatch for
sites the library cannot yet name. Leave `target` null AND set **exactly one**.
Identify an un-named site **once** via an advanced locator, name it, and refer
to it by name thereafter — hex authored once, not per call, and shareable by
name with other authors who never touch the hex.

- **`pattern`** — a byte/wildcard AOB scanned in `module`.
- **`addressId`** — a numeric Address Library ID.
- **`targetSymbol`** — a cross-plugin published-symbol lookup.

`context` and `anchorString` refine a `pattern` locator.

> **Note — no raw `address` locator.** Unlike `kcdxHookOptions.address`,
> `kcdxBytesOptions` has **no** raw-VA locator: this interface always locates a
> SITE by name/pattern/symbol (mirroring Lua `kcdx.bytes`). If you already hold
> a bare address and want to write *now*, that is
> [`kcdxMemoryInterface::WriteBytes`](memory.md), not this interface.

## Options (`kcdxBytesOptions`)

POD, C-ABI struct ([`Interfaces.h:1711-1765`](../../include/kcdx/Interfaces.h)).
Default-zero every field (`kcdxBytesOptions opts = {};`), then set only the
fields you use. Sentinel for unset: `null` for strings, `0` for numerics.

| Field | Type | Meaning |
|---|---|---|
| `name` | `const char*` | Optional log/conflict label; null = engine default `"cpp_bytes"`. |
| `description` | `const char*` | Optional free text; may be null. |
| `target` | `const char*` | **The common path.** A *name* the engine resolves to an address. |
| `pattern` | `const char*` | `[advanced]` AOB hex at the rewrite site; null = unset. |
| `addressId` | `uint64_t` | `[advanced]` Address-Library numeric ID; 0 = unset. |
| `targetSymbol` | `const char*` | `[advanced]` cross-plugin symbol-table lookup; null = unset. |
| `replacement` | `const char*` | **Required.** Bytes to write (`"45 31 F6"`); null/empty = rejected. |
| `original` | `const char*` | Optional verify bytes; when set, must equal the `replacement` byte length, and the apply pass refuses to write if the site does not currently match. |
| `module` | `const char*` | Default `"WHGame.dll"` (engine substitutes if null). |
| `offset` | `int` | Default 0 — applied after locator resolution. |
| `idempotent` | `bool` | Default true — skip re-apply if the bytes already match `replacement`. |
| `context` | `const char*` | `[advanced]` AOB disambiguation for `pattern`; null = none. |
| `anchorString` | `const char*` | `[advanced]` string anchor for `pattern`; null = none. |
| `owningPlugin` | `kcdxPluginHandle` | **Required.** Your plugin handle (from `api->GetPluginHandle("<name>")`). Drives the self-tier of self > engine > other for a bare-name `target`. Pass `kcdxInvalidPluginHandle` (or 0) for the anonymous path. The wrapper threads this for you; **raw-interface callers set it themselves**. |

Exactly **one** locator (`target` / `pattern` / `addressId` / `targetSymbol`)
must be set; `replacement` is always required. Setting zero locators, or more
than one, is a `Register`-time rejection with a teaching error.

## Query / control methods

Operate on a handle returned by `Register`
([`Interfaces.h:1791-1810`](../../include/kcdx/Interfaces.h)):

- **`IsApplied(h)`** — true iff the apply pass has written the rewrite. False for
  an unknown, still-pending, or failed-to-apply handle. Mirrors Lua
  `h:applied()`.
- **`GetReason(h)`** — the teaching failure string for a rewrite that registered
  but did not apply (locator miss, byte-mismatch, wrong game version); null when
  `h` is valid AND applied. Engine-owned, process-lifetime. Mirrors Lua
  `h:reason()`.
- **`GetName(h)`** — the author-supplied (or engine-default `"cpp_bytes"`) name;
  null for an unknown handle. Engine-owned. Mirrors Lua `h:name()`.
- **`Uninstall(h)` — no revert; returns `false`.** A byte rewrite has **no
  revert path**: the original bytes are not retained for restore, so a rewrite
  is permanent for the session. `Uninstall` is declared for signature parity
  with `kcdxHookInterface` but **returns `false` and logs a teaching line**
  explaining bytes cannot be reverted (use a [hook](hook.md) for reversible
  interception). It does **not** flip `IsApplied` — the rewrite stays live in
  memory. (Silently flipping status while patched bytes remain live would be a
  lie: safe is not the same as fixed.) Mirrors Lua `h:uninstall()` on a
  `kcdx.bytes` handle,
  which raises the equivalent teaching error.

## Minimal snippet

A copy-paste-runnable deferred rewrite by **named target** (the everyday path),
using the [`Kcdx.h`](wrapper.md) wrapper's pre-fetched `K.bytes` + `K.memory`:

```cpp
#include "kcdx/Interfaces.h"
#include "kcdx/Kcdx.h"

static Kcdx K;

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    if (!K.Init(api, "my_author", "my_plugin")) return true;  // logs why
    if (!K.bytes) return true;  // engine version mismatch — fail loud upstream

    kcdxBytesOptions opts = {};
    opts.owningPlugin = K.self;                    // self-tier resolution
    opts.name         = "nop_check";
    opts.target       = "outfit_swap_callsite_aob";  // name resolves the address
    opts.original     = "44 8A F0";                // optional verify (same length)
    opts.replacement  = "45 31 F6";                // same-length rewrite

    kcdxBytesHandle h = K.bytes->Register(&opts);
    if (h == 0) return true;  // engine already logged the teaching reason

    // Apply is deferred; query IsApplied(h) after the apply pass (e.g. in
    // kcdxPlugin_PostGameLoad or a kcdxMessage_InputLoaded listener). To
    // confirm the write landed, read the live bytes back with K.memory:
    //   unsigned char live[3];
    //   if (K.bytes->IsApplied(h) && K.memory)
    //       K.memory->ReadBytes(site, live, sizeof(live));  // == 45 31 F6
    return true;
}
```

For an **immediate** raw write at an address you already hold (no locator, no
deferral), use [`kcdxMemoryInterface::WriteBytes`](memory.md) instead:

```cpp
uintptr_t site = K.memory->ScanPattern("WHGame.dll", "44 8A F0");
const unsigned char repl[3] = { 0x45, 0x31, 0xF6 };
if (site) K.memory->WriteBytes(site, repl, sizeof(repl));  // same-length, now
```

---

See also: [../lua/bytes.md](../lua/bytes.md) (the Lua peer), [memory.md](memory.md)
(the immediate raw write/read floor + `ScanPattern`), [addr.md](addr.md) (name →
address resolution), [hook.md](hook.md) (reversible, code-adding interception),
and [cross-cutting.md](cross-cutting.md) (the deferred-apply model / ABI
append-only discipline).
