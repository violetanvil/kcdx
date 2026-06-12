# kcdxBehaviorInterface (↔ kcdx.behavior.*)
> Part of the [kcdx C++ API](index.md).

The C++ mirror of Lua's [`kcdx.behavior.*`](../lua/behavior.md) — named
behaviors: a behavior is a named, settable unit of intent (a value plus the
declarer's implementation that reconfigures the game to match it, under the
engine's apply contract). Two tiers — engine-catalog `kcdx.behavior.<bare>`
names and plugin-declared `<author>.<plugin>.<bare>` names — register through
ONE runtime registry, shared by both languages: a behavior declared in C++ is
settable and listable from Lua and vice versa.

Fetch it via `QueryInterface(kcdxInterface_Behavior,
kcdxBehaviorInterface_Version)` (or `K.behavior` through the
[`Kcdx.h`](../../include/kcdx/Kcdx.h) wrapper). The author writes **names** and
**typed values**; the engine carries everything else — no hex, no ABI, no raw
VM pointer.

## The four verbs

| Method | Mirrors | What |
|---|---|---|
| `Declare(name, desc, default, impl, revert /*nullable*/, userCtx, owningPlugin)` | `kcdx.behavior.declare` | declare a behavior you own (the engine stamps `<author>.<plugin>.<bare>` from your manifest — you write the BARE name) |
| `Set(name, value, owningPlugin)` | `kcdx.behavior.set` | set a behavior's value (records at load; toggles a `revert` declarer post-load) |
| `Get(name, &outValue, owningPlugin)` | `kcdx.behavior.get` | read a behavior's current value as a value handle |
| `List(prefix, callback, userCtx)` | `kcdx.behavior.list` | enumerate behaviors (both tiers, one registry) |

`Declare`/`Set`/`Get` return `bool` (false + a teaching error on failure, read
via `GetLastError()` — also logged); `List` returns the entry count. `owningPlugin`
is your own handle (`api->GetPluginHandle("your.name")`), which drives the
self > engine > other resolution of a bare name.

## The value-handle model — values live in the one VM, never marshalled out

A behavior's value can be ANY Lua type (bool, number, string, table, function);
it lives in the engine-owned Lua VM. `Get` hands back an **opaque value handle**
(`kcdxBehaviorValue`) — never a copy of the value, never a raw VM pointer. Read
it through the accessors:

- **`TypeOf(value)`** → `kcdxBehaviorType` (Bool/Number/String/Table/Function);
  branch on it before coercing.
- **Coercion accessors** for scalars — `AsBool` / `AsInt64` / `AsDouble` /
  `AsString` — one call for the common case. Each returns `kcdxBehaviorAccess`
  (`Ok` writes the out-param; a non-Ok result leaves it UNTOUCHED and
  `GetLastError()` carries the teaching text).
- **Table traversal** — `Length(table, &len)`, `Index(table, i, &child)`
  (1-based), `Field(table, key, &child)`. A child is itself a value handle (read
  with the same accessors; tables nest).

**Coercion mismatch fails loud.** `AsInt64` on a table value returns
`kcdxBehaviorAccess_TypeError` and `GetLastError()` names the actual type — never
a silently-wrong value.

**Staleness is generation-checked.** A handle is valid only while its value is
the behavior's current recorded value. When the recorded value is replaced (a
set/toggle on that behavior, from EITHER language), an outstanding handle goes
stale — every accessor on it returns `kcdxBehaviorAccess_Stale` (a teaching
error), never a dangle into the replaced ref. Re-`Get()` for a fresh handle.

## Building values — typed builders

Construct a value on the VM with a builder, then pass the handle to
`Set`/`Declare`. Each builder returns a `kcdxBehaviorValue` (0 + a teaching error
if the VM is unavailable):

- **`NewBool(v)` / `NewInt64(v)` / `NewDouble(v)` / `NewString(s, len)`** —
  scalars (`len == 0` ⇒ `strlen`).
- **`NewTable()`** then **`SetIndex(t, i, child)` / `SetField(t, key, child)`** —
  build a table by writing child value handles into it (the child is consumed —
  its value moves into the table).
- **`NewCallable(fn, ctx)`** — a C function pointer + context registers AS a
  callable value (full parity — no value type is Lua-only). Call it with `Invoke`
  (below).

A built handle is consumed when passed to `Set`/`Declare` or a table builder; nil
is the unset sentinel — never a value.

## Calling a callable value — `Invoke`

A value whose type is `kcdxBehaviorType_Function` — built by `NewCallable` from a C
function pointer, OR a Lua-declared function value reached through `Get`/an accessor
— is **called** with `Invoke`:

```cpp
kcdxBehaviorAccess (*Invoke)(kcdxBehaviorValue callable,
                             const kcdxBehaviorValue* argv, size_t argc,
                             kcdxBehaviorValue* outResult);
```

Args are **value handles** — the SAME concept as construction (one value model for
building AND calling, no second arg regime). Build each arg with a builder, pass an
`argv` array + `argc`. The pcall's **first return value** is pinned into a fresh
value handle written to `*outResult` (read it with the accessors); a callable that
returns nothing sets `*outResult` to `0` (a valid no-result handle) and returns
`kcdxBehaviorAccess_Ok`.

```cpp
// A Lua-declared callable: function(a, b) return a + b end, reached via Get.
kcdxBehaviorValue fn = 0;
g_beh->Get("redmoon.realism.adder", &fn, g_self);
kcdxBehaviorValue argv[2] = { g_beh->NewInt64(2), g_beh->NewInt64(3) };
kcdxBehaviorValue result = 0;
if (g_beh->Invoke(fn, argv, 2, &result) == kcdxBehaviorAccess_Ok) {
    int64_t sum = 0;
    g_beh->AsInt64(result, &sum);   // 5
}
```

`Invoke` is a **query** — it needs the live VM, so it honors the same thread-wall as
every accessor (legal during the load waves under the wave-end gate; post-load on
the game main thread only — an off-thread post-load `Invoke` returns
`kcdxBehaviorAccess_Thread`). `callable` must resolve to a function value (else
`kcdxBehaviorAccess_TypeError` naming the actual type); each `argv[i]` must be a
valid non-consumed handle (else `kcdxBehaviorAccess_BadHandle`); `outResult` must be
non-null. A pcall **raise** is `kcdxBehaviorAccess_TypeError` with the Lua error
text via `GetLastError` — the call fails loud, never a silently-wrong result.

**Single-surface (C++): the Lua author calls a function value natively** (`local f
= kcdx.behavior.get("adder"); f(2, 3)`), so Lua needs no `Invoke` verb. `Invoke` is
the C++ expression of the same capability — calling a Lua function value from C++,
where the language cannot do it directly. Both surfaces can call a callable value;
only the spelling differs.

## Copy-paste-runnable snippet

```cpp
#include "kcdx/Interfaces.h"

static const kcdxBehaviorInterface* g_beh = nullptr;
static kcdxPluginHandle g_self = kcdxInvalidPluginHandle;

// The C++ implementation: invoked once at the apply boundary with the final
// settled value (and at each post-load toggle). Read the value via the handle.
static void HardcoreCombatImpl(kcdxBehaviorValue value, void* /*ctx*/) {
    bool on = false;
    if (g_beh->AsBool(value, &on) == kcdxBehaviorAccess_Ok && on) {
        // ... reconfigure the game to match `on` ...
    }
}

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    g_self = api->GetPluginHandle("redmoon.realism");  // your [plugin] name
    g_beh  = static_cast<const kcdxBehaviorInterface*>(
        api->QueryInterface(kcdxInterface_Behavior, kcdxBehaviorInterface_Version));
    if (!g_beh) return true;  // engine too old — every behavior call is a no-op

    // Declare a behavior you own. The engine stamps
    // redmoon.realism.hardcore_combat; you write the bare name.
    kcdxBehaviorValue def = g_beh->NewBool(false);       // default
    if (!g_beh->Declare("hardcore_combat",
                        "lock fast-travel and timed saves while in combat",
                        def, HardcoreCombatImpl, /*revert=*/nullptr,
                        /*userCtx=*/nullptr, g_self)) {
        api->Log(g_self, kcdxLog_Error, "REALISM", g_beh->GetLastError());
    }
    return true;
}

// Set / Get a PLUGIN behavior from your MAIN stop (kcdxPlugin_PostGameLoad),
// not the early kcdxPlugin_Load (the window law, below).
extern "C" __declspec(dllexport)
bool kcdxPlugin_PostGameLoad(const kcdxInterface* api) {
    kcdxBehaviorValue on = g_beh->NewBool(true);
    g_beh->Set("hardcore_combat", on, g_self);           // record / toggle

    kcdxBehaviorValue h = 0;
    if (g_beh->Get("hardcore_combat", &h, g_self)) {
        bool cur = false;
        g_beh->AsBool(h, &cur);                          // read the current value
    }
    return true;
}
```

## The thread contract — queries are main-thread; commands queue

One law, two halves:

- **Queries** (`Get` + every value-handle accessor) need the live VM: legal
  during the load waves under the **VM-adoption wave-end gate** (the loader holds
  the engine off the VM until the C++ wave finishes, so a `kcdxPlugin_Load` query
  reaches the live VM), and post-load **on the game main thread only**. An
  off-thread post-load query returns a teaching error naming the two sanctioned
  patterns: *capture the value in your implementation at apply, or copy it out on
  the main thread*. No query hides a blocking marshal. (Regression-tested: a
  worker thread driving a post-load `Get` is rejected with the thread error — the
  wall returns before any VM access, so the off-thread call is crash-safe.)
- **Commands** (`Set`) — work from **any thread**. A load-window `Set` records; a
  post-load main-thread `Set` toggles a `revert` declarer inline; a post-load `Set`
  from a **non-main thread QUEUES** and executes its toggle on the game main thread
  at the next apply point (FIFO arrival order, each set its own toggle, no
  coalescing — riding the engine's existing off-thread→main task pump, no new
  dispatch path). The off-thread `Set` returns having **queued** (`true`) — it never
  carries the toggle's eventual outcome; a queued toggle's failure logs **async** (a
  consumer-misuse failure — a revert-less post-load set, an unresolvable name —
  attributed to the **setting** plugin; a declarer-code raise attributed to the
  **declarer**). `Get` flips only when the queued toggle actually executes (an
  off-thread setter may briefly read the prior value — the same
  applies-at-the-next-apply-point semantics a load-time set has).

  **Off-thread value construction stages.** A value built off-thread (the builders,
  called from a non-main thread post-load) stages engine-side as the queued
  command's plain-data payload — scalars/strings/function-pointers directly, a table
  as a description materialized on the main thread at execution. The SAME builders,
  not a second access regime; pass the built handle to the off-thread `Set` as you
  would on the main thread.

## The window law — a plugin-tier `Set` from `kcdxPlugin_Load` is out-of-window

`kcdxPlugin_Load` is an EARLY stop (the worker wave, before any plugin's main
entry runs), so the declarer's plugin-tier behaviors do not exist yet. A `Set`
on a plugin-tier `<author>.<plugin>.<bare>` name from `kcdxPlugin_Load` fails
loud with the same teaching error the Lua surface raises ("plugin behaviors
resolve at the main stop; set from your main entry") — set plugin behaviors from
**`kcdxPlugin_PostGameLoad`** (the C++ main stop, the mirror of Lua's
`lua_after`). Engine-catalog `kcdx.behavior.*` names are settable from any stop.
The discriminating resolution errors (reorder / failed-load / disabled /
rejected / absent / typo / bare-name) are the same as the Lua surface — the wall
is keyed on the early-vs-main stop, not the language.

This is the C++ mirror of [kcdx.behavior.*](../lua/behavior.md).
