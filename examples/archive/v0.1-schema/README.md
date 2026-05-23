# Archived examples — v0.1 pre-restructure schema (do NOT copy these)

**Archived 2026-05-22.** These examples teach the **old v0.1 TOML-behavior
schema** — `[[patch]]` / `[[hook]]` / `[[mid_hook]]` / `[[trampoline]]` /
`[[command]]` / `[[event]]` declared in `kcdx.toml`, plus the early C++ DLL
shape. The v0.2 restructure **replaced** that model: `kcdx.toml` is now
**manifest-only** (identity / entrypoints / load-order — no behavior), and
behavior is written as **code** through the `kcdx.*` surface in a `plugin.lua`
or a C++ DLL.

**These are kept for history only — do not copy them as a starting point.**
A plugin built from these will not match the current engine.

## Where to look instead

- **The API reference:** [`docs/lua-api.md`](../../../docs/lua-api.md) — every
  `kcdx.*` accessor you can call today, with its arguments, return, errors,
  and a minimal snippet.
- **How to write a plugin:** [`README.md`](../../../README.md) — the
  manifest-only `kcdx.toml` + `plugin.lua` model.
- **What passes live right now:** [`test-plugins/README.md`](../../../test-plugins/README.md)
  — the regression suite exercises every shipped capability with a real
  working plugin (these are the de-facto "correct example" set until the
  `examples/` refresh lands).

## Refreshed examples are coming

A curated set of correct `plugin.lua` + `kcdx.*` examples (a hello-plugin, an
outfit-swap hook, a both-phase plugin, a cross-plugin pub/sub pair, a C++ DLL
with `kcdxPlugin_Load` + `kcdxPlugin_PostGameLoad`) is tracked as a follow-up
in [`docs/outstanding-work/docs-deep-rewrites.md`](../../../docs/outstanding-work/docs-deep-rewrites.md).
Until then, the `test-plugins/` are the accurate working references.
