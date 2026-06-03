# Step 7 — `kcdxAssetInterface` C++ mirror + docs (parity)

**Status: NOT STARTED.** Ledger: [`README.md`](README.md) → step 7.

## What

The C++ mirror of the `kcdx.assets.*` Lua surface: `kcdxAssetInterface` /
`K.assets->GetByPath` / `GetByName` / `Replace` / `Declare` / `Register`, same
model, idiomatic C++ (positional Lua args → typed params; the cross-plugin
namespace → the C++ spelling). Full Lua↔C++ parity (`lua-api-surface.md`): a C++
plugin does everything a Lua plugin can. Ships its `docs/cpp/` entries; removes the
step-6 NYI markers.

## Scope

- Add `kcdxAssetInterface` to `include/kcdx/Interfaces.h` (append-only — new members
  after the marker; never insert/reorder — `anti-patterns.md` AP11) + wire its
  thunks in `src/interfaces.cpp`, mirroring the Lua binder's behavior exactly.
- The C++ spelling of the cross-plugin reference (the navigable namespace's C++
  equivalent — owner+name/path into the resolvers).
- `docs/cpp/assets.md` per-method entries + `docs/cpp/index.md` map row + glossary;
  remove the step-6 Lua-side NYI mirror markers (`docs-discipline.md`).

## Test bar

Exercised at step 8 (the parity row — a C++ plugin driving the surface). This
step's own check: each `kcdxAssetInterface` method is callable from a C++ plugin
and produces the same result as its Lua verb; the ABI is append-only (verify via
the existing-plugin InputLoaded-listener-count check — AP11). Falsifiable: a
missing asset errors the same way the Lua side does.

## Dependencies

**Step 6** (the Lua surface this mirrors — same behavior, the other language);
**step 5** (the navigable namespace, for the C++ cross-plugin spelling). Ordered
after — the mirror is exercisable when it lands.

## Disassembler-test / author-burden

CLEAN — the C++ author passes a path or name they know; no hex. Same disassembler
clean as the Lua side.

## UX

Author-facing C++ surface (not UI). The contract + teaching-error states mirror
step 6's, idiomatic in C++. Carried from design §5.

## Reference

[`../plan-spec.md`](../plan-spec.md); design authority
[`../../restructure/phase-08.5-asset-replacement/asset-design.md`](../../restructure/phase-08.5-asset-replacement/asset-design.md)
§5 (the surface — the C++ mirror column) + the parity invariant (`lua-api-surface.md`).
Build to §5, not to this summary.
