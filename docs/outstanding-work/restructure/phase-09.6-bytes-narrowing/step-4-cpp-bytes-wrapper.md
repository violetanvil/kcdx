# Phase 9.6 step 4 — empowered C++ `kcdx::bytes::Replace` wrapper + test

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 4.

## What

The empowered C++ wrapper for `kcdx.bytes`, peer to the existing `kcdx::hook::*`
helpers. Ships in this phase since this is bytes' next unshipped phase. Direction
was user-locked in the 2026-05-28 senior-architect-reply thread for the hook docs
flip; this is the bytes-side mirror.

## Scope

- `kcdx::bytes::Replace(K, "name", replacement, opts={})` in `include/kcdx/Kcdx.h`
  — required args (target name + replacement bytes) positional; optional knobs
  (name / description / original / module / offset / idempotent / context /
  anchorString / `[advanced]` pattern / addressId / targetSymbol locators) in a
  designated-initializer-style trailing struct. Auto-threads `owningPlugin =
  K.self`. Wraps `K.bytes->Register(&opts)`. Header-only. The raw
  `K.bytes->Register(&opts)` floor (Phase 3 sub-2, DONE) stays as the
  always-available form.
- `docs/cpp/bytes.md` common-path lead flips to the empowered form; the raw form
  demoted to the labeled raw-floor drop-down (3-floor model).
- Test row `test-plugins/cap-NN-cpp-bytes-wrapper/` — BOTH surfaces (raw +
  wrapper) under permanent regression, paralleling cap-36/cap-37.

## Test bar

`cap-NN-cpp-bytes-wrapper`: the wrapper and the raw floor both apply the same byte
rewrite (two surfaces, one capability). Suite-gated.

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.6" → "Empowered C++
wrapper for `kcdx.bytes`".
