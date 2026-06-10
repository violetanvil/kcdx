# Phase 9.3 step 7b — `kcdxStatementInterface` (C++ static-bytes mirror)

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 7b.

## What

The C++ mirror of the Lua `kcdx.statement.*` surface (step 5) at full parity
(`.claude/rules/lua-api-surface.md`). A NEW C++ interface for static-bytes
modification — `ReplaceWith` (static op) + `InsertBefore` / `InsertAfter`
(callback). The insert methods mirror the register-and-DEFER contract the Lua side
has (NOT a firing feature — the Lua `kcdx.statement.insert_*` apply path is unwired
today). Consumes the `kcdxFunctionRef` from step 7a via its `targetRef` opts field.

## Scope (`include/kcdx/Interfaces.h` — one new interface; the C++ binder)

- **`kcdxStatementInterface`** (new QueryInterface ID + `kcdxStatementInterface_Version`).
- `ReplaceWith(const char* target, const kcdxOp* op, const kcdxStatementOptions* opts) → kcdxStatementHandle`
  — takes a STATIC op (the C++ peer of a `kcdx.op.*` value; NOT a callback), an
  optional locator (in opts; defaults to function-entry), and resolves +
  kind-checks + emits the determinate op's bytes + writes them as a deferred-apply
  `Kind::Statement` entry — the C++ mirror of the built `kcdx.statement.replace_with`
  (same resolve→kind-check→emit→write path; the engine side already exists, this
  adds the C++ entry point that feeds it).
- `InsertBefore(const char* target, const kcdxLocator* locator, void* callback, const kcdxStatementOptions* opts) → kcdxStatementHandle`
  / `InsertAfter(...)` — callback form, locator REQUIRED ("insert before what?" has
  no default).
- **The insert methods mirror the register-and-DEFER contract, not firing.** The
  Lua `kcdx.statement.insert_*` apply path is unwired today (it registers, then
  fails LOUD at apply — an honest deferral, never faked-green). The C++ mirror has
  the SAME contract: `Insert*` returns a handle, `IsApplied(h)` is false,
  `GetReason(h)` carries the not-yet-wired teaching reason. Wiring the insert
  apply-path to actually FIRE is separate deferred work on BOTH surfaces and is OUT
  of this phase's scope — the C++ side mirrors the deferral, it does NOT get ahead
  of the Lua side.
- **`kcdxStatementHandle`** carries `IsApplied` / `GetReason` / `GetName` /
  `Uninstall`, mirroring `kcdxHookHandle`.
- **`kcdxStatementOptions`** (new struct) carries the optional knobs (the locator
  for `ReplaceWith`, a name, a description) AND a `const kcdxFunctionRef* targetRef`
  field (§9.3-C.4): each verb's bare `const char* target` is the common path (a
  name string — the disassembler-test default); to pass a `kcdxFunctionRef` (from
  7a) the author sets `targetRef`, which wins when set. The struct's own
  append-only marker is established at creation (`anti-patterns.md` AP11 — future
  fields append after it).
- The new QueryInterface ID appends to the `kcdxInterface_*` enum after 7a's
  additions; never renumber (AP11). The C++ binder wires the method pointers,
  positional order mirroring the struct (AP11).
- The `Kcdx.h` wrapper gains a `K.statement` accessor (fetched via `QueryInterface`
  in `Kcdx::Init`).

## Test bar (runs AT this step)

A C++ test plugin `test-plugins/cap-NN-cpp-statement/` (next free `cap-NN`; a DLL
plugin) — the C++ mirror of cap-92's structural rows: `K.statement->ReplaceWith`
with a static op registers as a `Kind::Statement` entry (handle non-nil, NO
callback path — the static-op-only contract); a kind-mismatch is rejected with a
teaching reason naming both kinds; a deferred op surfaces a not-yet-emittable
deferral, never a fabricated byte; and `K.statement->InsertBefore(...)` registers
and is honestly DEFERRED (`IsApplied` false, `GetReason` carries the not-yet-wired
reason — mirroring the Lua defer contract, NOT a fired insert). A row FAILS if a
static op is rejected, an insert silently applies, a deferred op fabricates a byte,
or the C++ result differs from its Lua mirror (parity). **ABI safety:** re-launch
with the EXISTING (not-rebuilt) plugin set; InputLoaded listener count UNCHANGED
(AP11). The dual-Lua sentinel canary stays zero.

## Dependencies

**Step 7a** (the `kcdxFunctionRef` the `targetRef` opts field consumes; the
function-resolution path a target may name). Step 5 (the built Lua
`kcdx.statement.*` + the engine-side resolve/emit/write path the C++ entry point
feeds). Independently verifiable once 7a lands (its own C++ test exercises the
statement surface; it does not need 7c).

## Design authority

[`../00-original-plan.md`](../00-original-plan.md) §9.3-C.2 (the interface shape:
`ReplaceWith` / `InsertBefore` / `InsertAfter`, the register-and-defer insert
contract, `kcdxStatementHandle` queries) + §9.3-C.4 (the `targetRef` opts-field
shape) + §9.3-C intro (AP11 + InputLoaded-ABI-check). Build to those §s, not this
summary. `.claude/rules/lua-api-surface.md`, `.claude/rules/anti-patterns.md` AP11
(append-only ABI), AP14 (fail loud — the insert defers loud, never faked-green).

## Disassembler-test / author-burden note

The author names a target + a `kcdx.op.*`-peer op value; the engine owns the byte
emit, the fit decision, the trampoline. No author hex, no offset, no instruction
length, no new DB rows. The `targetRef` form lets a C++ author resolve a function
reference once (7a) and pass it to the statement verb — no re-naming at the call
site.

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §9.3-C.2 + §9.3-C.4.
The NYI design stub being resolved to built:
[`../../../cpp/statement.md`](../../../cpp/statement.md).
