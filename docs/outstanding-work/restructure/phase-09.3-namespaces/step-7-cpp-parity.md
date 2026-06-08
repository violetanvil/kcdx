# Phase 9.3 step 7 — C++ parity (`kcdxStatementInterface` / `kcdxFunctionsInterface` + hook insert methods)

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 7.

## What

Mirror the new Lua shapes on the C++ surface at full parity (the authoring-surface
rule: Lua↔C++ feature parity is an invariant on the shipped product,
`.claude/rules/lua-api-surface.md`). Mostly NEW interfaces + append-only additions —
the C++ hook interface is ALREADY sub-method-shaped (it ships `Before/After/Around/
Replace/Mid/Callsite` at `kcdxHookInterface_Version 2`, `include/kcdx/Interfaces.h`),
so this is not a mode-as-field→sub-method migration; it ADDS the missing pieces.

## Scope (`include/kcdx/Interfaces.h` — append-only; the C++ binders)

- **`kcdxHookInterface`** gains `InsertBefore` / `InsertAfter` method pointers
  (mirroring the Lua `kcdx.hook.insert_before/insert_after`, step 4). APPEND-ONLY at
  the END after the `--- APPEND-ONLY BELOW ---` marker (the interface-ABI rule, AP11
  — never insert mid-struct; a pre-built plugin AVs on load otherwise). If the shape
  genuinely changes beyond an append, bump `kcdxHookInterface_Version` and gate the
  new layout (the existing version-gate mechanism, already exercised at v2/v3).
- **`kcdxStatementInterface`** (NEW interface) — static-bytes work, same shape as
  `kcdx.statement.*` (step 5): `ReplaceWith` / `InsertBefore` / `InsertAfter`. New
  `kcdxStatementInterface_Version` define.
- **`kcdxFunctionsInterface`** (NEW interface) — mirrors `kcdx.dll.declare` (step 3):
  a C++ plugin declares its own functions via `K.functions->Declare(...)`. New
  `kcdxFunctionsInterface_Version` define.
- The C++ binders (`src/interfaces.cpp` / the relevant thunk files) wire the new
  method pointers — positional initializer order mirrors the struct exactly (AP11).
- **Migrate the C++ test plugins IN THIS STEP** alongside the Lua ones (so both
  surfaces of each capability are under permanent regression; the suite stays green —
  no C++ red window).
- Docs (`.claude/rules/docs-discipline.md`): the new `docs/cpp/` per-interface entries
  (`statement.md`, `functions.md`, the hook insert methods) + the NYI/parity rows
  resolved to built; glossary terms.

## Test bar (runs AT this step)

A C++ test plugin (`test-plugins/cap-NN-cpp-statement-functions/` or migrated C++
caps) exercises: `K.statement->ReplaceWith(...)` produces zero per-call dispatch (the
C++ mirror of cap-statement-replace); `K.functions->Declare(...)` declares a function
another plugin resolves by name; `K.hook->InsertBefore(...)` fires. Each row FAILS if
the C++ surface behaves differently from its Lua mirror (the parity rows). **ABI
safety: re-launch with the EXISTING (not-rebuilt) plugin set — the InputLoaded
listener count is UNCHANGED** (a drop = an ABI break, AP11). PROBE Q silent.

## Dependencies

The settled Lua shapes (steps 3, 4, 5) — the C++ mirrors them. The existing
`kcdxHookInterface` (v2, sub-method-shaped, ships today).

## Design authority

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.3" → "C++ side parity" +
`.claude/rules/lua-api-surface.md` (one model, two languages, parity-at-all-times on
the shipped product) + `.claude/rules/anti-patterns.md` AP11 (append-only interface
ABI). Build to those, not this summary.

## Disassembler-test / author-burden note

The C++ surface mirrors the Lua one — `K.functions->Declare(...)` lets a C++ author
declare from their own source (no disassembly), same as `kcdx.dll.declare`. No author
hex on the common path. No new DB rows.

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.3" → "C++ side parity".
