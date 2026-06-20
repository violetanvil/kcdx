# KI-0026 — VERIFIED MECHANISM (PROBE P + P2, 2026-06-20)

The complete, falsifiable root cause of the `0xC8` boot CryFatalError. Every link
is observed (a live probe capture or a static disk/binary read), none inferred.

## The chain (engine-named, end to end)

1. The engine (during graphics/system init, reached from `C_Game::CreateInstance`)
   opens **`%engine%/config/engine_core.thread_config`**.
2. kcdx owns the CCryPak vtable (full takeover, `kcdx_owned=28`, swap seated). The
   open dispatches into kcdx `FOpen` → `OpenResolvedAndMint`.
3. The asset index (built over `<game-root>/Data`) has **no entry** for it → index
   MISS. The miss arm thunks the captured-original `AdjustFileName`, which resolves
   the name to the **relative loose path `engine\config\engine_core.thread_config`**
   (PROBE P2: `resolved_disk="engine\config\engine_core.thread_config"`,
   `pct_expanded=yes` — `%engine%` was consumed to a bare relative `engine\…`).
4. The miss arm then `_wfopen`s that loose path on kcdx's CRT → **`errno=2`**
   (PROBE P2: `resolved_exists_on_disk=no`; `loose_open_failed … errno=2`). The
   file does **not exist as a loose file** anywhere under the game tree.
5. The file actually lives **inside `Engine/Engine.pak`** as
   `Config/engine_core.thread_config` (verified: zipfile open of `Engine.pak`,
   972 entries, exact hit). kcdx's index covers the `Data` paks, **NOT the
   `Engine/*.pak` archives**, so kcdx never finds it and never serves it from the
   pak.
6. kcdx returns a failed open (0). The engine treats the thread-config load as a
   fatal failure and raises **`CSystem::FatalError(0xC8)`** with the message
   **"Error loading thread config '%engine%/config/engine_core.thread_config'"**
   (PROBE P: `fatalerror_fired err_id=200`, `a4` → that string).

## Why every prior probe saw "kcdx clean"

The bug is a **MISS on an engine-pak-resident file**, not a wrong value:
- PROBE K/N: every kcdx slot that *fires* returns correct values + byte-identical
  object state — true, because the defect is a not-found, not a mis-serve.
- PROBE G (swap bypassed) boots clean: the engine's own FOpen body walks the
  `Engine/*.pak` archives and finds the config; only kcdx's from-scratch miss arm
  (loose-only) does not.
- The NGX/FSR2 frame names were nearest-export noise (no WHGame PDB); graphics-init
  is merely where the thread-config is consumed.

## The defect, precisely

kcdx's index-MISS arm assumes **miss ⇒ loose file**: resolve to a disk path, then
`_wfopen` it. That is correct for a save/cache/write/loose-config tail, but WRONG
for a file that is **pak-resident in an engine pak kcdx never indexed**. The
takeover indexed `<game-root>/Data` (mod + game-data paks) but not the
`Engine/*.pak` engine archives — so the engine's own config/shader/thread files,
which the vanilla FOpen body reads from those paks, are unreachable under the
takeover. This is the totalizing-invariant gap (`spec-conformance.md`): the
takeover owns "every file op", but its miss arm narrows the long tail to loose
files only and silently hands the engine-pak case to a `_wfopen` that can't reach
it.

## The fix is a DESIGN fork (the user's call, NOT decided here)

The takeover must serve `Engine/*.pak`-resident files too. Options (cornerstone-
screened, to surface): (a) index the `Engine/*.pak` archives into kcdx's asset
index alongside the `Data` paks (broadest — kcdx owns the engine paks too); (b)
the miss arm, after a failed loose `_wfopen`, falls through to the engine's
ORIGINAL FOpen body for the pak walk (narrower, but reintroduces an engine-CRT
open — the cross-CRT concern); (c) a dedicated engine-pak index tier. This is the
takeover's coverage contract — the user decides.

## Files / evidence

- PROBE P: `src/fs_takeover/probe_p_fatalerror.{h,cpp}` — FatalError capture.
- PROBE P2: `src/fs_takeover/open_slots.cpp` miss arm — `probe_p2_threadconfig_resolve`.
- Dumps: `kcdx_2026-06-19_23-58-27.dmp` (P: a4→the message), the P2 crash log
  `kcdx-dev_2026-06-20_00-05-14.log`.
- `Engine/Engine.pak` contains `Config/engine_core.thread_config` (zipfile verified).
