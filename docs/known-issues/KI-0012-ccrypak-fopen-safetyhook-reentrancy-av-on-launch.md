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

## PROBE D — bisect the WHOLE survival lane out (build at `288e8fa`, pre-survival)

Refined from "disable the self-tests" to the more decisive form: the survival self-tests were
`PENDING` (not yet run) at crash time, so disabling just them could be inconclusive — the survival
lane's LOAD-TIME footprint (`survival.cpp:50` `GetModuleHandleW(WHGame.dll)` + `pe_helpers` PE-header
parsing, the registration path) is also a suspect. So PROBE D builds at **`288e8fa`** — the last
commit BEFORE survival 3.1 (`8008e3d`). This tree has ALL of hook-backend (through Phase 5) + the
statement-layer + foreign-hook work, but ZERO survival 3.x code (`survival_verify.cpp` absent,
confirmed). One variable removed: the entire survival lane (`8008e3d..ffc51ae` engine code).
- **Boots clean** → the survival lane IS the cause; attribution + the mechanism hunt hand to the
  survival chat for the fix (a load-time PE-parse / memory-walk that corrupts graphics-init state).
- **Still crashes (same FSR2 AV)** → survival is exonerated too; the regression predates survival,
  in the hook-backend Phase 1-4 BEHAVIORAL changes (the backend seam / foreign-hook / mid adapter) —
  bisect the earlier window. (My comment-scrubs are zero-behavior, already excluded by PROBE C.)
- DEPLOYED, awaiting launch.

## PROBE D RESULT — the survival lane is EXONERATED too (pre-survival build crashes identically)

Built at `288e8fa` (zero survival 3.x code, `survival_verify.cpp` absent), deployed 21:54, relaunched
→ **byte-for-byte identical crash** (21:57): `mov rax,[rdx]` at `ffxFsr2ResourceIsNull+0x633120`,
garbage `rdx`, the same `CreateInstance+0x306c3 → NGX → FSR2` stack, `INVALID_POINTER_READ`. Removing
the ENTIRE survival lane changed nothing. The launch-log boundary (18:34 clean → 20:48 crash) was
real but its survival-lane attribution is WRONG — the survival code is not the cause.

**Three things now exonerated by direct probe:** the safetyhook function-entry swap (PROBE C), the
survival lane (PROBE D). The crash reproduces at `288e8fa`, which still contains ALL of today's
hook-backend Phase 1-5 BEHAVIORAL work (the backend seam, foreign-hook detection/chaining, the
make_jit_midfunc→MidHook adapter) + the statement-layer. Two live hypotheses remain:
1. The hook-backend Phase 1-5 behavioral changes regressed it (present at `288e8fa`).
2. It is NOT a code regression — the 18:34 "clean" boot was a FLAKE (an intermittent graphics-init
   AV), and the crash is environmental / pre-existing. (The 2/2-then-3/3 determinism today argues
   against a flake, but a build that flips clean↔crash run-to-run would not have shown it yet.)

## PROBE E — the pre-today known-good bisect (build at `3b99fea`, June 5)

The decisive split between hypotheses 1 and 2: build at `3b99fea` (the last pre-today engine commit —
the engine state this morning's clean boots ran, BEFORE any of today's hook-backend / statement /
survival work) and launch.
- **Boots clean** → today's hook-backend Phase 1-5 behavioral work regressed it (hypothesis 1);
  bisect the `9de81ea..288e8fa` hook-backend range next.
- **Crashes (same FSR2 AV)** → it is NOT today's code at all (hypothesis 2) — the crash predates
  today / is environmental / is a flake. The fix axis is then a graphics-settings / driver / FSR2
  angle, and code-bisecting today's range is pointless. The launch-clean history would then be luck,
  not a real known-good.

## PROBE F — the improved crash log (KI-0013) characterizes the garbage: an UNINITIALIZED / wrong-struct read

With KI-0013's full crash logging live, the AV's nature is now readable from the log directly (no
minidump dive). Across the FOUR reproductions, the faulting source pointer (`rdx`, = `rbx`) is:
- run 20-48: `0x580000019a3019ad`
- run 21-41: `0x0000000020c00fdd`
- run 21-56: `0xe6000002 0c124023`
- run 22-15: `0x670000017884B334`

**The high bits vary wildly run-to-run (`0x58` / `0x00` / `0xe6` / `0x67`)** — this is NOT a
consistent corrupted pointer (a fixed-offset overwrite would land a stable wrong value). It is the
signature of an **uninitialized / wrong-structure read**: the graphics code (`ffxFsr2ResourceIsNull
+0x633120`, a `*dst=*src` 8-byte copy) is handed a SOURCE pointer field that holds whatever garbage
is at that address that boot — i.e. either a structure read at the wrong base/offset, or a field
never initialized before this DLSS/FSR2 path consumed it. The full 20-frame `FAULTED_FRAME` chain is
entirely WHGame graphics init (`CreateInstance → NGX (NVSDK_NGX_UpdateFeature) → FSR2`) →
`KingdomCome.exe` → `KERNEL32`/`ntdll`; no kcdx frame anywhere. `FAULTED_INVENTORY = (inventory not
yet captured)` — the crash is EARLY, before kcdx records its modification inventory.

**Where this leaves KI-0012 (honest):** the AV is a deterministic uninitialized/wrong-struct read in
WHGame's DLSS/FSR2 upscaling init. THREE lanes exonerated by direct probe (the safetyhook swap, the
survival lane); the crash reproduces at `288e8fa` (all hook-backend Phase 1-5 behavioral work
present). The next probe (PROBE E, build at `3b99fea`, pre-today) still splits "today's hook-backend
behavioral work regressed it" vs "not a code regression (the graphics-init state kcdx perturbs is a
DLSS/FSR2 init-order / config issue, or environmental)". The crash log is now good enough that the
remaining work is a focused lane-attribution, not a blind dump-archaeology grind.

## PROBE E RESULT — pre-today code (June 5) crashes IDENTICALLY → NOT a kcdx code regression

Built at `3b99fea` (the last pre-today engine commit, June 5 — a week older than today, before ALL
of today's work across every lane; safetyhook_backend absent, MinHook-era `hook_chain` confirmed
installing `ccrypak_fopen`), deployed, relaunched → **the SAME FSR2 AV**: `ACCESS_VIOLATION` in
`WHGame.DLL` at `module_rva=11723296` (`0xB2E220` = `ffxFsr2ResourceIsNull+0x633120`), identical to
all four prior crashes. The crash reproduces on code from BEFORE today entirely.

**This exonerates today's code completely.** Every lane is now killed by direct probe: the safetyhook
swap (C), the survival lane (D), and now ALL of today's code (E — the crash predates it). The AV is
NOT a kcdx code regression — it reproduces across a week of kcdx versions.

**The remaining variable is ENVIRONMENTAL.** The user reported a clean boot THIS MORNING before the
changes. If even June-5 code crashes now, the morning-clean → now-crash difference is not in any
kcdx code — it is something on the machine that changed today (a GPU driver update, a graphics /
DLSS-FSR2 / upscaler setting, GPU memory/VRAM state, a Windows update, a config file), OR the
morning's clean boots were intermittent luck on a flaky graphics-init AV. The fault is in WHGame's
DLSS/FSR2 upscaling init reading an uninitialized/wrong-struct pointer (PROBE F) — a base-game
graphics-init defect kcdx does not touch (no kcdx frame on the 20-frame stack across every run).

**Disposition (for the user):** this is not a kcdx bug to fix in code. Options: (a) check/repair the
environment (GPU driver, graphics settings — esp. DLSS/FSR upscaling, which the faulting symbols name
directly; try disabling upscaling or resetting graphics config); (b) confirm with a fully vanilla
launch (KingdomCome.exe with kcdx un-injected) that the base game crashes the same way; (c) close
KI-0012 as an external/base-game graphics-init crash (not kcdx), keeping the full investigation record.

## REFRAME (user direction) — it IS kcdx; PROBE E tested the wrong variable

User: "it worked today. so it's something that's making us think it's that. something we hook or
something is changing an address the engine is trying to use. it is 100% kcdx." This is correct and
PROBE E does NOT contradict it — June-5 code is STILL kcdx (still hooks `ccrypak_fopen`, still does
the ModManager ctor takeover, still builds the Lua VM). "Pre-today code crashes too" means the kcdx
mechanism at fault is OLDER than today and present in every build tested — which is exactly why
swapping today's lanes (C/D/E) never moved it. The off-stack-corruption shape (no kcdx frame; the
engine reads a value kcdx perturbed earlier) fits "kcdx changes an address/value the engine's
graphics init later reads."

**PROBE E reinterpreted:** not "not kcdx" — it RULES OUT today's lane-swaps and points at a
LONG-STANDING kcdx mechanism. The variable to test is kcdx-vs-NO-kcdx, never yet run.

## Prime mechanism candidate — the ModManager ctor takeover (an RE'd field map)

`src/mod_absorb/ctor_bracket.cpp` `HookedCtor` FULLY replaces the engine's `C_ModManager` constructor
— "no original ctor call, no original SELECT call" (logged every boot). It allocates a `kObjectSize =
0x68` block via WHGame's allocator, `memset`s it to zero, and writes a REVERSE-ENGINEERED field map
(+0x00 vtable, +0x08 sys, +0x10 modsDir CryString, +0x30/0x38/0x40 enabled-list triple, +0x60 init
flag). The object itself is zeroed, so it is not a raw uninitialized read — BUT this is a hand-built
reconstruction from an RE'd layout. If the field map is INCOMPLETE or WRONG (the real C_ModManager is
larger than 0x68; a field at an offset kcdx does not know the engine later reads; the modsDir/sys/
enabled-list values differ subtly from the native ctor's), the engine reads a field kcdx got wrong
and faults downstream — and graphics init (or something it transitively reaches) could be that
consumer. The garbage-varies-per-boot signature (PROBE F) fits a field whose VALUE kcdx leaves zero/
wrong where the engine expects a live pointer. This is the strongest "kcdx changes an address the
engine uses" candidate; it is OLD (present in June-5), matching PROBE E.

(Other long-standing kcdx surfaces that perturb engine state, lower priority: the 4 engine hooks —
`ccrypak_fopen`, `ccrypak_adjustfilename`, `lua_pcall`, `lua_newstate`; the Lua VM build-and-adopt
keystone; any byte patch.)

## PROBE G — vanilla baseline (the foundation: kcdx vs NO kcdx)

Never actually run. Launch `KingdomCome.exe` with kcdx FULLY un-injected (no `kcdx.dll` loaded at
all — bypass `kcdx.exe`, or rename it so Steam's launch option no-ops to the raw game). This is the
clean kcdx-vs-no-kcdx test the whole investigation rests on:
- **Vanilla boots clean** → it is DEFINITIVELY kcdx (confirms the user's call); the ModManager ctor
  takeover is the prime mechanism to instrument next (probe what field the engine reads post-ctor
  that kcdx's 0x68 map gets wrong).
- **Vanilla ALSO crashes (same FSR2 AV)** → genuinely base-game/environmental after all. (The user's
  strong prior is that this will NOT happen — vanilla will be clean.)

## PROBE G RESULT — VANILLA BOOTS CLEAN → it is kcdx, 100%, CONFIRMED

Launched `KingdomCome.exe` with kcdx fully un-injected (no `kcdx.dll` loaded — kcdx injects only via
the `kcdx.exe` launcher, no auto-inject DLL). **Vanilla booted clean to menu, no crash** (user-
observed). Combined with: every kcdx build crashes — today's (run 1-4) AND a week-old June-5 build
(PROBE E) — and vanilla is clean (PROBE G).

**CONFIRMED: the FSR2/NGX boot AV is caused by kcdx.** Not the base game, not the environment, not
today's lanes specifically. It is a LONG-STANDING kcdx mechanism (present in June-5) that perturbs
state the engine's DLSS/FSR2 graphics init later reads — the off-stack-corruption shape (no kcdx
frame; the engine derefs a value kcdx changed earlier; the garbage varies per boot, PROBE F). Every
earlier "not kcdx" lean is dead; the user's call was right.

## The hunt — what kcdx does that the engine's graphics init reads (in priority order)

The mechanism is one of kcdx's long-standing engine perturbations. The crash is EARLY (inventory not
yet captured) — around the ModManager takeover / Lua VM bootstrap, BEFORE `C_Game::CreateInstance`'s
graphics init. Candidates, by how directly they "change an address the engine uses":

1. **ModManager ctor takeover (PRIME, `ctor_bracket.cpp` `HookedCtor`)** — kcdx fully replaces the
   engine's `C_ModManager` constructor with a reverse-engineered 0x68 field map (no original ctor
   call). If `kObjectSize=0x68` is too small, or a field the engine later reads lives at an offset
   kcdx doesn't write, or the modsDir/sys/enabled-list values differ from the native ctor's, the
   engine reads a wrong field. NEXT PROBE: instrument what the engine reads from the C_ModManager
   object (or the csys install slot it returns) AFTER the ctor, and compare a kcdx-built object's
   bytes vs what the native ctor would produce.
2. **The 4 engine hooks** (`ccrypak_fopen`, `ccrypak_adjustfilename`, `lua_pcall`, `lua_newstate`) —
   a hook whose trampoline/relocation or callback corrupts a value a graphics consumer reads.
   (`ccrypak_fopen` swap already exonerated by PROBE C, but the OTHER hooks / the chain itself not.)
3. **The Lua VM build-and-adopt keystone** — kcdx builds the VM and the engine adopts it; if a
   structure the engine expects from its own `lua_newstate` differs, a downstream read could fault.
4. Any byte patch / the allocator hook / the early_hook bugsplat install.

NEXT PROBE (H): the cheapest decisive narrowing is a HOOK-SET bisect — disable kcdx's engine hooks /
the ctor takeover one group at a time (one-variable, like PROBE C) and find which one, when removed,
makes the boot clean. Start by disabling the ModManager ctor takeover (the prime suspect) and
launching: clean → the takeover's field map is the bug (instrument it); still crashes → move to the
hook set.

## PROBE H RESULT — the ModManager ctor takeover IS the cause (CONFIRMED)

Disabled the ModManager ctor takeover (`InstallCtorBracket()` commented out, `dllmain.cpp:280`), so
the engine runs its OWN native `C_ModManager` ctor + SELECT (the documented fallback). One variable
changed; everything else in kcdx identical. **Result: NO CRASH — booted clean to menu.** The user
also observed "my mods/plugins didn't load from the Workshop" — which is the EXPECTED fallback (no
takeover → no kcdx mod-absorption → mods don't mount), confirming the diagnostic took effect.

**CONFIRMED: the cause of KI-0012 is `src/mod_absorb/ctor_bracket.cpp` `HookedCtor` — kcdx's full
replacement of the engine's `C_ModManager` constructor.** With the engine building its own object,
the FSR2/NGX graphics-init AV is gone. With kcdx's reverse-engineered 0x68-byte reconstruction, the
engine later reads a field kcdx got wrong/missing and faults in DLSS/FSR2 init. The bisect chain:
vanilla clean (G) · every kcdx build crashes (E) · takeover-off clean (H) → the takeover, specifically.

## The root-cause hunt — WHAT in the reconstructed C_ModManager is wrong

`HookedCtor` allocates `kObjectSize = 0x68` (104 bytes), `memset`s it to 0, and writes a hand-RE'd
field map: +0x00 vtable · +0x08 sys · +0x10 modsDir CryString · +0x30/0x38/0x40 enabled-list triple ·
+0x60 init flag. Slots +0x18/0x20/0x28 and +0x48/0x50/0x58 are deliberately left zero. The engine's
graphics/FSR2 init (or something it transitively reads off the C_ModManager / the csys[+0x2B30] slot
the ctor installs) reads a field this map gets wrong. Candidate root causes (one of these):
- **The object is LARGER than 0x68** — the real C_ModManager has fields past 0x68 the native ctor
  initializes; kcdx's undersized alloc means the engine reads PAST kcdx's block into adjacent heap
  (garbage that varies per boot — matches PROBE F exactly).
- **A field kcdx leaves zero (+0x18/0x20/0x28/0x48/0x50/0x58) is actually live** — the native ctor
  writes a pointer there the graphics path later derefs; kcdx's zero → a null/garbage deref.
- **A written field has a wrong value** — the modsDir CryString, the sys ptr, or the enabled-list
  triple is subtly off in a way a graphics consumer chokes on.

NEXT (root cause): disassemble the native `C_ModManager` ctor (refdb `ModManager_ctor`) to recover
the TRUE object size + the COMPLETE field-init map, and diff against kcdx's 0x68 reconstruction —
the missing/wrong field IS the bug. Reuse-first per `reverse-engineering.md` (refdb row → prior
`_research` → Ghidra). The crash being in graphics init that reads a C_ModManager field is itself a
checkable RE question (what does the FSR2/CreateInstance path read off the modMgr / csys slot).

## ROOT CAUSE (PROBE I — static RE, prior `_research/init-cycle-recon`) — kcdx leaves the SCANNED-LIST (+0x18/+0x20/+0x28) NULL

The native `C_ModManager` ctor + its inline SELECT driver populate TWO vectors in the 0x68 object
(`_research/init-cycle-recon/FINDINGS.md`, live-probed in two boots, POINT B vs POINT C):
- **scanned-list** at +0x18/+0x20/+0x28 — a `std::vector<I_Mod>` of **0x70-stride INLINE records**
  (the actual mod records; SELECT's "scan `mods/` + Steam-workshop" pass writes it: 7 records boot 1,
  15 boot 2).
- **enabled-list** at +0x30/+0x38/+0x40 — a `std::vector<I_Mod*>` (8-byte pointers into the scanned
  records; SELECT's "read mod_order + enable" pass writes it).

**kcdx's `HookedCtor` populates ONLY the enabled-list (+0x30/+0x38/+0x40) and leaves the scanned-list
(+0x18/+0x20/+0x28) NULL** — mislabeling those three slots "unused" (`ctor_bracket.cpp:197`). But the
scanned-list is NOT unused: the engine populates it natively and READS it downstream (MOUNT, the
report cmd, and — per the crash — a graphics/`CreateInstance` consumer). With +0x18/+0x20/+0x28 all
null, a consumer that iterates the scanned list computes `(end-begin)/0x70` over nulls or derefs a
scanned `I_Mod` record at a null/garbage base → the FSR2/NGX invalid-pointer read. The
garbage-varies-per-boot signature (PROBE F) fits: a null vector deref'd reads whatever adjacent heap
holds that boot.

**This is the falsifiable mechanism:** the wrong VALUE is the null scanned-list begin/end/cap; the
WRONG WRITE is kcdx skipping SELECT (which would populate it) AND not populating it itself; the
ORIGINAL PATH made it inevitable because kcdx "fully replaces the ctor, no original SELECT call"
(logged every boot) while only rebuilding the enabled-list — so the scanned-list the engine still
reads is never built. PROBE H proved it: with the takeover OFF (native ctor + SELECT run), the
scanned-list IS populated and the crash is gone.

**Also explains the user's symptom** ("mods didn't load from the Workshop" with the takeover off):
SELECT's Steam-workshop scan is what kcdx's takeover replaces — so kcdx's absorption is HOW workshop
mods normally load; disabling it dropped them. The fix must keep mod-loading working AND give the
engine a valid scanned-list.

## The fix is a design fork (for the user — surfaced after root-cause verification)

Root cause is identified; the FIX is a real design decision (how to give the engine a valid
scanned-list while keeping kcdx's mod-absorption). Candidate approaches (to surface, not decide):
(a) kcdx populates the scanned-list +0x18/+0x20/+0x28 with the `I_Mod` records its enabled-list
points at (a valid `std::vector<I_Mod>` of the 0x70-stride records); (b) the takeover CALLS the
native ctor (which runs SELECT → builds both lists natively), then kcdx re-orders the enabled-list on
top (the "bracket" model rather than "full replace"); (c) point the scanned-list at the same records
the enabled-list does, if the layout allows. Each has different RE + correctness implications. This
goes to Gate B (root-cause-verifier) first, then to the user as a design fork (`/debug` §2.5 Gate A).

## PROBE I-fix RESULT — the null scanned-list theory is FALSIFIED (populating it changed nothing)

Built the candidate fix (takeover ON + the scanned-list +0x18/+0x20/+0x28 populated with a contiguous
0x70-stride copy of the 104 enabled records — `ctor_bracket_scanned_list_probe records=104` confirms
it fired), relaunched → **STILL CRASHES, identical FSR2 AV** (`module_rva=11723296`, same stack). So
the null scanned-list is NOT the cause. My ROOT CAUSE (PROBE I) was a wrong INFERENCE from the
field-map RE — I built a fix for an unverified theory instead of first OBSERVING what the engine
actually reads. That is the fix-#2-on-a-theory anti-pattern; correcting course.

**What IS still solid (bisect-proven, not inferred):** the ctor takeover IS the cause (PROBE H:
takeover off → clean; PROBE I-fix: takeover on + scanned-list populated → still crashes, so the
specific field is something ELSE the takeover gets wrong, not +0x18/+0x20/+0x28).

## The logging gap (user's question) — the crash log shows WHERE it died, not WHAT kcdx corrupted

KI-0013 made the CRASH well-logged (full stack + registers) — a real improvement. But it does NOT
tell us what kcdx did wrong: the fault is in WHGame's graphics init reading a bad value, and nothing
in the log shows (a) what value/pointer the engine read, (b) which kcdx action produced the object
the engine choked on, or (c) how kcdx's C_ModManager differs from a native one. The crash log is a
WHERE-it-died tool; diagnosing a kcdx-caused off-stack corruption needs a WHAT-kcdx-DID tool — an
OBSERVATION of the engine's own data structures (the C_ModManager the engine reads) vs. what the
native ctor would produce. This is the gap PROBE J addresses, and it is itself a candidate
diagnostics improvement.

## PROBE J — OBSERVE, don't theorize: dump the native vs kcdx C_ModManager + trace the fault source

Stop guessing which field is wrong. Two theory-independent observations:
1. **Run the native ctor in PARALLEL and DIFF.** In `HookedCtor`, ALSO call the original
   `ModManager_ctor` (via the MinHook trampoline kept this time) into a scratch slot to get a
   GENUINE engine-built C_ModManager, then byte-DIFF the native object vs kcdx's reconstruction
   field-by-field (all 0x68 bytes, and probe whether the native object is LARGER than 0x68). Log
   every differing offset. The differing field the graphics path reads IS the bug — observed, not
   inferred. (This needs the trampoline, currently discarded at `ctor_bracket.cpp:377`.)
2. **Trace the faulting `rdx` to its source.** The faulting instruction is `*dst=*src` with `rdx`
   (src) garbage. Frame 1 (`CreateInstance+0x306c3`) loaded `rdx` from somewhere — instrument /
   disassemble what frame-1..frame-3 read to produce `rdx`, walking back to the C_ModManager field
   (or whatever object) it came from. That names the exact field.
Both are OBSERVATIONS (ground truth) that kill the guessing. Start with #1 (the parallel-native diff)
— it directly shows every way kcdx's object differs from a correct one.

## ROOT CAUSE (PROBE J — full native-ctor disassembly, OBSERVED) — our replacement SKIPS the ctor's `IConsole::AddCommand` call

Read the COMPLETE native `ModManager_ctor` body (`_research/init-cycle-recon/_disasm_full_bodies.txt`,
56 instructions). The ctor does EXACTLY (in order):
1. `xor edi,edi` → rdi = 0 (so the +0x18..+0x58 writes store ZERO — confirming the scanned-list SHOULD
   be null; PROBE I-fix populating it was WRONG, now explained).
2. alloc 0x68 (`call 0x4f7820`), build modsDir CryString (`0x4f692c` + `0x4fd468`).
3. write +0x08 sys, +0x00 vtable, +0x10 modsDir, +0x18..+0x58 = 0, +0x60 = 1.  ← our replacement does ALL of this.
4. **`call 0x180b99098`** ← `IConsole::AddCommand` wrapper (seed id 17: `void(cstr name, ptr func,
   i32 nFlags, cstr sHelp)`; the ModManager_ctor seed prose: "registers console cmd
   `wh_mod_GenerateReport`"). **OUR REPLACEMENT SKIPS THIS ENTIRELY.**
5. `call 0x180da104c` (SELECT) — we skip intentionally (we build the enabled list ourselves).
6. `mov [rsi], rbx` (return the obj), then a conditional `call 0x1804fc884` (CryString cleanup of the
   stack-local modsDir temp).

**The bug: our replacement never calls `0x180b99098` (`IConsole::AddCommand`) to register
`wh_mod_GenerateReport`.** The native ctor registers a console command as part of construction; we
omit it. The crash is the engine iterating a `{base, count@+0x08, cap@+0x0C, stride 0x10}` array
(disasm of the faulting frames) — **the shape of the engine's console-command / CVar registry**. The
`AddCommand` call grows/initializes that registry; skipping it leaves the registry in a state a later
graphics-init consumer (DLSS/FSR2 reads CVars for upscaler config — `ffxFsr2GetUpscaleRatioFromQualityMode`
is ON the faulting stack) iterates and faults on a garbage base pointer (uninitialized → garbage
varies per boot, PROBE F). Every prior probe is consistent: bisect → the takeover (H); not the
scanned-list (I-fix); the ctor's own disassembly names the one call we drop.

This is OBSERVED (the native ctor's instruction-level body + the seed-verified identity of
`0xb99098`), not inferred. The fix: our replacement must ALSO register the console command via
`IConsole::AddCommand` — i.e. replicate `call 0x180b99098`'s effect (register `wh_mod_GenerateReport`),
completing the replacement. Still a FULL replacement (no original ctor call) — we just stopped
omitting one of the ctor's steps.

## Fix + verification

The fix completes the replacement: in `HookedCtor`, after the field writes + before/around the
return, call `IConsole::AddCommand` (the `0xb99098` wrapper, resolvable by refdb name) to register
the same `wh_mod_GenerateReport` command the native ctor registers. Verify: build + launch → boots
clean (the registry is properly initialized) AND mods still load (full replacement intact). Then Gate
B (root-cause-verifier) on the mechanism, land via `/execute`, close. First a PROBE to CONFIRM adding
the AddCommand call fixes it (the last observation before the production fix).

## PROBE J-fix RESULT — the skipped AddCommand is NOT the cause either (FALSIFIED)

Restored the native ctor's `IConsole::AddCommand` call (the probe fired:
`ctor_bracket_addcommand_probe name=wh_mod_GenerateReport` BEFORE the crash), relaunched → STILL
CRASHES, identical FSR2 AV (`module_rva=11723296`, same frame chain). The skipped AddCommand is a
REAL gap in our replacement (the ctor does call it, we don't) — but it is NOT the cause of THIS crash.
Two theories now falsified by direct probe (scanned-list I-fix; AddCommand J-fix).

## Method failure (must fix) — guess-a-field → launch → "did it move" is the wrong loop

I have been GUESSING which field of our replacement is wrong (scanned-list, then AddCommand) and
confirming only by whether the crash moves — each guess a full launch. That is the
fix-#2-on-a-theory anti-pattern repeated. The logs show WHERE it died (graphics init, the same
{base,count,cap,stride-0x10} array iteration) but never WHAT VALUE was garbage or WHERE in our object
it came from — so every probe is a blind yes/no. Stop guessing; OBSERVE the divergence directly.

## PROBE K — the divergence oracle (capture a GENUINE ctor object, byte-DIFF vs ours)

The user authorized: use the original ctor ONCE as a diagnostic ORACLE to learn exactly what a
correct C_ModManager contains, understanding it fully (the full 56-instruction body is now read), then
our FULL replacement reproduces it completely. The probe (observe-only, then we still ship a full
replacement):
1. In `HookedCtor`, call the original ctor (via the MinHook trampoline, kept this once) into a SCRATCH
   outResult slot → a genuine engine-built C_ModManager at `*scratch`.
2. Byte-DIFF the genuine object vs kcdx's reconstruction: dump BOTH objects' 0x68 bytes (and read
   past 0x68 — is the real object LARGER than our allocation?), log every differing offset with both
   values. The differing field(s) ARE the divergence — observed, complete, no guessing.
3. Let the engine use kcdx's object (unchanged behavior) so the crash still reproduces, OR (cleaner)
   for this ONE diagnostic boot let the engine use the GENUINE object to confirm it boots — separating
   "our object is wrong" from "something else."
   The side-effects (SELECT runs, AddCommand registers, an alloc) are understood from the full disasm;
   the scratch object is the throwaway. This is a one-boot observation, then removed — the production
   code stays a 100% replacement, now COMPLETE because we KNOW the full correct object.

This ends the guess loop: one launch yields the exact byte-level divergence.

## The logging gap is the SAME problem (user's repeated question)

Our crash log (post KI-0013) shows the full stack + registers — WHERE it died — but for a
kcdx-OBJECT-TAKEOVER bug it cannot show WHAT we built wrong, because we never log what our
reconstructed object looks like vs. a correct one. The PROBE-K divergence dump is exactly the missing
signal: for any object kcdx synthesizes to hand the engine, kcdx should be able to log "here is what I
built" (and, where an oracle exists, "here is how it differs from the genuine one"). That is a real
diagnostics improvement (a follow-up KI candidate) the takeover specifically needs.

## ROOT CAUSE — OBSERVED (PROBE K divergence oracle): the object is UNDERSIZED + missing a pointer at +0x70

PROBE K ran the genuine engine ctor into a scratch slot, returned the genuine object (booted CLEAN —
confirming kcdx's reconstruction is the bug), and byte-diffed genuine vs kcdx. The diff names the
fatal divergence unambiguously:

| Offset | genuine | kcdx | meaning |
|---|---|---|---|
| +0x00 vtable | `0x…8B2E60` | SAME | correct |
| +0x08 sys | same | SAME | correct |
| +0x10 modsDir | per-boot ptr | differs (both valid CryString) | expected value diff |
| +0x18..+0x40 | scanned/enabled lists | differ (kcdx's lists) | expected value diff (both valid heap) |
| +0x48..+0x58 | scanned-list tails | differ | value diff, in-bounds |
| +0x60 init flag | `0x1` | `0x1` SAME | correct |
| +0x68 | `0x0` | `0x0` SAME | correct |
| **+0x70** | **`0x7FF976F23A60` (WHGame code/data ptr, RVA `0x2113A60`)** | **PAST OUR 0x68 ALLOC** | **THE BUG** |
| +0x78+ | `0x0` | — | zero |

**The real `C_ModManager` is LARGER than 0x68 — at least 0x78 bytes — with a non-null pointer at
+0x70 (RVA `0x2113A60`, a WHGame address).** kcdx allocates `kObjectSize = 0x68` and writes nothing
at +0x70. When the engine reads the object's +0x70 field (a graphics/DLSS-FSR2-init consumer does —
the faulting `mov rax,[rdx]` derefs it), it reads **past kcdx's 0x68 heap block into adjacent
memory** = whatever garbage is there that boot (the per-boot-varying garbage signature, PROBE F,
EXACTLY explained). genuine has a valid `0x7FF9…` pointer at +0x70; kcdx has un-allocated garbage.

This is OBSERVED, not inferred — the byte-level diff against a genuine object. Every prior probe is
now explained: the AddCommand (J) and scanned-list (I) weren't it because the real defect is the
OBJECT SIZE + the missing +0x70 field. The earlier RE (`init-cycle-recon` "0x68-byte object, +0x48..
+0x58 unused") was WRONG on the size — it stopped at 0x68 and never saw +0x70.

## The fix (observed, precise)

1. **Allocate the correct size.** The object is ≥0x78 (+0x70 populated, +0x78 zero in this boot — the
   true size needs confirming: re-RE the ctor's alloc-size arg, OR observe how far the engine reads).
   Our `kObjectSize = 0x68` is too small — bump it to the true size.
2. **Write +0x70 with the correct value** (RVA `0x2113A60` this boot → resolve what it IS: likely a
   second vtable / interface sub-object pointer the engine derefs). Identify it by RE (`0x2113A60`)
   and write it in HookedCtor like the other fields (by refdb name per AP1, not a raw RVA).
Still a FULL replacement — now COMPLETE (correct size + the +0x70 field we were missing). The fix
goes through Gate B (root-cause-verifier) then `/execute`.

NEXT: identify RVA `0x2113A60` (what +0x70 points at) + confirm the true object size, then build the
production fix.

## PROBE K ROOT CAUSE — FALSIFIED by instruction-level RE (the +0x70 reading was a heap-adjacency artifact)

A focused RE pass (capstone against the binary, `_research/ki0012-modmanager-size-recon/`) DISPROVED
the +0x70 root cause:
- **The object IS exactly 0x68.** `0x180da0ed1 lea ecx,[rdi+0x68]` (rdi=0 → ecx=0x68) → the allocator
  call; the size arg is provably 0x68 (the allocator records rcx into its byte-counter). kcdx's
  `kObjectSize = 0x68` is CORRECT — do not grow it.
- **Nothing writes [modMgr+0x70]** — not the ctor (highest write +0x60), not SELECT (writes only its
  own stack frame; reads modMgr +0x18..+0x40), not AddCommand (operates on the console singleton).
- **`0x2113A60` (FUN_182113a60) is a process-init magic-static factory callback**, in ZERO .rdata
  vtable slots — not a C_ModManager member.
- **The artifact:** PROBE K's genuine scratch object sat exactly 0x70 above kcdx's object on the heap
  (`genuine_obj - kcdx_obj = 0x70`). So my "kcdx +0x70 = PAST_OUR_0x68 / genuine +0x70 = a pointer"
  diff read ONE QWORD PAST a 0x68 object on BOTH sides — into the heap neighbor. Every +0x70/+0x78
  diff row is OUT-OF-BOUNDS NOISE, not a field. My method error: I diffed 0x00..0x88 over a 0x68
  object and treated the out-of-bounds tail as signal — I even noted the two objects were 0x70 apart
  and failed to connect it. This is the SAME phantom-field error `init-cycle-recon/FINDINGS.md`
  documents (the original narrow probe read the caller's stack and reported "+0x48=15").

So PROBE K's "ROOT CAUSE / The fix" above is WRONG and withdrawn (do NOT grow the alloc, do NOT write
0x2113A60 at +0x70 — that would be an AP1 raw-RVA violation AND make kcdx DIVERGE from the genuine
0x68 object). FOUR theories now falsified: scanned-list (I), AddCommand (J), +0x70/size (K). The
discipline lesson the trail keeps teaching: a byte-diff that reaches past the object is noise; observe
WITHIN the real bounds, value-by-value, or trace the fault to the exact field.

## What is STILL solid (bisect-proven, survives every falsification)

- The ctor takeover IS the cause (PROBE H: takeover off → clean; genuine object → clean).
- kcdx's object is 0x68, correctly sized; +0x00 vtable, +0x08 sys, +0x60 flag all MATCH genuine.
- The wrong thing is a VALUE within the 0x68 block kcdx writes (the modsDir CryString, or the
  enabled-list begin/end/eos triple at +0x30/0x38/0x40, or +0x18..+0x58), OR a construction-time
  SIDE EFFECT the native ctor produces that kcdx omits (the AddCommand call J restored did not fix
  it, so re-observe rather than assume the registry).

## PROBE L — within-bounds field-value diff (the RIGHT observation, bounded to 0x68)

The corrected PROBE K: byte-diff ONLY 0x00..0x68 (NEVER past), and for each DIFFERING field understand
its VALUE SEMANTICS (is kcdx's enabled-list begin/end/eos a VALID std::vector triple the way the
genuine one is? is the modsDir CryString header correct? what does SELECT write at +0x18..+0x40 that
kcdx's hand-built lists get subtly wrong?). The enabled-list (+0x30/0x38/0x40) is the prime suspect:
kcdx points it at `g_enabledList` (a `std::vector<void*>` of synthesized I_Mod* records); the genuine
one is SELECT's own. If kcdx's I_Mod* records (record_synth) are malformed in a way a graphics
consumer that walks the enabled list chokes on, that is the bug — within 0x68, a wrong VALUE.
ALTERNATIVELY: trace the faulting graphics `rdx` back to the exact field it derived from (PROBE J
option 2, never run) — disassemble what frame-2/3 read off the C_ModManager (or `csys[+0x2B30]`) to
produce the garbage pointer. Both are within-bounds observations; no more out-of-bounds dumps, no more
field guesses.

## PROBE L — WITHIN-0x68 field-value diff (clean, no out-of-bounds). The real divergences:

Bounded diff (0x00..0x68 ONLY) + deref of the list fields + the modsDir CryString. Genuine object
booted clean. The divergences, with semantics:

| Field | genuine | kcdx | verdict |
|---|---|---|---|
| +0x00 vtable | `0x…B02E60` | SAME | ✓ correct |
| +0x08 sys | same | SAME | ✓ correct |
| +0x10 modsDir CryString | data ptr → "mods", **nRefs=1**, nLength=4, nAllocSize=4 | "mods", **nRefs=2**, nLength=4, nAllocSize=4 | **refcount differs (1 vs 2)** — kcdx's CryString has an extra ref. Likely benign (a CoW over-ref) but NOTED. |
| +0x18/0x20/0x28 scanned-list | 16 records (0x70-stride) | NULL (count -1) | by design (kcdx leaves null); PROBE I-fix proved populating it ≠ the fix |
| +0x30/0x38/0x40 enabled-list | **count=14**, I_Mod* entries vtable `0x…A70AF00` | **count=104**, entries vtable `0x…A70AF00` (SAME vtable) | **count 14 vs 104** — kcdx enables 104, genuine 14. kcdx's I_Mod* records have the CORRECT vtable. |
| +0x48/0x50/0x58 | genuine: scanned-list tail ptrs (`0x…A36B0/A3720/A3748` — INSIDE the scanned range) | kcdx: repeats enabled begin/end-ish | genuine's +0x48..0x58 point INTO its scanned-list; kcdx's are different (it has no scanned list) |
| +0x60 flag | `0x1` | SAME | ✓ correct |

**Key observations (NOT yet a root-cause claim — observed facts):**
1. **+0x48/+0x50/+0x58 are NOT "unused/zero"** — the genuine object has them pointing INTO its
   scanned-list (`0x1A9375A36B0` etc., within the scanned range). The earlier RE + kcdx's code call
   these "unused, leave zero" — but the GENUINE object populates them. kcdx's are NON-zero too but
   point at different (enabled-related) memory. This is a SECOND vector/iterator the genuine object
   has that kcdx's lacks correct values for. (In the diff, +0x48..0x58 were "DIFF".)
2. **The enabled-list +0x30..0x40 also feeds +0x48..0x58 in the genuine object** — a `{begin,end,eos}`
   at +0x30 AND a related triple at +0x48 (genuine: +0x48/0x50/0x58 = `A36B0/A3720/A3748`, and
   +0x30/0x38/0x40 = the SAME `A36B0/A3720/...`? No — genuine +0x30..0x40 read as the enabled-list
   begin/end/eos. Need to recheck: genuine +0x48=A36B0 EQUALS genuine enabled begin=A36B0!). So in the
   genuine object the enabled-list triple appears at BOTH +0x30..0x40 AND +0x48..0x58 region — OR the
   object has two vectors and kcdx mismapped which offset is the enabled list.

**The likely bug, to verify:** kcdx may have the enabled-list at the WRONG offset, OR there is a
SECOND list (+0x48..0x58) the engine reads that kcdx leaves wrong. The genuine enabled begin
(`0x1A9375A36B0`) == genuine +0x48 (`0x1A9375A36B0`) — they MATCH, meaning +0x30..0x40 and +0x48..0x58
may be TWO vectors sharing data, or the real enabled-list is at +0x48 not +0x30. kcdx writes the
enabled list ONLY at +0x30..0x40 and leaves +0x48..0x58 as stale/wrong — so if the engine reads the
enabled list at +0x48..0x58, kcdx's is wrong there.

**CORRECTION (avoiding another mis-read):** I started to claim "SELECT writes +0x48, kcdx omits it" —
but the dump's `[rax+0x10/0x18/0x20]` writes are to `rax = rsp` (SELECT's STACK FRAME, `mov rax,rsp`
at entry), and the `[r11+0x38/0x40/0x48]` entries are in the READS section over an inner object that
may not be the modMgr. The filtered dump is NOT sufficient to claim what SELECT writes to the modMgr;
asserting +0x48 from it would repeat the +0x70 over-reach. Pulling back to OBSERVED facts only.

**OBSERVED, in-bounds, not-yet-attributed divergences (PROBE L):**
- +0x10 modsDir: kcdx `nRefs=2` vs genuine `nRefs=1` (a CryString over-ref).
- enabled-list count: kcdx 104 vs genuine 14 (kcdx enables every discovered plugin; genuine 14 mods).
- +0x48/0x50/0x58: genuine points INTO its scanned-list; kcdx differs (it has no scanned list).
- scanned-list (+0x18..0x28): genuine 16 records; kcdx null.

NEXT (static, no launch): a CAREFUL instruction-level RE of SELECT (FUN_180da104c) + the genuine
object — what EXACTLY does the object look like field-by-field after construction, which offsets hold
the enabled-list vs a second iterator, and what does +0x48..0x58 hold in the genuine object — WITHOUT
inferring from filtered dumps. Then attribute which PROBE-L divergence is the fault. (Dispatched to a
focused RE pass; no more in-line dump mis-reads.)

## PROBE M (instruction-level RE) — NONE of the field divergences is read on the graphics path; it is a MISSING SIDE EFFECT

A rigorous instruction-cited RE pass (`_research/ki0012-modmanager-size-recon/`) attributed the PROBE-L
divergences and cleared them ALL as the graphics-crash cause:
- **SELECT (`FUN_180da104c`) writes NOTHING to the modMgr object** — it only READS it (+0x18/+0x20/
  +0x30/+0x38) and writes its own stack frame (`mov rax,rsp` at entry; the `[rax+0x10..]` are
  register-home spills, NOT object writes — the exact false-positive I nearly made). +0x48..0x58 are
  written by neither the ctor nor SELECT nor SELECT's helpers.
- **No consumer on the crash path reads the divergent fields.** The enabled-walker reads ONLY +0x30;
  MOUNT reads ONLY +0x30/+0x38. Nothing reads +0x48..0x58, the enabled-list COUNT, or the scanned
  list on the graphics path.
- **The faulting `{ptr,count@+8,cap@+0xC,stride 0x10}` array is a DLSS/NGX feature-parameter array
  under `NVSDK_NGX_UpdateFeature`, NOT the C_ModManager** (whose vectors are 8-byte-stride
  `{begin,end,eos}` triples — a different shape). The copy helper at the fault is a generic 872-call-
  site `*dst=*src` utility, not modMgr-specific.
- The modsDir **nRefs=2** over-ref is mechanistically real (kcdx's two-step init→placement bumps the
  source refcount, `FUN_1804fd468`) but an over-ref DELAYS free, never dangles → cannot cause a
  graphics AV.

**Conclusion (the pivot):** the bug is NOT a wrong byte/value in the 0x68 object — the object's
graphics-relevant fields (+0x00 vtable, +0x08 sys, +0x30 enabled-list) are all CORRECT (PROBE L). The
crash is a **missing INIT / SIDE EFFECT** that kcdx's full replacement of ctor+SELECT skips, which the
engine's DLSS/FSR2 init depends on. The native ctor does `call 0x180b99098` (console AddCommand — J
restored ONLY this, crash persisted) AND the native SELECT runs a whole body of engine interactions
(the Steam-workshop scan, mod_order read, per-mod manifest validate via the engine's own paths) that
kcdx replaces wholesale. One of those side effects sets up state the DLSS feature array later reads;
kcdx skips it → the array's base is uninitialized garbage (per-boot-varying, PROBE F). AddCommand
alone wasn't enough → it is a DIFFERENT or ADDITIONAL SELECT side effect.

## PROBE N — trace the DLSS array's garbage base to its origin (the decisive observation)

The RE names the one decisive probe: a READ-ONLY hook on the DLSS-feature caller frame the dump names
(`C_Game::CreateInstance+0x303xx`, the frame that builds the `{ptr,count@+8,cap@+0xC,stride 0x10}`
array + calls `NVSDK_NGX_UpdateFeature`), logging where that array's `base`/`count`/`cap` come from —
does the garbage base trace (through any pointer chain) back to a C_ModManager field, OR to an engine
structure (a CVar/registry/feature table) that an engine init step kcdx SUPPRESSED populates? That
distinguishes "wrong modMgr field after all" from "missing engine-init side effect," and names the
exact suppressed step. This is the next step — but it is a NEW KIND of probe (instrument WHGame's
graphics code, not the modMgr), and the direction (which side effect to restore) is a real call.

## ROOT CAUSE CONFIRMED (PROBE N) — a native SELECT SIDE EFFECT is the missing piece

kcdx built its object as normal, then called native `ModManager_Select` on it
(`ctor_bracket_select_probe` → `..._returned` → `ctor_bracket_complete`), and the boot went **CLEAN
to the ready tick** (`CAP-61-bracket-reached-ready pass=true ... reached the first update tick
end-to-end past C_ModManager construction`; ZERO FAULTED). SELECT writes nothing to the object (RE-
proven) — so running it adds only its ENGINE SIDE EFFECTS, and that fixes the graphics crash.

**Confirmed mechanism:** kcdx's full ctor+SELECT replacement skips `ModManager_Select`, which performs
an engine side effect (in its Steam-workshop scan / mod_order read / per-mod manifest-validate passes,
all via the engine's own code paths) that WHGame's DLSS/FSR2 graphics init later depends on. Without
that side effect, an engine structure the DLSS feature-array builder reads is left uninitialized → the
per-boot-varying garbage base → the AV. With SELECT's side effects present, the structure is
initialized and the crash is gone. This is OBSERVED (crash present without SELECT, absent with it),
not inferred — the cleanest confirmation in the whole trail.

**Architectural finding (for the user):** the takeover's design — "fully replace ctor + SELECT, never
call the originals" — is the ROOT of this bug class. SELECT does more than build the two mod lists; it
performs engine interactions (a CVar/console/feature-registry touch, or an engine-init call inside its
scan/validate path) with effects beyond the C_ModManager object. A from-scratch replacement that
reproduces only the OBJECT and not these side effects will keep hitting "engine state X wasn't set up"
crashes. KI-0012 is the first symptom to surface.

## The fix is a design fork (surfaced to the user)

Root cause is confirmed; the FIX is a real design decision:
- **(A) Call native SELECT, re-order on top.** Let native SELECT run (it builds the lists + does all
  side effects correctly via the engine), then kcdx re-orders/overlays the enabled-list afterward.
  Drops the "fully replace SELECT" tenet but eliminates this entire bug class (every SELECT side effect
  is correct by construction). Smaller, most robust — but is NOT a full SELECT replacement.
- **(B) Keep full replacement; replicate the missing side effect(s).** Stay 100% replacement, but
  identify the SPECIFIC engine side effect(s) SELECT performs that graphics needs and reproduce them in
  HookedCtor (a further probe narrows which — bisect SELECT's sub-calls). Preserves the tenet but is
  more RE surface + risks the next un-replicated side effect.
This needs Gate B (root-cause-verifier) on the confirmed mechanism, then the user picks A vs B. NEXT:
surface the A-vs-B fork; if B, a probe to narrow WHICH SELECT sub-call is the needed side effect.

## FIX DIRECTION (user-decided) — isolate the specific native side-effect call, invoke it from kcdx's init

The takeover stays a COMPLETE hijack: kcdx keeps full ownership of the C_ModManager object AND the
enabled-list order (this is the design intent, not scope to walk back). The fix ADDS the one engine
SIDE EFFECT the engine needs — by calling the SPECIFIC native function that produces it, from inside
`HookedCtor` — the SAME pattern kcdx already uses to call native helpers (the allocator, the CryString
builders). NOT calling native SELECT wholesale (that would surrender list-building to the engine =
LESS hijack). NOT replicating from scratch (keeps missing side effects).

**The open RE fact to isolate:** WHICH native function (a `ModManager_Select` sub-call, or an engine-
init call SELECT makes) produces the graphics-needed side effect. PROBE N proved calling FULL SELECT
fixes it; the isolation narrows that to the minimal call. SELECT's sub-calls (from `_disasm_modmanager.txt`
CALL sites): `FUN_180da0fb0`, `FUN_1804d4510`, a vtable call `[rax+0x20]`, `FUN_180da1178` (the mods/
scan), `FUN_180da1294` (ReadModOrder). The graphics side effect is one of these (or an engine call
nested inside one). NEXT: a bisect probe — call SELECT's sub-calls individually (instead of full
SELECT) and find the minimal set that makes the boot clean; that names the exact call kcdx must make.
Then the production fix calls JUST that, by refdb name (AP1), from HookedCtor.

## PROBE O RESULT — it is the ENABLED-LIST, not a side effect (the N-vs-O isolation)

PROBE O: call native SELECT (side effects done), THEN restore kcdx's enabled-list over SELECT's →
**CRASH, same FSR2 AV** (`module_rva=11723296`). The one-variable comparison is decisive:
- **PROBE N:** call SELECT, KEEP SELECT's list → CLEAN.
- **PROBE O:** call SELECT, RESTORE kcdx's list → CRASH.

N and O differ in EXACTLY ONE variable: the enabled-list. SELECT's side effects ran in BOTH. So the
fix is NOT a missing side effect — **it is the ENABLED-LIST itself.** The "missing side effect"
conclusion (PROBE N) is FALSIFIED: PROBE N booted clean because it kept SELECT's LIST, not (only)
because of side effects. With SELECT's genuine list → clean; with kcdx's list → crash.

**The difference is kcdx's enabled-list vs the genuine one** (PROBE L data): genuine count=**14**, kcdx
count=**104**; both have I_Mod* entries with the correct vtable, but the records + count differ. The
graphics path reads the enabled-list (or something derived from it), and kcdx's 104-entry list breaks
it where SELECT's 14-entry list does not. Candidates for the fatal difference:
- **The COUNT (104 vs 14):** kcdx enables every discovered plugin (104 — incl. all test-plugins);
  genuine enables 14 real mods. Does graphics size something by the enabled-list count? 104 vs 14 is a
  big delta.
- **kcdx's synthesized I_Mod records:** record_synth builds 0x70-byte records with harvested vtables +
  CryString fields. A field graphics reads (NOT the vtable, which matches) may be malformed in kcdx's
  records vs genuine ones — a string field, a flag, an offset the graphics path touches.

This is the tightest isolation in the trail (N vs O = one variable, flips the outcome). NEXT: narrow
WHAT about kcdx's list breaks graphics — the count, or the record contents. Probe: (a) does a kcdx
list TRUNCATED to ~14 entries boot clean (count); (b) does deep-diffing one kcdx I_Mod record vs a
genuine one (within 0x70) show a field graphics-relevant. The fix then makes kcdx's list/records match
what graphics needs — while kcdx keeps full ownership + order.

## ROOT CAUSE CONFIRMED (PROBE P) — kcdx puts its own `kind="plugin"` entries in the engine's mod-mount list

PROBE P (truncate kcdx's enabled-list to 14) booted CLEAN. The log shows WHY — the enabled-list
breakdown by kind:
- **17 `kind="pak_mod"`** (real game/Workshop mods — `FastLaunch`, `cheat`, `ebapmod`, `instagather`,
  … from `mods/` + `steamapps/workshop/`). The genuine SELECT enables ~14 of THESE.
- **90 `kind="plugin"`** (kcdx's OWN test-suite plugins — `cap_01_patch`, `comp_03_a`,
  `cap_36_cpp_hook_interface`, … from `kcdx-plugins/test-suite/`, DLL/Lua plugins with NO pak).

The truncate-to-14 kept (mostly) the real pak mods + dropped the 90 plugins → clean. **The bug: kcdx
adds its own `kind="plugin"` entries to the engine's `C_ModManager` enabled-list as synthesized
`I_Mod` records.** That list is the ENGINE's mod-MOUNT list — for real **pak mods** the engine mounts
(each a valid pak with a manifest, the full I_Mod contract the engine understands). A kcdx PLUGIN (a
DLL or Lua script loaded by KCDX'S OWN loader, no pak) is NOT a thing the engine's mod-mount path can
process; synthesized as a fake `I_Mod` and put in the engine's list, the 90 plugin entries are
malformed for the engine, and the DLSS/FSR2 init (which walks/sizes by this list) chokes on them →
the AV. The genuine SELECT's list has ONLY pak mods → clean.

**This is the confirmed root cause** — observed (truncate-to-14-real-mods = clean; the 90 plugin
entries are the crash-causers), and it matches the user's hypothesis exactly ("perhaps a specific
plugin, engine test possibly"). The whole probe trail converges: it's the enabled-list (N vs O), and
specifically the `kind="plugin"` entries kcdx wrongly enables (P).

## The fix (clean + architecturally correct)

kcdx's enabled-list handed to the engine's `C_ModManager` must contain ONLY real pak mods
(`kind="pak_mod"`), NOT kcdx's own plugins. kcdx plugins are loaded by KCDX's loader — they do not
belong in the ENGINE's mod manager at all. Fix in the enabled-list builder
(`src/mod_absorb/enabled_list_builder.cpp` + `record_synth`): FILTER OUT `kind="plugin"` entries when
building the engine enabled-list (keep them in kcdx's own plugin pipeline). This both FIXES the crash
AND is correct (the engine should only ever see real mods; kcdx plugins are a separate kcdx concern).
kcdx keeps full ownership + order of the REAL mods; it just stops polluting the engine list with its
own plugins. Routes through Gate B (root-cause-verifier) then `/execute`.

(Open sub-question for the fix: do the ~17 pak_mod entries need kcdx's RECORDS, or do real pak mods
also need the genuine engine record? The 17 pak_mods booted fine in the truncate, so kcdx's pak_mod
records are OK; only the plugin entries are the problem. Confirm the filter is `kind=="plugin"` →
exclude, all else → keep.)

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
