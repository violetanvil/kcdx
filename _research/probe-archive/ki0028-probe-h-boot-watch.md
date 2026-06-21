# PROBE H — KI-0028 boot-progress watcher + auto-stackdump

**Verdict:** the KI-0028 "boot hang" is NOT a deadlock and NOT a wedged Main thread.
The Main thread is in a **non-progressing `SleepEx` poll-loop inside
`C_Game::CreateInstance` → FSR2 code** (`ffxFsr2ResourceIsNull` neighborhood),
polling for a condition that, under the FS takeover, never becomes true.
`CreateInstance` never returns → no menu, no message pump (Alt+F4 ignored).
**Known-issue:** `docs/known-issues/KI-0028-fs-takeover-boot-hang-ui-render-init.md`.
**Ran:** 2026-06-20 22:34–22:38, live cdb on the still-running process.

## Question

Is the ~20:29:43-class boot wedge a PERMANENT deadlock or pathological LATENCY?
(The captured logs could not tell — one 37s-late snapshot ≠ "never wakes".)

## Wiring (reconstruct from here — do NOT leave it in live source)

Two parts, both in `src/fs_takeover/boot_watch.{h,cpp}` (removed on retire):

- **H1 heartbeat** — `BootWatchTick()` called every frame from `HookedUpdate`
  (`src/hooks.cpp`, just before `g_orig_update`). Advances an atomic tick counter
  + a `GetTickCount64()` last-ms; emits one `BOOT_WATCH heartbeat tick=N wall_s=S`
  line per wall-second (integer-second transition edge — `floor(now_s) != floor(last_s)`,
  NOT a timer, satisfies logging.md/polling.md). Its CESSATION = wedge onset; its
  RESUMPTION after a stall = the decisive LATENCY falsifier.
- **H3 watcher** — `BootWatchStart()` armed once at FS-takeover seating
  (`src/fs_takeover/seating_hook.cpp`, after the `g_swapped` CAS). Spawns a
  dedicated watcher thread that polls the heartbeat's last-ms every 250ms; on a
  ≥10s stall it dumps every thread's stack (onset), again at +30s, then exits.

**Gate A binding discipline (the load-bearing correctness point):** every
`LOG_*_KV` takes the log stream mutex (`src/log.cpp:520/529/538`). Suspending a
thread mid-log and then logging its frames from the dumper DEADLOCKS (the holder
is suspended, can't release). So the dumper does, PER THREAD:
`SuspendThread → GetThreadContext → WalkToBuffer (alloc-free, NO logging) →
ResumeThread`, and only AFTER every thread is resumed emits the buffered frames.
`WalkToBuffer` reuses crash_guard's x64-native-unwinder walk (RtlLookupFunctionEntry
+ RtlVirtualUnwind + ReadProcessMemory-guarded leaf pop) writing pc→buffer instead
of logging. Frames are pc + module_rva, offline-symbolizable vs WHGame.dll.

## Outcome → meaning (pre-committed)

- heartbeat RESUMES after a stall → LATENCY (decisive).
- heartbeat NEVER resumes + two identical zero-progress dumps → DEADLOCK.
- heartbeat NEVER stalls → Main update tick is healthy; the wedge is NOT a hung
  Main thread (this is what happened — see below).

## Result — heartbeat healthy; Main in a SleepEx/FSR2 poll-loop (NEITHER deadlock nor latency)

- **197 heartbeats, tick 1→47539, 3m16s, no gap >2s; watcher never fired.** The
  Main update tick is alive at ~240/sec the whole run. (PROVEN — boot_watch heartbeat log)
- **Main thread (`90c4.adc0`) stack:** `NtDelayExecution ← RtlDelayExecution ←
  KERNELBASE!SleepEx ← WHGame!ffxFsr2ResourceIsNull+0x36af90 ←
  C_Game::CreateInstance+0x2e8c63 ← +0x2e8d7d ← ffxFsr2ResourceIsNull+0x16cce2 …`.
  Top frame is a TIMED sleep (`SleepEx`/`NtDelayExecution`), NOT an SRW condvar wait.
- **Two samples 2s apart are byte-identical** → a non-progressing loop, not progress.
- The heartbeat comes from a DIFFERENT thread pumping `CGame::Update` while Main is
  still inside `CreateInstance` — so "tick alive" proved Main is not HUNG, correctly
  distinguishing this SLEEP-LOOP from a lost-wakeup deadlock.

## Next (open in KI-0028)

Root-cause the FSR2 poll: WHAT condition does `ffxFsr2ResourceIsNull+0x36af90`'s
caller loop on, and what does the FS takeover serve FSR2 init that makes it never
satisfy? The wait mechanism is now PINNED (a SleepEx retry-poll inside CreateInstance,
not an SRW condvar) — narrows the disasm target to the loop around CreateInstance+0x2e8c63.

## Captures (this directory)

- `ki0028-ph-boot_watch-heartbeat.txt` — the 197-line heartbeat continuity proof.
- `ki0028-ph-live-allthreads-22-38.txt` — all-thread `~*k` at 22:38 (Main + JobWorkers).
- `ki0028-ph-main-renderthread-deep-22-38.txt` — Main deep stack (the SleepEx/FSR2 loop).
