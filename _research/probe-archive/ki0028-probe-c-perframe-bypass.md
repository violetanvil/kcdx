# PROBE C — KI-0028 per-frame `HookedUpdate` body bypass

**Verdict:** kcdx's per-frame `HookedUpdate` body is INNOCENT of the KI-0028 boot hang.
**Known-issue:** `docs/known-issues/KI-0028-fs-takeover-boot-hang-ui-render-init.md`.
**Ran:** 2026-06-20, engine `kcdx.dll` redeployed, live cdb on the hung process.

## Question

Does kcdx's steady-state per-frame body in `HookedUpdate` (the per-tick
`lua_registry::ApplyZone(AfterGame)` drain + `task::DrainQueue()` + the cap-NN
`static bool`-latched report blocks) cause the `SleepEx` wedge in the game's
`CreateInstance`/FSR2 path — or is the wedge in the boot STATE the FS takeover
changed (read inside `g_orig_update`)?

## Wiring (reconstruct from here — do NOT leave it in live source)

In `src/hooks.cpp`, immediately AFTER the one-shot `if (!done.load(...))` init
block closes (the line just before the per-tick `lua_registry::ApplyZone(AfterGame)`
drain — was ~line 683), insert an early-jump that skips the whole steady-state
body and calls only the game's original update:

```cpp
// === DIAGNOSTIC (PROBE C) — KI-0028 boot-hang isolation ===
{
    static std::atomic<unsigned> s_probeC_skips{0};
    const unsigned n = s_probeC_skips.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n == 1 || (n % 600) == 0) {
        log::Debug("PROBE_C bypassing kcdx per-frame body; calling only g_orig_update");
    }
    g_orig_update(p1, p2, p3);
    return;
}
// === END PROBE C ===
```

**Design note (load-bearing):** the early-jump goes AFTER the one-shot `done`
init block, NOT before it. The first tick still runs full init (RegisterKcdxTable,
RunAll, behavior catalog, ApplyZone passes, InputLoaded fire) so the VM / plugins /
InputLoaded come up identically; only the steady-state per-tick body is skipped.
This isolates exactly ONE variable (the per-frame body), not "kcdx on/off."
The `n % 600` log cadence keeps the dev log readable (one line per ~600 ticks) and
proves the update loop is still being called — event-driven floor, no per-tick spam.

## Outcome → meaning (pre-committed)

- wedge CLEARS (boots past menu) → kcdx per-frame body is the cause.
- wedge PERSISTS → `HookedUpdate`'s body innocent; cause is the boot-STATE the FS
  takeover changed.

## Result — wedge PERSISTS (body innocent)

- The `PROBE_C` line fired for ~2 minutes (18:49:36 → 18:51:30) at the `n % 600`
  cadence → the update loop was ALIVE; `g_orig_update` returned each frame. This is
  a never-completing init (livelock), NOT a hard freeze of the update loop.
- Live cdb (`~*k`) over ~199 threads: **ZERO kcdx frames on any thread.** No thread
  inside any kcdx / CCryPak / FOpen frame at hang time — kcdx's FS work had
  COMPLETED (last serve `cursor_green.dds` @ 18:49:31); the wedge is downstream.
- Main thread: `NtWaitForAlertByThreadId` ← `RtlSleepConditionVariableSRW` ←
  `SleepConditionVariableSRW` ← `WHGame!NVSDK_NGX_UpdateFeature+0x368f0`, inside
  `C_Game::CreateInstance` — waiting on an SRW condvar for an NGX feature update.
- An NGX/FSR2 `JobWorker_NN` thread spins in `KERNELBASE!SleepEx` ←
  `NVSDK_NGX_UpdateFeature`; other `JobWorker_NN` idle in `RtlSleepConditionVariableSRW`.
  A circular/never-signalled wait INSIDE NGX `UpdateFeature`.
- `kcd.log` goes silent at 18:49:32 (menu reached: `[Pros]` banners, MFX libs) while
  kcdx keeps ticking 2 min — the engine is gated on NGX completing.
- FS-takeover serve content is clean: `system.cfg` / shader caches served with
  `diffs=0` (byte-identical to engine/revert); NGX's own data files don't route
  through kcdx (`ngx|dlss|fsr` served-path count = 0). So NOT wrong content (H3 weak).
- The FS takeover ran concurrently on multiple tids (45620: 44894 hits, 33940: 27610,
  plus others) → the takeover's slots execute on several threads incl. render/worker
  threads. Points at H4 (takeover changed init timing/threading/ordering) over H3.

## Next (open in KI-0028)

The hang is an NGX `UpdateFeature` never-completing init, kcdx-introduced (P-B) but
with kcdx on no stack and serving clean content. Root-cause work = read the FS
takeover's init/threading model for what it reordered/serialized that NGX's async
init depends on. That is a design fork (fix direction) → route through architect-review,
not raw to the user.
