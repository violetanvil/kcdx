# KI-0028 — Boot wedge investigation: HANDOFF

**Date written:** 2026-06-21
**Bug:** KI-0028 (`docs/known-issues/KI-0028-fs-takeover-boot-hang-ui-render-init.md`, status: OPEN)
**HEAD at handoff:** `f0b1a3f`
**Author's note on rigor:** This document separates VERIFIED facts (directly observed, end-to-end,
this investigation) from INFERRED claims (reasoned but not fully proven) from OPEN questions. Where a
prior commit or the KI body states something with more confidence than the evidence supports, this
document supersedes it and says so. Do not act on an INFERRED claim as if VERIFIED.

> **CORRECTION — 2026-06-21 (supersedes the "entity-init" identification throughout this doc and the
> KI body).** The wedge-stack frames `WHGame!ffxFsr2ResourceIsNull+0x36eb39` / `+0x36ff17` / `+0x36af90`
> are `ffxFsr2ResourceIsNull` (a NEAREST-EXPORT label, §2.6) **+ offset** — NOT raw RVAs. The prior
> work stripped the prefix and disassembled the bare offset `0x36eb39` AS a raw RVA, landing in an
> unrelated entity-name stub (which holds `"dummy_no_ai"`/`"player"`/GUID strings by coincidence), and
> concluded "entity/AI init." **That identification is an offset-vs-RVA conflation artifact.** Real RVA
> = export `0x4fb100` + offset → the wedge fn is **`0x869c39`**, a **window/display-mode/fullscreen**
> function (reads `r_Fullscreen`, polls the `0x492b890` window-manager singleton). Proven 3 ways:
> (1) `0x4fb100+0x36af90 = 0x866090` = Main's confirmed focus-poll RIP (§2.5); (2) raw `0x36eb39` has 0
> back-edges, real `0x869c39` has the enclosing loop; (3) `0x869c39` carries `r_Fullscreen` + the
> window-mgr singleton KI line 436 already ID'd. **Every "entity-init"/"CreateInstance entity
> construction" claim below (2.4 and downstream) is downgraded to "window/display-mode loop."** Full
> read + the pinned mechanism: `FINDING-real-rva-window-mode-loop.md` (this dir). The offset-vs-RVA trap
> applies to EVERY `ffxFsr2ResourceIsNull+0x…` / `NVSDK_NGX_…+0x…` frame in the trail — add the export
> RVA before disassembling.

---

## 1. The symptom (what the user observes)

On launching KCD2 with the kcdx engine built at/after the KI-0027 fix (`4befc07`):
- Audio plays.
- No menu / no video renders — black screen.
- The window does not respond to input; the user kills the process via Task Manager (or it is left
  running until cdb detaches).

This is reproducible across every kcdx-enabled launch in this investigation. A VANILLA launch
(kcdx disabled via the `kcdx.disabled` marker) boots to an interactive menu — VERIFIED (P-B, below).

---

## 2. VERIFIED facts (directly observed, end-to-end, this investigation)

Each fact cites its evidence. "VERIFIED" means observed directly this investigation, not inferred.

### 2.1 The bug is kcdx-introduced
- VANILLA (kcdx-disabled) boots to an interactive main menu; kcdx-enabled wedges. Same game, same
  machine, kcdx the only variable. (P-B — `kcdx.disabled` marker launch.)

### 2.2 The main thread is NOT stalled at the wedge — it keeps ticking
- The kcdx per-frame heartbeat (`BOOT_WATCH heartbeat`, emitted from `HookedUpdate` via
  `BootWatchTick()` at `hooks.cpp:1033`) advances continuously. In the most recent run (PROBE L,
  session `11-13-16`): tick=1 at 11:14:01 → tick=41686 at 11:22:59, on `tid=7708`, with NO stall.
  (VERIFIED — `kcdx-dev_2026-06-21_11-13-16.log`, 537 heartbeat lines.)
- The heartbeat thread id (`44480`/`0xadc0` in an earlier run, `7708` in PROBE L) is the SAME thread
  cdb labels "Main". So the main thread runs the full per-frame update loop for many minutes; it is
  not frozen. (VERIFIED — heartbeat tid == cdb "Main" tid, cross-checked in two runs.)

### 2.3 At the wedge, Main is in `C_Game::CreateInstance`, reached through kcdx's update hook
- Live invasive cdb on the wedged process (multiple runs, most recently PID 18100, PROBE L) shows
  Main's stack:
  ```
  ntdll!NtDelayExecution → RtlDelayExecution → KERNELBASE!SleepEx+0x91
  WHGame!…+0x36af90                          (window/focus poll, RVA 0x865fb4)
  WHGame!wh::game::C_Game::CreateInstance+0x2e8c63
  WHGame!wh::game::C_Game::CreateInstance+0x2e8d7d
  WHGame!…+0x16cce2
  kcdx!kcdx::hooks::HookedUpdate+0x94a       (kcdx per-frame update hook)
  WHGame!…+0x16c7a0                          (engine update dispatcher)
  WHGame!…+0x36eb39                          (entity-init fn — see 2.4)
  WHGame!…+0x36ff17
  KingdomCome+0x36db / +0x4ad5 / +0x898a (main)
  ```
  (VERIFIED — `cdb_pl_probeL_wedge.txt`, kcdx PDB loaded, frames resolved.)
- This stack is byte-for-byte identical across runs (P-A and PROBE L). The wedge shape is stable.

### 2.4 ~~`0x36eb39` is the entity/AI-init function~~ — WITHDRAWN (offset-vs-RVA artifact); the real frame is a WINDOW/DISPLAY-MODE loop at RVA 0x869c39
- **WITHDRAWN.** The stack frame is `ffxFsr2ResourceIsNull+0x36eb39`; `0x36eb39` is an OFFSET, not an
  RVA (see the CORRECTION at the top). The prior read disassembled raw `0x36eb39` (an unrelated entity-
  name stub holding `"dummy_no_ai"`/`"player"`/GUID strings) and mis-identified the wedge as entity-init.
- **The real wedge frame is RVA `0x869c39`** (= `ffx export 0x4fb100 + 0x36eb39`): a window/display-mode
  function that reads the `r_Fullscreen` cvar and polls the `0x492b890` window-manager singleton, with an
  enclosing loop around the wedge call site. (VERIFIED — `disasm_36eb39_outer_loop.py` /
  `disasm_869c39_exit_cond.py`; 3-way cross-check in the CORRECTION above.)
- So the code Main runs at the wedge is **window/display-mode bring-up**, NOT entity construction.

### 2.5 The window/focus poll at the TOP of the stack is BOUNDED, not the infinite loop
- The function at RVA `0x865fb4` (the `SleepEx` caller, labeled `…+0x36af90` on the stack) is a
  `GetActiveWindow`-vs-expected-handle poll that runs AT MOST 5 iterations (`cmp edi,5; jl`), ~25ms,
  then returns. It is NOT an infinite wait. (VERIFIED — body read, `FINDINGS.md`, instructions
  decoded at 0x866021–0x86609e.)
- Therefore the infinite repetition is an OUTER loop higher in the stack re-running this chain — see
  OPEN questions (the outer back-edge is NOT yet read).

### 2.6 The "NGX / FSR2" frame labels are nearest-export NOISE
- WHGame.dll has no PDB; cdb labels addresses by the nearest export below them. Frames labeled
  `NVSDK_NGX_UpdateFeature+…` and `ffxFsr2ResourceIsNull+…` are 2–9 MB past those exports — unrelated
  functions. (VERIFIED — offset magnitudes; cross-checked against KI-0026's identical pattern.)
- Genuine NVIDIA-driver threads DO exist in the process but are in idle waits, in the REAL modules
  `NvTelemetryAPI64` and `nvcuda64` — not the wedge. (VERIFIED — `cdb_pl_probeL_wedge.txt` resolved
  module names.)
- CONSEQUENCE: any claim that "NGX/FSR2 is the problem" rests on a mislabel. The earlier commits
  `ae08c0b` ("wedge is NGX/FSR2 UpdateFeature") and the Reframe-4/5 NGX framing are SUPERSEDED by
  this — do not treat NGX/FSR2 as the subsystem.

### 2.7 The FS-takeover SWAP is on the differentiating path
- A `kcdx-noswap` marker suppresses ONLY the vtable swap + index build; everything else kcdx does
  (ctor bracket, worker threads, ready-event, overlay map) runs identically. With the marker present
  (swap OFF), boot reaches an interactive menu; without it (swap ON), boot wedges. (P-F — VERIFIED
  that swap-off reaches menu, swap-on wedges.)
- CAVEAT (see 4.1): P-F's two arms differ by MORE than just the FS dispatch. This fact establishes
  "the swap path is involved", NOT "the FS dispatch alone is the cause".

### 2.8 During the wedge there is NO filesystem activity
- With the FS-op trace window widened to cover the freeze, a 4-minute freeze gap had ZERO
  `FS_BOOT_TRACE` operations — no entity reads, no enumerations, no game-data loads, no failures.
  The only freeze-period FS was benign `kcd.log` open-misses + a periodic cursor reload. (VERIFIED —
  `grep FS_BOOT_TRACE` over the freeze window = 0, commit `fb0de7f`.)
- CONSEQUENCE: the wedge is NOT a file being served wrong DURING the wedge. Whatever the swap
  perturbs, the perturbation is not an in-progress file op at wedge time. (This does NOT rule out a
  file served wrong EARLIER in boot whose effect surfaces later — see OPEN questions.)

### 2.9 FS content served by the swap was byte-correct where checked
- PROBE I (FS content diff swap-on vs the expected bytes) found diffs=0 for the files it covered.
  (VERIFIED for the files PROBE I covered — NOT a claim that every file the entity system reads was
  checked; see OPEN questions.)

### 2.10 The KI-0027-class enumeration over-match is NOT recurring here
- The entity enum `Libs\Tables/ai/smartEntity/SmartEntity__*.xml` returned `matched=577`, and every
  returned name is `smartentity__*.xml` — zero non-`__` over-match. (KI-0027 was a 528-entry
  whole-directory over-match; this run is not that.) (VERIFIED — `FS_BOOT_TRACE` enum entry-name set,
  commit `fb0de7f`.)

### 2.11 No crash — it is a wedge
- The crash zip at the wedge timestamp is the watchdog's kill-time snapshot; the BugSplat log inside
  is empty (BOM only). No engine exception. (VERIFIED — `crash_2026-06-21_11-13-16.zip` →
  `crash/bugsplat_C28L17R6.log` is 0.0 KB.)

---

## 3. The PROBE L result (most recent probe) — stated precisely

**Intent:** P-F changed 4 things between its swap-on/off arms (see 4.1). PROBE L was meant to disarm
the two diagnostic threads to test whether they (not the FS swap) cause the wedge.

**What was ACTUALLY disarmed (VERIFIED from the PROBE L log, NOT assumed):**
- `PresentProbeStart()` — disarmed. 0 `PRESENT_PROBE` lines in the log. CONFIRMED off.
- `BootWatchStart()` (the watcher thread that auto-dumps stacks on a heartbeat stall) — disarmed.
  0 `BOOT_WATCH_STALL` / `BOOT_WATCH_RESUMED` lines. CONFIRMED off.
- **`BootWatchTick()` (the per-frame heartbeat emitter) — NOT disarmed.** It is a SEPARATE call site
  (`hooks.cpp:1033`, inside `HookedUpdate`), which I did not touch. It ran the whole time (537
  heartbeat lines). So PROBE L did NOT remove all diagnostic instrumentation — the per-frame
  heartbeat logging stayed live.

**Result (VERIFIED):** the wedge persists (black screen; Main stack identical to P-A, 2.3).

**What PROBE L DOES establish (VERIFIED):**
- Removing the present-probe thread and the boot-watcher stall-dump thread does NOT change the wedge.
- So those two threads are not the cause.

**What PROBE L does NOT establish (CORRECTION to commit `f0b1a3f`'s message):**
- Commit `f0b1a3f` says "probe threads EXONERATED" and frames it as both probe threads disarmed.
  That is OVERSTATED: the per-frame heartbeat tick (`BootWatchTick`) was still firing every frame.
  PROBE L did not test a fully-instrumentation-free boot.
- It remains POSSIBLE (not tested) that the per-frame heartbeat tick itself contributes. Considered
  LOW likelihood (it is a single atomic increment + a once-per-wall-second log line, and it also runs
  in the pre-PROBE-L wedging builds), but it is NOT ruled out by a probe.

---

## 4. INFERRED claims (reasoned, NOT fully verified) — do not treat as proven

### 4.1 "The FS swap is the sole cause of the wedge" — INFERRED, NOT proven
- P-F (2.7) proves the swap PATH is involved, but its swap-ON arm also arms the BootWatch thread, the
  PresentProbe thread, and runs an `INFINITE WaitForSingleObject` (the asset-index build,
  `BuildAssetIndexAtSeat`) on the main thread inside `CSystem::Init`. The swap-OFF arm skips all four.
  So P-F isolated 4 differentiators, not 1. PROBE L removed 2 of them (present-probe, watcher thread)
  and the wedge persisted → narrows to {FS dispatch, the index-build INFINITE wait}. The index-build
  wait has NOT been independently suppressed and tested. So "the FS dispatch alone causes it" is not
  proven; it is one of two remaining candidates.

### 4.2 The outer loop IS now read (RVA 0x869c39) — its exit condition is a completion-token spin; the ACTOR is still INFERRED
- **UPDATE (2026-06-21): the outer loop has been read.** The focus poll (2.5) is bounded; the enclosing
  loop is in the real wedge fn `0x869c39` (the window/display-mode fn — §2.4 corrected). It re-runs
  WHILE a counter (`0x56628d8`/`0x56628dc`, `.data`) is `!= -1`, and exits (`ret`) when it is `-1`.
  Helper `0x1c1e988` (EnterCriticalSection-guarded) flips the counter to `-1` only when it reads 0;
  helper `0x1c1e91c` registers a task id into the counter + a TLS slot. So it is a critical-section-
  guarded **producer/consumer completion handshake**: the loop spins waiting for a registered task to
  complete (counter → `-1`); under the swap it never does. (VERIFIED static — `disasm_869c39_exit_cond.py`.)
- **INFERRED, NOT proven:** WHICH task/producer, WHICH thread completes it, and HOW the swap stalls it.
  Those are runtime facts (the live counter value + the critical-section owner + who registered the id) —
  the owed live probe. It is also NOT yet proven that this loop's own spin is the wedge vs. the loop
  returning each frame and being re-entered from above (the live counter value decides this).

### 4.3 "The wedge is compute/sync, not I/O" — PARTIALLY supported
- 2.8 (no FS during the freeze) is VERIFIED. But "compute/sync" is a characterization of what's left
  after removing I/O; the actual blocking primitive at the wedge is a `SleepEx` inside the bounded
  focus poll, re-entered every outer-loop iteration. Whether the outer loop spins on a compute value,
  a cross-thread sync object, a state flag, or an entity-registry value is NOT determined.

### 4.4 The "0-byte log" episode — a READING ARTIFACT, now resolved; record it so it is not repeated
- During the live PROBE L run, PowerShell reported the engine logs (`kcdx_…`, `kcdx-dev_…`) as 0
  bytes for ~10 minutes while the process ran. This led to an (incorrect) line of reasoning that the
  engine had wedged before logging. **That was wrong.** After the process exited, the same files were
  461 KB / 11.9 MB — fully populated, with the banner, suite summaries, and 537 heartbeat lines.
- VERIFIED conclusion: the on-disk file SIZE/mtime reported by the OS lagged the actual writes (the
  directory entry was not updated until a flush/close boundary), even though kcdx `fflush`es every
  line (`log.cpp:238`). The log was being written all along; the 0-byte reading was a filesystem
  metadata artifact, not a property of kcdx's logging.
- HANDOFF LESSON: do NOT infer "logging failed / engine wedged early" from a 0-byte log size on a
  STILL-RUNNING process. Read the log after the process exits, or read kcdx's in-memory log state via
  cdb. The size field is unreliable mid-run.

---

## 5. What has been TRIED (probes, in order) and what each settled

| Probe | What it did | VERIFIED outcome |
|-------|-------------|------------------|
| P-A | Live cdb on the wedged process, all-thread + Main stacks | Main wedged in `C_Game::CreateInstance` via `HookedUpdate`; no deadlock (no thread on a kcdx lock) |
| P-B | Vanilla (kcdx.disabled) control launch | Vanilla boots to interactive menu → bug is kcdx-introduced |
| P-C | Bypass kcdx's per-frame body in `HookedUpdate` | Wedge persists → kcdx's per-frame work (ApplyZone/DrainQueue/report blocks) is not the cause |
| P-D | Read-only: intersect kcdx-served files with NGX-class paths | (Historical — predates the NGX-is-noise finding 2.6) |
| P-E | Clean re-launch after removing an earlier probe confound | Wedge persists; 3 threads parked (later shown to be nearest-export-mislabeled driver/anon frames) |
| P-F | `kcdx-noswap` marker: suppress swap + index build | Swap-off → menu; swap-on → wedge. Establishes the swap path is involved (CAVEAT 4.1) |
| P-H | Boot-progress heartbeat + auto-stackdump watcher | Heartbeat advances continuously → Main not stalled (2.2) |
| P-I | Widen FS trace; diff FS content | FS content diffs=0 where covered (2.9) |
| P-J | Static-identify the frames Main/RenderThread sit in | Frames are anonymous/compute leaves; `0x36eb39` = entity-init (2.4); NGX labels = noise (2.6) |
| P-K | DXGI present-count probe (no present hook) | present_count frozen at 3840, d_present=0 → present is idle because never called, not blocked. NOTE: this probe was DISARMED in PROBE L; its earlier reading stands but was on a pre-PROBE-L build |
| (logging upgrade) | FS-op trace names file+result; FindFirst logs entry names | Ruled out KI-0027-class over-match (2.10); proved freeze is FS-silent (2.8) |
| PROBE L | Disarm present-probe + watcher threads (NOT the heartbeat tick) | Wedge persists → those two threads not the cause (§3, with the correction that the heartbeat tick stayed live) |

---

## 6. OPEN questions (what is genuinely unknown)

1. **~~What is the outer-loop exit condition?~~ READ (2026-06-21).** It is a critical-section-guarded
   completion-token spin in the window/display-mode fn `0x869c39`: loop re-runs while counter
   `0x56628d8`/`0x56628dc` `!= -1` (§4.2). The remaining unknown is the live counter value + the
   producer that should flip it — a runtime fact (owed live probe), not a static one.
2. **Of P-F's two remaining differentiators (FS dispatch vs the index-build INFINITE wait on Main),
   which causes the wedge?** Not separated by a probe — BUT the index-build wait is near-eliminable
   from existing logs: `BuildAssetIndexAtSeat()` logs `seat_index_stored entries=307006` on wedging
   runs (`src/fs_takeover/seating_hook.cpp:267`), i.e. its `WaitForSingleObject(gate, INFINITE)`
   demonstrably RETURNED — a wait that returns is not where Main is stuck. This re-collapses the
   suspect to the FS-dispatch/swap (consistent with P-F). Still worth a clean P-L.2 confirm, but
   weaker than "two equal candidates."
3. **If it is the FS dispatch: what state/value does the swapped CCryPak answer differently that the
   window/display-mode completion-handshake consumes?** 2.8 rules out an in-progress file op AT wedge
   time, but not a value served wrong EARLIER, or a NON-FS side effect of the swap, that stalls the
   producer the `0x869c39` loop waits on. The handshake is cross-thread (critical section + TLS id),
   so the swap likely perturbs the producer thread or a state it needs — to be observed live (Q#1).
4. **Does the menu ever render if left long enough (latency) or never (true wedge)?** The heartbeat
   runs 9+ minutes with no menu, which strongly suggests a true wedge, but a definitive "never" has
   not been asserted with a controlled long wait + a menu-ready signal.
5. **The `game/kcd.log` (32 KB) inside the PROBE L crash bundle has NOT been read this session.** It
   is the engine's own log for the wedging run and may carry engine-side state at the wedge. Unread.

---

## 7. Constraints on the fix (user-stated, binding)

- The fix MUST live inside kcdx's full-init ownership. NO thunk / hand-back to the original engine
  for any part of init. ("We own the full init, full stop, don't thunk.")
- Per AP17, KI-0028 does not close until the Resolution names the root-cause MECHANISM in falsifiable
  terms (what value is wrong, who writes it, in what order, why the original path makes the wrong
  state inevitable). "Boots now" / "black screen gone" is symptom restatement, not root cause.

---

## 8. Current repository / environment state

- **HEAD:** `f0b1a3f` (PROBE L committed — see the §3 correction; the commit MESSAGE overstates
  "exonerated", this handoff corrects it).
- **Live source has the PROBE L disarm in place** (`seating_hook.cpp`: `BootWatchStart()` +
  `PresentProbeStart()` commented out). Before ANY next launch that needs the present probe or the
  stall-dump watcher, re-arm both (uncomment, remove the `=== DIAGNOSTIC (PROBE L) ===` block). The
  per-frame heartbeat tick (`hooks.cpp:1033`) was never disarmed and stays live.
- **Deployed engine DLL** at the live install is the PROBE L build (hash
  `6F86DC5667151B7BB42E28408F385DC84951260A9E1A6A202227168FB6A2865C`).
- **The game process from the PROBE L run has exited.** Its logs are now fully written:
  `kcdx-dev_2026-06-21_11-13-16.log` (11.9 MB), `kcdx_2026-06-21_11-13-16.log` (461 KB), and the
  crash bundle `crash/crash_2026-06-21_11-13-16.zip` (contains both + `game/kcd.log` 32 KB +
  per-plugin logs).
- **Build/deploy:** `pwsh ./build.ps1`; deploy `build/Release/kcdx.dll` →
  `<game-bin>/kcdx-engine/kcdx.dll`, hash-verify with `Get-FileHash`. Dev mode is ON
  (`<game-bin>/kcdx-engine/engine.toml` `dev_mode = true`). The agent builds/deploys/reads logs; the
  user only launches.

## 9. Evidence file index (all under `_research/ki0028-fsr2-poll-loop-recon/`)

- `FINDINGS.md` — static recon: the focus poll body, the NGX-is-noise finding, `0x36eb39` = entity-init.
- `cdb_pl_probeL_wedge.txt` — PROBE L live invasive cdb capture (Main stack = §2.3).
- `cdb_pl_logger_state.txt` — incomplete (cdb symbol-read attempt that did not produce output; the
  0-byte-log question was resolved by §4.4 instead, not by this file).
- `cdb_pe_*.txt`, `cdb_pj*.txt`, `cdb_pk_present_state.txt` — earlier captures.
- `disasm_*.py` — capstone/pefile recon scripts (reuse-first per `reverse-engineering.md`).
- PROBE L run logs (live install, also in the crash zip):
  `<game-bin>/kcdx-engine/logs/kcdx-dev_2026-06-21_11-13-16.log` + `kcdx_…` + `crash/crash_…_11-13-16.zip`.
