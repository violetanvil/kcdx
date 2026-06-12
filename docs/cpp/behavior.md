# kcdxBehaviorInterface (↔ kcdx.behavior.*)
> Part of the [kcdx C++ API](index.md).

The C++ mirror of Lua's [`kcdx.behavior.*`](../lua/behavior.md) — named
behaviors: a behavior is a named, settable unit of intent (a value plus the
declarer's implementation that reconfigures the game to match it, under the
engine's apply contract). Two tiers — engine-catalog `kcdx.behavior.<bare>`
names and plugin-declared `<author>.<plugin>.<bare>` names — register through
ONE runtime registry, shared by both languages: a behavior declared in C++ is
settable and listable from Lua and vice versa.

**Not yet implemented (NYI).** There is no behavior interface in
[`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h) today — do not
link against it. `kcdxBehaviorInterface` is the **planned** mirror name; it is
tracked parity debt — both docs map a capability even when only one is built —
discharged when the C++ parity phase ships it and it is verified callable.
This entry maps the planned shape so both surfaces describe the capability
while the engine catches up. (On the Lua side, `declare`/`set`/`get`/`list`,
the apply boundary, and the post-load `revert` toggle (main-thread inline) are
built today; the off-thread command queue lands with this interface — see
[`kcdx.behavior`](../lua/behavior.md).)

## Planned mirror shape (NYI)

Following the C++ surface model (configuring → options-struct/typed params,
fetched via `QueryInterface`), the planned mirror carries the four verbs plus
an engine-owned **value handle** model:

```cpp
// PLANNED — NOT in Interfaces.h yet. Sketch of the mirror shape.
//
// Declare: a C++ implementation/revert is a C function pointer + context; the
// engine invokes it at the same apply-boundary / toggle points as a Lua one,
// handing it the behavior's current value handle.
// kcdxBehaviorInterface::Declare(name, desc, defaultValue,
//                                implFn, revertFn /*nullable*/, userCtx)
// kcdxBehaviorInterface::Set(name, value)
// kcdxBehaviorInterface::Get(name, &outValueHandle)
// kcdxBehaviorInterface::List(prefix, perEntryCallback)
```

- **Value model — an engine-owned handle, values never marshalled out.** A
  behavior's value can be ANY Lua type (bool, number, string, table,
  function); C++ receives an opaque value handle valid while that value is the
  behavior's recorded value. Coercion accessors cover the everyday scalars
  (`AsBool` / `AsInt64` / `AsDouble` / `AsString`); table-traversal accessors
  cover table values; `Invoke(handle, …)` covers callable values. A stale
  handle (the recorded value was replaced) returns a generation-checked
  teaching error through the interface's error channel — it never dangles.
  Value CONSTRUCTION for `Set`/`Declare` uses typed builders for
  scalars/strings/tables; a C function pointer + context registers as a
  callable value. No value type is Lua-only — the handle model exists
  precisely for full parity.
- **Thread contract — commands queue, queries are main-thread.** `Set` (a
  command) works from any thread — an off-thread post-load set queues and
  executes on the game main thread at the next apply point. `Get` and every
  handle accessor (queries) need the live VM: legal during the load waves and
  post-load on the game main thread only; an off-thread post-load query gets a
  teaching error naming the two sanctioned patterns (capture the value in your
  implementation at apply, or copy it out on the main thread).
- **The window law — a plugin-tier `Set` from `kcdxPlugin_Load` is
  out-of-window.** `kcdxPlugin_Load` is an EARLY stop (the worker wave, before
  any plugin's main entry runs), so the declarer's plugin-tier behaviors do not
  exist yet. A `Set` on a plugin-tier `<author>.<plugin>.<bare>` name from
  `kcdxPlugin_Load` fails loud with the same teaching error the Lua surface
  raises ("plugin behaviors resolve at the main stop; set from your main
  entry") — set plugin behaviors from `kcdxPlugin_PostGameLoad` (the C++ main
  stop), the mirror of Lua's `lua_after`. Engine-catalog `kcdx.behavior.*`
  names are settable from any stop. The Lua surface's discriminating resolution
  errors (reorder / failed-load / disabled / rejected / absent / typo /
  bare-name) mirror identically once this interface ships — the wall is
  language-symmetric (it is keyed on the early-vs-main stop, not the language).

## Today (the built fallback)

Until `kcdxBehaviorInterface` lands, the behavior surface is reached from
**Lua** ([`kcdx.behavior`](../lua/behavior.md) — `declare`/`set`/`get`/`list`
+ the apply boundary today). A C++ plugin that needs a behavior before the
mirror ships can pair with a small `plugin.lua` in the same plugin folder,
the standard two-language-plugin shape.

This is the C++ mirror of [kcdx.behavior.*](../lua/behavior.md).
