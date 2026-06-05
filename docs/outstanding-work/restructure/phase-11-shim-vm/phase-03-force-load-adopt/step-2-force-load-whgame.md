# P3 step 2 — force-load WHGame.dll from DllMain + LDR before_game apply

## What

Add `LoadLibraryW(L"WHGame.dll")` to kcdx.dll DllMain BEFORE the before_game
registration pass, so WHGame's compiled Lua (and the modules its dependency chain
pulls in) are mapped for the shim. Wire the LDR before_game apply pass: for each
newly-mapped module the LDR notification fires, and before_game patches/hooks
declaring that module apply at its mapping.

## Scope

- `src/dllmain.cpp`: `LoadLibraryW(L"WHGame.dll")` before the before_game
  registration pass.
- Verify `src/ldr_notify.cpp`'s `LdrRegisterDllNotification` fires SYNCHRONOUSLY
  inside the `LoadLibraryW` call (it exists; this step confirms + wires the apply).
- For each mapped module, the before_game apply pass runs (the generalized
  `early_hook` install from step 1) — the bugsplat fix's `BugSplat64.dll` target
  lands here when WHGame's chain maps it; then WHGame's own DllMain runs.
- Log the loader-lock wall-clock (DllMain start → worker-thread spawn).

## Test bar

A `test-plugins/cap-NN-force-load/` regression (or a dev-log assertion row): the game
boots with WHGame force-loaded; the dev log shows `LDR notification fired for
<module> → before_game apply` per mapped module BEFORE the next phase line; the
loader-lock budget line is <500ms (target <200ms). **Boot itself is the falsifiable
observable** — a bad force-load AVs at startup. The user launches; the agent reads
the dev log. Suite stays green.

## Dependencies

P3 step 1 (the generalized `early_hook` install is what the apply pass drives). The
shim (P2) is NOT a dependency of the force-load itself — but the force-load must
precede the VM build (step 3), which IS shim-dependent.

## Design authority

[`../lua-vm-design.md`](../lua-vm-design.md) §6.2 (force-load + the LDR apply pass) +
the mechanism steps in [`../00-original-plan.md`](../00-original-plan.md) §"Phase 11"
(steps 5–7).

## RE / author-burden note

No author-facing hex. The force-load targets `WHGame.dll` by name; before_game
targets resolve by module+export name through the generalized install. No new DB
rows.

## Reference

[`../plan-spec.md`](../plan-spec.md) coverage row E9; design §6.2.
