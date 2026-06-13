# Phase 9.6 step 3 — docs: tiered front door + `extensibility.md` + bytes tier-pointer

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 3.

## What

Three doc deliverables that make the final surface navigable: the tiered
author-model front door in `docs/lua/index.md`, the cross-plugin extensibility
guide `docs/lua/extensibility.md`, and the `docs/lua/bytes.md` tier-pointer
(folded in from the struck [step 1](step-1-bytes-narrow-and-migrate.md) — its
only buildable remnant).

## Scope

- **`docs/lua/index.md` leads with the four-way tier model** — in the first three
  sentences the author sees which surface to reach for: `kcdx.behavior.set` (named
  behavior exists; one line), `kcdx.hook.before/after/around/replace` (per-call Lua
  logic at a code site), `kcdx.statement.replace_with` (static change with a named
  op, zero per-call cost), `kcdx.bytes` (the lowest-level tier — a raw byte rewrite
  at any located site when no named op or higher tier fits; the `pattern` AOB
  locator is the labeled-expert hatch). Plus the discovery pointer:
  `kcdx.behavior.list()`, `kcdx.find{...}`, `kcdx_dev_inspect`. (The
  §"Tiers of intent" ladder already exists in `index.md` — confirm its current
  state and tighten the lead, don't author it from scratch.)
- **`docs/lua/bytes.md` tier-pointer** (folded from struck step 1) — `bytes.md`
  states the tier relationship explicitly: `kcdx.bytes` is the **low-level tier**
  under `kcdx.statement.*`; reach for `kcdx.statement.replace_with` when a named
  op fits (zero per-call cost, hash-tracked), and use `kcdx.bytes` when you need a
  raw byte rewrite no higher tier expresses. This is a *steer*, NOT a narrowing —
  bytes stays the general primitive; function-internal raw rewrites remain valid
  on bytes (the narrowing was struck — see step 1). Deletion-hygiene: confirm no
  prose anywhere describes a `kcdx.bytes` "non-function-only" remit (none should
  exist, since the narrowing never landed — but sweep to be sure).
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
