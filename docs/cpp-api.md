# kcdx C++ API — author reference

Reference documentation for the kcdx C++ authoring surface (DLL plugins), as
built. Every interface and method here is verified exported by the engine and
declared in `include/kcdx/Interfaces.h` / `include/kcdx/Kcdx.h`; if a call is
not in this document, it does not exist yet (see [Planned](#planned--not-yet-available)).

This is the C++ mirror of [`docs/lua-api.md`](lua-api.md). The two surfaces are
ONE model in two languages at full feature parity (`.claude/rules/lua-api-surface.md`):
the concepts, names, and structure match; only the spelling is idiomatic to each
language. Where a section here is a stub, its Lua counterpart is built and the
C++ mirror is tracked parity debt for the v0.2 restructure backfill — not a
permanent single-surface capability.

This is a reference, not a tutorial. Each entry states the call shape, the
arguments (type + meaning), the return value, the error behaviour, and a
minimal correct snippet.

---

## 1. The model

The C++ surface is the same model as the Lua surface (`docs/lua-api.md` §1),
expressed as engine interfaces a plugin DLL receives at load. The Lua
`{ named table }` for configuration becomes a C++ options-struct
(`kcdxHookOptions`); positional Lua args become typed C++ params; the verb
`kcdx.hook` becomes `kcdxHookInterface::Install` / `K.hook->Install`.

> **Stub — fill during the C++ parity backfill.** Mirror `docs/lua-api.md` §1's
> three rules in their C++ spelling: one entry interface, grouped capability
> interfaces, options-struct vs positional-param call shape.

## 2. Glossary

Shared terms with `docs/lua-api.md` §2 (plugin, manifest, entrypoint, zone,
load-order priority, hook mode, locator, signature, handle, lifecycle event,
dev mode, deferred-apply model) — same definitions, C++ spellings noted where
they differ.

> **Stub.** Add the C++-specific spelling of each shared term as its mirror
> entry lands (e.g. *entrypoint* → `kcdxPlugin_Load` / `kcdxPlugin_PostGameLoad`).

## 3. The plugin DLL shell

The sibling DLL exporting `kcdxPlugin_Load`, declared via `[plugin]` in
`kcdx.toml`, linked against `include/kcdx/Interfaces.h`.

> **Stub.** Document the export contract, the load/post-game-load entry points,
> and the interface-query handshake the loader uses.

## 4. Interfaces

The C++ mirrors of the core verbs and domains. Each interface entry: methods,
arguments (type + meaning), return value, error behaviour, minimal snippet.

> **Stub — mirror `docs/lua-api.md` §4–5 per the binding rule.** Each lands in
> its parity-backfill phase (`.claude/rules/lua-api-surface.md` timing):
> - `kcdxHookInterface` (↔ `kcdx.hook`)
> - byte-rewrite interface (↔ `kcdx.bytes`)
> - event/lifecycle interface (↔ `kcdx.on`)
> - console-command interface (↔ `kcdx.command`)
> - `kcdxLogger` (↔ `kcdx.log.*`)
> - memory / addr / test / cosave interfaces (↔ the matching Lua domains)

## 5. Lifecycle events

The C++ spelling of the lifecycle events in `docs/lua-api.md` §6.

> **Stub.**

## 6. Cross-cutting rules

The C++ spelling of `docs/lua-api.md` §7 (threading, precision, error
conventions).

> **Stub.**

## Planned — not yet available

Mirrors `docs/lua-api.md` §8 from the C++ side. An interface listed here is
designed but not yet exported; do not link against it.
