# Phase 2 step 9 — `kcdxAssetInterface` C++ mirror (full parity)

**Status: NOT STARTED.** Ledger: [`README.md`](README.md) → step 9.

## What

The C++ author's mirror of the `kcdx.assets.*` surface — `kcdxAssetInterface` with
`GetByPath` / `GetByName` / `Replace` / `Declare` / `Register`, reached via
`K.assets->...` (design §5). Full Lua↔C++ parity (`lua-api-surface.md`): the C++
author gets every capability the Lua author does, same model, the other language's
spelling. The interface is append-only ABI (AP11) — new members at the end after
the `// --- APPEND-ONLY BELOW ---` marker, the positional initializer order in
`src/interfaces.cpp` mirrored exactly.

## Scope

- `include/kcdx/Interfaces.h`: add `kcdxAssetInterface` (append-only; if it extends
  an existing interface, new members at the END, never mid-struct — AP11) with the
  five mirror methods; bump the interface version if a shape change warrants
  (gate the new layout).
- `src/interfaces.cpp`: implement each thunk over the same engine-side resolution
  the Lua verbs call (one shared resolution path, not a parallel C++ copy —
  `structure-by-responsibility.md`).
- **Docs move with the surface** (`docs-discipline.md`): each method ships its
  `docs/cpp/` entry + glossary term + matrix row, SAME step; remove any NYI marker
  step 8 left on the C++ side.

## Test bar

A `cap-NN` C++ test plugin (suite-gated; both surfaces of a capability get rows —
`feedback_test_suite_must_grow`): the C++ mirror of each verb produces the SAME
result as its Lua peer (parity — a falsifiable claim, FAILS if the C++ path
diverges from the Lua path, AP15). The InputLoaded listener-count check confirms no
ABI break against the existing (not-rebuilt) plugin set (AP11). Build green;
live-confirmed via the launch.

## Dependencies

**Step 8** (the Lua surface + the shared engine-side resolution the C++ thunks call
— the mirror wraps the same path). Ordered after step 8 so there is a resolution
path to mirror, and parity can be tested Lua-vs-C++ (`.claude/rules/incremental-delivery.md`).

## Reference

Design authority: [`../../../design/asset-replacement.md`](../../../design/asset-replacement.md)
§5 (C++ mirrors each — full parity) + §10.1 (`kcdxAssetInterface` responsibility).
Shared spec: [`../plan-spec.md`](../plan-spec.md) §"Cross-step invariants" (parity).

## Disassembler-test / author-burden

The C++ author calls `K.assets->GetByPath("icons/my_icon.dds")` — a path, no engine
internal (the disassembler test, `cornerstones.md`). Append-only ABI so pre-built
plugins never break (AP11). No hand-written hex/ABI.
