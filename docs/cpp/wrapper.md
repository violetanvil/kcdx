# Kcdx.h — the empowered C++ wrapper

> Part of the [kcdx C++ API](index.md).

> **Single-surface: C++ template sugar over `kcdxHookInterface`.** Lua's
> `kcdx.hook.before/after/around/replace` ([../lua/hook.md](../lua/hook.md)) is
> the native peer — Lua's dynamic marshaling means the Lua author never sees a
> mangled callback ABI, so there is nothing for a kcdx *interface* to hide on
> the Lua side. `Kcdx.h` is header-only C++ sugar that hides the per-mode
> mangled cFn ABI from the C++ author the same way; it owes no Lua mirror (no
> capability gap to backfill — this is **not** NYI debt). The capability —
> installing typed hooks — is at full parity through the raw
> `kcdxHookInterface` ([hook.md](hook.md)); `Kcdx.h` is an ergonomics layer on
> top of it.

`Kcdx.h` is the **empowered floor** over [`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h).
It does two things:

1. **`struct Kcdx`** — one `Init()` fetches every shipped sub-interface and
   stashes your plugin's identity (handle + author/plugin names), so the hook
   helpers thread `owningPlugin` for you.
2. **`namespace kcdx::hook`** — typed templated install helpers. You write a
   **natural callback** typed in the original target's signature; the header
   emits the per-mode adapter that unpacks the engine's JIT-thunk ABI, calls
   your callable, and writes back. You never hand-write `uintptr_t args[],
   int* outCount` or the per-mode mangled `cFn` shape.

Including the header changes nothing about the ABI — `Kcdx.h` is pure
consumer-side sugar (AP11: it never modifies `Interfaces.h`). Everything it
does is reachable through the raw interface without it.

---

## The 3-floor model

| Floor | Call | When |
|---|---|---|
| **1 — empowered** | `kcdx::hook::Before<Sig, &fn>(K, target)` | the everyday path: typed natural callback, auto-threaded `owningPlugin`, auto-log-on-failure |
| **2 — `Try*`** | `kcdx::hook::TryBefore<Sig, &fn>(K, target)` | same codegen, returns the `kcdxHookHandle` (for `IsApplied`, branch-on-failure) |
| **4 — raw** | `K.hook->Before(target, (void*)&cFn, &opts)` | the unchecked raw `kcdxHookInterface`. `K.hook` **is** the drop-down — you write the mangled `cFn` and thread `opts.owningPlugin` yourself |

There is **no Floor 3** "`InstallRawUnchecked`": `K.hook` is the unchecked floor
by construction (a `void*` callback is unchecked already).

**Mid and Callsite have no empowered helper.** They are expert sub-verbs
(register captures / call-instruction sub-locators — disassembler-tier inputs,
`cornerstones.md` / AP12). Drop to Floor 4 for them: `K.hook->Mid(target,
(void*)&cFn, &opts)` / `K.hook->Callsite(...)`. The mangled `cFn` ABI for those
is documented on [hook.md](hook.md).

---

## `struct Kcdx`

```cpp
struct Kcdx {
    const kcdxInterface*           api;            // root — floor-4 for everything below
    kcdxPluginHandle               self;           // your handle (threaded into owningPlugin)
    const char*                    author;         // stashed [plugin].author
    const char*                    plugin;         // stashed [plugin].name
    const kcdxHookInterface*       hook;           // floor-4 hook drop-down
    const kcdxMemoryInterface*     memory;
    const kcdxConsoleInterface*    console;
    const kcdxTrampolineInterface* code;           // kcdx.code peer
    kcdxMessagingInterface*        messaging;
    kcdxTaskInterface*             task;
    kcdxScriptingInterface*        scripting;
    kcdxSerializationInterface*    serialization;
    kcdxLogger                     log;            // stamped with self

    bool Init(const kcdxInterface* api, const char* author, const char* plugin);
};
```

**`Init(api, author, plugin)`** — `author` / `plugin` are the `[plugin].author`
/ `[plugin].name` from your `kcdx.toml`. It:

- fetches every shipped sub-interface via `api->QueryInterface(id, version)`
  (a null field means the running engine doesn't implement that
  interface/version — same as a raw miss);
- resolves `self = api->GetPluginHandle(plugin)` (the bare `[plugin].name`, as
  the loader's resolver expects);
- builds `log` from `self`.

**Returns** `false` (after logging the reason to your plugin log) only when a
**required** interface is missing — at minimum `Hook`, the wrapper's reason to
exist. The optional sub-interfaces are best-effort.

### `addr` and `test` live on `K.api`, not on a field

There is no `kcdxAddrInterface` / `kcdxTestInterface`. The Address Library and
test reporting are methods on the **root** `kcdxInterface`. Reach them through
`K.api`:

```cpp
uintptr_t a = K.api->ResolveAddress(/*id*/ 1234);
uintptr_t b = K.api->ResolveAddressByNameAs(K.self, "IsInCombat");  // self-tier resolution
K.api->ReportTestResult(K.self, "MY-ROW", /*pass=*/1, "ok");
```

`ResolveAddressByNameAs(K.self, ...)` threads your handle so a bare name
resolves self > engine > other (`naming-namespaces.md`); the bare
`ResolveAddressByName` is the anonymous engine-seed-only path.

---

## `namespace kcdx::hook` — the empowered helpers

Eight helpers: `Before` / `After` / `Around` / `Replace` (void-returning,
auto-log on failure) and `TryBefore` / `TryAfter` / `TryAround` / `TryReplace`
(return the `kcdxHookHandle`).

### Call shape

```cpp
template<class Sig, /* fn ptr */ Fn>
void           Before   (const Kcdx& K, const char* target,
                         const kcdxHookOptions* opts = nullptr);
template<class Sig, /* fn ptr */ Fn>
kcdxHookHandle TryBefore (const Kcdx& K, const char* target,
                          const kcdxHookOptions* opts = nullptr);
// …After / Around / Replace identically.
```

- **`Sig`** — the **original target's** signature, written as a function type:
  `int(int seed)`, `void(float, float)`, `bool()`. This drives the generated
  adapter's ABI and (on the no-name path) the derived signature string.
- **`Fn`** — your callback, passed as the **second template parameter** (a
  function pointer), *not* a runtime argument. This is what lets the generated
  adapter be a non-capturing static whose **address** is the `void*` the engine
  receives (the SKSE trampoline-generation idiom). Pass a free function or a
  named captureless lambda decayed to a fn ptr.
- **`target`** — the COMMON path: an Address-Library name, an explicit
  cross-plugin `"<author>.<plugin>.<bare>"`, or the engine-seed
  `"kcdx.<seedname>"`. The name resolves to address **and** verified signature
  (the disassembler test) — you do not re-type the ABI. Pass `nullptr`/`""`
  only when using an `[advanced]` locator in `opts`.
- **`opts`** — optional. `owningPlugin` is **always** overwritten with `K.self`
  (you never set it). For the no-name path (a raw `opts.address` / `opts.pattern`
  with no published ABI), the wrapper also fills `opts.signature` with the
  string derived from `Sig` — so the unnamed-locator gate is satisfied without
  you hand-writing `"i32 (i32 seed)"`. A named target leaves `opts.signature`
  null and the engine substitutes its verified ABI. An `opts.signature` you set
  yourself is never overwritten.

### The natural callback shapes (`Sig = R(Args...)`)

| Helper | Author callable | Notes |
|---|---|---|
| **Before** | `void(Args&...)` | mutate args **by reference** in place; the adapter writes the mutated slots back and sets `outCount` for you |
| **After** (non-void `R`) | `R(R origReturn, Args...)` | receive the original return, return the new one; args observe-only (by value) |
| **After** (void `R`) | `void(Args...)` | observe-only |
| **Around** | `R(R(*call_original)(Args...), Args...)` | `call_original` is a plain typed fn ptr; call it, return its (or any) `R` |
| **Replace** | `R(Args...)` | original never runs; return the replacement |

**Constraint — callables must be NON-CAPTURING.** A free function or a
captureless lambda (both convert to a plain function pointer). This is the same
constraint the raw `kcdxHookInterface` states ("capturing lambdas are NOT
directly callable") — not a new limitation. To carry state, use a free function
that reads/writes your own statics.

### Returns / errors

`Before`/`After`/`Around`/`Replace` return `void` and, on a **zero** (failed)
handle, log an Error to your plugin log naming the helper + target; the engine
also logs the teaching reason to both the engine and plugin logs. The `Try*`
forms return the handle (0 = registration failed) for programmatic branching —
keep it to call `K.hook->IsApplied(h)` after the apply pass, `GetReason(h)`,
`GetName(h)`, or `Uninstall(h)`.

A non-zero handle does **not** yet mean applied — kcdx defers apply to a later
pass. Query `K.hook->IsApplied(h)` after the apply pass.

---

## Minimal snippet (named target — the common path)

```cpp
#include "kcdx/Kcdx.h"

static Kcdx K;

// Natural callback, by-reference arg mutation.
void on_is_in_combat(int& flag) { flag = 1; }   // force "in combat"

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    if (!K.Init(api, "redmoon", "outfit")) return true;   // logs why; Hook missing
    kcdx::hook::Before<int(int), &on_is_in_combat>(K, "IsInCombat");
    return true;
}
```

The name `"IsInCombat"` carries both address and verified ABI; `opts` is
omitted entirely. `owningPlugin` is threaded from `K.self`; `signature` stays
null (the engine substitutes the verified one).

## Snippet (advanced raw-address locator — the no-name path)

When the target has no published name (e.g. a function inside your own DLL),
supply the `[advanced]` `address` locator. The wrapper derives the signature
from `Sig` and threads it for you:

```cpp
int my_return_42(int /*seed*/) { return 42; }   // Replace: original never runs

void install(Kcdx& K, uintptr_t target_va) {
    kcdxHookOptions opts = {};
    opts.address = target_va;     // [advanced] raw VA — no name to carry the ABI
    opts.name    = "force_42";
    kcdxHookHandle h =
        kcdx::hook::TryReplace<int(int), &my_return_42>(K, /*target=*/nullptr, &opts);
    if (h && K.hook->IsApplied(h)) { /* … */ }
}
```

This is the path the `cap-37-kcdx-wrapper` regression plugin exercises for its
Before/After/Around/Replace rows + the Try\* handle row + the type→DSL-trait
row (its targets are DLL-internal stubs, so it uses the raw-address locator +
the wrapper-derived signature). `cap-36-cpp-hook-interface` is the peer
regression net for the **raw** `kcdxHookInterface` floor underneath
([hook.md](hook.md)) — it does not use this wrapper.

---

This is C++ template sugar over [kcdxHookInterface](hook.md); see that page for
the raw Floor-4 surface (including the Mid / Callsite sub-verbs the wrapper does
not wrap) and [kcdx.hook](../lua/hook.md) for the Lua peer.
