# KI-0028 ROOT CAUSE (found 2026-06-22, PROBE W) — kcdx IsFileExist over-reports existence vs vanilla

**Status:** root cause IDENTIFIED + dump-confirmed. Fix is a design decision (kcdx existence semantics) — surfaced to the user, NOT yet built. AP17 mechanism below.

## The mechanism (falsifiable, sourced)

kcdx's `IsFileExist3` (CCryPak existence-by-name, slot 67) returns **EXISTS (kcdx=1)** for files the engine's OWN original check returns **NOT-EXISTS (vanilla=0)**. The engine, told a file exists then unable to actually load it through the path that "yes" implies, **aborts the level load** ("The level can't be loaded, exiting — kutnohorsko", the main-menu background level).

### Every link sourced

1. **PROBE W differential — 675 `VANILLA_DIFF` lines, ALL `slot=IsFileExist3 kcdx=1 vanilla=0`** (`kcdx-dev_2026-06-22_18-21-45.log`). kcdx's index-HIT says EXISTS; the captured engine original (same `(pName, location)` args, read-only) says NOT. Spans many asset classes:
   - `.cfi` (294) + `.cfx` (287) — CryFX shader source/includes under `data/GameShaders/HWScripts/CryFX/`
   - `.xml` (53) — `Scripts/AI/Factions.xml`, `Libs/MovementTransitions/*`, `Animations/Mannequin/ADB/*tags.xml`
   - `.adb` (28) — animation databases
   - `.gfx` (10) — Scaleform UI
   - `.bk2` (2) — `Videos/startup/startup_01/startup_01.bk2`
   - `.dat` (1)
   So this is a GENERAL existence-over-report, NOT shader-specific.

2. **The engine aborts deliberately.** Crash dump `kcdx_2026-06-22_18-21-45.dmp` (96 MB, a REAL minidump — first for KI-0028): exception `0x000000d2` via `KERNELBASE!RaiseException` (`rdi=0xd2`). `0xd2` is a controlled engine fatal-exit code, NOT an access violation — matches the "level can't be loaded, exiting" popup. The engine chose to exit; it did not corrupt-and-crash.

3. **NOT new, NOT a PROBE W artifact.** "level can't be loaded" (`cant_load=1`) appears in BOTH prior black runs (`kcdx_2026-06-22_17-34-39.log`, `…16-39-40.log`) — runs with NO differential. The black screen WAS this level-load failure all along; the prior runs were killed (Task Manager) before/around the popup, so it was never seen. PROBE W's differential is READ-ONLY (compares + discards the original's answer, returns kcdx's unchanged) — it made the WHY visible, it did not cause the failure.

4. **The `FAULTED_FIRE` storm is downstream teardown, not cause.** `cap_03_hook_lua_callback`/`cap03_update_callee` faults thousands of times starting 18:23:40 — ~110s AFTER the VANILLA_DIFF storm (18:21:50) and after the level-load failure. It is the per-frame test hook firing into the engine's exit/teardown, a consequence of the abort, not its cause.

## Why kcdx says EXISTS where vanilla says NOT

The divergence is in the index-HIT arm of `kcdx_IsFileExist3` (`metadata_slots.cpp`). kcdx resolves the vpath against its unified index and, on a HIT, returns true — but the engine original returns false for the same `(name, location)`. Candidate sub-mechanisms (the fix probe/read settles which):

- **`location` semantics not honored on the index HIT.** `IsFileExist3(self, pName, location)`: location==2 pak-only, ==1 disk-only, else either. kcdx's index HIT may report existence for a `location` the engine is querying as a DIFFERENT location (e.g. the engine asks "does this exist as a LOOSE/disk file?" (location==1) and kcdx answers yes from a PAK-resident index entry — which is NOT a disk file). The engine then commits to a disk-load path that fails.
- **kcdx's index carries entries under a normalized path/casing/alias the engine's original query does not match** — kcdx's `ResolveVPath` normalizes (fold + alias) so it HITS where the engine's literal pak-dir lookup MISSES; kcdx then claims existence for a name the engine cannot resolve the same way.

The decisive next read: for a sample diverging vpath, what `location` did the engine pass, and what is the kind (Pak/Loose) of kcdx's index entry — does kcdx report a PAK entry as existing when the engine asked location==1 (disk-only)? That is the §-precise bug.

### PINNED (2026-06-22, same-log read) — it is NOT a location-filter bug; kcdx HITS where the engine MISSES on location=either

ALL 675 divergences are `how=index-either` (the FS_BOOT_TRACE meta line for each). So the engine passed `location` = "either" (not 1=disk-only, not 2=pak-only), and kcdx's either-arm returns true on ANY index hit. The location-filter sub-theory is FALSIFIED — location is "either", trivially handled. The bug is: **kcdx's `ResolveVPath` (normalized fold + alias) HITS the index for these names, while the engine's OWN original `IsFileExist3(name, either)` MISSES.** Two precise candidates:

1. **Timing/mount-order:** kcdx's index is built ONCE at seat over ALL paks; the engine's original existence check reflects only what is MOUNTED at the moment of the call. kcdx reports a not-yet-mounted-by-the-engine file as existing → the engine commits to a load the mount state can't satisfy → abort. (The dominant `data/GameShaders/` set fits this: the shader paks' mount/alias timing.)
2. **Alias over-match (KI-0026 lineage):** the `data/GameShaders/...` set is the SAME alias class as KI-0026 (the `data/gameshaders/` → indexed `shaders/` engine-pak alias). kcdx's index resolves that alias and reports EXISTS; the engine original, querying its pak-dir with the raw `data/GameShaders/` name at this point, MISSES. kcdx is answering for a key the engine cannot resolve the same way yet.

Both reduce to: **kcdx's index existence answer is computed over a DIFFERENT (broader, alias-resolved, fully-mounted) view than the engine's own existence check has at call time — so kcdx says EXISTS where the engine, with its current view, says NOT, and the engine's load logic (which trusts its own subsequent operations, not kcdx's existence claim) then fails.** The §-precise fix probe: for a diverging `data/GameShaders/*.cfi`, does the engine ORIGINAL resolve it under the `shaders/`-aliased key (KI-0026) but not the raw `data/GameShaders/` key — i.e. is kcdx claiming existence under a name the engine only finds via the alias?

## This answers "why couldn't our logging see it"

kcdx never FAILED — it confidently returned "exists". Every prior probe measured kcdx's outputs and found them self-consistently correct. The bug is a CORRECT-LOOKING answer that is WRONG RELATIVE TO VANILLA, invisible until the differential compared the two. The fix (PROBE W as kept infrastructure) is exactly the observability the project wanted.

## The fix is a DESIGN decision (surfaced, not chosen)

kcdx's existence answer must match what the engine needs — but "match vanilla exactly" vs "honor location and report the unified set correctly" is a design call about the takeover's existence semantics (the totalizing-invariant §5.1 vs the engine's location-filtered contract). Surfaced to the user. The fix stays inside kcdx ownership (no thunk-back) — make `kcdx_IsFileExist3`'s index HIT honor the `location` filter the way the engine original does, so kcdx reports existence ONLY where the engine would, while still covering the pak-resident set the takeover legitimately owns.
