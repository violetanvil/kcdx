---
id: KI-0026
opened: 2026-06-16
status: open
commit_at_filing: ba391e0
---

# 0xC8 CryFatalError in NGX/FSR2 graphics-init (C_Game::CreateInstance) — first boot with the fs-takeover metadata/enum slots live

**Status:** open

A boot crash on the first launch carrying the file-system-takeover step-3.3
existence/metadata + enumeration slots live (engine build deployed from the
working tree at `247d295`+`ba391e0`, plus the uncommitted PROBE M mount-slot
instrumentation). The engine raises a `0xC8` CryFatalError (decimal 200) inside
its NGX/FSR2 graphics-init path during `C_Game::CreateInstance` — the same
graphics-init signature as KI-0012 (the mod-mount-list-walk fatal) and the
KI-0019/KI-0006 cross-CRT family. No kcdx frame appears in the fault stack; the
crash is the engine fatal-erroring on data/state, not a direct kcdx code fault.

**Repro:** launch KCD2 (RTX 3080 Ti — NVIDIA, so the NGX/DLSS path is active);
crash ~6s into boot, before the main menu, during graphics init. The launch was
the PROBE M acceptance run (intended to reach gameplay to answer the pak-mount
lifecycle question); it never got past init.

## Trail

| Date       | Action | Result |
|------------|--------|--------|
| 2026-06-16 | Launched the PROBE M build (step-3.3 metadata/enum slots live + 5 mount-slot logging stubs) | Crash ~6s in, during graphics init; minidump + crash bundle written (`crash_2026-06-16_15-10-22.zip`, dmp `kcdx_2026-06-16_15-10-22.dmp`) |
| 2026-06-16 | Read the dump (`cdb -z … -c ".ecxr;!analyze -v;k 40"`) | Exception `0xC8` via `KERNELBASE!RaiseException`; 14-frame stack all WHGame→KingdomCome.exe: `RaiseException` ← `NVSDK_NGX_UpdateFeature+0x871b5a` ← `CreateGameStartup` ← `ffxFsr2ResourceIsNull` (×3) ← `C_Game::CreateInstance` ← KingdomCome.exe thread start. Bucket `APPLICATION_FAULT_c8_WHGame.dll`. No kcdx frame. |
| 2026-06-16 | Checked PROBE M (the new mount-slot stubs) | EXONERATED — all 5 stubs installed (`mount_slot_wrapped` slot=6/7/9/10/100 at 15:10:25) but ZERO `pak_lifecycle_event` fires before the crash; the trampoline never ran. Not the cause. |
| 2026-06-16 | Read the dev-log tail for last kcdx activity before the fatal | `ctor_bracket_complete enabled_n=15` (kcdx replaced ModManager_ctor, MOUNT iterates kcdx's order) → `swap_live_first_open` slot 36 FOpen dispatched into kcdx (`./system.cfg`) → `kcdx_open_first` (kcdx handle minted) → **`loose_open_failed slot=FOpen vpath="engine/config/engine_core.thread_config" errno=2`** → ~430ms later the `0xC8` fatal. |
| 2026-06-16 | PROBE A: deploy the known-good `842e5d5` DLL (no metadata slots, no PROBE M), relaunch | STILL CRASHES — identical fault (`code=200`/`0xC8`, same culprit `WHGame.DLL rva=38115914` = the NGX raise site, same 14-frame stack; only ASLR base differs). The metadata slots are NOT the cause; the leading theory is killed. (dmp `kcdx_2026-06-16_16-05-19.dmp`) |
| 2026-06-16 | PROBE C: re-observe — grep the known-issue tree for this exact stack/code | DUPLICATE FOUND. `docs/known-issues/save-load crash 0xC8 raised from WHGame.md` carries the BYTE-IDENTICAL stack (`RaiseException 0xC8` ← `NVSDK_NGX_UpdateFeature+0x871b5a` ← `CreateGameStartup+0xda687` ← `ffxFsr2…`) — a pre-existing bug, CONFIRMED kcdx-caused (vanilla loads clean, 7/7 with kcdx crash), latent corruption set up early + tripped later in the asset/shader path. KI-0026 is the SAME crash class. |

## Probe plan (persisted before running — flip each row as it lands)

| Probe | Status | One-variable action |
|-------|--------|---------------------|
| A | DONE | Time-bisect: deploy the last-known-good DLL (`842e5d5`, the open+read cutover, accepted clean) — ONE variable = the engine DLL build. Boots clean ⇒ regression is in `247d295`+ (the metadata/enum slots); still crashes ⇒ cause pre-dates the metadata slots (kills the leading theory). |
| B | superseded | (was gated on A=clean — A came back crash, so the metadata-slot observation probe is moot; the cause is not the metadata slots) |
| C | DONE | Re-observe (A killed the leading theory): grep the known-issue tree for this exact stack/code → DUPLICATE of `save-load crash 0xC8 raised from WHGame.md` (a pre-existing, CONFIRMED-kcdx-caused bug). KI-0026 is the same crash class; the investigation reframes to that doc, which is further along. |
| D | INVALID | Engine-vs-plugin bisect attempt — moved `test-suite/` to `kcdx-plugins/_test-suite-DISABLED-probeD/`. INVALID: the discovery walker RECURSED into the `_`-prefixed dir and loaded all 118 plugins from it anyway (`Discovered plugin … from …\_test-suite-DISABLED-probeD\cap-01-patch\`); `enabled_n=15` unchanged. The plugin-set variable did NOT change; the crash this run is uninformative. Same trap as the canonical doc's PROBE C–F. Redo as D2 (move the folder OUTSIDE the kcdx-plugins tree). |
| D2 | DONE | Engine-vs-plugin bisect, DONE RIGHT (test-suite parked OUTSIDE the tree). VALID: discovered-plugin count dropped to 3 (the 2 cap-38 + builtin bugsplat-fix; the 119 test plugins gone). STILL CRASHES — byte-identical 0xC8 / culprit `WHGame.DLL rva=38115914`. Cause is kcdx's ENGINE LAYER (vtable swap / ctor-bracket / hooks), NOT the plugin set. Note: `enabled_n=15` UNCHANGED with ~0 test plugins → the engine MOUNT list is not built from the test plugins. |
| E | DONE | Content-vs-mechanism bisect on the MOUNT list. VALID: `enabled_n=0` (`ctor_bracket_empty_list` — every mod disabled via load_order.toml). STILL CRASHES — byte-identical 0xC8 / culprit `WHGame.DLL rva=38115914`. The mod CONTENT is NOT the cause; the crash is the ctor-bracket / vtable-swap MECHANISM itself, content-independent (zero mods, zero test plugins, still fatals in graphics-init). |
| F | code landed; awaiting launch | LOGGING GAP — the crash window is a black box. The log shows kcdx's last actions (swap live, FOpen ./system.cfg, loose_open_failed engine_core.thread_config) then the engine fatals 240ms later in graphics-init with NOTHING about the engine-side path: `FAULTED_INVENTORY=(not captured)`, WHGame frames unsymbolicated, no trace of what file ops graphics-init drove through kcdx's slots before the 0xC8. The 3-part observability build (F.1 early ctx-B inventory capture, F.2 PDB-verified for offline cdb symbolication, F.3 `FS_BOOT_TRACE` full boot-window slot trace) is landed (see the PROBE F table) — the next crash launch will show the populated `FAULTED_INVENTORY` + the full crash-window slot stream under `FS_BOOT_TRACE`, and the dump symbolicates offline against `kcdx.pdb`. |

## PROBE F — the 3-part logging enhancement (settled design, Gate A clear)

User-directed: "if these logs dont clearly state exactly the issue, then our
logging isnt strong enough." Scope chosen: "All three — full crash-window
observability." Symbolication: "Offline — emit the PDB, symbolicate the dump
after." Gate A (architect-review) returned forward-and-wait with one design fork
(symbolication safety), now resolved offline; #1 and #3 cleared as mechanically
sound. This build does NOT attempt to fix the `0xC8` — it makes the mechanism
observable for the next probe.

| Part | What | Status |
|------|------|--------|
| F.1 | **Earlier inventory capture** — ADD a `LogInventory(...)` call at an init-phase boundary BEFORE graphics-init (`CtorBracketInstalled`/`EngineHooksInstalled`), so `FAULTED_INVENTORY` is populated when a graphics-init fault reads it. NOT moving the existing boot call (`hooks.cpp:445`, in the [ctx C] first-update-tick block AFTER graphics-init). Extend cap-45 coverage. | DONE (code) — second `LogInventory(Info)` added at `src/dllmain.cpp` right after `AdvanceTo(CtorBracketInstalled)` (ctx B, before `CSystem::Init`/graphics-init), token `early_capture_ctxB` + `EarlyInventoryCapture` distinct from the boot call. `modification_inventory::{MarkEarlyCaptureRan,EarlyCaptureRan}` latch added; cap-45 gains the falsifiable `cap-45-early-inventory` row (PASS iff the early ctx-B capture ran AND populated a non-sentinel summary). Awaiting build + launch. |
| F.2 | **PDB emission + offline symbolication** — verify `/Zi`+`/DEBUG` (CMakeLists.txt:387-401, already present) emits `kcdx.pdb` beside `build/Release/kcdx.dll`; confirm `package-release.ps1` excludes it from the shipped zip. The crash guard is UNTOUCHED (stays the allocation-free no-SymInitialize walker); symbolication is done offline by running cdb against the dump+PDB. | DONE (verify, no code change) — CMakeLists.txt:396-401 emit the PDB for the `kcdx` target in the MSVC branch (`/Zi` compile + `/DEBUG`+`/OPT:REF`+`/OPT:ICF` link, Release-gated), so `kcdx.pdb` lands beside `build/Release/kcdx.dll`. `package-release.ps1` ships an explicit allowlist (`kcdx.exe`, `kcdx-engine/kcdx.dll`, watchdog, builtins, catalog, load_order) AND post-build VERIFIES the zip against that allowlist (lines 164-184), throwing on ANY unexpected entry — `.pdb` is excluded by omission and would actively FAIL the package step if it ever leaked. Crash guard untouched. No code change needed. |
| F.3 | **Boot-window FS-slot trace** — a permanent diagnostic gated `init::Current() < AfterGameApply`: ONE relaxed-atomic gate load per slot call, branch-predicted-skip after boot, ZERO allocation. Records which file ops graphics-init drives through kcdx's slots (path + slot + result) in the crash window. A kept diagnostic, not a scratch probe — no-residue discipline does not apply. | DONE (code) — new header-only inline helper `src/fs_takeover/boot_trace.h` (`BootWindowActive()` = `init::Current() < AfterGameApply`, one relaxed-atomic load + predicted-skip after boot; `TraceMeta`/`TraceOpen`/`TraceEnum` log via `LOG_DEBUG_KV` under tag `FS_BOOT_TRACE`, every string a borrowed inbound/literal pointer, zero allocation). Wired into all 8 metadata slots (`metadata_slots.cpp`), the open/resolve path (`open_slots.cpp` FOpen via `OpenResolvedAndMint` + both `kcdx_AdjustFileName` arms), and enum (`enum_slots.cpp` ForEachFile, with a match count). Existing first-only latches kept alongside. Read slots untouched (they carry no path — opaque kcdx handle-ids only; the spec's slot list is open/metadata/enum). No new cap (diagnostic; manager verifies the trace live on the next crash launch). Awaiting build + launch. |

## Facts

- Exception code is `0xC8` / decimal 200 (`code=200` in the kcdx GUARD log, `0xC8`
  in the dump — same value), raised via `KERNELBASE!RaiseException`, NOT an access
  violation. `rdi=0xC8`, `r14=0xC8` in the fault context.
- The fault stack is entirely engine: `NVSDK_NGX_UpdateFeature` →
  `CreateGameStartup` → `ffxFsr2ResourceIsNull` (×3) → `C_Game::CreateInstance`.
  Graphics init (NGX/FSR2). No kcdx frame.
- This is the FIRST launch with the step-3.3 metadata/enum slots (13/45/67/68/69/
  70/92/93 + 14) live and dispatching during boot. The open+read cutover (3.2,
  `842e5d5`) was accepted live earlier and was clean (KI-0019 clean) — but that
  was before the metadata slots answered engine queries during graphics init.
- The 8 metadata-slot originals were captured at swap time
  (`metadata_originals_captured` at 15:10:25) — the Decision-C miss-thunk wiring
  is present, so an index MISS *should* fall through to the engine original.
- One resolution anomaly immediately precedes the fatal: `loose_open_failed` on
  `engine/config/engine_core.thread_config` (errno=2 / not-found) through kcdx's
  FOpen. Whether this is causal (an engine config/asset graphics-init needs,
  mis-resolved by kcdx) or benign (the engine probing an optional path) is
  UNVERIFIED — a probe target, not asserted.
- PROBE M is in-tree but uncommitted (the mount-slot logging stubs +
  `src/fs_takeover/probe_m_pak_lifecycle.{h,cpp}` + the vtable_swap wiring). It
  did not cause the crash but is part of the deployed build.
- Hardware: RTX 3080 Ti (NVIDIA) — the NGX/DLSS path is active (bugsplat
  `Attributes` GPU Info), consistent with the `NVSDK_NGX_UpdateFeature` frame.
- The SAME crash reproduces on the known-good `842e5d5` DLL (no step-3.3 metadata
  slots, no PROBE M): identical `code=200`/`0xC8`, identical culprit
  `WHGame.DLL rva=38115914` (the NGX raise site), identical 14-frame stack shape
  (only the ASLR load base differs). The step-3.3 metadata/enum slots are NOT the
  cause (PROBE A).
- `842e5d5` was accepted CLEAN earlier this session (cap-113 PASS, KI-0019 repro
  clean per the commit), yet the SAME binary now crashes — so the engine DLL is
  NOT the variable that changed between the clean acceptance and this crash
  (PROBE A). Something else in the live install / environment changed.
- This crash is a DUPLICATE of the pre-existing `save-load crash 0xC8 raised from
  WHGame.md`: byte-identical stack (`RaiseException 0xC8` ← `NVSDK_NGX_Update
  Feature+0x871b5a` ← `CreateGameStartup+0xda687` ← `ffxFsr2…`). That doc has it
  CONFIRMED kcdx-caused (vanilla loads clean; 7/7 with kcdx crash) and latent
  (corruption set up early by kcdx, tripped later in the asset/shader path — no
  kcdx frame because it is latent) (PROBE C).
- The prior doc's repro was save-LOAD (~10s after the load hooks); KI-0026's is at
  BOOT graphics-init (before the menu). Same fatal-error path + stack, reached
  from a different entry point. (PROBE C)
- With the 119 test plugins parked OUTSIDE the tree (3 plugins discovered), the
  crash is byte-identical → the cause is kcdx's ENGINE LAYER (vtable swap /
  ctor-bracket / hooks), NOT the plugin set (PROBE D2).
- The engine MOUNT list kcdx's ctor-bracket synthesizes is `enabled_n=15` and is
  UNCHANGED by parking the test plugins — it is 15 REAL installed pak mods
  (FastLaunch, cheat, easytoseeherbs, kcdx_test_paklua, lua_memory_verify,
  luck_laid_bare, mh_rebalanced_sharpening, + 7 Workshop mods incl. ebapmod,
  instagather, xnude, znpcoverhaul, + lua_sandbox_probe). kcdx feeds these to the
  engine C_ModManager MOUNT list; graphics/DLSS/FSR2 init walks it — the KI-0012
  mod-mount-list surface. (PROBE D2)
- With `enabled_n=0` (every mod disabled, `ctor_bracket_empty_list` — the engine
  mounts NO mods, the list slots are a valid empty vector begin==end==cap==0) the
  crash is byte-identical. The mod CONTENT is NOT the cause; the crash is the
  ctor-bracket / vtable-swap MECHANISM itself, content-independent (zero mods, zero
  test plugins, still fatals in graphics-init). The suspect narrows to: the vtable
  swap, the ctor-bracket's from-scratch C_ModManager synthesis (even empty), or a
  kcdx hook — one leaves the engine in a state graphics-init fatals on. (PROBE E)
- LOGGING GAP — the crash window is unobservable. The dev log's last kcdx lines
  are `ctor_bracket_complete enabled_n=0` → `swap_live_first_open` →
  `kcdx_open_first ./system.cfg` → `loose_open_failed
  engine/config/engine_core.thread_config` (errno=2) → the engine `0xC8` fatal
  ~240ms later. NOTHING records the engine-side graphics-init path:
  `FAULTED_INVENTORY=(inventory not yet captured)`, the WHGame frames are
  unsymbolicated bare offsets, and no instrumentation traces which file ops
  graphics-init drove through kcdx's slots (or what they returned) in that window.
  The mechanism is invisible to the current logging — the canonical `0xC8` doc
  flagged the same gap (no PDB; GUARD reports KERNELBASE not the real culprit).

## Open questions

- This is the same crash class as `save-load crash 0xC8 raised from WHGame.md`
  (and adjacent to KI-0019/KI-0006, the cross-CRT FSR2/DLSS family the file-system
  takeover §9 exists to fix structurally). Should KI-0026 be CLOSED as a duplicate
  and the investigation folded into the canonical `0xC8` doc (which is further
  along — confirmed kcdx-caused, deterministic), rather than re-run here? (A triage
  decision — the user's call.)
- The prior doc's NEXT step was "a VALID engine-DLL bisect with kcdx confirmed
  injecting" — PROBE A is exactly that (the crash reproduces on `842e5d5` WITH
  kcdx injecting, GUARD log present), so the bisect is now valid and the crash is
  NOT a recent-DLL regression — it is the standing kcdx-latent-corruption bug.

## Open questions

- Is the crash caused by THIS build's kcdx resolution (the metadata/open slots
  mis-serving an engine file graphics-init reads), or does it pre-date the
  step-3.3 metadata slots? (A revert-to-`842e5d5`-DLL relaunch, or a `/debug`
  probe, isolates it.)
- Does graphics init (NGX/FSR2 / `C_Game::CreateInstance`) query a kcdx
  metadata/open slot (IsFileExist / GetFileSize / GetFileAttributes / FOpen) for
  a file that lives in an engine-mounted pak the kcdx index does NOT carry — and
  does kcdx's answer (or the Decision-C miss-thunk fall-through) return the wrong
  result (not-found / size 0) that the engine then fatal-errors on? (The §6 /
  Decision-C pak-long-tail gap — the suspected mechanism, unproven.)
- Is `engine_core.thread_config`'s `loose_open_failed` causal or incidental?
- The `0xC8` is the engine's own fatal-error code — what assertion/condition
  inside the NGX/FSR2 init raises it? (The dump's WHGame frames are unsymbolized;
  the RVA `WHGame+0x871b5a` past `NVSDK_NGX_UpdateFeature` is the raise site.)

## Evidence

- Minidump: `<game-bin>/kcdx-engine/logs/kcdx_2026-06-16_15-10-22.dmp` (60 MB).
- Crash bundle: `<game-bin>/kcdx-engine/logs/crash/crash_2026-06-16_15-10-22.zip`
  (dev log + per-plugin logs + dmp + `bugsplat_C92T3LA2.log` + `game/kcd.log`).
- Dev log: `<game-bin>/kcdx-engine/logs/kcdx-dev_2026-06-16_15-10-22.log`.
- Static recon context: `_research/fs-takeover-pak-mount-recon/FINDINGS.md`
  (the pak-mgmt slot model), KI-0012 (the mod-mount-list graphics-init fatal),
  KI-0019 (the cross-CRT FSR2/DLSS-init crash family).
