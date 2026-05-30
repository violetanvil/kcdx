# Phase 11b — force-load WHGame.dll from kcdx.dll DllMain

**Status: BLOCKED** on 11a. Ledger row: [`README.md`](README.md) → 11b.

## What

kcdx.dll's DllMain force-loads WHGame.dll before the before_game registration pass,
so WHGame's compiled Lua (and the modules its dependency chain pulls in) are mapped
and available for the shim.

## Scope

- Add `LoadLibraryW(L"WHGame.dll")` to kcdx.dll DllMain BEFORE the before_game
  registration pass.
- Add `LdrRegisterDllNotification` for WHGame.dll mapping (already exists in
  `src/ldr_notify.cpp`; verify it fires synchronously inside the LoadLibraryW
  call). For each newly-mapped module the LDR notification fires; before_game
  patches/hooks declaring that module as their target apply — incl. the bugsplat
  fix's BugSplat64.dll target when WHGame's chain maps it. THEN WHGame's own DllMain
  runs (BugSplat init now sees the patched colon-free string).

## Depends on

11a (the shim must be ready before the VM is spun up in 11c).

## Test bar

The game boots with WHGame force-loaded from DllMain; the LDR notification fires per
mapped module; before_game targets apply at the right mapping. (A bad force-load
here AVs at startup — boot is the falsifiable observable.)

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 11b" + the DllMain
mechanism steps 1–13 in §"Phase 11".
