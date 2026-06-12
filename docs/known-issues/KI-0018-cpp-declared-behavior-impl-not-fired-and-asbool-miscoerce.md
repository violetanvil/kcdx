---
id: KI-0018
opened: 2026-06-12
status: Open
commit_at_filing: 0290980a7b50e1ddfa8d5bbbf8bc847f73c4d588
---

# KI-0018 — a C++-declared behavior's implementation never fires at the apply boundary, and AsBool mis-coerces a recorded bool

## Symptom

In the Phase 9.5 P2 s1 launch (the new `kcdxBehaviorInterface`, cap-102), a value
set from Lua onto a **C++-declared** behavior records correctly (the shared
registry serves both languages — `Get` returns the value), but two things go
wrong on the C++ side: (a) the C++-registered **implementation never fires** at
the apply boundary (`impl ran=0`), and (b) the value-handle **`AsBool` accessor
returns false** for a recorded `true` that the raw `Get` reports as `1`. The
cross-language VALUE FLOW works (record + cross-language `Get`); only the C++
impl-fire-at-boundary and the bool coercion are broken. 9 of 12 cap-102 rows pass
and all the interface infrastructure (the VM-adoption wave-end gate, the
off-thread query wall, the early-stop out-of-window wall) is GREEN.

## Evidence (facts)

From `kcdx-dev_2026-06-12_11-45-08.log` (the P2 s1 launch, cap-102-cpp-behavior +
cap-102-cpp-behavior-lua deployed, dev mode):

- **`CAP-102-cpp-declare-set-get` — FAIL.** Verbatim:
  `round-trip WRONG — Get(cpp_scalar)=1 AsBool=0 value=1; impl ran=0 impl saw=0`.
  The Lua sibling set `cpp_scalar=true` at its main stop; `Get(cpp_scalar)`
  returns `1` (the value RECORDED on the C++-declared behavior — cross-language
  record works), but `AsBool` reads `0` for that same handle (a coercion
  mismatch: the raw get says `1`, the bool accessor says false), and the
  C++-registered implementation never fired at the apply boundary (`impl ran=0`,
  `impl saw=0`).

- **`CAP-102-crosslang-lua-sets-cpp` — FAIL.** Verbatim:
  `Lua-sets-C++-declared WRONG — Get(cpp_crosslang)=42 (access=0); impl ran=0 impl saw=-1`.
  The Lua sibling set `cpp_crosslang=42`; the value crossed into the
  C++-declared behavior's record (`Get(cpp_crosslang)=42`, `access=0`/OK — the
  ONE registry serves both languages), but the C++ implementation never fired at
  the boundary (`impl ran=0`, `impl saw=-1` = the default-init sentinel, never
  written).

- **`CAP-102-cpp-wave-end-gate-order` — FAIL (downstream casualty of the same
  root).** The row asserts `cpp_scalar`'s implementation fired at the boundary to
  prove the C++ wave reached the live VM under the gated guarantee; its FAIL text
  reports `impl ran=0`. The wave-end gate ITSELF is proven correct — the engine
  log shows `LUA_VM_BUILD wave_end_gate_signaled` precedes
  `LUA_VM_BUILD engine_adopted_kcdx_state` (the required boot order). The row
  fails only because its positive observation (the impl firing) is missing, which
  is the same impl-not-firing root as the two rows above — not a gate defect.

- **What works (9/12 rows + infra GREEN, for scoping the cause):**
  `CAP-102-cpp-list`, `-cpp-table-value`, `-cpp-coercion-mismatch`,
  `-cpp-stale-handle`, `-cpp-stale-handle-on-raise`, `-crosslang-cpp-sets-lua`,
  `-crosslang-cpp-sets-lua-impl-fired`, `-cpp-offthread-query-wall`,
  `-cpp-early-stop-out-of-window`, and the Lua sibling
  `-lua-sibling-crosslang`. Notably `-crosslang-cpp-sets-lua-impl-fired` PASSES —
  a C++ `Set` of a **Lua-declared** behavior DOES fire the Lua impl at the
  boundary — so the boundary-invocation path works for a Lua impl; only a
  **C++-declared** behavior's impl (the C-function-pointer trampoline) is not
  fired.

- **Suspect source sites (not yet confirmed by reading):**
  `src/behavior_interface.cpp` (the C-impl trampoline — a C function pointer
  wrapped via `lua_pushcclosure` into a Lua-callable ref the registry pcalls at
  the boundary; and the `AsBool` accessor coercion); the apply-boundary
  invocation in `src/behavior_registry.cpp` `RunApplyBoundary` (does it invoke a
  C++-declared behavior's stored impl ref the same way it invokes a Lua one?).

## Hypothesis (NOT verified)

- Hypothesis only — not verified: the apply-boundary implementation-invocation
  path (`behavior_registry::RunApplyBoundary`) may not pick up a C++-declared
  behavior's implementation ref — the C-impl trampoline ref may be registered or
  stored differently than a Lua impl ref (e.g. a never-set `implementationRef`,
  or a ref stored in a slot the boundary does not walk), so the boundary skips
  it. The contrast row (`crosslang-cpp-sets-lua-impl-fired` PASS — a C++ Set of a
  Lua-declared behavior DOES fire) suggests the boundary invocation is fine; the
  break is specific to a **C++-declared** behavior's impl storage/registration.

- Hypothesis only — not verified: the `AsBool` accessor may read the wrong slot
  or apply a wrong truthiness rule for the recorded value — `Get` reporting `1`
  while `AsBool` reports `0` on the same handle points at the coercion path
  (`AsBool` in `src/behavior_interface.cpp`) reading a different ref/slot than
  the one `Get` resolves, or coercing a non-bool Lua representation of the value.

- Hypothesis only — not verified: (a) and (b) may share a single root (a
  C++-declared behavior's value/impl refs being mis-stored at `Declare` time, so
  both the impl ref the boundary needs and the value ref `AsBool` reads are
  wrong), OR they may be two independent defects. The probe must isolate.

## Reproduction

Reliably reproducible. Deploy `cap-102-cpp-behavior` (the built `cap-102.dll` +
its `kcdx.toml`) and `cap-102-cpp-behavior-lua` (its `plugin.lua` + `kcdx.toml`)
to `<game-bin>/kcdx-plugins/test-suite/`, enable dev mode, launch to the main
menu, quit. Read `kcdx-dev_<ts>.log`: the three rows
`CAP-102-cpp-declare-set-get`, `CAP-102-crosslang-lua-sets-cpp`, and
`CAP-102-cpp-wave-end-gate-order` report `verdict=FAIL` with the evidence above.

## What this report does NOT do

- Does not propose a fix.
- Does not assign root cause beyond labeled hypothesis.
- Closure handled by `/debug KI-0018` (which lands the fix and closes per
  doc-organization.md).
