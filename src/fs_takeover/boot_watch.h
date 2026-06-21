#pragma once

// === DIAGNOSTIC (PROBE H) — KI-0028 boot-progress watcher + auto-stackdump ===
//
// WHY: with the FS takeover live, boot progresses ~41s (the update tick fires,
// suite reaches 320) then the dev log goes SILENT at the wedge onset, with the
// RenderThread/main/ShaderCompile parked in NVSDK_NGX_UpdateFeature /
// C_Game::CreateInstance SRW-condvar waits. The open fork the captured logs
// CANNOT resolve: is that wait PERMANENT (deadlock) or LATENT (the takeover
// makes NGX/FSR2 init take minutes)? A single 37s-late manual cdb snapshot
// proves "still parked at T", not "never wakes".
//
// This probe answers it from logs alone (no eyeball):
//   H1 — a heartbeat the main-thread update tick advances (BootWatchTick). Its
//        CESSATION is the wedge signature; its RESUMPTION after a stall is the
//        DECISIVE "it was only latency" falsifier.
//   H3 — a dedicated WATCHER THREAD that, when the heartbeat stalls for N=10s,
//        dumps every thread's stack (onset), then again at +30s, so "zero
//        progress across the interval" is OBSERVED, not inferred from one frame.
//
// Gate A (architect-review) BINDING discipline — honored here:
//   F1: every LOG_*_KV takes the log stream mutex. Suspending a thread that is
//       mid-log and then logging from the dumper would DEADLOCK (the holder is
//       suspended, can't release; the dumper blocks forever). So: NO logging
//       while ANY thread is suspended. Per thread: SuspendThread -> GetThreadContext
//       -> walk into a RAW BUFFER (alloc-free, lock-free) -> ResumeThread. Only
//       AFTER every thread is resumed do we emit the buffered frames through the
//       normal logger. suspend -> capture-raw -> resume -> log.
//   F2: the native-unwinder ReadProcessMemory of another thread's stack is valid
//       only while that thread is suspended — the walk runs strictly inside the
//       per-thread suspended window.
//   F6: dump at onset AND +30s; heartbeat-resume is the primary falsifier.
//   The watcher thread NEVER suspends itself (skips its own tid) and suspends
//       one thread at a time (never holds two suspended — can't self-lock on a
//       lock the walker needs).
//
// NO-RESIDUE: on retirement, capture the finding + this wiring to
// _research/probe-archive/ then REMOVE from live source (working-artifacts.md).
// Greppable tags: "BOOT_WATCH" (heartbeat) / "BOOT_DUMP" (the stack dumps).

#include <cstdint>

namespace kcdx::fs_takeover {

// H1 — called once per main-thread update tick from HookedUpdate. Advances the
// heartbeat (a monotonic tick counter + a last-seen wall-clock ms the watcher
// reads). Emits one BOOT_WATCH line ONLY on an integer-second transition edge
// (floor(now_s) != floor(last_s)) — event-debounced on the existing tick, NOT a
// timer (logging.md / polling.md). Cessation of these lines = the wedge onset.
void BootWatchTick();

// H1 read side — the current heartbeat tick count (0 before the first update
// tick). The FS boot-trace gate reads this to extend its window N frames PAST
// AfterGameApply (the first tick), so the render/UI-init phase — which runs
// after the first tick and is where KI-0028 fails — is traced instead of dark.
uint64_t BootWatchTickCount();

// H3 — start the watcher thread once, early in boot (called from the FS-takeover
// seating, after the takeover is live). Idempotent; a second call is a no-op.
// The watcher polls the heartbeat's last-seen wall-clock; when it has not
// advanced for N=10s it dumps all threads (onset), then again at +30s, then
// stops arming (one wedge, two dumps). It NEVER dumps while the heartbeat is
// advancing — a live boot produces no dump.
void BootWatchStart();

}  // namespace kcdx::fs_takeover
