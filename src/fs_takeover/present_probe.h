#pragma once

#include <cstdint>

// === DIAGNOSTIC (PROBE K) — KI-0028 present-count delta (NO present hook) ===
//
// WHY (KI-0028, after the J.2-J.5 reframe + Gate A architect-review): the game
// is NOT hung — every thread runs (tick ~35/s, Main + RenderThread change RIP
// per invasive sample), the window is visible + Responding, yet the screen is
// black. The pinned fork: does the per-frame loop REACH frame presentation, or
// is it stuck UPSTREAM (the P-J.5 static read put Main in entity/instance init —
// CreateInstance — not render)? The architect's theory-INDEPENDENT probe answers
// it WITHOUT a present hook: read the swapchain's OWN present counters.
//
//   K1 — capture the IDXGISwapChain* ONE-SHOT: MinHook dxgi!CreateDXGIFactory*,
//        patch the factory's CreateSwapChain/CreateSwapChainForHwnd vtable slot,
//        on the first swapchain creation capture the returned pointer and
//        immediately unhook (no per-frame detour — a single init-time capture,
//        NOT a Present hook; honors no-thunk: reads a counter, hands nothing back).
//   K2 — a watcher thread reads GetLastPresentCount + GetFrameStatistics off the
//        captured swapchain every ~1s and logs the delta. PresentCount advancing
//        => Present IS being called; PresentRefreshCount advancing => the GPU
//        actually scans out presented frames.
//
// Outcome -> meaning (pre-committed, flat; the first FALSIFIES "present fails"):
//   present-count delta ~0          => loop never reaches present; wedge is
//                                      UPSTREAM (entity/instance init). FALSIFIES
//                                      the present-failure framing.
//   present-count > 0, refresh ~0   => present called but no GPU scanout; present
//                                      path IS the problem -> a present HOOK next.
//   both advance (~ a live rate)    => frames ARE presented; black screen is a
//                                      surface/compositor association, not present.
//   swapchain never captured        => the engine creates its swapchain by a path
//                                      this hook misses -> widen the capture point.
//
// Gate A discipline (same as boot_watch): the watcher is a dedicated diagnostic
// thread; its 1s read cadence is the one already-sanctioned diagnostic poll (it
// reuses boot_watch's Sleep(kPollMs) shape, NOT a second poll loop). No logging
// while any thread is suspended (this probe suspends NO thread — simpler than H).
//
// NO-RESIDUE: on retirement capture the finding + wiring to
// _research/probe-archive/ then REMOVE from live source (working-artifacts.md).
// Greppable tag: "PRESENT_PROBE".

namespace kcdx::fs_takeover {

// K1 — arm the one-shot DXGI swapchain-capture hook. Call once, early (from the
// FS-takeover seating, beside BootWatchStart). Idempotent. Installs a MinHook on
// dxgi!CreateDXGIFactory{,1,2}; on the first swapchain creation it records the
// IDXGISwapChain* and starts the K2 watcher, then unhooks the factory. If the
// game already created its factory before this arms, the hook simply never fires
// and K2 logs "swapchain never captured" (an outcome, not a failure).
void PresentProbeStart();

// PROBE Y read accessors — read present progress off the captured swapchain
// DIRECTLY (a live GetLastPresentCount), NOT the WatcherMain-cached value, so
// they stay valid the whole process life (the K2 watcher self-terminates after
// 120 reads ~2min). PROBE Y's stall trigger uses PresentProbeLastCount() as the
// "present climbing" arm factor and PresentProbeSwapchainCaptured() to
// distinguish "not armed yet" from a genuine flat count.
bool     PresentProbeSwapchainCaptured();
uint64_t PresentProbeLastCount();

}  // namespace kcdx::fs_takeover
