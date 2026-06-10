# Phase 9.3 step 7a — `kcdxFunctionsInterface` + `kcdxDllInterface` (C++ function-reference + declare)

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 7a.

## What

The C++ mirror of the Lua `kcdx.functions.*` + `kcdx.dll.declare` surfaces (step
3) at full parity (`.claude/rules/lua-api-surface.md` — Lua↔C++ parity on the
shipped product). Two NEW, SEPARATE C++ interfaces, mirroring the Lua surface's
two distinct paths (`kcdx.dll.declare` ≠ `kcdx.functions.*` — the surfaces-mirror
cornerstone). This is the PRODUCER step: it introduces the passable by-value
`kcdxFunctionRef` that steps 7b/7c consume via their `targetRef` opts field, so it
is ordered first and is independently verifiable on its own (a declare-then-resolve
C++ test, no dependency on 7b/7c).

## Scope (`include/kcdx/Interfaces.h` — two new interfaces; the C++ binders)

- **`kcdxFunctionsInterface`** (new QueryInterface ID + `kcdxFunctionsInterface_Version`)
  mints a **passable by-value `kcdxFunctionRef`** carrying exactly the 8 fields
  `{ found, isGame, stem, name, address, hasAddress, signature, reason }` — the
  C++ peer of the Lua reference VALUE (one-to-one with the Lua `:resolve()` table,
  camelCase). The reference carries its resolution directly (the author reads the
  fields — NO separate `Resolve(handle)` call, NO opaque engine-owned handle, NO
  two-call dance) AND is the value the hook/statement verbs (7b/7c) accept as a
  target. Mint forms, one-to-one with the Lua accesses:
  - `GameByName(const char* stem, const char* name) → kcdxFunctionRef` ↔
    `kcdx.functions.WHGame.SaveGame`
  - `GameById(unsigned long long kcdxId) → kcdxFunctionRef` ↔
    `kcdx.functions.by_id[N]` (game-only stable id)
  - `PluginByName(const char* pluginNamespace, const char* name) → kcdxFunctionRef`
    ↔ `kcdx.functions["a.b"].Fn`
  - A miss returns a ref with `found=false` + a `reason` token (`name_unknown` /
    `db_not_loaded` / `not_declared`), never a silent empty (fail-loud,
    `anti-patterns.md` AP14). `address` is a real resolved VA when `hasAddress`
    (game: from the DB; plugin: from the PDB-auto-load path), a raw `void*` (no
    `LUA_NUMBER=float` rounding hazard on the C++ side). The `const char*` fields
    point at engine-owned, process-lifetime strings (do not free).
- **`kcdxDllInterface`** (new QueryInterface ID + `kcdxDllInterface_Version`)
  mirrors `kcdx.dll.declare`:
  `Declare(const char* pluginNamespace, const kcdxDeclaredFn* fns, int count) → bool`,
  where `kcdxDeclaredFn = { const char* name; const char* signature; }`. Maps the
  Lua `function_map` (`{ FnName = { signature = "…" } }`) to the typed array; a
  malformed entry is rejected with a logged teaching diagnostic (the C++ peer of
  the Lua call's raised error); `signature` is required on every entry. Declared
  functions land in the SAME in-memory per-stem store the Lua `kcdx.dll.declare`
  binder writes (`src/lua_bind_functions.cpp` / the declared-plugin-function
  store), so they resolve identically via `PluginByName` (C++) and
  `kcdx.functions["<ns>"]` (Lua) — ONE store, both surfaces. (Disjoint from the
  EXISTING built `kcdxDeclareInterface` (ID 10) — that is the Address-Library
  declare, a different concept; keep the new ID + name distinct.)
- The new QueryInterface IDs append to the `kcdxInterface_*` enum AFTER the
  existing entries (the last is `kcdxInterface_Assets = 11`); never renumber an
  existing ID (`anti-patterns.md` AP11).
- The C++ binders (`src/interfaces.cpp` / the relevant thunk file) wire the new
  method pointers; positional initializer order mirrors each struct exactly (AP11).
- **Survivor-comment fix (same commit):** correct the stale header comment in
  `src/lua_bind_functions.cpp` ("the hook and statement verbs that consume this
  value as arg-1 are not yet built" — they DO today), per the survivor-sweep
  discipline (`.claude/rules/deletion-hygiene.md` / `docs-discipline.md`).
- The `Kcdx.h` wrapper gains `K.functions` / `K.dll` accessors (fetched via
  `QueryInterface` in `Kcdx::Init`, the existing pattern).

## Test bar (runs AT this step)

A C++ test plugin `test-plugins/cap-NN-cpp-functions-declare/` (pick the next free
`cap-NN`; a DLL plugin like cap-07/cap-80): declares a function via
`K.dll->Declare("<author>.<plugin>", entries, count)`, then resolves it via
`K.functions->PluginByName("<author>.<plugin>", "Fn")` and asserts the returned
`kcdxFunctionRef` carries `found=true` + the author-declared `signature`; AND
resolves a GAME function via `K.functions->GameByName("WHGame", "SaveGame")` (and
`GameById`) asserting `found=true` + a non-zero `address` (or `hasAddress=false`
with a deploy-state reason — degraded PASS, mirroring the Lua cap-83/cap-86
posture). A row FAILS if a declared function does not resolve, the signature is
lost, a miss does not carry a reason token, or the C++ result differs from its Lua
mirror (the parity assertion). **ABI safety:** re-launch with the EXISTING
(not-rebuilt) plugin set and confirm the InputLoaded listener count is UNCHANGED
(a drop = an ABI break — AP11). The dual-Lua sentinel canary stays zero.

## Dependencies

The built Lua `kcdx.functions.*` + `kcdx.dll.declare` (step 3a, DONE) — the C++
mirror reads the SAME in-memory declared-plugin-function store + the same refdb
resolution the Lua side uses. No dependency on 7b/7c (this is their producer).

## Design authority

[`../00-original-plan.md`](../00-original-plan.md) §9.3-C.1 (the full settled
shape: the two interfaces, the 8-field `kcdxFunctionRef`, the mint forms, the
shared store) + §9.3-C intro (the AP11 append-only + InputLoaded-ABI-check
discipline). Build to those §s, not this summary.
`.claude/rules/lua-api-surface.md` (one model, two languages, full parity),
`.claude/rules/anti-patterns.md` AP11 (append-only interface ABI), AP14 (fail
loud, never a silent empty).

## Disassembler-test / author-burden note

`kcdxDllInterface::Declare` is the strongest disassembler-test win — the C++
author declares from their OWN source (they have the types for free because they
wrote the function), no disassembly. A game function resolves by NAME (the engine
carries address + ABI). No author hex, no hand-written signature for a game target,
no new DB rows.

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §9.3-C.1.
The existing NYI design stubs being resolved to built:
[`../../../cpp/functions.md`](../../../cpp/functions.md),
[`../../../cpp/dll.md`](../../../cpp/dll.md).
