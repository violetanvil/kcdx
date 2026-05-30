# Per-hook context slot for capturing-lambda support in the empowered wrapper

## Status

**Designed gap, not built.** The `Kcdx.h` empowered hook helpers
(`kcdx::hook::Before/After/Around/Replace<Sig, &fn>`) require a **non-capturing**
function pointer as the callback (a free function or a captureless lambda). This
is documented as an intrinsic constraint at
[`include/kcdx/Kcdx.h:30-44`](../../include/kcdx/Kcdx.h#L30-L44).

The constraint is not a wrapper limitation — it's the engine's hook callback ABI.
The C dispatch path threads no per-hook `void* userdata` slot to the C function
pointer it invokes (only Mid carries a context slot, via the capture-values
array). Without an engine-side context slot, the wrapper cannot box a capturing
lambda's storage into something the dispatcher can retrieve and rebind at fire
time.

Current workaround documented at `Kcdx.h:42-44`: pass a free function that reads
into your own static. Works; load-bearing in cap-37 + cap-36 test plugins today;
distorts plugin code that legitimately wants per-hook state.

## Trigger to revisit

EITHER:

1. A user-shipped plugin (or a kcdx-internal test plugin) hits the workaround
   friction enough that the author surface needs the affordance — surfacing
   as either a thread on the project, a code-review finding flagging the
   pattern as repeated, or an `/execute` brief that asks to add a capturing
   helper.
2. The engine grows a per-hook context slot for an unrelated reason (e.g. the
   smart-replace conflict-detection design at
   [`smart-replace-conflict-detection.md`](smart-replace-conflict-detection.md)
   ends up needing per-hook metadata; the wrapper rides on the same slot).

## Design

The shape — settled at the design step when the trigger fires:

- **Engine ABI extension.** Append `void* userdata` to `kcdxHookOptions`
  (APPEND-ONLY per AP11). The hook chain's `HookPayload` carries it; the C
  dispatch thunk (`dynamic_call_jit::BuildCDispatchThunk`) threads it into
  the C callback's first hidden arg. For backward compatibility, a null
  `userdata` keeps the existing ABI shape (the callback's first typed arg is
  not shifted) — the dispatch thunk branches on null-vs-set at JIT time. This
  may require a parallel C ABI variant; settled at the design step.

- **Wrapper change.** `kcdx::hook::Before<Sig>(K, "name", capturing_lambda)`
  overload that takes the capturing lambda by-value, heap-allocates a
  `std::function`-style box, hands its address as `opts.userdata`, and the
  generated adapter retrieves it via the first hidden arg and invokes the
  boxed lambda. The non-capturing `Before<Sig, &fn>` template-parameter form
  stays as the zero-cost path for free-function authors (no heap, no
  indirection).

- **Lifetime.** The boxed lambda's storage outlives the hook. The wrapper
  owns it; uninstall releases it. A handle-typed wrapper
  (cf. [`restructure/00-original-plan.md`](restructure/00-original-plan.md) Phase 12 sub-3 row 3 —
  `kcdx::Handle`) lets the wrapper hold the storage in the handle struct so
  RAII handles the release; settled at the design step.

- **Mid + Callsite.** Mid already has a per-capture context channel via the
  `values[]` array. Callsite would compose with the same ABI extension. Both
  capturing forms ship together.

## Files that need to change

- [`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h) — append
  `userdata` to `kcdxHookOptions`, bump `kcdxHookInterface_Version` (AP11
  append-only).
- [`include/kcdx/Kcdx.h`](../../include/kcdx/Kcdx.h) — capturing-lambda
  overloads for each sub-verb; storage management; the non-capturing path
  stays as the zero-cost fast path.
- `src/dynamic_call_jit.cpp` — `BuildCDispatchThunk` branches on
  `userdata` set / null to thread the hidden first arg through.
- `src/hook_chain.cpp` — `HookPayload::userdata` plumbed through
  `AddC`/`AddCMid`/`AddCCallsite` and the dispatch sites.
- `src/hook_interface.cpp` — the 10 sub-verb thunks read `opts->userdata`
  and stash on the payload.
- `src/lua_bind_hook.cpp` — the Lua side does NOT need this (Lua's dynamic
  marshaling has per-callback closure state via the registry ref); the
  surface stays single-language unless Lua finds a reason to want it.
- `test-plugins/cap-NN-cpp-hook-capturing-lambda/` — both surfaces under
  regression: the non-capturing fast path + the capturing-lambda overload.
- [`docs/cpp/wrapper.md`](../cpp/wrapper.md) — document the capturing-lambda
  overload alongside the existing non-capturing fast path; document the
  storage-lifetime contract.

## Why this is its own outstanding-work entry, not Phase 12 scope

This is an **engine ABI change**, not a wrapper-only change. Phase 12 sweeps
empowered wrappers across raw interfaces that are already correct + lands
header-only UX polish. This entry needs `kcdxHookInterface_Version` bumped + new
JIT-thunk codegen + a new C ABI shape — strictly more risk and more scope than
the Phase 12 sweep. Lands when its trigger fires, then folds into whichever
phase is unshipped at that time (or its own appended phase if all current
phases are complete).

## Related

- [`include/kcdx/Kcdx.h:22-44`](../../include/kcdx/Kcdx.h#L22-L44) — the 3-floor
  model + the current capturing-lambda constraint documentation.
- [`restructure/00-original-plan.md`](restructure/00-original-plan.md) Phase 12 — empowered-wrapper
  sweep + UX polish (the closing C++ ergonomics phase that this entry tracks
  outside of, because it needs an engine ABI extension).
- [`smart-replace-conflict-detection.md`](smart-replace-conflict-detection.md) —
  the other plausible source of a per-hook context slot in the engine.
