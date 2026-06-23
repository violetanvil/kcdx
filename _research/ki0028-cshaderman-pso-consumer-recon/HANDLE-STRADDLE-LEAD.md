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

---

## ⚠ PROBE U RESULT (RAN 2026-06-22 17:34, swap-ON) — the reswap / second-CCryPak theory is FALSIFIED

PROBE U (`reswap_probe.{h,cpp}`) armed at the seat capturing the swapped object + kcdx's vtable + the gEnv slot, then sampled `[obj+0x00]` (the object's vtable ptr) and `*(gEnv pCryPak slot)` (the live global) every 1s. Live swap-ON, user STILL BLACK (`kcdx-dev_2026-06-22_17-34-39.log`):

- `watcher_started`: `swapped_obj=2031441901472 seat_global=2031441901472` — the global IS the swapped object at seat (one object).
- **EVERY sample `vtable_ok=1 global_ok=1`** (i=0,30,60,90, through 90+s; killed before the 180 window closed). **Full-scan `vtable_diverged`/`global_diverged` = 0.** (PROVEN.)
- The black state was fully reached: `draw_indexed=0` (DRAW_PROBE 14226 instanced / 0 indexed), heartbeat alive (tick 1456), suite 320/343 — the geometry-build path EXECUTED in its failing state. (PROVEN.)

**VERDICT — FALSIFIED.** The geometry-build ran in its failing state while kcdx owned the ONE CCryPak object end-to-end: the vtable was never re-pointed away from kcdx's, and no different CCryPak became the live global. The engine does NOT re-swap post-seat and does NOT route geometry through a second/replacement object.

**Net after PROBE T + PROBE U — the filesystem-OBJECT layer is FULLY exonerated.** Every kcdx OUTPUT is correct (bytes/handle/sizes/enum) AND kcdx owns the one CCryPak object (vtable + global) end-to-end. The black-geometry divergence is NOT in the object kcdx swapped, NOT in what it serves, NOT in the handle, NOT in a second object.

**REFINEMENT (seat-hook re-read, same session) — the original constructor DID run.** `HookedConstructStore` (`seating_hook.cpp:85-90`) is an AFTER-hook: it runs the original construct-store helper to COMPLETION first (constructs CCryPak + publishes the pointer exactly as vanilla), THEN swaps the vtable. So the engine's CCryPak constructor's side-effects on engine state ALL happened — "kcdx skips the constructor side-effects" is WRONG (the constructor ran). The remaining mechanism class is narrower:

1. **A vtable slot that is BOTH a file op AND a state mutator, where kcdx implemented the file half and dropped the state half.** The engine calls a CCryPak method during render/geometry init that, in vanilla, serves bytes AND mutates engine/render state (registers a search path, updates a pak-mount table, sets a ready flag, caches a resolved root). kcdx's KCDX slot serves the bytes correctly but does not reproduce the state side-effect — so the bytes are right but a piece of engine state the geometry build reads is left unset. Prime suspects: the THUNK slots kcdx did NOT take (15 = ForEachFile inner callback, 101 = CCryPakFindData factory) AND any KCDX slot whose original body did more than I/O.
2. **A seat timing/order effect** — the constructor's side-effects ran, but kcdx's index build (`BuildAssetIndexAtSeat`, an INFINITE wait on the overlay-ready gate, ON the seat thread) or the worker threads shifted WHEN the filesystem becomes usable relative to when the geometry path expects it.

The next probe targets class 1 first (cheaper, static): read the engine's render/geometry-init CCryPak calls for a slot whose original body has a state side-effect kcdx's impl drops — starting at the THUNK slots and any KCDX slot whose original did more than serve bytes.

---

## PROBE V — DESIGNED (fresh-frame, 2026-06-22), NOT YET BUILT — request-stream differential with caller attribution

A fresh-frame subagent (leading theory WITHHELD) designed the next probe. Reuse-first check: the existing `FS_BOOT_TRACE` (`boot_trace.h`) already logs the open/read/enum REQUEST STREAM (slot + vpath + how + result + the enum pattern + a sampled name list via `TraceOpen`/`TraceEnum`/`TraceEnumNames`). The ONE missing discriminator is the **caller return address** — no existing trace captures it.

**The single most ground-truth-direct unobserved fact (fresh frame):** every kcdx OUTPUT is correct, so the surviving question is whether the engine even ASKS for the indexed geometry swap-ON, and what request-stream divergence (driven by an enum/existence answer) makes it skip — i.e. does the swap-ON OPEN/READ/ENUM call sequence on the menu/geometry asset load diverge from swap-OFF in WHICH paths are requested / what enumeration returns, not in file content.

**PROBE V (passive, zero behavioral change — no "broke differently" risk):** add `_ReturnAddress()` (module-relative) to the existing `FS_BOOT_TRACE` lines, so each request is attributed to the engine SUBSYSTEM making it. Tag the geometry/UI-asset-loader caller(s). Keep the existing boot-phase gate (near-zero cost) + bounded read logging (open+first-read per handle, not per chunk; enum name-dumps capped). For ENUM/FIND, log `count_engine` / `count_kcdx_virtual` / `count_returned` (the unified-set triple). Run swap-ON and swap-OFF; the agent diffs the two `FS_BOOT_TRACE` streams aligned by caller+path.

**Outcome→meaning map (pre-committed, flat — theory-independent):**
- **A — swap-ON MISSING geometry-asset `path=` requests swap-OFF makes** → the engine never ASKS; divergence is UPSTREAM (an earlier enum/existence answer or a non-CCryPak subsystem steered it to skip). Next: bisect the last common request before divergence — its answer steered the engine. FALSIFIES "kcdx serves correctly so the engine gets what it needs."
- **B — same requests, but an ENUM/FIND returns a DIFFERENT list swap-ON** (extra unified/pak-virtual entries, different order, missing loose entry) → divergence is enumeration RESULTS, not content. Next: from INSIDE kcdx's owning enum impl (NOT a 63/64/65 thunk — preserves the table-DB load), return the engine-loose-only set for ONLY the asset-loader prefix (caller-scoped) and re-launch. FALSIFIES "the unified enumeration set is harmless." (This subsumes the enumeration-set lean I had narrowed to.)
- **C — same requests, same enum lists, all served with matching bytes, MISS=0 both** → the request stream is identical end-to-end; the divergence is NOT any CCryPak op → pivot OFF the filesystem to a non-CCryPak subsystem the swap perturbs (a global/timing/state side-effect of the vtable write). Next: cdb capture comparing the asset-load subsystem state ON/OFF. FALSIFIES the entire "it's something kcdx serves/answers" family — the strongest possible kill.
- **D — a `path=` returns MISS/notfound ONLY swap-ON** where swap-OFF serves/exists → a path-normalization/alias/case difference on that request class (the minority of metadata NOT answered `how=original`). Next: dump that path's full normalization kcdx vs engine. FALSIFIES "metadata/existence answers all correct."

**Cleanliness:** the first launch (passive trace) carries ZERO "still-black-but-broke-differently" risk — it changes no byte/handle/return, the log is the deliverable. Only the B follow-up is behavioral; keep it caller-scoped (one prefix) + assert in-log that table-DB prefixes still get the unified set, so a table-DB regression shows as a distinct signal, never silently conflated with "still black."

**NOTE — this reframes the takeover question if B fires:** the unified-set enumeration (the totalizing-invariant design §5.1) returning pak-resident entries the engine's loose walk wouldn't list could make the geometry loader load/skip/iterate differently. The fix (per total-ownership) would be to make kcdx's enum impl return the RIGHT set for the geometry caller from inside its own ownership — not thunk back.
