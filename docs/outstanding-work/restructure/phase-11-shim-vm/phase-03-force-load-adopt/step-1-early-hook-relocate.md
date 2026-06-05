# P3 step 1 — relocate/generalize the early-install primitive → src/early_hook

## What

Relocate the proven before_game-hook install machinery out of `src/probes/` into a
permanent engine home (`src/early_hook.{h,cpp}`), generalizing it from the one baked
bugsplat-ctor target into an author-parameterized install (module + export +
signature + the plugin's named detour). This is the engine half the force-load +
before_game apply pass (step 2) drives.

## Scope

- Move `src/probes/bugsplat_ctor_probe.{h,cpp}`'s `ArmLdrInstall()` + `HookedCtor`
  install path into `src/early_hook.{h,cpp}` (or an extension of `ldr_notify`), per
  `before-game-hooks.md` §5.
- Generalize: the install takes (module, export, signature, detour) rather than the
  baked BugSplat64 target. The bugsplat fix becomes the FIRST CONSUMER of the
  generalized primitive (it lands later, riding this phase — not a step here).
- Update `dllmain.cpp`'s `ArmLdrInstall()` call site to the new home.
- **Survivor sweep** (`.claude/rules/deletion-hygiene.md`): the relocation removes
  `src/probes/bugsplat_ctor_probe` — sweep `docs/`, `.claude/rules/`, `CLAUDE.md`,
  and the restructure plan for prescriptive references to the old path and repoint
  them to `src/early_hook`.

## Test bar

A `test-plugins/cap-NN-early-hook/` regression installs a detour on a known
already-mapped module via the generalized primitive and self-reports the detour
fired — proving the author-parameterized install works (not just the baked target).
Runnable at this step (no VM needed — MinHook is up in DllMain). Build green.

## Dependencies

None on Phases 1–2 (this is engine-install plumbing, independent of the shim). Placed
in Phase 3 because step 2 (force-load) drives it. The source primitive
(`bugsplat_ctor_probe`) exists and is live-confirmed (`before-game-hooks.md` §5).

## Design authority

[`../lua-vm-design.md`](../lua-vm-design.md) §8 (the unit) +
[`../../before-game-hooks.md`](../../../before-game-hooks.md) §5 (the proven
machinery + the generalization spec).

## RE / author-burden note

The generalized install resolves its target by module+export NAME (the author
declares a name; the engine resolves the address + the install), never an
author-supplied RVA (the disassembler test, `.claude/rules/cornerstones.md`). The
bugsplat consumer's target ABI is already verified (`before-game-hooks.md` §6).

## Reference

[`../plan-spec.md`](../plan-spec.md) coverage row E18; design §8.
