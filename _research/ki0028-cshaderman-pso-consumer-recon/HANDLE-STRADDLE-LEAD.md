# KI-0028 — the handle-straddle lead (static, sourced; UNVERIFIED mechanism — probe owed)

**Date:** 2026-06-22 (inheritance session).
**Trust:** the static facts below are PRIMARY (source + log read this session). The MECHANISM (a non-kcdx engine path tag-tests a kcdx handle) is **UNVERIFIED** — a probe is owed before it is asserted as the root cause (AP17/AP19).

## What this lead is

A candidate root-cause mechanism for the black screen that the whole investigation walked past because it is invisible to a content/vpath trace AND to the swap-ON-vs-swap-OFF A/B (swap-OFF serves no files, so the handle the engine holds there is its OWN, never a kcdx one — the divergence is structurally unobservable in that A/B). It fits every prior observation: FS serve healthy, bytes correct (`diffs=0`), yet the menu's indexed geometry never builds.

## The static facts (all sourced this session)

1. **kcdx hands the engine small odd-tagged integer handles.** Encoding `(id << 1) | 1`, id 1-based → smallest is 3. (SOURCE: `src/fs_takeover/file_handle.h:27-28`.)
2. **The live black run minted exactly 9 handle values: `3, 5, 7, 9, 13, 15, 17, 19, 21`** (max 21). (SOURCE: `kcdx-dev_2026-06-22_16-39-40.log`, `FS_BOOT_TRACE handle=` distinct set.)
3. **The engine's native CCryPak handle dispatch is `handle - 1 < pakEntryCount` → pak arm (a small index+1), else the OS arm.** `pakEntryCount` is thousands. (SOURCE: `file_handle.h:22-24, 33-34` — the header documents the engine's own tagged-union test.)
4. **∴ every live kcdx handle (3..21) satisfies `handle-1 < pakEntryCount`** → under the engine's OWN test it routes to the engine pak arm with index `handle-1` (2,4,6,8,12,14,16,18,20) — a VALID but WRONG pak entry, read from the engine's ZipDir, NOT kcdx's buffer.
5. **kcdx's safety argument is scoped to READ slots only.** The §4.4 constraint is "every handle-operating **READ** slot is KCDX, never THUNK" (`vtable_table.cpp:59`). The header's no-aliasing claim rests on "kcdx owns every read slot, so the engine never runs its OWN tag test on a kcdx handle" (`file_handle.h:34-37`).
6. **Slots 15 and 101 stay THUNK (engine-original).** Slot 15 = the ForEachFile inner per-entry callback; slot 101 = the CCryPakFindData iterator factory. (SOURCE: `vtable_table.cpp:48-51, 83-86, 200`.)

## The UNVERIFIED link (the probe target — AP19)

The header's safety claim is an **asserted call-edge**: "no engine path runs the native `handle-1 < pakEntryCount` tag test on a kcdx handle." That is true for the slots kcdx overrides — but it is NOT read in, and does NOT cover:

- **An INLINED tag test** — engine render/streaming/geometry code that does its own `if (handle-1 < pakEntryCount)` inline (not a vtable dispatch at all), reading a kcdx handle `3` as engine pak-index 2.
- **A THUNK consumer** — slot 15 / slot 101 / any non-read slot that receives a kcdx handle and routes it through the engine's native path.
- **A handle DUPLICATED / memory-mapped / passed to a Win32 API** that treats `(id<<1)|1` as a real OS HANDLE (Reframe 4 H3a, never probed).

If ANY render-path consumer tag-tests a kcdx handle natively, it reads the WRONG pak entry with NO error, NO log, NO `diffs=0` mismatch (kcdx's content was correct; the engine never asked kcdx). The menu's indexed geometry would then be built from wrong/garbage bytes → no real geometry → `draw_indexed=0` → black. This is the H3a "opaque-handle straddle" sub-mechanism (KI doc Reframe 4), now with the live handle values that make it concrete.

## Why this is the strongest surviving lead

- It is the ONE class a content/vpath/bytes trace cannot see (the engine reads the wrong entry through its OWN arm — kcdx is off that path).
- It is the ONE class the swap-ON-vs-swap-OFF A/B cannot see (swap-OFF the engine holds its own handles; no kcdx handle exists to straddle).
- It explains `diffs=0` + clean FS serve + black geometry simultaneously — the prior "wrong content" theories could not (they were all falsified BY the clean serve).

## Secondary note (logged, not a conclusion)

`data/pak.cfg` read `want=994 got=961` served `result=ok` (a 33-byte short read reported success). May be benign (trailing data / a legitimately shorter entry) — flagged for the probe to confirm want==got across the geometry reads, since a silent short read is the AP14 class.

## The probe owed (next)

Establish whether a render/geometry-path consumer runs the native tag test on a kcdx handle. Two routes (the fork put to the user):
- **Static:** read the engine bodies that consume a CCryPak handle on the render/geometry path for an inline `handle-1 < pakEntryCount` test (the consuming-body read AP19 demands), starting at slots 15/101 and the DrawIndexed resource-load path.
- **Live:** instrument kcdx to detect its own handle being operated by a non-kcdx path — e.g. mint handles in a range that CANNOT alias a valid pak index (a high-bit-set encoding) and observe whether the black screen clears. A handle that the engine's native test routes to the OS arm (never the pak arm) would falsify or confirm the straddle in one launch: clears → straddle CONFIRMED as the mechanism; still black → straddle is not the cause, the wrong read is elsewhere.

The live route is the more decisive (theory-independent: it changes the one variable — whether kcdx handles can alias a pak index — and reads the black/menu outcome). The static route is cheaper but can only find the test, not prove it is on the failing path.

---

## ⚠ PROBE T RESULT (RAN 2026-06-22 17:18, swap-ON) — the handle-straddle is FALSIFIED

The live re-tag probe (bit-40 anti-straddle bit in `Encode`/`DecodeId`, `file_handle.cpp`) ran swap-ON, user-confirmed STILL BLACK. Ground truth (`kcdx-dev_2026-06-22_17-18-29.log`):

- **PROBE T TOOK EFFECT:** handles minted at `1099511627779, ...781, ...783, ...` (= bit-40 + the old 3,5,7…) — every kcdx handle now `handle-1 ≫ pakEntryCount`, so the engine's native pak-arm test can no longer match a kcdx handle. The variable changed. (PROVEN — `FS_BOOT_TRACE handle=` values.)
- **kcdx's own read path is UNAFFECTED:** `bad_handle` count = 0 (same as the pre-probe run); serve health BYTE-IDENTICAL (15827 index-pak, 3764 index-pak-serve, 2504 miss-original, 671 index-either, 312 original — identical distribution to 16-39). The high-bit handles round-tripped through `DecodeId` correctly. (PROVEN — grep diff vs 16-39.)
- **NO fault.** The crash zip is the watchdog KILL-TIME snapshot (logs only, no minidump); the 2 `FAULTED` log lines are the `cap-45-early-inventory` TEST scaffolding, not a real AV. Heartbeat ticked continuously to the end (tick 2182, advancing). Suite 320/343, identical to the black baseline. The game RAN, black, killed by the user. (PROVEN — zip contents + heartbeat + suite line.)
- **`draw_indexed=0` swap-ON, UNCHANGED.** `draw_instanced=16544 draw_indexed=0 om_null_rt=0` — the indexed-geometry path is STILL absent with handles forced off the pak-alias range. (PROVEN — DRAW_PROBE summary.)

**VERDICT — the handle-straddle (H3a) is FALSIFIED as the cause.** Forcing kcdx handles out of the engine's pak-index range changed NOTHING: same black, same `draw_indexed=0`, same serve health, no fault. So NO engine render/geometry path was silently reading the wrong pak entry via the native tag test on a kcdx handle — if it had been, either the screen would have cleared (the straddle removed) or a path would have faulted dereferencing the now-huge handle as a FILE*. Neither happened. The wedge does not run through the handle VALUE at all.

**What this ALSO rules out (the clean-run is itself evidence):** the engine consumed kcdx handles `1099511627779`+ for the WHOLE boot with zero error and identical behavior — proving the engine treats the kcdx handle as a fully opaque token end-to-end (it never tag-tests it, never dereferences it, never width-truncates it). The §4.4 opaque-handle contract HOLDS in practice. The entire handle-semantics class (H3a straddle, H3c handle-as-OS-resource) is now exonerated by direct measurement — the engine never operates the handle as anything but an opaque pass-back.

**RE-LOCALIZATION (honest):** the divergence is NOT in the handle value, NOT in served content (bytes correct, `diffs=0`), NOT in serve health (identical), NOT in PSO/shader build (six probes), NOT in present (PROBE K). Every kcdx OUTPUT the engine consumes — the bytes, the handle, the sizes, the enumeration — has now been measured identical-or-correct swap-ON. Yet `draw_indexed=0` and black. This forces the conclusion that the divergence is NOT in what kcdx SERVES but in a STATE or ORDER the takeover changes that the geometry-build path depends on — the H4-class (init timing/threading/state) that P-F was read as killing, but which P-F only killed for *kcdx's added threads*, never for *the swap's effect on engine init ORDER/STATE*. The next probe must target what the indexed-geometry build READS that is a kcdx-perturbed STATE, not a kcdx-served FILE.
