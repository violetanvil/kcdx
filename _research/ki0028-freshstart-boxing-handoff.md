# KI-0028 fresh-start boxing — HANDOFF (2026-07-03)

## The goal (user directive, verbatim intent)

The KI-0028 investigation (black screen when the kcdx filesystem-takeover swap is ON) spun for
WEEKS across 8 HOPs / 17 Reframes / 3 pivots, theory-hopping. The user called a HARD RESET:

> Box away ALL the KI-0028 (+ the FS-related KI-0026/27) research, evidence, and probes OUTSIDE the
> repo, so a FRESH perspective (no knowledge of any prior work) starts clean with ONE plan:
> **"from the beginning of the file-system swap, find the FIRST divergence in behavior between
> swap-ON (broken/black) and swap-OFF (kcdx-noswap marker; boots to menu)."** That is the ONLY
> question. No inference, no theory — logs must be 100% fact (what ran, where, what it returned).

Swap-ON = kcdx swaps its CCryPak (filesystem object) onto the engine → all file ops go through
kcdx. Swap-OFF = `<game-bin>/kcdx-engine/kcdx-noswap` marker present → swap skipped, engine keeps
its own FS, boots to menu. The swap MECHANISM itself is not the suspect; HOW kcdx's FS answers a
specific request downstream is.

## The sealed box (OUTSIDE the repo — fresh frame CANNOT grep it)

`../kcdx-ki0028-sealed/` (sibling of the repo at `C:\Users\Michael\Documents\KCD2 Mods\kcdx-ki0028-sealed`):
- `research/` — ~26 FS-era recon dirs (all ki0028-*, ki0026/27-FS, fs-takeover-*, adjustfilename,
  asset-loadpath, ccrypak-init, pak-mount, asset-fopen-handle, phase8.5-pak-resolver).
- `probe-archive-ki0028/` — the ki0028/ki0026-FS entries pulled from `_research/probe-archive/`.
- `probes/` — 9 probe pairs boxed from `src/fs_takeover/`: render_trace, drawcall, pso, present,
  stall_stack, dispatch, reswap, draw_caller_tally, boot_watch (.cpp+.h each).
- `known-issue-docs/` — the full KI-0028 theory-narrative doc.

## DONE this session (all uncommitted — needs a commit at the end)

1. **Research + KI doc + probes physically moved out of the repo** (above).
2. **Probe wiring removed:**
   - `CMakeLists.txt` — the 9 probe .cpp entries deleted.
   - `src/fs_takeover/seating_hook.cpp` — 8 probe includes + 7 ProbeStart arms + BootTraceResolveWhBounds
     + PROBE Z2 family-mask block + PROBE U reswap arm ALL removed. The kcdx-noswap marker (PROBE F)
     KEPT but comment/log de-theoried (now "swap_suppressed_by_marker"). `SwapVtableOnObject(pCryPak,
     kFamAll)` — the mask replaced with the plain kFamAll default. Product swap UNCHANGED.
   - `src/hooks.cpp` — boot_watch include + BootWatchTick() call removed.
3. **`src/fs_takeover/boot_trace.h` REWRITTEN to pure-fact logging** — kept TraceOpen/Read/Meta/Enum/
   EnumNames (slot + path + how-branch + result, all fact). REMOVED: TraceVanillaDiff/EnumDiff (they
   logged the CONCLUSION "kcdx diverges from vanilla" — inference), BootTraceResolveWhBounds,
   BootTraceCallerRva, BootTraceCallerFirstSeen, the WHGame-bounds atomics, all PROBE W/I/L/K wording,
   the `.bk2` skip, the 200000-frame window, the `#include boot_watch.h` dep. Gate is now purely
   `init::Current() < AfterGameApply`.

## IN FLIGHT — the ONE remaining decontamination (do this FIRST on resume)

Remove the vanilla-differential machinery from the 3 product slot files. It calls the now-DELETED
`TraceVanillaDiff`/`TraceVanillaEnumDiff` (build will FAIL until removed). The precise rule:

**metadata_slots.cpp:**
- DELETE the `DiffExist3(...)` helper function (lines ~127-136) entirely.
- DELETE every `DiffExist3(...)` call (lines ~290, 299, 306) and the `TraceVanillaDiff(...)` calls
  (lines ~134, 230, 379).
- DELETE the `caller` locals fed only to the diff: the `BootTraceCallerRva(_ReturnAddress())` lines
  (~210, 280, 364) and `BootTraceCallerFirstSeen(...)` gates (~131, 227, 376) — but CHECK each
  `caller` is not also passed to a KEPT TraceMeta/TraceOpen (it isn't — TraceMeta takes no caller).
- DELETE `#include <intrin.h>` (line 7, only _ReturnAddress used it).
- **KEEP `g_origIsFileExist3` / `g_origIsFileExist2` + their captures (lines 63,66,157,166) and the
  MISS-arm uses (316,389) — THESE ARE PRODUCT** (the real thunk-to-original for a location-gated /
  pak-only name). VERIFIED this session: lines 316/389 are "MISS — thunk the captured original",
  the actual product fallback, NOT diff.

**enum_slots.cpp:**
- DELETE the `TraceVanillaEnumDiff(...)` call (~218) + the `caller` local (~128-129) +
  `#include <intrin.h>` (line 8).
- `countDisk`/`countPakAdded` (~136,137,172,213): CHECK if the product uses them beyond the diff.
  If the walk needs the split for its OWN logic → keep; if ONLY the diff read them → collapse to the
  single `matched` count `TraceEnum` already logs. (Likely diff-only — the pre-split product just
  needed `matched`. Read the walk body to confirm before collapsing.)

**find_slots.cpp:**
- DELETE the `TraceVanillaEnumDiff(...)` call (~354-355) + the `caller` local (~349-350) +
  `#include <intrin.h>` (line 4). `countDisk`/`countAdded` (~351-353) are computed inline right
  there for the diff — delete them with the call (diskNames.size()/entries.size() stay if used
  elsewhere; the subtraction is diff-only).

Then: `grep -rnE "TraceVanilla|BootTraceCaller|DiffExist|PROBE|DIAGNOSTIC|NO-RESIDUE|_ReturnAddress"
src/fs_takeover/*.cpp src/fs_takeover/*.h src/hooks.cpp` must return CLEAN (no probe/theory residue).

## UPDATE (mid-session) — vanilla-diff removal DONE; a DEEPER contamination layer found

The vanilla-differential removal (step above) is COMPLETE across metadata_slots.cpp, enum_slots.cpp,
find_slots.cpp — `DiffExist3`, `TraceVanillaDiff/EnumDiff`, the `caller`/`_ReturnAddress` machinery,
the `countDisk/countPakAdded` split all removed; `g_origIsFileExist3/2/GetFileSize` KEPT (product
miss-thunks). `#include <intrin.h>` removed from metadata_slots + enum_slots. **STILL TO DO in
find_slots: remove `#include <intrin.h>` (line 4).**

A residue sweep then found a SECOND, deeper theory layer NOT yet handled — these are more than
comments, some change BEHAVIOR:

1. **PROBE Q in find_slots.cpp (lines ~199-280) — a LIVE behavioral experiment.** It emits SYNTHETIC
   DIRECTORY entries from FindFirst (immediate-child subdir names) that vanilla would not. This is a
   theory-driven behavior MODIFICATION baked into the enum, not just logging. DECISION NEEDED (surface
   to user): is the synthetic-dir emission (a) pure experiment → REMOVE it (revert to vanilla enum
   behavior), or (b) did it become a real fix kcdx now depends on → keep the behavior, strip only the
   PROBE Q framing? Must determine from whether any product path relies on the synthetic dirs. Likely
   REMOVE (it was a KI-0028 experiment), but confirm — removing behavior the game now needs would
   break the FS. `synthDirs` telemetry local goes with it.

2. **PROBE Z2 family-mask in vtable_swap.cpp/.h — plumbed into the product swap API.** `SwapVtableOnObject`
   takes a `liveFamilyMask` param (vtable_swap.h ~34-47); the body (vtable_swap.cpp ~96-190) uses it to
   selectively thunk slot families. seating_hook now always passes `kFamAll` (full swap), so the mask is
   effectively a no-op, BUT the param + the per-slot family-gating logic remain. CLEAN-UP: simplify
   `SwapVtableOnObject` to drop the `liveFamilyMask` param entirely and always do the full swap (remove
   the family-bit gating in the body + the `kFam*` enum in the .h + SlotFamily map). Pure simplification
   — product behavior with kFamAll is unchanged. Verify vtable_table.cpp's family map is only used by Z2.

3. **Comment-only PROBE references (strip wording, no behavior change):**
   - `read_slots.cpp:6,62` — "KI-0026 PROBE K" / "PROBE K" in comments on the kept TraceRead. Reword to
     neutral ("read-family boot-window trace").
   - `vtable_swap.cpp:9` — the boot_trace include comment says "PROBE L pak-handle-vector snapshot"; if
     vtable_swap still calls a Trace* that no longer exists, remove the call; else just reword.
   - `file_handle.cpp:160`, `file_handle.h:122,128,159` — "DIAGNOSTIC (FS-op trace contract)" + the
     vpath field. The vpath-on-handle field is KEPT (the neutral TraceRead resolves it) — just reword
     the "DIAGNOSTIC"/"NO-RESIDUE" comments to plain description. Behavior stays.
   - `seating_hook.cpp:12` — the boot_trace include comment still says "BootTraceResolveWhBounds" (now
     deleted). Reword to "FS boot-window operation logging".
   - `find_slots.cpp:118` — a "PROBE Q" mention in a comment above the mask logic; goes with PROBE Q.

**Sweep command to reach CLEAN** (must return nothing but legitimate `kcdx-noswap`):
`grep -rnE "PROBE|DIAGNOSTIC|NO-RESIDUE|VANILLA|BootTrace(Caller|Resolve)|liveFamilyMask|kFam(None|All|Open|Read|Metadata|Enum)|synthDir|wedge" src/fs_takeover/ src/hooks.cpp`

## THEN (in order)

4. **Build green:** `pwsh ./build.ps1` — exit 0 + 3 artifacts. Fixes any missed reference.
5. **Verify product FS behavior unchanged** — the swap + resolve + read/enum/metadata slots are
   untouched in logic; only the diff instrumentation + probes were removed. A menu boot (swap-OFF)
   + a swap-ON boot should behave EXACTLY as before this session (still black swap-ON — we removed
   NO fix, only instrumentation).
6. **Commit** the whole box-away as one milestone (research moved out = deletions in git; product
   de-instrumented; boot_trace neutralized). Message: the reset + what was boxed + what stays.
7. **Write the fresh-frame plan doc** — `docs/known-issues/KI-0028-*.md` gets a NEW minimal stub
   (bug still open, tracked) with ONLY: symptom (swap-ON black, swap-OFF menu), the swap-ON/OFF
   mechanism, and the plan ("instrument from the swap point forward; find the FIRST swap-ON vs
   swap-OFF behavior divergence via the neutral FS_BOOT_TRACE logging; no prior theory"). NO
   reference to any boxed work. This is what the fresh frame reads.

## SCRUB VERIFICATION (2026-07-03, post-commit 7a247af) — found MORE leaks, full scrub in progress

A verification sweep after the box-away commit found the scrub was INCOMPLETE. The commit boxed
the _research dumps + probes, but KI-0028 theory survived in three more places a fresh frame reads.
User directive: FULL scrub (product-code comments too). Complete contamination map:

### (i) 17 tracked Ghidra scripts — the boxed-theory probe GENERATORS. BOX ALL to ../kcdx-ki0028-sealed/ghidra-scripts/:
third-party-ghidra/ghidra_scripts/{FindShaderManAnchors, Ki28CacheLoadDecomp, Ki28CfxbLookupDecomp,
Ki28CfxbLookupDecomp2, Ki28DrawRecordDecomp, Ki28Hop3CallersDecomp, Ki28Hop5CallerDecomp,
Ki28Hop7EnqueueDecomp, Ki28Hop7bAppendScan, Ki28Hop7cFrameDriver, Ki28IndexedCallerDecomp,
Ki28PsoCreateDecomp, Ki28PsoCreateDecomp2, Ki28PsoCreateDecomp3, Ki28PsoCreateDecomp4,
Ki28RenderSubmitAnchors, Ki28ScenePassTrace}.java — `git rm` them (physical move out).
NOTE: third-party-ghidra heavy binaries are gitignored but these .java SCRIPTS are TRACKED.

### (ii) PRODUCT-CODE comments carrying KI-0028 theory (~52 lines, 13 files). Per comment:
KEEP the real product decision, STRIP the theory/conclusion framing + the KI-0028 xref. E.g.
asset_index.cpp: keep "this fold maps data/gameshaders/X -> shaders/X (the Shaders.pak stored
prefix)"; STRIP "This IS KI-0028 root cause / the level-load abort / black screen / aborted the load".
Files + theory-line counts:
- src/fs_takeover/asset_index.cpp (13) — HEAVIEST: 'KI-0028 level-load abort', 'black screen',
  'This IS KI-0028 root cause', collision-check (448->182), bind-root narrative, KI-0026/28 xrefs.
- test-plugins/cap-112-asset-index/cap-112.cpp (8), cap-115-engine-pak-index/cap-115.cpp (6),
  cap-118-fs-takeover-finditer/cap-118.cpp (4) — test-plugin header comments referencing KI-0028.
- src/asset_overlay.cpp (6), src/lua_bind_assets.cpp (3), test-plugins/README.md (4),
  docs/known-issues/README.md (2 — one is the fixed row, check the other), asset_namespace.h (1),
  asset_index.h (1), enum_slots.cpp (1), find_slots.cpp (1).
  Sweep term set: `ki-?0028|black.?screen|level-load abort|render-item|draw_indexed|no.?render|HOP [0-9]|render.?submission`.
  BE SURGICAL: some 'KI-0028' xrefs are legit historical (a real fix's provenance) — reword to state
  the fact ('the bind-root prefix keying', 'the recursive pak walk') without the black-screen/abort
  theory or the bug-number, since that number now points at a DIFFERENT (reset) framing.

### (iii) docs/outstanding-work/file-system-takeover/README.md (2) — the FS-takeover FEATURE plan (legit,
KEEP the tree) but its ~line-30 block points at the OLD KI-0028 filename with the 'boot HANGS at
UI/render, KI-0026->27->28 chain' theory. REPOINT to KI-0028-fs-takeover-boot-no-render.md + drop the
hang/chain theory (restate: 'boot renders nothing with the swap active — tracked as KI-0028'). The
'PROBE F' refs in that tree are a DIFFERENT, completed probe-removal step (historical DONE) — leave.

### CLEAN sweep target (must return only the new stub + this handoff + legit feature refs):
`git grep -niE "ki-?0028|black.?screen|level-load abort|render-item|draw_indexed|HOP [0-9]|render.?submission" -- src include docs test-plugins third-party-ghidra`
Then rebuild green + commit the scrub as a follow-up to 7a247af.

## SCRUB PROGRESS (2026-07-03 session 2) + the DECIDED approach for the remainder

DONE this session (uncommitted, staged where noted):
- 17 Ki28*.java ghidra scripts BOXED to ../kcdx-ki0028-sealed/ghidra-scripts/ + git-rm-cached (staged).
- src fully scrubbed: asset_index.cpp (13 theory lines), asset_index.h (2), asset_overlay.cpp (1),
  enum_slots.cpp (1), find_slots.cpp (1). Approach: KEEP the real product-decision explanation,
  STRIP the bug-number + the black-screen/level-abort/fatal THEORY framing. (lua_bind_assets.cpp,
  asset_namespace.h were already clean — only generic 'hop 1' feature terms, not KI-0028.)

REMAINING (do these, then build + commit):
- **Test-plugins — DECIDED APPROACH (user): RE-ANCHOR to the fix, drop the bug-number.** These are
  SHIPPED regression tests of REAL landed fixes (NOT boxed investigation). test-suite.md/AP15 requires
  their falsifiable claims — DO NOT strip them. For EACH: KEEP the full mechanism + what-FAIL-means
  claim; only replace the 'KI-0028'/'KI-0026' bug-NUMBER with the FIX's neutral name:
    - cap-115 'the KI-0028 fix' / 'KI-0028 black-frame path' -> 'the gameshaders-alias fold' /
      'the alias-fold's failure path'. 'KI-0026' -> 'the Engine-root/%engine%-alias fix'.
    - cap-118 'the KI-0028 regression' / 'KI-0028 PROBE Q mask-bypass' -> 'the synthetic-dir mask-gate'.
    - cap-112 'KI-0028 regression' / 'level-load abort (black screen)' -> 'the bind-root keying' /
      'the bind-root miss'.
    - test-plugins/README.md rows (cap-115 §1862-1873, cap-45 §605, cap-99 §1622-1625 KI-0015,
      cap-116/117 KI-0026): same re-anchor — keep the falsifiable claim, neutralize the bug-#.
      NOTE cap-99/cap-45 reference KI-0015/KI-0026 (DIFFERENT closed bugs) — those are fine to keep as
      historical provenance OR neutralize for consistency; they are NOT the boxed KI-0028 investigation.
      The load-bearing rule: never weaken a falsifiable claim; only swap the bug-# for the fix name.
- **docs/outstanding-work/file-system-takeover/README.md ~line 30**: repoint the KI-0028 link to
  KI-0028-fs-takeover-boot-no-render.md + drop the 'boot HANGS at UI/render, KI-0026->27->28 chain'
  theory (restate: 'boot renders nothing with the swap active — tracked as KI-0028'). Keep the rest of
  the (legit FS-takeover feature-plan) tree. The 'PROBE F' refs there = a different completed step, leave.
- **docs/known-issues/README.md**: the KI-0028 row is already the new stub (fixed earlier); check the
  2nd hit is just the closed KI-0026/27 rows (legit, leave).

FINAL: `git grep -niE "ki-?0028|black.?screen|level-load abort|render-item|draw_indexed|HOP [0-9]|render.?submission" -- src include docs test-plugins third-party-ghidra`
must return ONLY: the new stub, this handoff, and legit re-anchored FIX descriptions (no bare bug-#
pointing at the boxed investigation). Then build green + commit the scrub as a follow-up to 7a247af.

## What STAYS in the repo (product — the thing under investigation)

`src/fs_takeover/` product code: vtable_swap, vtable_table, seating_hook, asset_index, file_handle,
open_slots, read_slots, enum_slots, metadata_slots, find_slots, pak_reader, loose_mode, boot_trace.h
(now neutral). The kcdx-noswap marker mechanism (for the swap-OFF control arm). The neutral
FS_BOOT_TRACE logging is the fresh frame's tool — it logs every slot's fact during boot.

## Method contract the user set (hold to it)

- Forward walk from the swap point, in boot order. First swap-ON-vs-OFF divergence = the lead.
- Logs are 100% fact: what function ran, where, what it returned. NEVER "x didn't work" / a verdict.
- One probe/theory at a time; after ONE failed fix, re-observe — never fix #2 on a new theory.
- Reuse-first: read existing logs before launching. Agent builds/deploys/reads logs; user only launches.
