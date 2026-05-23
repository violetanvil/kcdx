# kcdxHookInterface (↔ kcdx.hook)
> Part of the [kcdx C++ API](index.md).

> **NYI** — mirror of [kcdx.hook](../lua/hook.md); lands in the C++ parity
> backfill. There is **no `kcdxHookInterface` in
> [`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h) today** — the
> deferred, conflict-resolved function-interception surface exists only in Lua
> so far. This entry maps the planned C++ shape at the model level; it does
> **not** present an exact signature, because the header does not define one yet.

Intercept a game function: run your C++ callback when the game calls it, and
optionally change its arguments, return value, or whether it runs at all. The
C++ mirror of the core Lua verb `kcdx.hook{...}`.

## Planned shape (model-level)

Per the one-model-two-languages mapping (`.claude/rules/lua-api-surface.md`),
the C++ spelling of `kcdx.hook` is expected to be:

- **Fetched via `QueryInterface`** as a `kcdxHookInterface`, with a
  `kcdxHookInterface_Version`, like every other capability interface.
- **The verb becomes a method:** `kcdx.hook{...}` (Lua) → `K.hook->Install(...)`
  / `kcdxHookInterface::Install(...)` (C++).
- **The Lua `{ named table }` becomes an options-struct** (working name
  `kcdxHookOptions`): the `name`, locator, `signature`, behaviour, `module`,
  `offset` fields of the Lua call become struct members.
- **The behaviour keys become a mode enum + a typed callback:** the Lua
  `before` / `after` / `around` / `replace` / `mid` keys map to
  `kcdxHookMode_Before` / `_After` / `_Around` / `_Replace` / `_Mid` plus the
  callback in the matching position.
- **Locators map field-for-field:** the common path `target = "<name>"` resolves
  address AND verified signature (the disassembler test — `cornerstones.md`);
  the advanced locators (`address`, `address_id`, `pattern`, `target_symbol`,
  `target_lua_cfunction`) and the `signature` ABI string carry over. Naming
  follows SKSE conventions (`.claude/rules/skse-parity.md`).
- **Returns a handle** carrying the same `:name()` / `:applied()` / `:reason()`
  status the Lua handle exposes, resolved at the end-of-zone apply pass.

The exact struct layout, method signature, mode enum values, and handle type
are **not defined here** — they land with the interface in its backfill phase
(`.claude/rules/lua-api-surface.md` timing). Until then, the full vocabulary
(modes, locators, signature grammar, chaining, callsite scope, mid captures) is
documented on the Lua side.

This is the planned C++ mirror of [kcdx.hook](../lua/hook.md) — read that for
the full as-built behaviour the C++ surface will match. For the built runtime
byte-write and address-resolution pieces a hook builds on, see
[memory.md](memory.md) and [addr.md](addr.md).
