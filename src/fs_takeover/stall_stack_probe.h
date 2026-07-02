// === DIAGNOSTIC (PROBE Y) — KI-0028 stall-no-geometry main-thread stack trigger =
#pragma once

// WHY (KI-0028 Measurement 1 — the decisive never-obtained observation, handoff
// §12.B): the proven KI-0028 case is NOT a heartbeat stall. Main ticks ~35/s the
// WHOLE time; present climbs at 120fps; only INDEXED GEOMETRY is never requested
// (draw_indexed=0 vs 96 swap-OFF) and every presented frame is BLACK. Because the
// heartbeat never stops, boot_watch's cessation dumper (BootWatchStart) NEVER
// fires on this bug — which is exactly why the ground-truth stack was never
// obtained. PROBE Y fires the SAME Gate-A-blessed capture (BootWatchDumpAllThreads
// — the suspend->walk-raw->resume->log F1/F2 discipline) on the ACTUAL stall
// signature: present advancing but draw_indexed stuck at 0.
//
// The swap-ON vs swap-OFF (kcdx-noswap) main-thread stacks, diffed offline, name
// the boot-phase SEQUENCER gate that decides "advance to geometry" — the frame
// that diverges is the gate (Measurement 1 → feeds Measurement 2).
//
// TRIGGER (two-factor arm; Gate A 2026-07-02):
//   ARM  = present count climbed past a threshold from its first non-zero read
//          (present genuinely advancing) AND the heartbeat is past a floor
//          (not a transient).
//   FIRE = armed AND DrawcallProbeIndexedCount()==0 across a confirm window
//          (geometry never requested) -> BootWatchDumpAllThreads("stall_no_geometry").
//   NEVER-ARMED = not armed within the give-up window -> log a DISTINGUISHABLE
//          STALL_STACK_NEVER_ARMED with the reason (swapchain never captured /
//          present never climbed). A never-fire is an OBSERVED outcome, never
//          silence.
//
// It reads present/draw progress off the sibling probes' LIVE accessors
// (PresentProbeLastCount reads the captured swapchain directly; DrawcallProbe
// IndexedCount reads the raw draw atomic) — NOT their bounded watcher caches, so a
// stall reached AFTER those watchers exit (~2min) is still observed (Gate A).
//
// Armed in seating_hook.cpp BEFORE the kcdx-noswap early-return (the PROBE W/K/P
// A/B pattern) so swap-ON and swap-OFF capture at the same phase.
//
// NO-RESIDUE: on retirement capture the finding + this wiring to
// _research/probe-archive/ then REMOVE from live source (file + seating arm +
// CMakeLists + the boot_watch BootWatchDumpAllThreads export if unused elsewhere).
// Greppable tag: "STALL_STACK".

namespace kcdx::fs_takeover {

// Arm PROBE Y once. Idempotent. Starts a dedicated diagnostic watcher thread (the
// same sanctioned diagnostic-poll shape as PROBE K/S — one Sleep-cadence thread,
// not a new production poll) that samples the present/draw accessors and fires the
// stack dump on the stall signature. A run that advances past the transition
// (draw_indexed > 0) or never reaches it logs a terminal line and the watcher
// self-exits — a healthy/advancing boot produces no stack dump.
void StallStackProbeStart();

}  // namespace kcdx::fs_takeover
