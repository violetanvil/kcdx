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

## PROBE A.2 — WITHDRAWN (the reasoning was flawed; user reports a clean boot TODAY before the changes)

PROBE A.2 originally argued the FSR2/NGX AV is a pre-existing base-game crash because the
same `ffxFsr2ResourceIsNull+0x633120` symbol appears in June-5/May-28 dumps. **That
reasoning is WRONG and is withdrawn:**

- Those older dumps were ALSO kcdx-loaded sessions (every dump in `kcdx-engine/logs/` is a
  kcdx launch). "kcdx crashed in FSR2 before" does NOT prove "the BASE GAME crashes in
  FSR2" — it only shows kcdx-loaded sessions have hit this region before. The
  base-game-exoneration was an unsupported leap from a symbol match.
- **Decisive correction (user ground truth):** the game **booted CLEANLY today, BEFORE the
  changes, with NO game patch.** A clean boot today + a crash today on the same base game
  means SOMETHING in today's deployed DLL changed between the two — and the only thing that
  changed is the AGENTS' engine code (the day's interleaved `src/` work), not the base game.
  A same-day clean→crash transition with no game patch IS a regression in today's code, by
  definition.

**The hook-backend swap is BACK IN SCOPE as the prime suspect.** `8a02bd8` (14:38 today,
"SafetyhookBackend on the function-entry path") moved `ccrypak_fopen`'s function-entry
install from MinHook to safetyhook. The crash DLL (`369a99c`) carries this swap. "Zero kcdx
frames on the faulting stack" does NOT exonerate it — a detour that changes what `FOpen`
returns, or how the prologue is relocated for the `call_original` trampoline, can feed a
consumer (incl. graphics init reading an asset/config via FOpen) a bad pointer that faults
LATER, off the kcdx stack. The investigation re-opens on this axis.

## Engine-code suspects in the crash DLL (369a99c) — today's interleaved `src/` lanes

The crash DLL was built from a tree interleaving THREE lanes' engine changes today
(chronological, `src/`-touching):
- **hook-backend** (the prime suspect — it changed the detour backend under EVERY
  function-entry hook incl. `ccrypak_fopen`): `64fba7d` IDetourBackend+MinHookBackend →
  `ed9ff7f` relocate seam to InstallRuntime + dissolve detour_hook → **`8a02bd8`
  SafetyhookBackend on function-entry (the MinHook→safetyhook swap)** → `6a3d15b` routing
  predicate → `aabd37f` make_jit_midfunc→MidHook → `1b6500c`/`847e573`/`aca788e`
  foreign-hook → `aecc2de`/`ec7cae5`/`bda7b90` comment/AP16 scrubs (no-behavior).
- **survival/verification** (`8008e3d` 3.1 → `3c5e065` 3.2 → `69c7cc2` 3.3 startup pass +
  on-disk hash → `ffc51ae` 3.4) — touches `survival.cpp` + a startup verification pass that
  reads WHGame.dll on disk.
- **statement-layer** (`f26c819`/`abdbee3`/`a60e63b`) — `refdb` eager-load + a self-test.

The clean→crash transition isolates to this commit set. The bisect (PROBE C) splits it.

## PROBE A.3 — the faulting instruction is a pointer-copy fed a corrupt SOURCE pointer (mechanism shape)

Disassembling the faulting site (`WHGame!ffxFsr2ResourceIsNull+0x633120`) shows a tiny leaf
copy helper:
```
mov rax, qword ptr [rdx]   ; load 8 bytes from [rdx]  ← AV: rdx is garbage
mov qword ptr [rcx], rax   ; store to [rcx]
mov rax, rcx ; ret         ; return rcx  (a *dst = *src primitive)
```
- `rdx` (the SOURCE) = `0x580000019a3019ad` — garbage. `rcx`/`rax`/`rdi` (the DEST + frame)
  are all valid stack addresses (`0x565350e…`). So the DESTINATION is fine; the SOURCE
  pointer handed to this copy is corrupt (PROBE A.3).
- The garbage value's fingerprint: the low 5 bytes `0x019a3019ad` look like a **plausible
  heap pointer** (cf. `r12 = 0x0000019a25b23594`, the same `0x19a…` heap region), with a
  high byte `0x58` bolted on making it non-canonical. A valid-looking `0x019a…` pointer
  wearing a `0x58` high byte is the signature of a value that was MIS-PRODUCED — e.g. a
  64-bit return value corrupted in its high bits, then used as a pointer by a consumer.

**This fits the `ccrypak_fopen` Around-return mechanism's failure mode.** `ccrypak_fopen`
is an Around hook whose cFn return value BECOMES `FOpen`'s result for every caller
(`asset_overlay.cpp:379` — the JIT writes the returned ptr into FOpen's rv slot). If the
MinHook→safetyhook swap (`8a02bd8`) changed how the Around return / `call_original`
trampoline marshals the 64-bit return (a high-bits clobber, a wrong rv-slot width, a
trampoline-relocation that returns a bad value), then a consumer that opens a graphics
resource via FOpen during `C_Game::CreateInstance` gets a corrupted handle/pointer and
faults deref'ing it — OFF the kcdx stack, LATER. This is a falsifiable hypothesis; the
bisect (PROBE C) tests it directly.

## PROBE A.4 — the corrupt pointer is a GRAPHICS-config value, NOT a FILE* (the FOpen-return hypothesis is falsified)

Tracing the corrupt `rdx` back through the caller's disassembly + the dump memory map:

- **Origin (caller disasm, `C_Game::CreateInstance+0x30674`):** the garbage value was already in
  `rdx` when this graphics-config function was entered. It is `mov rbx,rdx` (saved) BEFORE the
  `NVSDK_NGX_UpdateFeature+0x13380` (DLSS/NGX) call, then `mov rdx,rbx` (restored) and passed to
  the `*dst=*src` copy helper that faults. So `rdx` is an **incoming argument** to this graphics
  function, threaded down from the NGX/FSR2 init chain (frames 02-09), NOT produced locally
  (PROBE A.4).
- **The value is wholly invalid, not a tainted-good-pointer.** `!address 0x0000019a3019ad` (the
  low part, stripping the `0x58` high byte) is UNMAPPED (`????`), same as the full
  `0x580000019a3019ad`. So it is NOT "a valid `0x019a…` heap pointer with a corrupted high byte" —
  the whole qword is junk (or a non-pointer value being deref'd as a pointer). By contrast
  `r12 = 0x0000019a25b23594` IS a valid 1 KB heap region, so the graphics code holds valid
  `0x019a…`-family pointers nearby; the faulting `rdx` is a different, bad value (PROBE A.4).

**Consequence:** the PROBE A.3 hypothesis (a corrupted `ccrypak_fopen` Around-return used as a
pointer) is FALSIFIED — the corrupt pointer is a graphics-config value in the DLSS/FSR2 init path,
not a `FILE*`. The `ccrypak_fopen` Around-return marshaling (`dynamic_call_jit.cpp` `StoreReturn`)
is ALSO backend-independent (shared JIT, unchanged by the swap), further weakening "the swap
corrupted a return". What the swap DID change is `get_original()` (safetyhook's relocated
trampoline vs MinHook's `pOriginal`) — but that affects FOpen's own call-original, not a graphics
pointer.

**Dump ceiling reached.** This is a minidump (registers + stack + partial memory); the heap is not
fully committed, so the corruption ORIGIN cannot be traced further statically (the search/walk hits
unmapped regions). Establishing CAUSATION (is kcdx upstream of this graphics-pointer corruption at
all?) requires the bisect + live instrumentation, not more dump reading.

## Where the investigation stands (honest)

- CONFIRMED (dump): the AV is an invalid-pointer read of a GRAPHICS-config value in WHGame's
  DLSS/FSR2 init on the Main thread; no kcdx frame on the stack; the "spiral"/"stack-overflow"
  headline is a log-reading artifact (PROBE A, A.4).
- CONFIRMED (user): a clean boot today before the changes, no game patch → a same-day regression in
  today's deployed engine code (not the base game).
- NOT YET CONFIRMED: WHICH lane/commit regressed it, and the MECHANISM. The FOpen-return hypothesis
  is falsified; the live suspects are now broader (any of today's engine changes that could perturb
  graphics-init state — a hook mis-firing, a heap/allocation perturbation, an asset/overlay or
  survival-startup interaction). The bisect (PROBE C) is the REQUIRED next step to attribute the
  lane before any mechanism theory.

## Reframe after PROBE C — the swap is exonerated; re-observe, don't theory-hop

PROBE C cleanly killed the safetyhook-swap theory (the AV is byte-identical with `ccrypak_fopen`
on MinHook). Per the discipline, re-observe ground truth rather than jump to the next suspect.
Established facts now:

- The AV is a stable, reproducible `INVALID_POINTER_READ` of a garbage pointer in WHGame's
  DLSS/FSR2 graphics init (`C_Game::CreateInstance → NVSDK_NGX_UpdateFeature → FSR2`), Main thread,
  no kcdx frame, deterministic across two launches (different garbage address each run = ASLR/heap
  layout, same corruption shape) (PROBE A, A.4, C).
- It is NOT the hook backend (PROBE C). The remaining today's-engine-work suspects that could
  perturb graphics-init / process state:
  - **survival-startup (`69c7cc2`, 3.3)** — a startup verification pass that READS + HASHES
    WHGame.dll on disk + walks reachability. Touches process/module state during init; STRONG
    suspect (timing, a module read, an allocation, a thread).
  - survival 3.1/3.2/3.4 (`8008e3d`/`3c5e065`/`ffc51ae`) — per-kind dispatch + static checks +
    the JS↔C++ agreement pin.
  - statement-layer (`f26c819`/`abdbee3`/`a60e63b`) — refdb eager-load + a self-test.
  - The non-backend hook-backend changes (foreign-hook detection `1b6500c`/`847e573`, the mid
    adapter `aabd37f`) — less likely (they don't touch graphics), but not yet excluded.

NEXT bisect (PROBE D): establish a clean known-good anchor + narrow the lane. Two options —
(a) build at the FIRST engine commit of today (or last-known-good before today) and confirm clean
(proves "today regressed it", gives the bisect floor), then binary-search the today's-commit range;
or (b) disable the strongest single suspect (the survival startup pass) on the current tree and
re-launch (one-variable, like PROBE C). Option (b) is cheaper if the survival-startup suspicion is
right; (a) is the definitive bisect if (b) misses.

## PROBE C.2 — the re-entrancy is `ccrypak_fopen` ⟷ `lua_pcall`, a pre-existing benign pattern (red herring)

The `depth=2` re-entrant dispatch alternates between TWO VAs: `ccrypak_fopen` and a second hook
at `…8EA5A4` = **`engine.lua_pcall`** (the foundational Lua-bridge hook, present for many
versions — `detour_hook 'engine.lua_pcall': installed at …8EA5A4`). So the "spiral" is
`ccrypak_fopen ⟷ lua_pcall` ping-ponging during Lua script loading (a script open routes through
FOpen; FOpen's path touches Lua) — a NORMAL, depth-bounded, pre-existing pattern on every boot, NOT
a runaway and NOT new. The re-entrancy is fully a red herring; both hooks are old + exonerated
(PROBE C.2).

## PROBE C.3 — the launch-log boundary pins the regression window to the SURVIVAL lane

The session logs (`kcdx-dev_2026-06-08_*.log`, grep `FAULTED`) give the exact clean→crash boundary
today:

- `15:18`, `16:53`, `17:43`, `17:49`, `18:28`, **`18:34` — all CLEAN** (FAULTED=0; the last clean
  boot).
- `20:48` (the KI crash) + `21:41` (PROBE C) — **CRASHED.**

So the regression landed in the engine code deployed between the `18:34` clean boot and the `20:48`
crash. The commits in that window, by whether they change behavior:

- **My hook-backend commits (`aecc2de` 19:16, `ec7cae5` 19:41, `bda7b90` 19:54) are COMMENT/AP16-
  scrub ONLY** — zero behavior change (and PROBE C already proved the backend swap is irrelevant).
- **The SURVIVAL lane is the only substantive engine change in the window:** `8008e3d` (3.1 —
  `hooks.cpp` + `survival.{cpp,h}` + `survival_pass.cpp`), `3c5e065` (3.2), **`69c7cc2` (3.3 — a NEW
  310-line `src/survival_verify.{cpp,h}` startup verification pass + `src/pe_helpers.{cpp,h}` PE-
  header parsing + `refdb.cpp`)**, `ffc51ae` (3.4).

`survival_verify` + `pe_helpers` is exactly the shape that can corrupt graphics-init: a startup pass
that PARSES a loaded module's PE headers + walks/reads process memory during init. If it mis-reads,
writes out of bounds, or perturbs a structure FSR2/NGX later reads, the garbage graphics pointer
follows — off the kcdx stack, in `C_Game::CreateInstance`. **This is the other chat's lane** (not
mine); attribution is now strongly the survival-verification work, pinned by the launch-log boundary
+ the mechanism shape.

## PROBE D — disable the survival startup verification pass (one variable, current tree)

The decisive next probe: on the current crash-DLL tree, DISABLE the survival startup verification
pass (the `69c7cc2` 3.3 entry point) and re-launch — one variable, like PROBE C.
- **Boots clean** → the survival startup verification pass IS the cause; hand the mechanism +
  attribution to the survival lane for the fix.
- **Still crashes** → survival-verify is exonerated too; widen to a full known-good bisect at
  `3b99fea` (the last pre-today engine state — what this morning booted clean) to confirm code-vs-
  environment.

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
| PROBE A.2: `!analyze -v` the older dumps (claimed pre-existing base-game crash) | **WITHDRAWN — flawed reasoning.** Older dumps are also kcdx sessions; a symbol match does not prove base-game. User reports a CLEAN boot today before the changes (no game patch) → a same-day clean→crash IS a regression in today's code. The hook-backend swap is back in scope. |
| PROBE A.3: disassemble the faulting site (read-only) | A `*dst=*src` 8-byte copy helper; the SOURCE pointer (`rdx=0x580000019a3019ad`) is corrupt while DEST/frame are valid. The garbage = a plausible `0x019a…` heap ptr with a `0x58` high byte — the signature of a mis-produced 64-bit value used as a pointer. Fits the `ccrypak_fopen` Around-return failure mode. |
| PROBE A.4: trace the corrupt `rdx` back via caller disasm + `!address` (read-only) | The corrupt value is a GRAPHICS-config pointer threaded from the NGX/FSR2 init chain (an incoming arg, saved across the NGX call), NOT a `FILE*`. The whole qword is unmapped (not a tainted-good-pointer). FALSIFIES the FOpen-return hypothesis (A.3). Dump ceiling reached — causation needs the bisect. |
| PROBE C (one-variable bisect of the safetyhook swap): on the SAME crash-DLL tree, force `select_backend(ChainFunctionEntry)` → MinHook (a 1-line routing diagnostic in a worktree, everything else identical); deploy, user launches | **CRASHED IDENTICALLY.** With `ccrypak_fopen` installed via MinHook (`detour_hook`, confirmed in the log — not `safetyhook_backend`), the AV is byte-for-byte the same: `mov rax,[rdx]` at `ffxFsr2ResourceIsNull+0x633120`, `INVALID_POINTER_READ`, same `CreateInstance→NGX→FSR2` stack, garbage `rdx`. The safetyhook function-entry swap is **EXONERATED** — it is NOT the cause. |

## Resolution

(RE-OPENED — the earlier exoneration was WRONG. User ground truth: a clean boot today
before the changes, no game patch → a same-day regression in today's deployed engine code.
The prime suspect is the MinHook→safetyhook swap of `ccrypak_fopen`'s Around hook
(`8a02bd8`), with a concrete falsifiable mechanism (a corrupted 64-bit Around-return used
as a pointer by a graphics-init FOpen consumer — PROBE A.3). PROBE C (bisect the swap) is
the next step.)
