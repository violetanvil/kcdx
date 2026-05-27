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
- **No new dispatch path bypasses the guard.** Every new dispatcher (`hook_chain`, future `[[vtable_hook]]`, etc.) goes through the same thread-check helper. Inserting a `lua_pcall` site that skips the check is an AP6 violation.
- **The on-thread fast path stays zero-allocation.** TLS read + branch only. The marshal path allocates per-callback (arg arena, queue node); that cost is intrinsic to off-thread dispatch and acceptable per workspace performance discipline.

Related: [docs/design.md §"Threading"](../../docs/design.md), `cornerstones.md` (engine does the heavy lifting), `anti-patterns.md` AP6.
