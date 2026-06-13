# Phase 9.6 step 4 — empowered C++ `kcdx::bytes::Write`/`TryWrite` wrapper + test

**Status: SHIPPED — wrapper + test + docs landed in the 9.3/9.5 wrapper work;
this step's open work is the live confirmation of cap-63.** Ledger row:
[`README.md`](README.md) → step 4.

> **Verb rename — settled 2026-06-12.** This step was originally specced as
> `kcdx::bytes::Replace(K, "name", replacement, opts={})` with a
> designated-initializer trailing-struct opts. The as-built surface landed as
> `kcdx::bytes::Write` / `TryWrite` with a `kcdxBytesOptions*` opts pointer —
> the SAME 3-floor naming + opts idiom the shipped `kcdx::hook::*` wrapper uses
> (Floor 1 fire-and-log + Floor 2 `Try*`), so a C++ author sees one consistent
> wrapper convention across hook + bytes (consistency = predictability,
> cornerstone #1). User-accepted; the `Replace`/designated-init spec is
> superseded.

## What (as built)

- **`include/kcdx/Kcdx.h` `namespace bytes`** — the empowered C++ peer of Lua's
  `kcdx.bytes`, peer to `kcdx::hook::*`:
  - `kcdx::bytes::Write(K, "name", replacement, opts=nullptr)` — Floor 1,
    fire-and-auto-log (logs a teaching line on a zero handle).
  - `kcdx::bytes::TryWrite(K, "name", replacement, opts=nullptr)` — Floor 2,
    returns the handle for programmatic branching.
  - Required args (target name + replacement bytes) positional; optional knobs
    threaded via a `kcdxBytesOptions* userOpts` pointer (name / description /
    original / module / offset / idempotent / context / anchorString /
    `[advanced]` pattern / addressId / targetSymbol). Auto-threads
    `owningPlugin = K.self`. Wraps `K.bytes->Register(&opts)`. Header-only. The
    raw `K.bytes->Register(&opts)` floor stays as the always-available unchecked
    form.
- **`docs/cpp/bytes.md`** — common-path lead leads with `kcdx::bytes::Write`;
  the raw form is the labeled raw-floor drop-down (3-floor model). DONE.
- **`test-plugins/cap-63-cpp-bytes-wrapper/`** — the wrapper-floor regression
  (cap-39 covers the raw floor; both surfaces under permanent regression). The
  test installs the outfit-swap byte rewrite via the wrapper and asserts
  `IsApplied` + a byte read-back at the apply boundary.

## Open work (this step)

- **Live-confirm `cap-63-cpp-bytes-wrapper`** — its matrix row was `⏳ PENDING`
  (built, never launch-confirmed). The wrapper's matrix row reading GREEN in a
  live launch is this step's (and the phase's) verification gate
  (build-green ≠ in-game verified). The agent builds + deploys + reads the log;
  the user launches once.

## Test bar

`cap-63-cpp-bytes-wrapper` row `CAP-63-wrapper-installs`: the wrapper installs the
same byte rewrite the raw floor (cap-39) does — handle non-zero at Load,
`IsApplied == true` at the apply boundary, byte read-back `45 31 F6`. FALSIFIABLE:
handle 0 / `IsApplied` false / read-back mismatch → FAIL. Suite-gated.

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.6" → "Empowered C++
wrapper for `kcdx.bytes`".
