---
id: KI-0019
reported: 2026-06-13
status: Open
area: asset-system / HOOK 2 cross-CRT FILE* handed to the engine (FOpenLooseOverlay)
discovered_by: Phase-10 verification-probe launch (cap-105/106/107) — crash on inventory open, 2026-06-13 11:09:56 run
commit_at_report: f115640
---

> **STATUS: OPEN — NOT FIXED, NOT VERIFIED.** No code has changed; the crash still
> reproduces (non-deterministically). What exists is a LEADING DIAGNOSIS from the dump +
> source (cross-CRT `FILE*` `fseek`), with one residual UNCONFIRMED (HOOK 2 hit-vs-miss
> on the FSR2-init file — see Trail E). The fix is DEFERRED to Phase 11 / FIX A (bundled
> with KI-0006), not written and not tested. Nothing here is a verified fix; closure
> requires Phase 11's fix to land AND a launch confirming the crash is gone.

# KI-0019 — ACCESS_VIOLATION on inventory open: runaway re-entrant `engine.ccrypak_fopen` recursion

## Symptom

Repro (user, 2026-06-13): launch KCD2 → load save → enter world → **open inventory** →
ACCESS_VIOLATION crash. Boot + reaching the world is clean; the crash fires on the
inventory-open gesture.

## Evidence (ground truth, agent-read from kcdx-dev_2026-06-13_11-09-56.log + the .dmp)

- **The AV** (dev log L17748): `FAULTED site=unhandled plugin=(none) code=ACCESS_VIOLATION
  rip=0x7FF9575F2632 module=WHGame.DLL module_rva=27731506`. FAULTED_REGS:
  `rax/rcx/rdx/rsi/rdi/r8/r9` all `0x0` (null/freed-pointer deref).
- **The faulting fire** (L~17816+): a `FAULTED_FIRE` stack of repeated
  `plugin=kcdx hook=engine.ccrypak_fopen va=0x7FF955FE14A0` with `seq` counting DOWN
  from **568978** — a runaway re-entrant `FOpen` recursion half a million fires deep.
- **Frames**: frame 0 = WHGame.DLL @ rva 27731506; frames 1-9 = `ucrtbase.dll`
  (CRT heap/string family); frames 10-18 = WHGame.DLL in the `0x7FF955FE0xxx-FE2xxx`
  range (the `ccrypak_fopen` VA neighborhood). A CRT-heap fault reached through the
  pak-FOpen path.
- **Fault inventory** (L17815): `total=66 plugin_hook=48 engine=6 lifecycle=5 probe=2
  bytes=5`. The `probe=2` is ENGINE-internal diagnostics (PROBE Q dummynode etc.,
  L4363-4366), NOT the cap-105/106/107 probes.

## What this is NOT (yet — to confirm)

- **NOT (almost certainly) caused by the Phase-10 probes.** cap-105/106/107 all
  `REPORT pass=false "kcdx.hook returned nil — registration failed (raw-RVA expert
  hatch rejected at register time)"` (dev log L3889/3893/3897) — they installed ZERO
  hooks. The faulting hook is `engine.ccrypak_fopen`, kcdx's own asset hook, present
  before the probes. (Probe A below confirms by removal.)
- **NOT (yet confirmed) the same bug as KI-0012.** KI-0012 (CLOSED) was the SAME hook
  + SAME signature (ACCESS_VIOLATION, `ccrypak_fopen` re-entrancy spiral, seq counting
  down) but a different TRIGGER (boot/DLSS-FSR2 graphics init) and a different root
  cause (pak-less plugin records polluting the engine MOUNT list). KI-0012's mount-list
  gate is firing correctly this run (the log shows cap-105/106/107
  `enabled_list_plugin_no_pak ... excluded from the engine MOUNT list (KI-0012)`). So
  KI-0012's fix held; this is a DISTINCT re-entrancy on the same hook, at a different
  trigger.

## Resolution routing (user-decided 2026-06-13) — BUNDLED → KI-0006 / Phase 11, stays OPEN

KI-0019 is the **`fseek` sibling of [KI-0006](KI-0006-serve-execute-vehicle-not-found.md)**:
the SAME `src/asset_overlay.cpp` HOOK 2 cross-CRT `FILE*` ("return kcdx's OWN CRT
FILE*"), the SAME hazard class. KI-0006 found the cross-CRT **`fclose`** (PROBE D:
WHGame's `fclose` frees kcdx's `/MT` handle, mod-init trigger); KI-0019 is the cross-CRT
**`fseek`** (`ucrtbase` `get_osfhandle` rejects kcdx's fd, FSR2/DLSS-init trigger). One
root cause, two engine consumers of the same foreign `FILE*`.

KI-0006 is BUNDLED → Phase 11 (FIX A drops the static vendored Lua/CRT and routes
through WHGame's symbols, collapsing the dual-runtime that CREATES the cross-CRT-`FILE*`
hazard class at the source). KI-0019 rides the same fix: **the user approved bundling it
into Phase 11 rather than a separate patch** — one structural fix, not two. KI-0019 stays
**OPEN, Phase-11-gated**; KI-0006 is the durable bundle handle. When Phase 11's single
runtime lands, both the `fclose` and the `fseek` cross-CRT instances are re-verified
against the post-FIX-A architecture (the surviving evidence here carries forward). No
separate fix track; no provisional mask (the mechanism IS root-caused — it is a known
capability constraint deferred to its structural fix, not an unknown).

## CORRECTED mechanism (from the minidump — supersedes the "re-entrancy" reframe below)

Read `kcdx_2026-06-13_11-09-56.dmp` with cdb (`.ecxr; !analyze -v; k 40`) — the
deterministic artifact that should have been read FIRST (before any live-launch
bisection). It overturns the text-log-only reading:

- **The fault is a NULL-POINTER WRITE, not a re-entrancy stack overflow.**
  `WHGame!CreateGameStartup+0x97932: mov dword ptr [rax],0Dh` with **`rax=0`** —
  writing `0x0D` (13) to address 0. `!analyze`: `AV.Fault: Write`,
  `AV.Dereference: NullPtr`, `Failure.Bucket: NULL_POINTER_WRITE_c0000005_WHGame.dll`.
  My earlier "runaway recursion, seq from 568978" read was the GUARD's hook-fire
  INVENTORY dump, NOT the fault mechanism — a misread of the text log.

- **`0x0D` = 13 = EACCES.** The faulting frames are the CRT's invalid-parameter path:
  `fseek` → `common_fseek` → `lseeki64_nolock` → **`get_osfhandle+0x55`** →
  **`invalid_parameter_noinfo`** → **`invalid_parameter`** → the AV. `ucrtbase`'s
  `get_osfhandle` was handed an INVALID file descriptor, invoked the invalid-parameter
  handler, which faulted writing `errno = EACCES (13)` through a null slot.

- **The deepest non-CRT caller is `fseek()` on a `FILE*`** inside WHGame's pak code:
  the frames `WHGame+0x46200a` / `0x461ba0` / `0x46088c` (the `0x...FE1xxx-FE2xxx`
  neighborhood) are the SAME region as the logged `engine.ccrypak_fopen` hook
  va `0x7FF955FE14A0` — grounded from TWO artifacts (the guard log's va + the dump's
  return addresses agree).

- **Outer frames are FSR2 / DLSS init**: `WHGame!ffxFsr2ResourceIsNull+…` →
  `WHGame!NVSDK_NGX_UpdateFeature+…` (recursive). The SAME graphics-init subsystem as
  KI-0012. `kcdx.dll` is loaded (its own static CRT); `ucrtbase.dll` is WHGame's CRT.

**Mechanism (evidence-grounded, the leading root cause):** kcdx's asset-system
`ccrypak_fopen` HOOK 2 (own-FILE* loose-open, `9590dd4` — "cross-runtime FILE*
confirmed live") returns a `FILE*` opened by **kcdx's CRT**. WHGame's FSR2/NGX init
later `fseek`s that `FILE*` through **`ucrtbase` (WHGame's CRT)**; the fd is not valid
in `ucrtbase`'s descriptor table → `get_osfhandle` invalid-parameter → null EACCES
write → AV. This is the **cross-CRT `FILE*` hazard KI-0006 named** ("cross-CRT FILE*
free confirmed-real") surfacing as a cross-CRT `fseek`.

**Why NON-DETERMINISTIC (explains B.3):** FSR2/DLSS init touching that specific `FILE*`
is GPU/driver/timing-dependent, so the identical probe config crashes only sometimes.
The probes are a RED HERRING — they were never on the faulting stack (they installed
zero hooks); the bug is in the asset HOOK 2 cross-CRT FILE* path and is independent of
Phase 10. (Probe A/B/B.2 "no crash" were non-determinism, not real eliminations.)

**Owed next (to confirm before a fix):** read the asset HOOK 2 source (the own-FILE*
loose-open path) to confirm it hands a kcdx-CRT `FILE*` back to the engine, and confirm
KI-0006's cross-CRT-free finding is the same FILE*. This is a design-surface fix (the
asset FOpen hook must hand back a handle the engine's CRT owns, or not intercept the
path FSR2 uses) → Gate A. NOT the probes.

## Reframe (SUPERSEDED — the text-log-only "re-entrancy" theory, kept for honesty)
The reframe below was my pre-dump theory. It is WRONG on the mechanism (the dump shows
a null write, not a recursion spiral). Kept un-rewritten per one-pass discipline; the
CORRECTED mechanism above is grounded.

### Original reframe (the leading frame, to falsify not confirm)

The `ccrypak_fopen` hook (specifically the asset-system **HOOK 2 own-FILE* loose-open**
path, `9590dd4`, and the AdjustFileName resolver HOOK 1, `4a687f3`) was substantially
reworked in the asset-system phase AFTER KI-0012 closed. Inventory-open drives many pak
FOpen calls (item icons/defs). Leading (UNVERIFIED) frame: a re-entrancy guard gap in
the asset-system FOpen detour lets an inventory-driven FOpen re-enter itself unboundedly
→ stack/heap blowup → AV. To be PROVEN by probe, not asserted.

## Probe plan (persisted before running — plan-persistence)

| # | Probe (one variable) | Status | Result |
|---|---|---|---|
| A | Remove cap-105/106/107 from the live install; repro inventory-open | DONE | NO CRASH (run 11-21-13: 0 AV, 0 fault, no dmp, reached kPostLoadGame, suite 332 = probes absent). Probes IMPLICATED — falsifies "nil-hook probes can't matter". |
| B | (gated on A) Re-deploy ONLY cap-107 (`after`-hook getter 0x1a7dac0); cap-105/106 absent; repro inventory-open | DONE | NO CRASH (run 11-27-05: 0 fault, suite 333 = only cap-107, reached kPostLoadGame). cap-107 EXONERATED. Culprit is cap-105 and/or cap-106 (the `before` probes). |
| B.2 | Re-deploy ONLY cap-105 (`before` on `CScriptTable::CallFunction` 0xb9ceb4); cap-106/107 absent; repro inventory-open | DONE | NO CRASH (run 11-30-12: 0 fault, only cap-105, reached kPostLoadGame). cap-105 alone EXONERATED. |
| B.3 | Re-deploy ALL THREE together (the original crashing config); repro inventory-open | DONE | NO CRASH (run 11-30-12... 11-31-17 reached kPostLoadGame, 0 fault, suite 333+... all 3 present). The IDENTICAL config that crashed at 11-09-56 did NOT crash → the crash is NON-DETERMINISTIC, not a deterministic function of the probe set. The bisection premise was WRONG; probes A/B/B.2 "exonerations" were non-determinism, not eliminations. |
| D | **Read the minidump** (`cdb.exe -z … .ecxr; k`) — the deterministic artifact, which should have been step 1 | DONE | **Found the actual fault.** See "CORRECTED mechanism" below. NOT a re-entrancy spiral; a cross-CRT `FILE*` `fseek` → CRT invalid-parameter → null EACCES write. |
| F | Read-only PROBE F: log every HOOK 2 HIT (vpath + kcdx-CRT fp) during inventory-open. Settle whether HOOK 2 serves a kcdx-CRT `FILE*` on the UI/inventory path at all (the hit-vs-miss residual). | DONE (no crash this run — non-determinism; 2nd no-repro of the gesture) | **HOOK 2 IS LIVE on the UI path.** 3 HITs (run 12-34-05): a `.lua` (`scripts/startup/sl_saveload.lua`, cap-77) AND a **UI `.dds`** (`libs/ui/textures/apse/attack_mode.dds` → comp-16, fired 2× at 12:35:41). The `.dds` is exactly inventory/HUD-texture territory → a kcdx-CRT `FILE*` IS handed to the engine for a UI texture and operated on cross-CRT. Corroborates the diagnosis (mechanism live + reachable on the UI path) but does NOT pin the EXACT crash file (crash didn't reproduce — non-deterministic). NOTE: the HITs are TEST-PLUGIN overlays (cap-77/comp-16); a clean game w/o them may serve fewer/no UI overlays. |
| G | (gated on F) the cross-CRT-handle FIX — design-surface, Gate A; approach surfaced to user (NOT the failed v1 engine-open) | pending | — |
| E | Read the HOOK 2 source (`src/asset_overlay.cpp` `FOpenLooseOverlay`) to confirm it hands a kcdx-CRT `FILE*` to the engine | DONE | CONFIRMED: HOOK 2 HIT mints a `FILE*` via kcdx's `_wfopen_s` (L339-342) + returns it as FOpen's result (L379). The design's cross-CRT safety proof (L259-261) is scoped to **FRead** only ("FRead routes any real heap FILE* to its OS arm; gate-verified `_research/asset-fopen-handle-recon/`") — it does NOT cover `fseek`/`get_osfhandle`, which validates the fd in the ENGINE's CRT. The gap is real. RESIDUAL (honest): the crash fires only on a HOOK 2 HIT (overlay map hit); whether the FSR2-init file was a HIT or a MISS (call_original, engine's own FILE*) is NOT yet confirmed — needs the crash-time overlay-map contents or a targeted probe. |

Probe A is the cheapest most-falsifying step (exonerates or implicates the probes in one
launch). Probe B is designed only after A's outcome; if A still crashes (probes
exonerated), B observes the re-entrancy ground truth in the asset hook directly.

## Facts

- The crash is an ACCESS_VIOLATION in WHGame.DLL with all-zero arg registers, reached
  through `ucrtbase` CRT-heap frames, under a `ccrypak_fopen` re-entrant fire stack
  (seq from 568978). (run 2026-06-13 11:09:56)
- The 3 Phase-10 probes installed zero hooks (kcdx.hook returned nil). (same run)
- KI-0012's mount-list gate excluded the 3 pak-less probes from the engine MOUNT list
  this run. (same run)

## Open questions (causal — NOT facts)

- Is the inventory-open crash caused by the probe deployment, or pre-existing/unrelated
  to it? (Probe A)
- Is the re-entrancy in the asset-system HOOK 2 own-FILE* path (`9590dd4`) or HOOK 1
  AdjustFileName (`4a687f3`)? (Probe B, gated on A)

## Separate, lower-priority defect (NOT this KI — flag for a later /execute)

The raw-VA `address = <pointer userdata>` entry-hook form (docs/lua/hook.md:215-218) was
REJECTED at register time for all 3 probes (`kcdx.hook returned nil`). Either the doc'd
form is wrong, or `get_module_base_address():add(rva)` doesn't produce what the hook
layer expects, or raw-VA entry hooks need something the docs omit. This blocks the
Phase-10 verification probes but is a separate fix from this crash.
