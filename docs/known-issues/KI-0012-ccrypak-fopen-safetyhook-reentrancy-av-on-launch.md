---
id: KI-0012
opened: 2026-06-08
status: open
commit_at_filing: 369a99cca57b3094c90a03a28fa5a8f1b370c185
---

# Launch AV — `engine.ccrypak_fopen` safetyhook-installed hook re-enters itself thousands of times → ACCESS_VIOLATION

**Status:** open

## Summary

A launch to acceptance-test the Phase-3 `kcdx.dll` crashed with an ACCESS_VIOLATION
during boot. The fault is a **re-entrancy spiral on the `engine.ccrypak_fopen` hook**
— a safetyhook-installed detour on `CCryPak::FOpen` that re-enters itself thousands of
times (`hook_chain: re-entrant dispatch depth=2`, then a `FAULTED_FIRE` stack of ~89
`ccrypak_fopen` fires with `seq` counting down from 17844) before the AV fires inside
WHGame.dll. The faulting subsystem is the **hook backend / asset-system FOpen hook**,
NOT the survival/verification code the launch was deploying. Filed-only (no
investigation, no fix) per `/report-bug`; `/debug KI-0012` picks it up.

**Attribution is unresolved** (recorded honestly, not assigned): the deployed
`kcdx.dll` was built from a tree (`369a99c`) interleaving TWO lanes — the Phase-3
survival work (the launch's nominal purpose) AND the just-landed hook-backend-marriage
Phase-6 rewiring (`aecc2de`/`ec7cae5`/`bda7b90` + earlier), which moved `ccrypak_fopen`'s
install to `safetyhook_backend`. The crash is in the safetyhook-installed FOpen detour
(the hook-backend's domain), but the survival deploy is what triggered this launch. The
other chat (hook-backend) states it was not their change; this record does not assign
blame — it captures the evidence so `/debug` can root-cause the actual mechanism.

## Reproduction

- Launch KCD2 from Steam with the `369a99c`-built `kcdx.dll` deployed (engine DLL at
  `<game-bin>/kcdx-engine/kcdx.dll`), `dev_mode = true`, with the cap-84 / cap-85
  test-suite plugins deployed.
- Crash during boot (before menu). A 96 MB minidump is written.
- Failing session: `2026-06-08_20-48-29` (logs + dump under
  `<game-bin>/kcdx-engine/logs/`).

## Evidence

- **The AV:** `[GUARD] FAULTED site=unhandled plugin=(none) code=ACCESS_VIOLATION
  rip=0x7FF986B8E220 module=WHGame.DLL module_rva=11723296 (0x0B2E220) tid=50472`
  (`kcdx-dev_2026-06-08_20-48-29.log` tail).
- **The re-entrancy spiral (the proximate cause):** immediately before the AV, repeated
  `[LEGACY] hook_chain: re-entrant dispatch depth=2 at 0x00007FF9864C14A0` (the
  `ccrypak_fopen` VA) and `0x00007FF98677A5A4`, then a `FAULTED_FIRE` stack of ~89
  fires `plugin=kcdx hook=engine.ccrypak_fopen va=0x7FF9864C14A0` with `seq` counting
  DOWN from 17844 — i.e. thousands of re-entrant `FOpen` calls (a runaway recursion,
  consistent with a stack-overflow → AV).
- **The hook is safetyhook-installed (the attribution-relevant fact):**
  `[20:48:32.716][LEGACY] safetyhook_backend 'engine.ccrypak_fopen': installed at
  0x00007FF9864C14A0 (detour=0x00007FF9860502E0, original=0x00007FF98604004A)` +
  `[hook 'engine.ccrypak_fopen'] post-install bytes at target: E9 AF EB B7 FF`. The
  `safetyhook_backend` install path is the hook-backend-marriage work (the
  MinHook→safetyhook rewiring landed in this DLL), NOT survival code.
- **The survival/verify code is present but not yet confirmed as the trigger:** 26
  survival-related log lines appear in the session; cap-84-survival-dispatch and
  cap-85-survival-agreement were ENABLED/taken-over (idx 84/85 in MOD_ABSORB at
  20:48:32.647) — but whether the survival-verify startup pass or the self-tests RAN
  (and whether any survival file-I/O interleaved with the `ccrypak_fopen` spiral) is
  NOT established here (a `/debug` investigation owes the ordered timeline: did
  survival run before the spiral started, and did any survival on-disk-file-open route
  through `ccrypak_fopen`?).
- **The dump:** `kcdx_2026-06-08_20-48-29.dmp` (96,372,754 bytes) — readable with the
  cdb one-liner for the faulting stack (`.ecxr; !analyze -v; k 30; q`).

## Open questions (for /debug — not answered here)

- Did the survival-verify startup pass RUN before the `ccrypak_fopen` spiral, and could
  its on-disk WHGame.dll file-open (`CreateFileW`/`GetModuleFileNameW`) have re-entered
  the FOpen hook to seed the recursion — OR is the re-entrancy independent of survival
  (a safetyhook-FOpen-detour self-recursion the hook-backend rewiring introduced)?
- Is `0x7FF9864C14A0` (the hooked FOpen) recursing because the safetyhook detour's
  `original`-call path itself routes back through the detour (a backend-install defect),
  or because a consumer (asset resolution, a plugin, the survival on-disk read) calls
  `FOpen` from inside the FOpen callback?
- What is at WHGame.dll `0x0B2E220` (the AV rip) — is the AV the stack-overflow endpoint
  of the recursion, or a separate fault the recursion led to?
- Bisect: does the crash reproduce with ONLY the survival commits (`8008e3d`..`ffc51ae`)
  deployed (the hook-backend Phase-6 reverted), or ONLY the hook-backend commits (survival
  reverted)? That cleanly attributes the lane. (A `git worktree` time-bisect per
  `results-driven.md` / the `/debug` probe-shapes.)

## Resolution

(pending — `/debug KI-0012`: root-cause the `ccrypak_fopen` re-entrancy mechanism; the
bisect above cleanly settles survival-vs-hook-backend attribution before any fix.)
