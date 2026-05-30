# Save-load crash — RaiseException(0xC8) from a pure-WHGame stack ~10s after the load hooks complete

**Status:** investigating — CAUSE CONFIRMED kcdx. Loading a save crashes ~10s after kcdx's save-load hooks fire; boot-to-menu is unaffected. **Clean game (kcdx removed from Steam launch options) loads the same saves with NO crash.** kcdx is the cause; the open question is WHICH part of kcdx. The absent kcdx stack frame means the corruption is latent (set up earlier by kcdx, tripped by the engine ~10s later).

**See also:** [KI-0001](closed/KI-0001-save-load-heap-corruption-on-chain-mediated-lua_pcall.md) — a structurally different third shape (STATUS_HEAP_CORRUPTION with kcdx Lua GC fully symbolicated on stack) surfaced after engine-direct-migration commit `1c01c9d`. Different exception code (0xC0000374 vs 0xC8/0xC0000005), different stack family. FIXED 2026-05-29 as the FIX-C mirror — kcdx's vendored Lua GC was freeing WHGame's static sentinel objects (dummynode + array sentinel), guarded by `kcdx_node_freeable` + `kcdx_array_freeable`. Filed separately because the mechanism families did not overlap; the 0xC8 shape this file tracks is still open.

## Symptom (required)

Loading a save crashes ~10s after `HookedLoadGameWrapper EXIT`. Two distinct crash shapes appeared in the same 16-minute test cluster (2026-05-26):

**Shape 1 — load crash (12:22, 12:28, 12:33), the most recent:**
```
KERNELBASE!RaiseException+0x8a
WHGame!NVSDK_NGX_UpdateFeature+0x871b5a        (nearest export; real fn unnamed)
WHGame!CreateGameStartup+0xda687
WHGame!ffxFsr2GetUpscaleRatioFromQualityMode+0x14f9e70
WHGame!ffxFsr2GetUpscaleRatioFromQualityMode+0x14df684
... (all WHGame, no kcdx frame)
```
Exception code: `0xC8` (200) — a non-SEH application code raised via `RaiseException`, NOT an access violation. Signature of a CryEngine internal fatal-error/assert path, not a memory fault.

**Shape 2 — boot crash (12:18), AV with a kcdx frame:**
```
WHGame!ffxFsr2ResourceIsNull+0x4f5cf4
kcdx+0xb6904                                   (UNSYMBOLICATED — no PDB)
```
Exception code: `0xC0000005` (AV). GUARD reported `module=WHGame.DLL rva=10423796`. Fired right after `PostPostLoad`, **before** the menu — a different lifecycle point than Shape 1.

## Facts (required)

- The most recent crash (12:33:43) fired **during in-game LOAD** of `autosave573.whs` (slot 99). The full test suite passed at boot; `HookedLoadGameWrapper` and `HookedSlotResolver` both fired and returned `result=1` cleanly at 12:33:59. The crash (`RaiseException 0xC8`) fired ~10s later at 12:34:09, with **no kcdx frame on the stack**. (PROBE — initial dump read)
- In the 12:33 session **both in-progress probes were already disabled**: no `FOPEN_PROBE` install line (its `Install()` is commented out in `dllmain.cpp`), and the loc_dump probe wire-in is disabled (commit 916fb17). The deployed `kcdx.dll` is byte-identical to the current `build/Release/kcdx.dll`. So the 12:33 crash is NOT caused by either in-progress probe.
- The `0xC8` RaiseException stack is 100% WHGame. The `ffxFsr2*` / `NVSDK_NGX_*` symbols are nearest-exports with multi-MB offsets — the real functions are unnamed; the chain points at the graphics/asset subsystem reached during load, not literally FSR2/DLSS.
- The 12:18 boot crash is a DIFFERENT shape (AV, kcdx frame present, fires before menu) and may be a separate bug from the 12:22–12:33 load crashes.
- The 09:01 crash today is `kcdx_crash_now` — the deliberate `probe-crash-trigger` console test. Not a real bug. Discarded.
- Boot-to-menu is unaffected: the 10:40 / 11:10 / 11:31 sessions all launched to menu and quit with no crash (the user never loaded a save in those — no `LoadGameWrapper` lines).
- The Release build emits **no PDB and no .map** (`build/Release/` has only `kcdx.dll`). The one crash with a kcdx frame (12:18, `kcdx+0xb6904`) therefore cannot be symbolicated. — logging/diagnostics gap.
- The 0xC8 crash reproduces across THREE different saves (autosave573, exit.whs, autosave548), each with the byte-identical WHGame stack and ~10s post-hook timing. The crash is a property of the LOAD PATH, not any save. (PROBE A — different saves)
- The "direct exe" launches all STILL loaded kcdx because **Steam's launch options re-route every launch through kcdx.exe**: `localconfig.vdf` (userdata\109250594, appid 1771300) sets `LaunchOptions = "...\kcdx.exe" %command%`. Steam was running continuously (since 09:00); double-clicking KingdomCome.exe from the folder while Steam is up makes Steam intercept and run kcdx.exe %command%. This is the kcdx install pattern, and it is why no "clean" attempt was actually clean — a launcher + watchdog log exists for every session. A genuinely clean (vanilla) load requires CLEARING that LaunchOptions string (or fully quitting Steam and launching offline), then confirming NO new kcdx_<ts>.log appears.
- "It worked with kcdx before" + "reproduces across 3 saves" + "deterministic 0xC8 from the asset/shader load path" → this is a kcdx-era REGRESSION in the load path, OR an external change (game patch / GPU driver) that the unchanged kcdx load path now trips. The clean-game test is the discriminator and has not yet been validly run.

## Trail (required)

| Date | Action | Result |
|------|--------|--------|
| 2026-05-26 | Read most-recent dump (12:33) + session log | `RaiseException 0xC8`, pure-WHGame stack, no kcdx frame; crash ~10s AFTER load hooks returned clean. Load-time, not boot. |
| 2026-05-26 | Compared the 4 cluster dumps (12:18/22/28/33) | 12:22/28/33 identical 0xC8 WHGame stack; 12:18 is a different AV with a `kcdx+0xb6904` frame, fires pre-menu. Two shapes. |
| 2026-05-26 | Checked probe activity + DLL identity in 12:33 | No FOPEN_PROBE/LOC_DUMP install; deployed DLL == current build (probes disabled). Crash is independent of both in-progress probes. |
| 2026-05-26 | Checked build for symbols | No PDB / no .map emitted → `kcdx+0xb6904` (12:18) unsymbolicatable. Diagnostics gap. |
| 2026-05-26 | User re-ran the load (12:36:52) | Byte-identical 0xC8 WHGame stack; same save autosave573; crash ~11s after hooks returned clean. Deterministic repro confirmed. |
| 2026-05-26 | Loaded two OTHER known-good saves (exit.whs 12:42; autosave548 12:43) | Both crash with byte-identical 0xC8 WHGame stack + timing. Save-specific theory KILLED — crash is a property of the load path, not the save. |
| 2026-05-26 | PROBE B: cleared kcdx from Steam launch options, loaded same saves vanilla | NO CRASH. kcdx CONFIRMED as the cause. Question reframed from "is it kcdx" to "which kcdx subsystem corrupts the load". |
| 2026-05-26 | PROBE C (batch 1): disabled 5 newest plugins by build date (cap-44, cap-43, comp-13-subject, comp-13-observer, comp-11-b), kcdx re-armed | NO CRASH. Culprit is one of these 5. Hypothesis: a feature only ever boot-tested (launch-to-menu), never load-tested → load-path bug shipped invisibly. |
| 2026-05-26 | PROBE D: re-enabled the 3 comp plugins, kept cap-43-loc-dump + cap-44-fopen-override disabled | NO CRASH. Culprit narrowed to cap-43-loc-dump OR cap-44-fopen-override (the two probe features hooking the ctor / asset-read path). |
| 2026-05-26 | PROBE E: re-enabled cap-43-loc-dump, kept ONLY cap-44-fopen-override disabled | NO CRASH (but see PROBE G — kcdx was not actually loaded). |
| 2026-05-26 | PROBE F (control): re-enabled cap-44 — "restored crashing config" — loaded a save | NO CRASH (but see PROBE G — kcdx was not actually loaded). |
| 2026-05-26 | PROBE G: audited session-log timeline for ALL launches | **The entire bisect (C–F) was INVALID.** NO kcdx engine session log exists after 12:43:44 — kcdx never injected in any bisect run (Steam launch option was cleared for the clean test and never restored). All "no crash" bisect results were VANILLA (no-kcdx) launches. The plugin toggles edited a kcdx that wasn't running. |
| 2026-05-26 | PROBE G (cont.): tallied load-attempt vs crash across all sessions | **7 of 7** sessions that attempted a save-load WITH kcdx present crashed (0xC8). 0 of the menu-only sessions crashed. Crash is DETERMINISTIC given kcdx-present + a load. Not intermittent, not save-specific, not plugin-specific. |

## Current state (2026-05-26, after PROBE G)

CONFIRMED: deterministic engine crash. kcdx present + load a save → 0xC8 from WHGame ~10s after `HookedLoadGameWrapper EXIT` (7/7 load attempts). kcdx absent → no crash (0/N). NOT save-specific, NOT intermittent, NOT plugin-specific (the C–F plugin bisect was invalid — kcdx wasn't injecting; no kcdx session log exists after 12:43:44). `HookedPostLoadGame` NEVER fires in a crash run — the load-game wrapper returns, then the engine dies in the post-load asset/shader window with no further kcdx log.

NEXT (after logging hardening lands): VALID engine-DLL bisect — restore the Steam launch option (`"...\kcdx.exe" %command%`), confirm kcdx injects (a fresh kcdx_<ts>.log MUST appear — verify before interpreting any result), then run current kcdx.dll vs an older one known to have loaded saves; the new load-time hook inventory line is the diff signal.

## Logging hardening (decided 2026-05-26, build FIRST, before the bisect)

This crash was invisible to the logs: nothing records what kcdx modifies on the load path. Decided fix (reuses the existing Level mechanism: Trace/Debug=dev-only, Info/Warn/Error=always-on — end-users get quiet+meaningful, authors get verbosity in dev mode):
- Re-emit the hook/patch/mid-hook INVENTORY SUMMARY (count + target fingerprint) at load-start — **Info** (always-on, diffable). hooks.cpp already builds this at boot (hooks.cpp:303); re-emit at load.
- Per-hook target DETAIL at load-start — **Debug** (dev-only).
- Demote the SAVE_LOAD `before/after` play-by-play from Info → **Debug** (dev-only) — it's internal-step spam at always-on today (backwards).
- GUARD FAULTED: walk past KERNELBASE to the real culprit module + dump the live-hook inventory — **Error** (always-on). Today it logs `module=KERNELBASE` (wrong frame) and nothing about kcdx's modifications.
- Emit a PDB (kept beside the build, NOT shipped) so a kcdx crash frame symbolicates (12:18 `kcdx+0xb6904` is currently unresolvable).

## Open questions

- **Is the 0xC8 load crash present with a CLEAN game (no kcdx at all)?** — Probe: launch vanilla KCD2 (rename/disable `kcdx.exe` boot so the engine DLL never injects) and load the SAME save `autosave573`. Crashes → game-side bug kcdx didn't cause; loads clean → kcdx is implicated despite the absent stack frame. This is the single most decisive next step and only the user can run it.
- **Does the 0xC8 crash depend on kcdx's save-load hooks specifically?** — Probe: keep kcdx loaded but disable the cap-12 serialization / save-load test plugins (and/or the engine save-load hook wire-in) and load the same save. Narrows from "kcdx present" to "save-load hooks present".
- **Is the 12:18 boot AV (`kcdx+0xb6904`) a separate bug?** — Probe: emit a PDB (see design implications) and re-resolve `kcdx+0xb6904` to a function. Cannot symbolicate until the build emits debug info.

## Hard rule / design implications

- **logging.md / build:** the Release build emits no PDB, so a kcdx frame in a crash dump is a bare `kcdx+offset` we cannot map to a function. A crash bundle is only as useful as its symbolication. Proposed: emit a stripped PDB (`/Zi` + `/DEBUG` + `/OPT:REF,ICF`, PDB kept beside the build, NOT shipped in the release zip) so dumps symbolicate offline. This is the "make logging robust enough to find errors like this" answer — see analysis below.
- **GUARD module attribution:** the GUARD line reports the faulting module + rva (good), but for a `RaiseException` the faulting RIP is always `KERNELBASE!RaiseException` — the GUARD should walk one or two frames up and log the first WHGame/kcdx return address so the log alone (without the dump) names the real culprit module.

## Active diagnostic instrumentation

| File | What | Safe to ship? |
|------|------|---------------|
| (none yet) | — | — |

## Resolution (filled in when bug closes)

- **Root cause:** TBD
- **Fix:** TBD
