# KI-0028 — full investigation handoff (beginning to end)

**Bug:** KI-0028 — boot fails at UI/render bring-up after KI-0027's table-DB load succeeds.
**Source doc:** `docs/known-issues/KI-0028-fs-takeover-boot-hang-ui-render-init.md` (status: OPEN).
**Prior authoritative split:** `_research/ki0028-fsr2-poll-loop-recon/HANDOFF.md` (2026-06-21, HEAD `f0b1a3f`).

**Rigor contract for this doc:** every claim is graded **VERIFIED** (directly observed, probe + evidence
file cited), **INFERRED** (reasoned, not fully proven), **FALSIFIED/WITHDRAWN** (a probe killed it), or
**OPEN**. Where an earlier entry claimed more confidence than later evidence supports, the later grading
governs and says so. Nearest-export frame labels (`ffxFsr2ResourceIsNull+…`, `NVSDK_NGX_UpdateFeature+…`)
are **noise** throughout — WHGame.dll has no PDB; cdb labels every address by the nearest export below it,
and the wedge frames sit 2–9 MB past those exports (see §"Standing traps"). Do not read NGX/FSR2 as the
subsystem.

---

## 1. What KI-0028 is — the chain it sits in

kcdx performs a **filesystem takeover**: at boot it lets the engine construct its `CCryPak` object,
then overwrites that object's vtable pointer (`[pCryPak+0x00]`) with a kcdx vtable, so every engine
file call routes through kcdx (open/read/metadata/enum slot families are kcdx impls; the rest thunk to
the captured engine originals). The standing design invariant (user-stated, binding): **kcdx owns the
full init; no thunk / hand-back to the original engine for any part of init.**

KI-0028 is the **third in a chain of boot blockers**, each unblocked by fixing the prior:
- **KI-0026** — `0xC8` engine-pak file miss at graphics-init (engine alias resolution). [separate KI]
- **KI-0027** — table-DB load failed because the fs-takeover did not serve the `<base>__*.xml`
  override-glob directory enumeration. **FIXED + verified** (`4befc07`): zero "Database system error" /
  `err_id=259`, table globs return correct match counts, suite ran to `passing=320/343`. (VERIFIED —
  KI-0028 doc §"Relationship to KI-0027".)
- **KI-0028** — with KI-0027 fixed, boot passes the table-DB load and proceeds, but **fails at UI/render
  bring-up**. This is the open bug.

KI-0028 was filed `2026-06-20`, `commit_at_filing: 4befc07`.

---

## 2. The symptom — as originally reported, then corrected by evidence

### 2.1 Original user report (the live experience)
Launched with the KI-0027-fixed engine: **sound loaded, "no video", input unresponsive**; the user killed
the process via Task Manager. No crash dump. (KI-0028 doc §Symptom.)

### 2.2 Corrected symptom (VERIFIED, multiple probes)
The "hang / no video" framing was wrong. The corrected, verified shape:
- **It is NOT a crash.** The crash zip at the wedge timestamp is the watchdog's kill-time snapshot; the
  BugSplat log inside is empty (BOM only). (VERIFIED — `crash_2026-06-21_11-13-16.zip` →
  `bugsplat_C28L17R6.log` is 0.0 KB. HANDOFF §2.11.)
- **It is NOT a hung main thread / deadlock.** The kcdx per-frame heartbeat (`BOOT_WATCH heartbeat`,
  emitted from `HookedUpdate`) advances continuously for many minutes with no stall (e.g. tick 1→41686
  over ~9 min, `tid=7708`). The heartbeat thread id is the SAME thread cdb labels "Main". (VERIFIED —
  `kcdx-dev_2026-06-21_11-13-16.log`, 537 heartbeat lines. HANDOFF §2.2.)
- **It is a NO-PRESENT + NO-INPUT failure on a RUNNING game.** Every key thread makes forward progress
  (Main + RenderThread change stacks across samples; tick advances ~35/s) yet no rendered frame is
  presented (black screen) and the window processes no game input. Audio is independent (own thread) so
  it plays. (VERIFIED — PROBE J.3, invasive cdb on the live process 47 min in; KI-0028 doc §"PROBE J.3
  RESULT".)
- **The window itself is healthy.** `EnumWindows`/`IsWindowVisible`/`GetWindowRect` + `Process.Responding`
  (zero perturbation): the game window EXISTS, is VISIBLE, correctly sized (`hwnd=0x60c64`, vis=True,
  2560×1440, normal fullscreen style, correct title), and `Process.Responding = True` (it answers OS
  message pings). So the black screen is NOT a missing/hidden/zero-size window, and the OS message pump is
  NOT dead. (VERIFIED — PROBE J.4; KI-0028 doc §"PROBE J.4 RESULT".)

---

## 3. The one decisive control — the bug is kcdx-introduced

- **VANILLA (kcdx disabled) boots to an interactive main menu.** Dropped a `kcdx.disabled` marker next to
  `kcdx.exe` (the launcher's pre-everything disable switch → `LaunchGameVanilla`: zero injection, zero
  engine, zero logging), launched: clean interactive menu, no hang, no crash, no new kcdx log (the disabled
  path sets up no logging — zero new logs IS the success signature). Same game, same machine, kcdx the only
  variable. (VERIFIED — P-B; KI-0028 doc §"P-B".)
- **CONCLUSION (VERIFIED): the failure is kcdx-introduced (H1), not engine/environment (H2).** The
  hypothesis that vanilla stalls identically is FALSIFIED.

---

## 4. The differentiator inside kcdx — the FS-takeover SWAP

- **A `kcdx-noswap` marker suppresses ONLY `SwapVtableOnObject` + the index build**; every other kcdx init
  (ctor bracket, worker threads, `g_kcdxReadyEvent`, overlay map) runs identically; the engine keeps its own
  `CCryPak` vtable. **Result: swap-OFF reaches an interactive menu; swap-ON wedges.** (VERIFIED that swap-off
  → menu and swap-on → wedge — P-F; KI-0028 doc §"P-F".)
- **CAVEAT (INFERRED, not proven — "the FS swap is the SOLE cause"):** P-F's swap-ON arm changed **four**
  things vs its swap-OFF arm, not one: (1) the FS dispatch (engine vtable → kcdx vtable); (2) `BootWatchStart()`
  arms a watcher thread; (3) `PresentProbeStart()` arms a present-poll thread; (4) `BuildAssetIndexAtSeat()`
  runs an `INFINITE WaitForSingleObject` on Main inside `CSystem::Init`. The swap-OFF arm returns early and
  skips (2)(3)(4). So P-F isolated 4 differentiators, not 1. (HANDOFF §4.1.)
  - **PROBE L removed (2) and (3)** (disarmed present-probe + watcher thread) → **wedge persisted** → those
    two threads are NOT the cause. (VERIFIED — PROBE L; HANDOFF §3.) ⚠ PROBE L did NOT disarm the per-frame
    heartbeat tick (`BootWatchTick()`, a separate call site) — it ran the whole time. So "a fully
    instrumentation-free boot" was NOT tested; commit `f0b1a3f`'s "probe threads EXONERATED" is OVERSTATED
    and corrected in the handoff.
  - **Differentiator (4) is near-eliminated from existing logs:** `BuildAssetIndexAtSeat()` logs
    `seat_index_stored entries=307006` on wedging runs — i.e. its `WaitForSingleObject(gate, INFINITE)`
    demonstrably RETURNED. A wait that returns is not where Main is stuck. This re-collapses the suspect to
    the FS-dispatch/swap, but a clean P-L.2 (suppress only the index-build wait, keep swap) was never run.
    (INFERRED from `seat_index_stored` log; HANDOFF §6 Q2.)
- **Net (VERIFIED): the swap path is the differentiator between menu and wedge.** Which of {FS dispatch,
  index-build wait} within it is the cause is not fully isolated by a probe, though the evidence points at
  the FS dispatch.

---

## 5. Where Main is at the wedge — and the corrections that govern it

### 5.1 The stable wedge stack (VERIFIED, byte-identical across runs)
Live invasive cdb (multiple runs, most recently PID 18100, PROBE L), kcdx PDB loaded:
```
ntdll!NtDelayExecution → RtlDelayExecution → KERNELBASE!SleepEx+0x91
WHGame!…+0x36af90                          (window/focus poll, RVA 0x865fb4 — BOUNDED, see 5.3)
WHGame!C_Game::CreateInstance+0x2e8c63
WHGame!C_Game::CreateInstance+0x2e8d7d
WHGame!…+0x16cce2
kcdx!HookedUpdate+0x94a                     (kcdx per-frame update hook)
WHGame!…+0x16c7a0                          (engine update dispatcher)
WHGame!…+0x36eb39                          (see 5.2 — the real-RVA correction)
WHGame!…+0x36ff17
KingdomCome+0x36db / +0x4ad5 / +0x898a (main)
```
The stack is byte-for-byte identical across P-A and PROBE L. (VERIFIED — `cdb_pl_probeL_wedge.txt`.)

### 5.2 The frame identity — WITHDRAWN "entity-init", real frame is a WINDOW/DISPLAY-MODE loop
- An earlier pass read the bare offset `0x36eb39` as a raw RVA, landed in an unrelated entity-name stub
  (holds `"dummy_no_ai"`/`"player"`/GUID strings **by coincidence**), and concluded "entity/AI init."
  **That is an offset-vs-RVA conflation artifact — WITHDRAWN.** (KI-0028 doc top CORRECTION; HANDOFF §2.4.)
- **The real wedge frame is RVA `0x869c39`** (= `ffxFsr2ResourceIsNull` export `0x4fb100` + offset
  `0x36eb39`): a **window/display-mode/fullscreen** function that reads the `r_Fullscreen` cvar and polls
  the `0x492b890` window-manager singleton, with an enclosing loop around the wedge call site. Proven 3
  ways: (1) `0x4fb100+0x36af90 = 0x866090` = Main's confirmed focus-poll RIP; (2) raw `0x36eb39` has 0
  back-edges, real `0x869c39` has the enclosing loop; (3) `0x869c39` carries `r_Fullscreen` + the
  window-mgr singleton. (VERIFIED static — `disasm_869c39_exit_cond.py`; HANDOFF §2.4, CORRECTION.)

### 5.3 The top-of-stack `SleepEx` poll is BOUNDED — not the infinite loop
- The function at RVA `0x865fb4` (the `SleepEx` caller) is a `GetActiveWindow`-vs-expected-handle poll that
  runs **at most 5 iterations** (`mov ecx,5; call Sleep; inc edi; cmp edi,5; jl`), ~25 ms, then returns. It
  is NOT an infinite wait. (VERIFIED — body read, imports resolved: Sleep @ IAT 0x3a02738, GetActiveWindow
  @ 0x3a03260. HANDOFF §2.5.)
- So the infinite repetition is an OUTER loop re-running this chain — the `0x869c39` loop (5.2).

### 5.4 The outer loop's exit condition — three successive corrections, ALL the loop-as-wedge readings FALSIFIED
This sub-area accumulated three superseding corrections. Carry only the final one as current; the earlier
two are recorded so they are not re-walked.
- **(a) Static read — a counter-gated loop (later re-read as a guard).** `0x869c39` re-runs WHILE a counter
  (`0x56628d8`/`0x56628dc`, `.data`) is `!= -1`, exits when it is `-1`; helper `0x1c1e988`
  (EnterCriticalSection-guarded) flips the counter to `-1` when it reads 0; helper `0x1c1e91c` registers an
  id into the counter + a TLS slot. Initially read as a cross-thread producer/consumer completion handshake.
  (VERIFIED static — `disasm_869c39_exit_cond.py`.)
- **(b) PROBE M FALSIFIED the loop as the wedge gate.** A live swap-ON vs swap-OFF read of those
  exit-condition globals (counters `0x56628d8/dc`, flags `0x556d080/084`, the `0x492b890`-family singletons)
  found them evolving **IDENTICALLY** in the wedged run (black) and the menu-reaching run (`suite: 319/343`):
  counters freeze at `0x80002B7x` in BOTH and NEVER reach `-1` (not even in the SUCCESS run that reaches the
  menu — so a value that is `!= -1` in the success case cannot be the wedge gate). `0x869c39` is normal
  per-frame code that runs the same with or without the swap. (VERIFIED — PROBE M, retired to
  `_research/probe-archive/ki0028-probeM-loop_state_probe.{h,cpp}`; KI-0028 doc CORRECTION 2; HANDOFF §4.2.)
- **(c) The "handshake" framing was itself WRONG — it is a `std::call_once` guard; the real exit gate is
  `GetActiveWindow()`.** A later static read (`_research/ki0028-window-exit-gate-recon/`) overturned (a): the
  `0x56628d8`/`0x56628dc` counters are a **`std::call_once` magic-static guard pair, NOT a cross-thread
  completion token** — the SAME thread drives the once to completion, there is NO awaited external producer.
  PROBE M's `0x80002Bxx` freeze is the normal in-flight call-once id, identical swap-on/off **because it is a
  local guard, not a differentiator** (this EXPLAINS PROBE M's null, not just observes it). The real loop
  exit gate was read as `GetActiveWindow() == <engine-expected HWND>` (test site `0x866029`; expected handle
  from `[this+0x2d0]→[+0x740]`) — the engine waiting for ITS window to become the OS foreground window.
  (VERIFIED static — `_research/ki0028-window-exit-gate-recon/`.)
- **(d) The `GetActiveWindow` window-activation theory was THEN FALSIFIED by PROBE W.** PROBE W's
  `WINDOW_PROBE_CONVERGED` fired at the FIRST sample (`wall_s=9025`): a process window was visible AND
  OS-foreground immediately → the `GetActiveWindow()==expected` gate is SATISFIED EARLY, swap-ON. `fg_is_ours`
  only dropped to 0 at the very end when the USER quit (teardown), not during the wedge. **The
  window-activation gate is NOT the wedge.** (VERIFIED — PROBE W run 1; KI-0028 doc / FINDING
  §"PREMISE CORRECTION (PROBE W run 1)".)
- **STANDING INSTRUCTION (from PROBE M + Reframe 6):** the `0x869c39`/`0x866090` frames are the per-frame
  trap — Main runs full per-frame ticks and a cdb sample lands on this recurring frame; it is NOT a wedge
  IN this loop. Stop chasing frames/globals on the wedged stack — they run identically swap-on/off. The probe
  must observe what the swapped CCryPak SERVES/CHANGES that diverges the boot.

---

## 6. What "Main runs the full loop" means — Reframe 6 (the premise inversion)

- The heartbeat tid is the SAME thread cdb labels "Main"; the heartbeat advanced to tick 58253 long after
  the cdb samples. So **Main is NOT stuck** — it runs the normal per-frame loop, each frame passing through
  the bounded window/`Sleep(5)` pacing helper. The `-pv` NONINVASIVE cdb samples repeatedly caught Main
  during that recurring per-frame Sleep at the same call depth — a SAMPLING ARTIFACT, not a freeze.
  "Identical RIP+RSP across 2s" was the recurring per-frame Sleep, not a pinned thread. (VERIFIED —
  Reframe 6; KI-0028 doc §"Reframe 6".)
- **The premise inverts:** the game's main loop runs at full rate (58k+ ticks), yet the user sees no menu
  and Alt+F4 doesn't close it → a RUNNING game that does not RENDER / does not process window input, NOT an
  init-never-completes hang.
- **Critical methodology correction from this:** `-pv` (noninvasive) cdb MISLEADS on a running game — use
  **invasive** (`cdb -p`, then `qd`/`.detach` to leave it running; never `q`, which kills the live debuggee).

---

## 7. The present path — two PROBE K runs; the GOVERNING run shows present SUCCEEDS, frames are presented BLACK

⚠ There are TWO PROBE K runs with OPPOSITE-looking readings. The governing one is run 2 (vtable slots fixed).
Do not cite run 1's "frozen" reading as the present status — it was a probe-vtable-slot bug.

- **PROBE K run 1 (vtable slots 13/18 — `hr_present=ERROR_BUSY`):** read `present_count=3840`/
  `refresh_count=2160`, STUCK across all 65 one-second reads, `GetLastPresentCount` returning
  `hr_present=0x800700AA` (`ERROR_BUSY`) every read. Read at the time as "present frozen, idle because never
  called." **This reading is SUPERSEDED by run 2** — the `ERROR_BUSY` + frozen count was the wrong-slot probe
  failing to read live counters, not present actually frozen. (KI-0028 doc §"PROBE K RESULT" — the early run.)
- **PROBE K run 2 (vtable slots corrected to 16/17 — `hr=0`/S_OK, the GOVERNING reading):**
  `hr_present=0`, `hr_framestats=0` (valid reads); present=0 for the first ~3s (swapchain just created), then
  **`d_present=120, d_refresh=120` PER SECOND for the rest of the run (~115s)** — `present_count` climbed
  0→10516, `refresh_count` (GPU scanout) advanced in lockstep. **Per PROBE K's pre-committed outcome map:
  "both advance → frames ARE presented; the black screen is a render-CONTENT failure, NOT present."** The game
  flips the swapchain at 120fps with real GPU scanout AND the screen is black → **the presented frames ARE
  BLACK.** Present succeeds; the content is empty. (VERIFIED — PROBE K run 2; FINDING §"RE-LOCALIZATION
  (PROBE K run 2)".)
- **What PROBE K run 2 definitively rules out (by direct measurement):** deadlock/hang (heartbeat ran, 9518
  ticks); stuck-in-a-loop/waiting-on-a-producer (call_once guard, Main ticks); window activation (converged
  at second 1); present-submission failure (present called 120×/s, succeeds); "ticks but never reaches
  present" (present IS reached, advancing). The bug is UPSTREAM in the render pipeline (scene draw /
  render-target binding / a render asset), not present, window, or FS control-flow. (VERIFIED — FINDING.)

---

## 8. The filesystem axis — EXONERATED end to end

Multiple independent probes, all pointing the same way:

- **8.1 No FS activity DURING the freeze.** With the FS-op trace widened over a 4-minute freeze gap: ZERO
  `FS_BOOT_TRACE` operations — no entity reads, no enumerations, no game-data loads, no failures. Only
  benign `kcd.log` open-misses (engine's own log, write retry) + a periodic cursor reload. So the wedge is
  NOT a file being served wrong DURING the wedge. (VERIFIED — `grep FS_BOOT_TRACE` over the freeze = 0,
  commit `fb0de7f`; HANDOFF §2.8.)
- **8.2 FS content byte-correct where checked.** PROBE I (FS content diff swap-on vs expected bytes) found
  `diffs=0` for the files it covered. (VERIFIED **for the files PROBE I covered** — NOT a claim that every
  file the engine reads was checked. HANDOFF §2.9.)
- **8.3 The KI-0027-class enumeration over-match is NOT recurring.** The entity enum
  `Libs\Tables/ai/smartEntity/SmartEntity__*.xml matched=577` — every returned name is `smartentity__*.xml`,
  zero non-`__` over-match (KI-0027 was a 528 whole-dir over-match; this is not that). (VERIFIED —
  `FS_BOOT_TRACE` enum entry-name set, commit `fb0de7f`; HANDOFF §2.10.)
- **8.4 NGX's own files never route through kcdx.** `nvngx|dlss|fsr|ngx` served-path count = 0. (VERIFIED —
  PROBE I grep = 0; KI-0028 doc §"PROBE I RESULT".)
- **8.5 The cached-data/mmap suspect is dead.** Slot 40 `FGetCachedFileData` was NEVER called this boot
  (count = 0). (VERIFIED — read-only on P-E log; KI-0028 doc §"P-G.0".)
- **8.6 A REAL FS-resolution defect was found AND FIXED — black persisted (CORRECTION 5).** The
  vanilla-vs-kcdx end-to-end FS map (`_research/ki0028-vanilla-init-fs-map/`) found that the asset index
  keyed every pak entry by the BARE central-directory name, dropping each pak's **bind-root** (the
  mount-point prefix vanilla's `OpenPack` slot-7 auto-derives from the pak's dir). A nested level pak stores
  `leveldata.xml` bare, but the engine requests `Levels/<lvl>/leveldata.xml` → kcdx MISSED every level-
  resource request. `BindRootOf` + `<bind-root>/<name>` keying fixes it (collision-safe: cross-pak
  collisions drop 448→182; cap-112 (c) is the permanent regression). **Live verify:** the documented abort
  is CLEARED — index built (77 paks, 516k entries), 4435 pak reads with ZERO level-resource misses, main
  thread in a healthy `PeekMessageW` pump, no `RaiseException`. **Black screen STILL persists.** (VERIFIED —
  fix `83a9279`; KI-0028 doc CORRECTION 5; `_research/ki0028-vanilla-init-fs-map/POST-FIX-LIVE-CAPTURE.md` +
  `ROOT-CAUSE-bind-root-prefix.md`.)

**Net (the FS axis):** served bytes correct where checked, no in-progress serve at wedge time, the level-
resolution gate now closed end-to-end, NGX files never routed through kcdx, the mmap path unused, no
KI-0027-class over-match. The FS axis is exonerated for the wedge.

---

## 9. The render/shader/PSO axis — EXONERATED (CORRECTION 3 → CORRECTION 4)

- **CORRECTION 3 (then itself overturned):** PROBE W showed the game TICKS ~35/s; PROBE K showed it PRESENTS
  the early frames → frames are presented BLACK (a render-CONTENT failure, not a hang). A REAL defect was
  found + FIXED (`e88a9eb`: the `data/gameshaders/` shader alias was not folded to the indexed `shaders/`
  key → 21 shaders incl. the Scaleform UI shader never loaded); **still black after.** PROBE P hooked
  `ID3D12Device::CreateGraphicsPipelineState` and found the engine creates exactly ONE graphics PSO the
  whole run. (KI-0028 doc CORRECTION 3.)
- **CORRECTION 4 overturned CORRECTION 3's premise AND exonerated the whole served-output class.** PROBE P
  swap-OFF showed `gfx_calls=1` on the WORKING menu too — the engine builds its menu pipelines from an
  on-disk cache through an API PROBE P doesn't hook, so `gfx_calls=1` is NORMAL, not a stall. Six probes
  (R/R2/R3/R4/P) confirmed the shader-cache-validation / offline-precache / runtime-precache / lazy-create /
  device-PSO-create paths ALL run identically swap-ON vs swap-OFF. (VERIFIED — KI-0028 doc CORRECTION 4;
  `_research/ki0028-cshaderman-pso-consumer-recon/FINDINGS.md`.)
- **PROBE S (draw recording):** swap-ON records MORE draws (9500 vs 1383) but **zero indexed-geometry draws**
  (vs 96 swap-OFF) to valid, non-null render targets. (VERIFIED — PROBE S; KI-0028 doc CORRECTION 4.)
- **PROBE T (handle-straddle test, H3a):** forced kcdx handles OUT of the engine's pak-index alias range
  (bit-40 encoding) to test whether NGX/Win32 treats a kcdx opaque handle-id as a real OS handle. **STILL
  BLACK, `draw_indexed=0` unchanged, serve health byte-identical, no fault** → falsifies the straddle AND
  proves the engine treats the kcdx handle as a fully opaque token (never tag-tested, dereferenced, or
  truncated). (VERIFIED — PROBE T; `_research/ki0028-cshaderman-pso-consumer-recon/HANDLE-STRADDLE-LEAD.md`
  §"PROBE T RESULT".)

**Net (CORRECTION 4, VERIFIED):** every kcdx OUTPUT the engine consumes — served bytes (`diffs=0`), handle
value/semantics, sizes, enumeration counts, shader/PSO build paths — is measured identical-or-correct
swap-ON, yet the menu's indexed geometry is never built and the frame composites black. **The divergence is
NOT in what kcdx SERVES; it is in a STATE or init-ORDER the swap perturbs that the geometry-build path
depends on.** The H4-class P-F result only killed kcdx's added THREADS as the cause, never the swap's effect
on engine init order/state.

### 9.1 PROBE P — the engine builds ONE PSO, not hundreds (the shader-system-init localization)
- **PROBE P (`ID3D12Device::CreateGraphicsPipelineState` slot 10 + `CreateComputePipelineState` slot 11, via a
  one-shot `d3d12!D3D12CreateDevice` detour):** swap-ON, `gfx_calls=1` for the ENTIRE run, `comp_calls=0`.
  The one PSO is a trivial blit/present shader (`vs_len=704 ps_len=752`, valid DXBC, `hr=0`). A CryEngine
  menu creates DOZENS-to-HUNDREDS of graphics PSOs; this engine creates exactly ONE — the present blit. So
  the render pipeline gets present up but **never builds the scene/material/UI pipelines.** (VERIFIED — PROBE
  P; FINDING §"PROBE P RESULT".) ⚠ As §9 records, CORRECTION 4's PROBE P swap-OFF later showed `gfx_calls=1`
  on the WORKING menu too — the menu's pipelines load from an on-disk cache via a path slot-10 does not see —
  so `gfx_calls=1` is NORMAL, not itself the stall. The "1 PSO" is a true measurement; its interpretation as
  a stall was corrected.

### 9.2 The shader-enumeration defects — three found + FIXED + verified, black persisted each time
A chain of real FS-enumeration/alias defects on the shader path, each fixed and live-verified, none
sufficient (the screen stayed black after every one — they are permanent correctness gains, not the cause):
- **`data/gameshaders/` alias not folded (`e88a9eb`).** The engine addresses shaders as
  `data/gameshaders/X.ext`; kcdx indexed them under the pak's stored key `shaders/X.ext` and did not fold the
  alias → 21 proven shaders (incl. `scaleform4.ext`, the UI shader) missed, fell to a loose open, errno=2.
  The fold (`data/gameshaders/` → `shaders/`) served the 21. (VERIFIED — FINDING §"ROOT-CAUSE MECHANISM
  (option b CONFIRMED)" + §"FIX LANDED, PARTIAL".)
- **Enum emitted NO directory entries (`d265732`, PROBE Q).** kcdx's index-walk enum returned only files at
  the exact requested level and never a synthetic DIR entry for a subdir, so `FindFirst("Shaders/HWScripts/*.*")`
  returned 0 (the 180 `.cfx` live one level deeper under `cryfx/`); the engine's own `_findfirst64` returns
  subdir entries and recurses. PROBE Q emits a synthetic dir entry per immediate child subdir → the engine
  recursed (`Shaders/HWScripts/*.*` matched=1 `cryfx` → `…/cryfx/*.*` matched=180). (VERIFIED — FINDING
  §"PROBE Q RESULT": the engine recurses; CORRECT fix, NOT sufficient.)
- **`//` double-slash not collapsed (`0249b2e`).** `%ENGINE%/Shaders/Cache/D3D12//*.*` (double slash) failed
  the prefix match → matched=0; collapsing `//` in NormalizeVPath → matched=193. (VERIFIED — FINDING §"ENUM
  FIXES COMPLETE".)
- **After all three: every shader enum succeeds (source tree 180 + compiled cache 193), the engine reads the
  full compiled cache (574 `.cfxb` + 588 `.cfib` served from `%engine%`, header-peeked), and `gfx_calls`
  STILL = 1, screen STILL black.** The `%user%/shaders/cache` misses are a genuinely empty first-run user
  cache (vanilla misses them too) — NOT a kcdx defect. (VERIFIED — FINDING §"ENUM FIXES COMPLETE" +
  §"CACHE IS READ, BLOBS SERVED, STILL NO PSO BUILD".)

### 9.3 The read-family handle behavior — CRT-faithful (the cheap FS-handle bridge FALSIFIED)
- Read kcdx's own read-family impls (`file_handle.cpp` + `read_slots.cpp`, reuse-first off
  `fs-takeover-readslot-abi-recon`): for a pak handle, size/EOF/seek/read/tell are mutually consistent
  (`MintPak` sets `size = pakBytes.size()`); `Gets` returns null at EOF (CRT), `Getc` returns -1 at EOF,
  `FileSize` returns the inflated size never -1 (the KI-0026 fix). The `FGets got=-1` in the log is a
  TraceRead format placeholder, not an error. So a read-handle quirk is NOT why the cache-load gate fails;
  the bytes AND the handle semantics are correct. (VERIFIED — FINDING §"FS-HANDLE-BEHAVIOR BRIDGE FALSIFIED".)

### 9.4 The current frontier — the render-build DISPATCH gate (past the FS layer)
After every shader enum/alias defect is fixed and the full cache serves, the localization (VERIFIED, a clean
phase boundary): **the engine has all shader inputs but never transitions from "shaders enumerated/served"
to "build the render pipelines + draw the menu."** `gfx_calls=1` (present blit only), all JobWorkers idle
(SRW job-wait, no compile/build work dispatched), Main pumping frames normally. Cheap ground-truth narrowing:
the engine writes ZERO compiled `.cfxb` to `%user%/shaders/cache` and only `lookupdata.binversion.txt` (a
cache-version-check write) — it reaches the cache-version check and stops before compile/build dispatch.
**The remaining blocker is a DIFFERENT axis than FS enumeration: what gates the render-pipeline build under
the swap, and why it fires swap-OFF but not swap-ON.** (VERIFIED localization — FINDING §"ENUM FIXES
COMPLETE" + §"NEXT-AXIS DESIGN" + §"CACHE IS READ…"; the causal edge "cache-state → no-dispatch" is INFERRED,
the dispatch tracer is owed to prove it — AP17.)

### 9.5 PROBE U + the after-hook refinement — the FS-OBJECT layer fully exonerated; the surviving class named
- **PROBE U (post-seat reswap watcher):** armed at the seat capturing the swapped object + kcdx's vtable +
  the gEnv pCryPak slot, sampled `[obj+0x00]` and `*(gEnv slot)` every 1s. Swap-ON, still black: EVERY
  sample `vtable_ok=1 global_ok=1`, full-scan `vtable_diverged`/`global_diverged` = 0, through 90+s — while
  the geometry-build ran in its failing state (`draw_indexed=0`, heartbeat alive). **VERDICT: FALSIFIED** —
  the engine does NOT re-point the CCryPak vtable away from kcdx's and does NOT make a different/replacement
  CCryPak the live global. kcdx owns the ONE object end-to-end. (VERIFIED — PROBE U; HANDLE-STRADDLE-LEAD.md
  §"PROBE U RESULT".)
- **REFINEMENT (load-bearing — the seat hook is an AFTER-hook):** `HookedConstructStore`
  (`seating_hook.cpp`) runs the engine's original construct-store helper to COMPLETION first (constructs
  CCryPak + publishes the pointer exactly as vanilla), THEN swaps the vtable. So **the engine's CCryPak
  constructor's side-effects on engine state ALL happened** — "kcdx skips the constructor side-effects" is
  WRONG (the constructor ran). (VERIFIED — `seating_hook.cpp` body read; HANDLE-STRADDLE-LEAD.md §"PROBE U
  REFINEMENT".)
- **The surviving mechanism class (the named current lead, INFERRED — probe owed):** a vtable slot that is
  BOTH a file op AND a STATE MUTATOR, where kcdx implemented the file half correctly and dropped the state
  side-effect. The engine calls a CCryPak method during render/geometry init that in vanilla serves bytes
  AND mutates engine/render state (registers a search path, updates a pak-mount table, sets a ready flag,
  caches a resolved root); kcdx's KCDX slot serves the bytes but does not reproduce the side-effect → bytes
  right, a piece of engine state the build reads left unset. Prime suspects: the THUNK slots kcdx did NOT
  take (15 = ForEachFile inner callback, 101 = CCryPakFindData factory) AND any KCDX slot whose original
  body did more than I/O. (INFERRED — HANDLE-STRADDLE-LEAD.md §"PROBE U REFINEMENT" mechanism class 1.)

### 9.6 Static slot-output diff (A/B/C/D) — 4 return-contract divergences found, ALL consumer-side FALSIFIED
A 4-way parallel static diff of each KCDX slot's return contract vs the engine original it replaces found
four VERIFIED divergences; each was then read on the CONSUMER side and killed as a DIRECT wedge driver — by
reading real consumer bodies, not by inference:
- **A — metadata existence-TIMING (slots 67/70/45/92/93):** kcdx reports every pak vpath EXISTS/sized from
  `CSystem::Init` on, bypassing the engine's pakPriority/location/mount-timing gates. **FALSIFIED:** an xref
  of `gEnv->pCryPak` (`0x18492B850`) found all 44 existence/size consumers — slot 70 = 0 callers, 67 = 41,
  45 = 3 — and EVERY one is an asset/level/data loader; NONE is a window/swapchain/present/display consumer.
  (Recon: `_research/ki0028-metadata-consumer-recon/`.)
- **B — find-handle straddle (slots 63/64/65):** kcdx FindFirst returns a small int; the engine returns a
  refcounted `CCryPakFindData` object. **FALSIFIED:** the engine FindClose body (`0x18097383c`) WOULD fault
  on an int, but ALL 53 genuine find-triplet consumers treat the return opaquely (`-1<h` + pass-back); the
  5 a deref-scan flagged were decompiler false positives, each body-read and cleared. No consumer derefs the
  handle. (Recon: `_research/ki0028-findfirst-straddle-recon/`.)
- **C — un-normalized pak path (slot 1 AdjustFileName):** on a pak hit kcdx returns raw `pName`
  (`%engine%/…`); the engine returns a normalized `Data/`-rooted path. **FALSIFIED + CLOSED:** 37 call sites
  in 31 funcs, every one a file-op consumer, ZERO branch on the string's FORM. AND reading kcdx's own
  `open_slots.cpp` shows the un-normalized return is a DELIBERATE design — `kcdx_FOpen` re-resolves through
  the same `NormalizeVPath`+alias strip, so the form round-trips to the same key; returning a `Data/`-rooted
  string would be the BUG (the KI-0026 failure). (Recon: `_research/ki0028-adjustfilename-consumer-recon/`.)
- **D — pak mtime=0 (slot 66 FGetModificationTime):** kcdx returns epoch for a pak asset; the engine returns
  the entry's DOS time. **FALSIFIED:** 3 vtable consumers (whole-`.text` scan found 0 other callers); none
  compares/gates on the mtime (one stores to a global never read back, one uses it as a `this`, one forwards
  it as an arg). The feared "mtime=0 → cache always-stale → never-settling rebuild" shape does not exist in
  the consumer set. (Recon: `_research/ki0028-mtime-consumer-recon/`.)

**Net (VERIFIED, a strong negative by reading real consumer bodies):** all four return-contract divergences
are killed as DIRECT wedge drivers — no window/present/swapchain/display-mode consumer branches on any
divergent slot value. What static CANNOT settle is an INDIRECT multi-hop path (a divergent value consumed at
load that surfaces at present N stages/threads later) — a runtime-only fact; static slot-output work is
COMPLETE. (FINDING §"STATIC SLOT-DIFF" + §"CONSUMER-SIDE STATIC READ".)

### 9.7 PROBE V / PROBE W — designed observability (the vanilla-differential self-validation)
- **PROBE V (designed, request-stream differential with caller attribution):** add `_ReturnAddress()`
  (module-relative) to the existing `FS_BOOT_TRACE` lines so each request is attributed to the engine
  subsystem making it; run swap-ON and swap-OFF and diff the two streams aligned by caller+path. Passive,
  zero behavioral change. (DESIGNED — HANDLE-STRADDLE-LEAD.md §"PROBE V".)
- **PROBE W (chosen over V — the vanilla-differential self-validation pass):** on an index HIT for a SAFE
  read-only metadata/enum op, ALSO call the captured engine original (idempotent — no handle, no cursor, no
  mutation) and log ONLY when the two answers DIFFER, with caller return address (tag `VANILLA_DIFF`). It
  closes the standing observability gap: the existing logging catches kcdx FAILURES, but kcdx serves
  CORRECTLY while the engine makes a different successful decision — PROBE W flags "correct serve that
  DIFFERS FROM VANILLA." If kept, it is permanent observability infrastructure (the standing tracked want,
  `feedback_debug_reset_frame_after_two_same_axis`), not a throwaway probe. SAFE ops only (existence/IsFolder/
  attributes/stat/size-by-name + the enum set); NEVER call the original alongside open/read/write.
  (DESIGNED — HANDLE-STRADDLE-LEAD.md §"PROBE W".)

---

## 10. Hypotheses raised and their status (the falsification ledger)

| Hypothesis | Status | Killed by |
|---|---|---|
| H2 — engine/environment stalls regardless of kcdx | **FALSIFIED** | P-B (vanilla boots to menu) |
| kcdx's per-frame body (ApplyZone/DrainQueue/reports) causes it | **FALSIFIED** | P-C (bypass body, wedge persists) |
| kcdx-internal lock-order inversion (`g_poolLock`) | **FALSIFIED** | Gate A code read — `g_poolLock` is a documented LEAF, never calls outward under hold; P-A shows no thread holds it |
| PROBE N per-open engine-CRT re-entry (M1) | **FALSIFIED** | P-E (PROBE N removed, wedge persists) |
| Live kcdx-thunk contention (NGX worker inside a kcdx slot) | **FALSIFIED** | P-E (no CCryPak/FOpen/AdjustFileName/thunk frame on any of ~199 threads) |
| Recycled-handle-id corruption (double/bad close) | **FALSIFIED** | zero `double_close`/`bad_handle` logged though `Close()` logs both |
| `FGetCachedFileData` / mmap lifetime (H3c) | **FALSIFIED** | slot 40 never called (count 0) |
| It's a deadlock / lost-wakeup | **FALSIFIED** | P-H heartbeat resumes after a 17s stall (latency-not-deadlock falsifier); Reframe 6 |
| It's recoverable boot LATENCY | **FALSIFIED** | user confirms still black 47 min in; P-J.3 |
| Present is failing/blocked | **FALSIFIED** | PROBE K run 2 (present advances 120fps, GPU scanout, frames presented BLACK) |
| The window-message pump is dead | **FALSIFIED** | PROBE J.4 (`Process.Responding=True`, window visible) |
| `0x869c39` window/display-mode loop is the wedge gate | **FALSIFIED** | PROBE M (exit-condition globals evolve identically swap-on/off) |
| The `0x56628d8/dc` counters are a cross-thread completion handshake the swap stalls | **FALSIFIED** | window-exit-gate-recon (they are a `std::call_once` magic-static guard, same-thread, no awaited producer) |
| The real gate is `GetActiveWindow()==expected HWND` (window never becomes foreground) | **FALSIFIED** | PROBE W run 1 (`WINDOW_PROBE_CONVERGED` at second 1 — window foreground early swap-ON) |
| The handle-straddle (kcdx opaque id used as OS handle, H3a) | **FALSIFIED** | PROBE T (forced out of alias range, still black, no fault) |
| A 2nd/replacement CCryPak or a post-seat reswap routes geometry | **FALSIFIED** | PROBE U (vtable + global never diverge; kcdx owns the one object end-to-end) |
| Any of the 4 slot return-contract divergences (A existence-timing / B find-handle / C un-normalized path / D mtime=0) DIRECTLY drives the wedge | **FALSIFIED** | consumer-side body reads (no window/present/display consumer branches on any divergent value) |
| Shader/PSO build path diverges under the swap | **FALSIFIED** | CORRECTION 4 (R/R2/R3/R4/P all identical swap-on/off) |
| A shader-enum/alias FS defect is the cause | **FALSIFIED as sufficient** | 3 real defects found+fixed (e88a9eb/d265732/0249b2e); all shader enums succeed, full cache serves, still black |
| FS serves wrong content / wrong enumeration at wedge time | **EXONERATED** | §8 (FS-silent freeze; diffs=0 where checked; bind-root fix closed the last real gate, black persisted) |
| H1 — the bug is kcdx-introduced | **VERIFIED** | P-B |
| The FS-takeover SWAP is the differentiator | **VERIFIED** | P-F (swap-off→menu, swap-on→wedge) |
| The wedge is a kcdx-perturbed STATE/init-ORDER the geometry build depends on (not a served output) | **OPEN / current working direction** | CORRECTION 4 + CORRECTION 5 (every served output correct, black persists) |

---

## 11. Real defects found + fixed along the way (each necessary, none sufficient)

These were genuine bugs the investigation surfaced and fixed; **each was verified fixed and the black screen
persisted** — so none is the KI-0028 root cause, but each is a permanent correctness gain:
- **`e88a9eb`** — `data/gameshaders/` shader alias not folded to the indexed `shaders/` key (21 shaders incl.
  the Scaleform UI shader never loaded). (§9.2; CORRECTION 3.)
- **`d265732` (PROBE Q)** — the index-walk enum emitted no synthetic directory entry for a subdir, so a
  single-level `FindFirst` over a tree with deeper files returned 0; the engine never recursed into the
  shader source tree. Emit a synthetic dir entry per immediate child subdir. (§9.2.) ⚠ This fix is currently
  entangled with PROBE Q's diagnostic logging in `find_slots.cpp` — promote the dir-entry emission, drop the
  logging (§"Armed probes" debt below).
- **`0249b2e`** — `NormalizeVPath` did not collapse a `//` double-slash, so `%ENGINE%/Shaders/Cache/D3D12//*.*`
  failed the prefix match (matched=0 → 193). (§9.2.) A clean fix, no probe residue.
- **`83a9279`** — asset index dropped each pak's bind-root, missing every nested-level-pak resource request
  (`Levels/<lvl>/...`). `BindRootOf` + `<bind-root>/<name>` keying; collisions 448→182; cap-112 (c)
  regression. The original abort it cleared: kcdx MISS on `Levels/<lvl>/leveldata.xml` → empty current-level
  record → `C_Game::CreateInstance` empty-record gate → `MessageBoxA` + `RaiseException(0xD2)`. Post-fix
  capture: main thread in a healthy `PeekMessageW` pump, no `RaiseException`, `C_Game::CreateInstance` on
  workers. (§8.6; CORRECTION 5.)

---

## 12. The current working direction (where the trail ends)

> **REFRAMED 2026-07-02 (fresh-frame reassessment after PROBE X falsified).** The render-build-DISPATCH and
> dropped-state-mutator framings below (12.A, preserved) were the 06-23 frontier. A cold fresh-frame re-read of
> the whole evidence base (theories withheld) reset the direction to the SEQUENCER axis (12.B). Both prior
> framings shared a blind spot: each stopped at a file-op or draw-call layer; the divergence lives in engine
> control-flow BETWEEN two boot phases that neither layer sees. The current direction is 12.B.

### 12.B — CURRENT direction: the boot-phase SEQUENCER (2026-07-02, reassessment-grounded)

**What the swap PROVABLY perturbs (from measured facts ONLY):** the transition from "base-asset + UI streaming"
into the next boot phase. The proven chain is narrow and one-directional: swap-ON, the engine streams 65k
textures + 18k XML + shaders, presents a UI/menu compositor at full framerate, and **stops advancing** — it
never begins the work that first requests indexed geometry. `draw_indexed=0` and black are proven CONSEQUENCES
of that stall, not independent faults.

**The unexamined subsystem the proven facts implicate:** the **boot-phase state machine / init sequencer** that
GATES the asset-stream→scene-geometry (menu→level) transition. The render lead looked BELOW it (draws); the
level-load lead looked at its FILE SYMPTOM (`.cgf`=0) — neither observed the sequencer deciding not to advance.
The swap perturbs a piece of STATE that sequencer reads to decide "advance." The NEW "long load then black"
symptom (this session, user-observed, in NO prior evidence doc) is a direct clue about THIS gate: a long stall
in a transition is a property of the transition gate, not of file serving.

**"The level never loads swap-ON" is NOT proven — it is the leading INFERENCE (a trap, §13).** It rests on
FS-trace ABSENCE (`.cgf`=0, `mmrm`=0), never on catching a load trigger fire-or-not. FS_BOOT_TRACE is
file-ops-only — structurally blind to reached-and-early-returned vs never-called. PROBE X after-hooked the
candidate trigger `CResourceList::Load @ 0x4dcb60` and it fired ZERO times on BOTH the black run AND the
working-menu control → it is not the trigger (red herring, like R2). No probe has yet OBSERVED the level-load
trigger firing or not.

**The single most-falsifying next observation (reassessment's pick — NOT another guessed hook):** a **live
invasive main-thread deep stack of the swap-ON process DURING the long-load / pre-black window, plus a swap-OFF
main-thread stack at the same phase, DIFFED.** One capture, ground truth. It NAMES the real sequencer function
for free (the KI-0026 method: identify the real RVA, then read the body) instead of testing one more guessed
function. It has been designed repeatedly and never cleanly obtained — every prior "identical" capture was `-pv`
NONINVASIVE on a possibly-post-AltF4 process (a flagged confound); the last invasive attempts died before
capture. The long-load window is the transition window and has never been sampled. Outcome map: swap-ON main
parked in a level/scene-init frame swap-OFF main already passed → level-load axis CONFIRMED by positive evidence
+ the sequencer named; swap-ON main where no level-load frame appears → "level never loads" FALSIFIED, axis
moves to where the stack actually sits. (cdb crashed once earlier on a post-black idle process — capturing
DURING the long load is the new, never-tried part; use invasive `-p` + `qd`, NEVER `q`.)

**The proven-vs-unproven map (the durable takeaway — do not re-litigate the proven; do not trust the unproven):**
- PROVEN (measured): not a hang/deadlock (ticks ~35/s); present SUCCEEDS at 120fps + GPU scanout, frame is
  presented BLACK; draws execute to valid targets but `draw_indexed=0` (vs 96 swap-OFF), `om_null_rt=0`; the
  ENTIRE shader/PSO axis runs identically both paths (`gfx_calls=1` both); ZERO mesh files read swap-ON; level
  EXISTENCE served correctly + identically both ways; every kcdx-served OUTPUT measured correct (bytes, handle,
  identity, the 4 slot contracts); test plugins NOT the cause (zero-plugin still black).
- UNPROVEN (inference/absence — traps): "the level never loads" (rests on FS absence); "the menu renders over a
  backdrop level that fails to load" (never checked whether the WORKING menu itself reads meshes); any residual
  "FSR2/NGX" instinct (nearest-export noise); the "long load" (brand-new, uncorrelated to any probe).

### 12.A — SUPERSEDED (06-23) frontier framings, preserved for the trail

**The pinned question (OPEN):** what STATE or init-ORDER does the swapped CCryPak perturb — NOT a file it
serves — that the render-pipeline build depends on, such that the engine runs its full loop, has every shader
input enumerated + served + cache-read, yet never builds the scene/material/UI pipelines (`gfx_calls=1`,
workers idle) and presents only black? (KI-0028 doc CORRECTION 4 + 5; FINDING §"ENUM FIXES COMPLETE".)

**The two converging frontier framings (same gate, two named entry points):**
1. **The render-build DISPATCH gate (FINDING's frontier).** Every shader input is present, the engine reaches
   the cache-version check (writes `lookupdata.binversion.txt`) and stops — no compile/PSO-build job is ever
   dispatched (workers idle, zero `.cfxb` written). The owed probe is the **3-layer DISPATCH tracer** armed
   before the swap decision (fires swap-ON and swap-OFF, A/B): Layer A job-queue enqueue, Layer B the
   `.cfxb`-cache CONSUMER (the fn that takes a read cache blob and calls `CreateGraphicsPipelineState` — the
   narrowest hook), Layer C the one-time "render-ready, build pipelines" trigger. Five-outcome map (O3 = empty
   registry despite served files; O4 = the one-shot trigger gated false by an init-ORDER divergence). Requires
   resolving the CShaderMan cache-consumer RVA via the reuse-first ladder first. (FINDING §"NEXT-PHASE PROBE
   SPEC".)
2. **The dropped-state-mutator class (HANDLE-STRADDLE-LEAD's frontier).** A vtable slot that is BOTH a file op
   AND a state mutator where kcdx did the file half + dropped the side-effect (a search-path register, a
   mount-table update, a ready flag, a cached root). The owed probe reads the engine's render/geometry-init
   CCryPak calls for such a slot, starting at THUNK slots 15/101 and any KCDX slot whose original did more
   than I/O. (§9.5; HANDLE-STRADDLE-LEAD.md §"PROBE U REFINEMENT".)

   These are not contradictory: both name "an engine STATE the swap perturbs that gates the render-build,"
   reached either from the dispatch site (1) or from the slot side-effect that would set the gating state (2).
   NOTE (2026-07-02): these remain plausible MECHANISMS for a confirmed sequencer stall, but are downstream of
   first OBSERVING where the stall is (12.B) — do not build either probe before the live stack names the frame.

**Standing constraints on any probe (from PROBE M + Reframe 6):** the divergence is NOT observable in the
wedged stack's own frames/globals (they run identically swap-on/off — the per-frame trap). Observe what the
swap CHANGES in engine state/order, not another per-frame global. A swap-ON-vs-swap-OFF A/B requires the
probe be armed BEFORE the `kcdx-noswap` early-return (the PROBE W/K/P trick) — swap-OFF emits no kcdx FS
trace, so an FS-only diff is impossible by construction.

**The fix constraint (user-stated, binding — HANDOFF §7):** the fix MUST live inside kcdx's full-init
ownership. NO thunk / hand-back to the original engine for any part of init.

**Closure bar (AP17):** KI-0028 does not close until the Resolution names the root-cause MECHANISM in
falsifiable terms — what value is wrong, who writes it, in what order, why the original path makes the wrong
state inevitable. "Boots now" / "black screen gone" is symptom restatement, not root cause.

**Armed-probe no-residue debt (working-artifacts.md — the next session inherits this):** these diagnostics
are CURRENTLY LIVE in the deployed `kcdx.dll` and must be captured-then-removed from live source on closure
(no `#if 0`, no dormant flag): `pso_probe.{h,cpp}` (P), `present_probe.{h,cpp}` (K), `boot_watch.cpp` (W/H),
`dispatch_probe.{h,cpp}` (R), `reswap_probe.{h,cpp}` (U), `drawcall_probe.{h,cpp}` (S), `boot_trace.h`
differential (W), **`levelload_probe.{h,cpp}` (X — 2026-07-02, falsified as a target; capture its red-herring
finding to the archive + remove from source/CMake/`seating_hook.cpp` arm).** `find_slots.cpp` (PROBE Q)
CONTAINS A REAL FIX — promote the synthetic-dir-entry emission, drop only its logging. The `//`-collapse
(`0249b2e`) is already a clean fix. (FINDING §"ARMED PROBES IN SOURCE".)

---

## 13. Standing traps (do not re-walk these)

- **Nearest-export labels are NOISE.** WHGame.dll has no PDB. Every `ffxFsr2ResourceIsNull+0x…` /
  `NVSDK_NGX_UpdateFeature+0x…` frame is the nearest export + a 2–9 MB offset = an unrelated function. Add
  the export RVA before disassembling any offset. NGX/FSR2 is NOT the subsystem. The genuine NVIDIA-driver
  threads sit idle in the REAL modules `NvTelemetryAPI64` / `nvcuda64`. (HANDOFF §2.6; KI-0028 doc PROBE I.)
- **Offset-vs-RVA conflation.** A bare stack offset (`0x36eb39`) disassembled as a raw RVA lands in an
  unrelated function. The real RVA = nearest-export RVA + offset (→ `0x869c39`). The "entity-init"
  identification was this artifact. (KI-0028 doc top CORRECTION; HANDOFF §2.4.)
- **`-pv` noninvasive cdb misleads on a running game.** It catches a fast-moving thread at the same per-frame
  Sleep depth and reads as "wedged in place." Use invasive `cdb -p` + `qd`/`.detach` (NEVER `q` — it kills
  the live debuggee). (Reframe 6; KI-0028 doc.)
- **A 0-byte log on a STILL-RUNNING process is a filesystem-metadata artifact, not a logging failure.** The
  OS lags the on-disk size/mtime until a flush/close boundary while the process holds the handle, even though
  kcdx `fflush`es every line. Read the log AFTER exit, or read kcdx's in-memory state via cdb. (HANDOFF §4.4.)
- **Read the dump, don't bisect by launches.** On a crash with a minidump, run `cdb -z <dmp> -c ".ecxr;
  !analyze -v; k 40"` FIRST. (Standing methodology.)
- **"The level never loads swap-ON" is an INFERENCE from FS-trace absence, not a measured fact (2026-07-02).**
  `.cgf`=0 / `mmrm`=0 says the engine read no mesh files; it does NOT say the engine never TRIED to load the
  level (FS_BOOT_TRACE is file-ops-only — blind to reached-and-early-returned vs never-called). No probe has
  observed the load trigger. `CResourceList::Load @ 0x4dcb60` was the candidate trigger — PROBE X proved it
  fires ZERO times on the WORKING menu too (red herring). Do not treat "level never loads" as settled; it needs
  positive evidence (a live stack showing where main actually is — 12.B).
- **The backdrop-menu premise is unchecked.** "The 9500 non-indexed draws are a menu compositor over a level
  that failed to load" assumes the WORKING swap-OFF menu reads meshes. Nobody has measured whether the working
  menu itself reads `mmrm`/`.cgf`. If it does NOT, "no level loaded" cannot be the black-vs-menu differentiator.

---

## 14. Probe ledger (chronological, each with its settled outcome)

| Probe | What it did | Settled outcome |
|---|---|---|
| P-A | Live cdb, all-thread + Main stacks on the wedged process | Main in `C_Game::CreateInstance` via `HookedUpdate`; no deadlock (no thread on a kcdx lock) |
| P-B | Vanilla (`kcdx.disabled`) control launch | Vanilla → interactive menu → bug is kcdx-introduced (VERIFIED) |
| P-C | Bypass kcdx's per-frame body in `HookedUpdate` | Wedge persists → per-frame body innocent (VERIFIED) |
| P-D | Read-only intersect served files with NGX-class paths | Historical (predates the NGX-is-noise finding) |
| Gate A + P-D | architect-review; remove the live PROBE N confound | `g_poolLock` leaf (no inversion); PROBE N removed (`678fd4f`) |
| P-E | Clean re-launch, PROBE N gone | Wedge persists → M1 (PROBE N re-entry) + live-thunk-contention FALSIFIED; takeover completed (`seat_index_stored entries=307006`) before wedge |
| P-F | `kcdx-noswap` marker (suppress swap + index build) | Swap-off → menu, swap-on → wedge (VERIFIED); 4-differentiator caveat |
| P-H | Per-frame heartbeat + auto-stackdump watcher | Heartbeat advances → Main not hung; later: stalls 17s then RESUMES → not a deadlock |
| P-I | Widen FS trace 600 frames; diff FS content | FS exonerated for covered files (diffs=0); RenderThread on an SRW condvar (one-instant sample); NGX files never route through kcdx |
| P-J / J.2–J.5 | Static-identify frames; invasive cdb 47 min in; Win32 window query; architect redirect | NOT a hang/deadlock/latency — NO-PRESENT + NO-INPUT on a running game; window visible + responding; frame `0x36eb39` mis-ID corrected |
| P-K / K.3 | DXGI present-count delta (no present hook) + invasive settle | Present FROZEN (3840, ERROR_BUSY), idle because the loop never reaches it; no thread in Present |
| PROBE L | Disarm present-probe + watcher threads | Wedge persists → those two threads not the cause (heartbeat tick stayed live — not a clean instrumentation-free test) |
| PROBE M | Live swap-on/off read of `0x869c39` exit-condition globals | Globals evolve IDENTICALLY both runs → the loop is NOT the gate; stop chasing wedged-stack frames |
| (window-exit-gate-recon) | Static re-read of the `0x869c39` exit gate | The counters are a `std::call_once` guard (not a handshake); the gate is `GetActiveWindow()==expected HWND` |
| PROBE W run 1 | Tick-rate + window-foreground probe | Game ticks ~35/s; `WINDOW_PROBE_CONVERGED` at second 1 → window-activation gate FALSIFIED; frames presented black |
| PROBE K run 2 | DXGI present-count delta (slots fixed 16/17) | present advances 120fps + GPU scanout (`d_present=120 d_refresh=120`) → frames ARE presented, BLACK (render-content failure) |
| PROBE P / R/R2/R3/R4 | `CreateGraphicsPipelineState` + shader-cache validation/precache/lazy-create paths, swap-on vs off | All identical (or not run) swap-on/off → render/shader/PSO axis exonerated (CORRECTION 4); `gfx_calls=1` both |
| PROBE S | D3D12 draw recording count | swap-ON more draws (9500 vs 1383) but `draw_indexed=0` vs 96 swap-OFF, om_null_rt=0 both |
| PROBE T | Force kcdx handles out of the pak-index alias range | Still black, no fault → handle-straddle FALSIFIED, kcdx handle is fully opaque to the engine |
| PROBE U | Post-seat reswap/2nd-CCryPak watcher | vtable + global never diverge → kcdx owns the one object end-to-end; refinement: the original constructor RAN (after-hook) |
| (slot-diff A/B/C/D) | Static return-contract diff + consumer-side body reads | 4 divergences found, ALL killed as direct wedge drivers (no window/present consumer branches on them) |
| PROBE V / W | DESIGNED: request-stream differential / vanilla-differential self-validation (not all built) | The standing observability want — flags "correct serve that DIFFERS from vanilla", caller-attributed |
| (shader enum chain) | gameshaders alias fold (`e88a9eb`) + synthetic dir entries (`d265732`, PROBE Q) + `//` collapse (`0249b2e`) | 3 real enum/alias defects FIXED + verified; all shader enums succeed, full cache serves; black persists |
| (bind-root) | vanilla-vs-kcdx FS map → bind-root keying fix (`83a9279`) | Real level-resolution gate (empty-record → `RaiseException(0xD2)`) found + FIXED; black persists (CORRECTION 5) |
| PROBE X | After-hook `CResourceList::Load @ 0x4dcb60` (first level-resource read), armed pre-swap, A/B | `load_calls=0` swap-ON AND swap-OFF (menu) → NOT the menu boot path; RED HERRING (like R2). "Level never loads" stays an inference, unconfirmed. (Reframe 8; 2026-07-02) |

---

## 15. Evidence file index

- **`docs/known-issues/KI-0028-fs-takeover-boot-hang-ui-render-init.md`** — the chronological investigation
  log + the five CORRECTION blockquotes that govern (read the corrections first).
- **`_research/ki0028-fsr2-poll-loop-recon/HANDOFF.md`** — the 2026-06-21 VERIFIED/INFERRED/OPEN split
  (HEAD `f0b1a3f`).
- `_research/ki0028-fsr2-poll-loop-recon/FINDINGS.md` — static recon: the bounded focus poll, the
  NGX-is-noise finding.
- `_research/ki0028-fsr2-poll-loop-recon/FINDING-real-rva-window-mode-loop.md` — the offset-vs-RVA
  correction + PROBE M result.
- `_research/ki0028-fsr2-poll-loop-recon/cdb_pl_probeL_wedge.txt` — the canonical Main wedge stack (§5.1).
- `_research/ki0028-cshaderman-pso-consumer-recon/FINDINGS.md` — the shader/PSO-axis chronological record:
  PROBE R/R2/R3/R4 (shader-cache validation/precache, exonerated), PROBE P (`gfx_calls=1`), PROBE S (draws).
- `_research/ki0028-cshaderman-pso-consumer-recon/HANDLE-STRADDLE-LEAD.md` — PROBE T (handle-straddle
  falsified) + PROBE U (reswap falsified, the after-hook refinement, the dropped-state-mutator class) +
  PROBE V/W designs.
- `_research/ki0028-cshaderman-pso-consumer-recon/KI-0028-MANAGER-RUNDOWN.md` — the render-side probe chain
  (R/R2/R3/R4/P/S); its §5/§6 render-routing conclusion is SUPERSEDED (banner in-file).
- `_research/ki0028-fsr2-poll-loop-recon/CLEAN-ZEROPLUGIN-BASELINE-2026-06-23.md` — the FS-trace-diff
  (`.cgf`=0 swap-ON; test plugins exonerated; the level-load-stall observation that IS an inference).
- `_research/ki0028-fsr2-poll-loop-recon/RECONCILE-render-vs-levelload-2026-06-23.md` — reconciles the render
  vs level-load reads of `draw_indexed=0`; the 06-23 pivot (superseded 2026-07-02 by the sequencer reframe §12.B).
- `_research/ki0028-vanilla-init-fs-map/ROOT-CAUSE-bind-root-prefix.md` + `POST-FIX-LIVE-CAPTURE.md` —
  CORRECTION 5 (bind-root keying fix `83a9279`; the `RaiseException(0xD2)` abort it cleared).
- `_research/ki0028-metadata-consumer-recon/` — slot-diff A (existence-timing) consumer reads, falsified.
- `_research/ki0028-findfirst-straddle-recon/` — slot-diff B (find-handle) all 53 consumers, falsified.
- `_research/ki0028-adjustfilename-consumer-recon/` — slot-diff C (un-normalized path) 31 funcs, falsified+closed.
- `_research/ki0028-mtime-consumer-recon/` — slot-diff D (pak mtime=0) 3 consumers, falsified.
- `_research/ki0028-window-exit-gate-recon/` — the `std::call_once`-guard correction + the `GetActiveWindow`
  gate read (itself later falsified by PROBE W).
- `_research/ki0028-findfirst-replay-contract-recon/` — find-slot replay/ABI contract recon.
- `_research/probe-archive/ki0028-probeM-loop_state_probe.{h,cpp}` — retired PROBE M.
- `_research/probe-archive/ki0028-ph-*` — retired P-H heartbeat/watcher.

---

## 16. One-paragraph summary for the next investigator

KI-0028: with kcdx's filesystem takeover (the `CCryPak` vtable swap) live, KCD2 runs its full update loop at
~35 ticks/s on a visible, OS-responsive window and **presents the swapchain at 120fps with real GPU scanout**
(PROBE K run 2) — yet every presented frame is BLACK, because the engine never builds the scene/material/UI
render pipelines (`gfx_calls=1`, the present blit only; all compile workers idle). It is NOT a crash, hang,
deadlock, latency, present-failure, or window-activation failure — all measured and falsified. The bug is
kcdx-introduced (vanilla boots fine) and the vtable swap is the differentiator (P-F). Every kcdx OUTPUT the
engine consumes — served bytes (`diffs=0`), handle value/semantics (PROBE T), object identity (PROBE U),
sizes, enumeration counts, the four slot return-contracts (A/B/C/D, consumer-read), and the shader/PSO build
paths (R/R2/R3/R4/P) — has been measured correct or identical swap-on/off. Four real FS/shader/enum defects
found along the way were fixed (e88a9eb, d265732, 0249b2e, 83a9279) without clearing the black screen — so
the engine now has every shader input enumerated, served, and cache-read, and STILL builds one PSO.
**Reframed 2026-07-02 (§12.B):** the swap PROVABLY perturbs the transition from asset/UI streaming into the
phase that first requests geometry — the engine completes streaming + presents a UI compositor, then STOPS
ADVANCING. The unexamined subsystem is the boot-phase SEQUENCER that gates that transition (the render lead
looked below it at draws; the level-load lead looked at its file symptom `.cgf`=0 — neither observed the
sequencer). "The level never loads" is an INFERENCE from FS-trace absence, NOT proven — PROBE X after-hooked
the candidate trigger `CResourceList::Load` and it fired zero times on the WORKING menu too (red herring). The
single most-falsifying next observation is a LIVE INVASIVE main-thread deep stack captured DURING the new
long-load / pre-black window, swap-ON vs swap-OFF, diffed — it names the real sequencer frame by ground truth
instead of guessing another hook (invasive `-p` + `qd`, never `q`; capture during the long load, never yet
sampled). The wedged-stack frames/globals run identically swap-on/off (PROBE M — the per-frame trap), so any
probe is armed before the swap decision and observes what the swap CHANGES in engine state/order, A/B. The fix
must stay inside kcdx's full-init ownership (no thunk-back); closure requires a falsifiable root-cause mechanism
paragraph (AP17), not "the black screen is gone."
