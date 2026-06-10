# Phase 9.3 step 7c — `kcdxHookInterface` insert methods + `targetRef` opts field

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 7c.

## What

The C++ mirror of the Lua `kcdx.hook.insert_before` / `insert_after` sub-verbs
(step 4) at full parity (`.claude/rules/lua-api-surface.md`). Two APPEND-ONLY
methods on the EXISTING `kcdxHookInterface` (already sub-method-shaped at
`kcdxHookInterface_Version 2` — this ADDS, it does not migrate), plus the
`targetRef` opts-field append that lets any hook verb accept a `kcdxFunctionRef`
(from step 7a). The insert methods mirror the register-and-DEFER contract (the Lua
`kcdx.hook.insert_*` apply path is unwired today — NOT a firing feature).

## Scope (`include/kcdx/Interfaces.h` — APPEND-ONLY additions; the C++ binder)

- **`kcdxHookInterface` gains `InsertBefore` / `InsertAfter`** — same shape as the
  existing sub-verbs:
  `(const char* target, void* callback, const kcdxHookOptions* opts) → kcdxHookHandle`.
  Appended AFTER the existing `--- APPEND-ONLY BELOW (kcdxHookInterface_Version >= 2) ---`
  marker (`anti-patterns.md` AP11 — new members at the struct END, never
  mid-struct; a pre-built plugin AVs on load otherwise). The positional
  initializer order in the binder mirrors the struct exactly (AP11).
- **Bump `kcdxHookInterface_Version` to 3** (the vtable grew — a method addition
  bumps the interface version; `.claude/rules/skse-parity.md`). A v2 plugin reads
  the prefix methods at their original offsets unchanged.
- **The insert methods mirror the register-and-DEFER contract, not firing.** The
  Lua `kcdx.hook.insert_*` apply path is unwired today (register-and-defer, fail
  loud at apply). The existing handle contract already expresses it (a non-zero
  handle ≠ applied; `IsApplied` false until the apply pass; `GetReason` carries the
  not-yet-wired reason), so NO new query mechanism is needed. Wiring the insert
  apply-path to actually FIRE is separate deferred work on BOTH surfaces, OUT of
  this phase's scope.
- **`targetRef` opts field** — append `const kcdxFunctionRef* targetRef` to
  `kcdxHookOptions` AFTER its existing append-only marker (AP11-clean — a field
  append, no method-set churn, no opts-struct version churn). Each hook verb keeps
  its bare `const char* target` first param as the common path (a name — the
  disassembler-test default); to pass a `kcdxFunctionRef` (from 7a) the author sets
  `targetRef`, which wins when set. This is the SAME opts-field shape §9.3-C.4
  settled for every hook verb (not just insert) — so `Before`/`After`/`Around`/
  `Replace`/`Mid`/`Callsite` also accept `targetRef` once the field exists.

## Test bar (runs AT this step)

A C++ test plugin `test-plugins/cap-NN-cpp-hook-insert/` (next free `cap-NN`; a DLL
plugin) — the C++ mirror of the hook-insert defer contract:
`K.hook->InsertBefore(target, cb, opts)` registers (handle non-nil) and is honestly
DEFERRED (`IsApplied` false, `GetReason` carries the not-yet-wired reason — NOT a
fired insert); AND a hook verb (e.g. `Before`) installed with `opts->targetRef` set
to a `kcdxFunctionRef` (resolved via 7a's `K.functions->GameByName`) resolves the
SAME target the `const char* target` name form would (the `targetRef` parity
affordance works). A row FAILS if an insert silently applies (`IsApplied` true
while the apply path is unwired), the deferred reason is missing, or a
`targetRef`-targeted hook resolves a different target than its name form (the
parity assertion). **ABI safety — THE load-bearing check for this step:** re-launch
with the EXISTING (not-rebuilt) plugin set (the v2-built cap-07/cap-09/cap-13/cap-16
DLL plugins) and confirm the InputLoaded listener count is UNCHANGED — a drop = the
append shifted a prefix offset = an AP11 ABI break. The dual-Lua sentinel canary
stays zero.

## Dependencies

**Step 7a** (the `kcdxFunctionRef` the `targetRef` opts field consumes). Step 4
(the built Lua `kcdx.hook.insert_*` + the existing `kcdxHookInterface` v2 this
appends to). Independently verifiable once 7a lands (its own C++ test exercises the
hook-insert + targetRef surface; it does not need 7b). Orderable after 7a in either
order with 7b; placed after 7b to match the §9.3-C.2-before-C.3 design order.

## Design authority

[`../00-original-plan.md`](../00-original-plan.md) §9.3-C.3 (the insert-method
append, the v3 bump, the register-and-defer contract) + §9.3-C.4 (the `targetRef`
opts-field shape, applied to all hook verbs) + §9.3-C intro (AP11 append-only +
the InputLoaded-ABI-check). Build to those §s, not this summary.
`.claude/rules/lua-api-surface.md`, `.claude/rules/anti-patterns.md` AP11
(append-only interface ABI — the central constraint of this step), AP14 (the
insert defers loud, never faked-green), `.claude/rules/skse-parity.md` (bump the
interface version on a struct change).

## Disassembler-test / author-burden note

A hook verb resolves its target by NAME (the engine carries address + ABI); the
`targetRef` form lets a C++ author resolve a function reference once (7a) and pass
it to N hook verbs — no re-naming, no hex. No new DB rows.

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §9.3-C.3 + §9.3-C.4.
The NYI design entry being resolved to built:
[`../../../cpp/hook.md`](../../../cpp/hook.md) (the insert-method entries).
