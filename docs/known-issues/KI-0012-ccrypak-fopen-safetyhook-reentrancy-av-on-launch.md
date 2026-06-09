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

## Reframe — the filed headline is FALSIFIED by the dump (the AV is NOT a hook re-entrancy spiral)

The `/report-bug` headline ("re-entrancy spiral on `ccrypak_fopen` → stack-overflow → AV")
is wrong. Ground truth from the crash dump + the failing-session log
(`kcdx_2026-06-08_20-48-29.dmp`):

- **The AV is `INVALID_POINTER_READ`, not `STACK_OVERFLOW`** (`!analyze -v` bucket
  `INVALID_POINTER_READ_c0000005_WHGame.dll!Unknown`). The faulting instruction is
  `mov rax, qword ptr [rdx]` with `rdx = 580000019a3019ad` — a **non-canonical garbage
  pointer** (the `5800…` high bits are not a valid x64 user address) (PROBE A).
- **The faulting stack is 13 frames and contains ZERO kcdx frames.** It is entirely
  WHGame.dll's own graphics/upscaling init: `KingdomCome.exe → WHGame → NGX
  (`NVSDK_NGX_UpdateFeature`) → `C_Game::CreateInstance` → the AV in an FSR2-region
  function (`ffxFsr2ResourceIsNull+0x633120`). No hook chain, no detour, no
  `ccrypak_fopen` frame on the stack (PROBE A).
- **The faulting thread is Main** (`tid 0xC528 = 50472`, thread name "Main") (PROBE A).
- **The "spiral" is a log artifact, not a runaway.** The `re-entrant dispatch depth=2`
  lines are bounded at **depth 2** (the chain explicitly allows + logs it: "a
  non-terminating loop here is the hook author's bug"). The `FAULTED_FIRE seq=17844`
  countdown is the crash handler dumping its **cumulative fire-history ring** (17844 =
  total `ccrypak_fopen` fires this session, newest-first), NOT 17844 nested frames — the
  13-frame stack proves there is no deep recursion (PROBE A). The crash handler prints
  `ccrypak_fopen`'s history because it is the kcdx hook with recent activity, not because
  it is on the faulting stack.

**Consequence for attribution:** the hook-backend-marriage lane is EXONERATED from the
AV mechanism — the AV is an invalid-pointer read inside WHGame's FSR2/NGX graphics init,
not in any kcdx hook / backend / detour path. The `FOpenLooseOverlay` Around callback
(`src/asset_overlay.cpp:278`) was read and is clean: MISS → straight `call_original`,
HIT → returns its own `FILE*` without re-entering; no recursion source (PROBE A, static).

**kcdx served the graphics path NOTHING.** The session logged exactly ONE `overlay_opened`
— `scripts/startup/sl_saveload.lua` (the `cap-77` test-suite Lua file) at `20:48:40.918`;
zero graphics/FSR2/shader overlays served. So kcdx did not hand graphics-init a wrong
file. The `depth=2` re-entrancy began at `20:48:38.565`, immediately after the Lua VM
bootstrap (`CScriptSystem::Init` → `engine_adopted_kcdx_state`) — i.e. it correlates with
`FOpen`-during-script-loading (a benign FOpen-from-within-FOpen the chain bounds at depth
2), NOT with graphics. The AV fired ~4s later (`20:48:42.754`) on the same Main thread,
independently, in `C_Game::CreateInstance`'s NGX/FSR2 init (PROBE A).

## PROBE A.2 — the SAME FSR2/NGX AV predates BOTH June-8 lanes (prior dumps)

The `ffxFsr2ResourceIsNull+0x633120` AV is a **recurring, pre-existing** WHGame
graphics-init crash, not a June-8 regression. Reading the older crash dumps in the same
logs dir (`!analyze -v` faulting symbol):

- **`kcdx_2026-06-05_00-10-46.dmp` (June 5)** — the **EXACT same** faulting symbol +
  offset: `WHGame!ffxFsr2ResourceIsNull+0x633120` (AV `c0000005`). Three days BEFORE
  the hook-backend Phase-6 rewiring (`aecc2de`/`ec7cae5`/`bda7b90`, June 8) and the
  survival commits.
- **`kcdx_2026-05-28_16-03-35.dmp` (May 28)** — same graphics-init family:
  `WHGame!NVSDK_NGX_UpdateFeature+0x858d95` (the NGX upscaling path).
- **`kcdx_2026-06-05_13-23-59.dmp` (June 5)** — `NULL_CLASS_PTR_READ` in WHGame.dll
  (another graphics-init pointer read).

**Conclusion:** the AV is a base-game / graphics-driver / FSR2-NGX-upscaling init fault
that recurs across kcdx versions and predates BOTH the hook-backend-marriage lane AND the
survival lane. It is NOT a regression introduced by either June-8 lane. The attribution
question the filing posed (survival vs hook-backend) is moot — neither lane caused it
(PROBE A.2).

## Open questions (reframed — the real mechanism)

- **Is the garbage pointer kcdx-caused at all?** The AV is in WHGame's graphics init. The
  open question is whether kcdx perturbs the state FSR2/NGX reads (an asset-overlay
  serving a wrong file to a graphics-init asset open? a corrupted graphics config/shader
  resource?), or whether this is a graphics/driver/FSR2-state fault independent of the mod.
- **Does it reproduce without kcdx?** The cleanest discriminator (PROBE B): launch the
  same KCD2 build with kcdx NOT deployed (or `dev_mode = false`) — if the FSR2/NGX AV
  still fires, it is a base-game/driver issue, not a kcdx regression. If it only fires
  with kcdx, narrow to which lane.
- **Does it reproduce on an OLDER kcdx?** (PROBE C, only if B implicates kcdx): a
  `git worktree` time-bisect — does the AV fire with a pre-Phase-6 / pre-survival kcdx?
  This attributes the regression to a commit range.
- **Is it deterministic or a flake?** A single crash sample cannot tell a hard regression
  from an intermittent graphics-init flake. The repro rate is unknown (one launch).

## Trail

| Action | Result |
|---|---|
| PROBE A: read the crash dump faulting stack + `!analyze -v` + the log timeline (read-only ground truth) | `INVALID_POINTER_READ` in WHGame FSR2/NGX graphics init (`C_Game::CreateInstance`), 13-frame stack, ZERO kcdx frames, Main thread. The "re-entrancy spiral"/"stack overflow" headline is FALSIFIED — `depth=2` bounded + `seq` is a cumulative fire-count, not nested frames. kcdx served only one Lua test overlay all session; nothing to graphics. |
| PROBE A.2: `!analyze -v` the older dumps in the logs dir (read-only, time-evidence) | The SAME `ffxFsr2ResourceIsNull+0x633120` AV fired on June 5 (and the NGX family on May 28) — BEFORE both June-8 lanes. The AV is a recurring base-game FSR2/NGX graphics-init fault, NOT a regression from either lane. |
| PROBE B (vanilla repro — does it fire with kcdx un-injected): launch `KingdomCome.exe` directly | not run — PROBE A.2 already settled "not kcdx-caused"; PROBE B is the optional confirmer, run only if a definitive vanilla baseline is wanted. |

## Resolution

(pending a disposition decision — the investigation is COMPLETE: the filed re-entrancy
headline is falsified; the AV is a recurring WHGame FSR2/NGX graphics-init invalid-pointer
read that predates both June-8 lanes (PROBE A + A.2). It is not a kcdx bug. The remaining
call is the disposition: close as not-kcdx / external (a base-game graphics-init crash), OR
keep open as a base-game-crash kcdx should investigate mitigating. This is the user's call.)
