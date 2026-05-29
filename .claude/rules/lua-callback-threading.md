---
paths:
  - "src/scripting.*"
  - "src/hook_engine.*"
  - "include/kcdx/Interfaces.h"
---

# Lua callback threading — engine marshals off-thread hits onto the main thread

KCD2's Lua VM (CryEngine 5.2.3-bundled Lua 5.1) is single-threaded — `lua_lock` / `lua_unlock` are no-ops in the shipped build. Property of the binary KCD2 ships, NOT a kcdx restriction on what authors may hook. The engine takes responsibility for getting every Lua callback onto the main thread; the author hooks whatever they need.

The engine captures TWO thread IDs, used by different consumers:

1. **Engine-init thread** (`log::g_engineInitThreadId`, captured at `log::Init`): used by the dev-log formatter to suffix `tid=N` on lines that ran on a non-init thread. Accessor: `log::IsMainThread()` (kept named for legacy callers — the body matches "engine-init thread"). DO NOT use this accessor for hook-dispatch thread classification — under the kcdx.exe injector model, `log::Init` runs on `CreateRemoteThread`'s injector thread, NOT the game's main thread.

2. **Game main thread** (`log::g_gameMainThreadId`, captured at `hook_chain::SetLuaState`'s first non-null L call): used by the hook dispatchers to classify off-thread fires. Accessor: `log::IsGameMainThread()`. The capture site IS the game main thread by construction — the first-update-tick hook at `src/hooks.cpp:316-321` fires on the main thread and triggers `hook_chain::SetLuaState`.

Every dispatch site (`DispatchPre` / `DispatchPost` / `MidDispatch` in `hook_chain.cpp`) compares `::GetCurrentThreadId()` to `g_gameMainThreadId` via `log::IsGameMainThread()` before invoking the callback:

- **On-thread (common path):** invoke `lua_pcall` directly. One TLS read of overhead.
- **Off-thread:** queue the callback onto `kcdx::task::DrainQueue` (the same primitive that backs `kcdxTaskInterface::AddTask`); it fires on the next main-tick. The original function returns synchronously with its pre-hook default behavior (`before` proceeds un-mutated, `after` sees the un-transformed return, `replace` / `around` skip the callback for this fire).

## Per-hook opt-out

`kcdx.hook{ off_thread = ... }`:

- `"marshal"` (default) — auto-queue per above.
- `"skip"` — silently drop off-thread fires; warn-once-per-hook in the engine log. Right choice for high-frequency off-thread sites (audio mixer at kHz) the author doesn't want flooding the queue.
- `"error"` — log an error and drop; author asserts this site MUST be main-thread.

## Rules

- **Authors hook whatever they need.** Off-thread sites are first-class; the engine marshals. The previous "main-thread-only" framing was a v0.1 placeholder that overstated the constraint — the actual constraint is that `lua_pcall` runs on one thread, which the dispatcher enforces.
- **No new dispatch path bypasses the guard.** Every new dispatcher (`hook_chain`, future `[[vtable_hook]]`, etc.) goes through the same thread-check helper. Inserting a `lua_pcall` site that skips the check is an AP6 violation. The engine bootstrap carve-out (next rule) is the documented exception; no other bypass.
- **The on-thread fast path stays zero-allocation.** TLS read + branch only. The marshal path allocates per-callback (arg arena, queue node); that cost is intrinsic to off-thread dispatch and acceptable per workspace performance discipline.

## Engine bootstrap carve-out — engine-stamped C-kind chain entries bypass the off-thread filter

Engine-stamped C-kind chain entries (`ChainEntry::isEngine == true && kind == ChainEntry::Kind::C` for `DispatchPre`/`DispatchPost`; the chain-level mirror `Chain::isMidEngine == true && midKind == Kind::C` for `MidDispatch`) bypass the off-thread filter at all three gate sites. AP6 (no Lua callback fires off-thread) does not apply — these are kcdx-internal C functions registered via `hook_chain::AddCEngine` (the engine team owns and audits them; plugins cannot stamp the engine identity, the class is closed by construction). The bypass is zero-cost: one-instruction predicate, no warn line, no map insert.

The bypass exists because the dispatcher's main-thread classifier is itself bootstrapped by an engine C-Before callback. Gating that callback on the classifier creates a self-perpetuating dead-classifier deadlock — observed live as the cap-59-fires regression on 2026-05-29.

The three-hop bootstrap loop the bypass breaks:

1. The migrated chain C-Before callback `HookedLuaPcall_Engine` (`src/hooks.cpp`) writes `hooks.cpp::g_L` on every `lua_pcall` fire.
2. `HookedUpdate` — `src/hooks.cpp`, installed by direct `MH_CreateHook` per the documented bootstrap-pump exception in `hook-engine.md`, deliberately NOT a chain entry — reads `g_L` in its first-tick latch and on `g_L != null` calls `hook_chain::SetLuaState(L)`.
3. `SetLuaState` calls `log::SetGameMainThread` which captures `log::g_gameMainThreadId`. After this, `log::IsGameMainThread()` starts returning true for the main thread; the chain's three off-thread gates (`hook_chain.cpp` DispatchPre / DispatchPost / MidDispatch) start classifying correctly.

Pre-`SetLuaState`, `log::IsGameMainThread()` returns false for every thread (the unset-capture default). Without the carve-out, every `lua_pcall` fire pre-bootstrap takes the `OffThreadShouldSkip + continue` path at the per-entry gate BEFORE the per-entry C-Before callback runs — hop 1 never executes, hop 2's latch never crosses `if (L)`, hop 3 never runs, the classifier stays dead forever. cap-59's `plugin.lua` (and every other Lua plugin's `plugin.lua` that loads via `lua_plugin_loader::RunAll(L)` from hop 2's latch) never executes. The carve-out is the type-level fix.

Related: [docs/design.md §"Threading"](../../docs/design.md), `cornerstones.md` (engine does the heavy lifting), `anti-patterns.md` AP6.
