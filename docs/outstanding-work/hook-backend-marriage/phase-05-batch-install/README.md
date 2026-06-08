# Phase 5 — batch install

**Intent.** Install N detours under ONE thread-suspend window instead of N. The
cost that does not scale: `safetyhook::InlineHook::enable()` calls `trap_threads`
— suspend ALL threads, patch, resume — once per hook (`inline_hook.cpp:383`). A
TC's hundreds of boot installs, or a multiplayer mod's cross-cutting hook set, is
hundreds of stop-the-world cycles back-to-back. safetyhook exposes NO batch
primitive, but `StartDisabled` (create without patching) + `trap_threads`
(suspend-and-run-a-closure) are the building blocks for a kcdx-authored path:
create all N hooks disabled, then patch all N inside ONE frozen window. The batch
API is expressed on `IDetourBackend` so each backend implements it natively
(safetyhook via the frozen window; MinHook via `MH_QueueEnableHook`/`MH_ApplyQueued`).
Two ordered steps: the batch mechanism (probe-gated), then the `comp-NN` N-hook
scale fixture. Covers E23, E24, E25, E26 (`../context.md`).

Shared spec: [`../context.md`](../context.md).

## Status ledger (step-grain)

| Step | Status | Commit |
|---|---|---|
| Step 9 — batch-install mechanism (probe-gated, U7) | NOT STARTED | — |
| Step 10 — comp-NN N-hook batch fixture | NOT STARTED | — |

## Verification gate

- Step 9: the U7 multi-target-window probe lands FIRST (a `comp-NN` fixture
  installs N>1 hooks `StartDisabled`, patches all in one `trap_threads` window,
  confirms all N fire live). All fire → the batch path is wired on `IDetourBackend`
  and both backends; any miss/instability → fall back to per-hook `enable()`
  (unbatched but correct), recorded. Build green; the full cap-NN suite
  unregressed (the batch path is additive — single installs still work).
- Step 10: the N-hook scale fixture (`comp-NN`) installs a batch set through ONE
  suspend window and confirms all fire; the unbatched baseline (N separate
  windows) is the comparison. Agent builds+deploys, user launches, agent reads the
  log.
