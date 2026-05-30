# Phase 9.6 step 3 — docs: tiered front door + `extensibility.md`

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 3.

## What

Two doc deliverables that make the final surface navigable: the tiered author-model
front door in `docs/lua/index.md`, and the cross-plugin extensibility guide
`docs/lua/extensibility.md`.

## Scope

- **`docs/lua/index.md` leads with the four-way tier model** — in the first three
  sentences the author sees which surface to reach for: `kcdx.behavior.set` (named
  behavior exists; one line), `kcdx.hook.before/after/around/replace` (per-call Lua
  logic at a code site), `kcdx.statement.replace_with` (static change, zero per-call
  cost), `kcdx.bytes` (raw bytes outside functions + the labeled `pattern` hatch).
  Plus the discovery pointer: `kcdx.behavior.list()`, `kcdx.find{...}`,
  `kcdx_dev_inspect`.
- **`docs/lua/extensibility.md`** — the highest-leverage friction reduction
  (cultural, not engine). Two directions, both leading with the disassembly-free
  paths:
  - *Make your plugin extensible (author A):* publish events (`kcdx.publish` — the
    recommended primary surface), declare behaviors (`kcdx.behavior.declare`), write
    extensible Lua, declare your DLL's functions (`kcdx.dll.declare`), ship your
    `.pdb`.
  - *Extend another plugin (author B):* subscribe to events, reconfigure behaviors,
    wrap Lua functions, hook declared C++ functions by name. The one boundary case
    — A's stripped, undeclared, compiled internal — named as the rare expert
    fallback (RE it, or ask A to add a one-line `kcdx.dll.declare`).

## Dependencies

The surfaces these docs describe (9.3 + 9.5) must be live.

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.6" → the index front
door + `extensibility.md`.
