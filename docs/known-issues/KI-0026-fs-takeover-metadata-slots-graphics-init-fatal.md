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
| 2026-06-16 | PROBE F: the 3-part observability build (early ctx-B inventory capture + `FS_BOOT_TRACE` + PDB), full normal mod set, relaunch + read the now-instrumented window | All 3 instruments fired. `FAULTED_INVENTORY` POPULATED (was sentinel): `total=6 plugin_hook=2 engine=3 lifecycle=0 probe=1 bytes=0` (lua_pcall, lua_newstate, update, bugsplat_ctor probe — NO fs vtable swap, NO mod content). `FS_BOOT_TRACE`: 23 ops, **21/23 `how=miss-original`, 2 `how=original` — ZERO index hits.** The asset index IS built + complete (`asset_index_built entries=305176 paks=41 loose=4` at 10.544, BEFORE the crash). Every traced op is an engine CONFIG/LOG/SAVE path (`kcd.log`, `./system.cfg`, `data/pak.cfg`, `engine/config/engine_core.thread_config`, save-games dir) — correctly NOT in the asset index → correctly thunked to the original engine body. Same `0xC8` / culprit `WHGame.DLL rva=38115914`, identical 14-frame stack. |
| 2026-06-19 | PROBE K (invalid run): read-family trace built, but PROBE J copy-mode flag left ON → `kcdx_owned=0`, zero kcdx slots, NO crash | INVALID — process error. The deployed build still had `kProbeJ_CopyOriginalVtable=true` from the prior probe, so every slot was the engine's own body (copy-of-original = PROBE J, which boots). Zero kcdx read slots ran → zero `FS_BOOT_TRACE read` lines. Reproduced PROBE J's clean boot, told us nothing new. Fixed: flipped copy-mode OFF, rebuilt, redeployed. |
| 2026-06-19 | PROBE K (valid): full takeover live (`kcdx_owned=28`), read-family trace, relaunch | **CRASH — and DECISIVE. The read family IS reached, with the CORRECT kcdx handle, CORRECTLY tagged.** Trace: `FOpen ./system.cfg result=3` → `read Fileno handle=3 tag=1` → `read FReadRaw_byPakIndex handle=3 tag=1` → `read FClose handle=3 tag=1`; identical full lifecycle for `data/pak.cfg` (FOpen→3, Fileno/FReadRaw/FClose all `tag=1`). The last kcdx FS op on the FAULT thread (tid=24152) is `FClose handle=3` at 41.512; the `0xC8` fatal fires at 41.866 (~354ms later), same culprit `WHGame.DLL rva=38115914`, identical 14-frame stack, on the SAME thread, with NO kcdx file op in between (only BugSplat collection on tid=15744 at 41.787). **The handle-id-straddle theory is DEAD: `tag=1` everywhere = valid kcdx handles, the engine did NOT mis-route through its own pak-arm test, the open→read→close lifecycle completes cleanly. Graphics-init reads system.cfg + pak.cfg through kcdx correctly, gets correct bytes, closes cleanly — then fatals ~354ms later with no kcdx file op feeding it.** ALL 28 kcdx slots are now instrumented (open+read+meta+enum); every dispatch in the crash window returned correctly. |

## Probe plan (persisted before running — flip each row as it lands)

| Probe | Status | One-variable action |
|-------|--------|---------------------|
| A | DONE | Time-bisect: deploy the last-known-good DLL (`842e5d5`, the open+read cutover, accepted clean) — ONE variable = the engine DLL build. Boots clean ⇒ regression is in `247d295`+ (the metadata/enum slots); still crashes ⇒ cause pre-dates the metadata slots (kills the leading theory). |
| B | superseded | (was gated on A=clean — A came back crash, so the metadata-slot observation probe is moot; the cause is not the metadata slots) |
| C | DONE | Re-observe (A killed the leading theory): grep the known-issue tree for this exact stack/code → DUPLICATE of `save-load crash 0xC8 raised from WHGame.md` (a pre-existing, CONFIRMED-kcdx-caused bug). KI-0026 is the same crash class; the investigation reframes to that doc, which is further along. |
| D | INVALID | Engine-vs-plugin bisect attempt — moved `test-suite/` to `kcdx-plugins/_test-suite-DISABLED-probeD/`. INVALID: the discovery walker RECURSED into the `_`-prefixed dir and loaded all 118 plugins from it anyway (`Discovered plugin … from …\_test-suite-DISABLED-probeD\cap-01-patch\`); `enabled_n=15` unchanged. The plugin-set variable did NOT change; the crash this run is uninformative. Same trap as the canonical doc's PROBE C–F. Redo as D2 (move the folder OUTSIDE the kcdx-plugins tree). |
| D2 | DONE | Engine-vs-plugin bisect, DONE RIGHT (test-suite parked OUTSIDE the tree). VALID: discovered-plugin count dropped to 3 (the 2 cap-38 + builtin bugsplat-fix; the 119 test plugins gone). STILL CRASHES — byte-identical 0xC8 / culprit `WHGame.DLL rva=38115914`. Cause is kcdx's ENGINE LAYER (vtable swap / ctor-bracket / hooks), NOT the plugin set. Note: `enabled_n=15` UNCHANGED with ~0 test plugins → the engine MOUNT list is not built from the test plugins. |
| E | DONE | Content-vs-mechanism bisect on the MOUNT list. VALID: `enabled_n=0` (`ctor_bracket_empty_list` — every mod disabled via load_order.toml). STILL CRASHES — byte-identical 0xC8 / culprit `WHGame.DLL rva=38115914`. The mod CONTENT is NOT the cause; the crash is the ctor-bracket / vtable-swap MECHANISM itself, content-independent (zero mods, zero test plugins, still fatals in graphics-init). |
| G | DONE — DECISIVE | RESULT: **NO CRASH — reached the main menu.** With the swap-write bypassed (`probe_g_swap_bypassed` confirmed at 22:52:23.328: vtable built `kcdx_owned=28`, object kept its NATIVE engine vtable) and EVERYTHING else identical (seating hook fired, kcdx vtable built, originals captured, index built), the game boots clean past graphics-init to the first update tick (`TEST SUMMARY passing=315` at 22:52:58, ~35s in). ZERO `0xC8`, zero GUARD fault. **The vtable swap-WRITE — overwriting `[pCryPak+0x00]` with kcdx's vtable pointer — IS the `0xC8` mechanism.** The crash is the swap's side-effect on the object/engine state that NGX/FSR2 graphics-init trips on, NOT anything kcdx serves (PROBE F/I already proved dispatch + metadata answers are correct/unused). Bonus: cap-45-early-inventory PASSED (F.1 verified on a real run). PROBE I removed (null, no-residue). |
| H | pending | Environment-delta probe — resolve the standing contradiction (`842e5d5` accepted CLEAN earlier this session, same binary crashes now). Retained as a fallback. |
| I | DONE — null (axis exonerated) | RESULT: ZERO `PROBE_I` lines fired — and zero is a FINDING. The metadata slots had ZERO index hits in the entire boot window (`FS_BOOT_TRACE how=` distribution: 21 miss-original + 2 original (BugSplat GetFileStat at crash time, on the collector thread tid=45792), and 0 index-loose / 0 index-pak / 0 index-either / 0 index). PROBE I instruments only the index-HIT arms, so with zero hits it correctly never fired. Graphics-init reads NO existence/size through a kcdx index-answering slot in the crash window → there is no divergence because there is no index-answered metadata query. The metadata-answer-divergence axis is DEAD. Same byte-identical `0xC8` (now PDB-symbolicated: `RaiseException` ← `NVSDK_NGX_UpdateFeature+0x871b5a` ← `CreateGameStartup+0xda687` ← `ffxFsr2ResourceIsNull`×3 ← `C_Game::CreateInstance+0xabf4f2`). The probe is captured-and-removable (scratch). |
| I | (orig) pending | **Metadata-slot answer-divergence probe (architect-named, the lead cut).** Gate A architect-review (re-task verdict) EXONERATED the slot-1 miss-thunk: it is string-only resolution (§5-sanctioned), P4 is RESOLVED (live boot-to-world through 101 thunked slots — `_research/probe-archive/p2-p4-seating-and-ki0019-persists.md`), the §1 invariant is held (kcdx owns the open/bytes on its CRT; only name→string resolution defers). The `0xC8` is graphics-init READING engine filesystem state that differs under the swap. The metadata/existence slots (13/45/67/68/69/70/92/93) are KCDX-owned answering from the unified index. PROBE: in the boot window, log every metadata-slot query with kcdx's answer vs what the captured-original would return for the SAME name (originals already captured at swap). A DIVERGENCE on an NGX/FSR2 config/shader asset graphics-init validates = the mechanism. Ground-truth-first, theory-independent (observes the raw answer-vs-original delta, does not test a theory about it). |
| F | DONE | LOGGING GAP closed. The 3-part observability build (F.1 early ctx-B inventory capture, F.2 PDB, F.3 `FS_BOOT_TRACE`) landed (`3f1a72c`) + ran. RESULT: the crash window is now fully observable and it EXONERATES the file-dispatch path — every traced op resolves correctly (config/log/save paths correctly miss the asset index → correctly thunk to the original engine body; the 305K-entry index is built + complete before the crash). kcdx serves no WRONG file answer. This CONFIRMS the PROBE E reframe with direct evidence: the crash is the takeover MECHANISM (the vtable swap / its side-effects on object or engine state), NOT a mis-served file. The next probe targets the swap's side-effects, not its dispatch results. |

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

## Probe plan — root-cause narrowing (post-PROBE-G)

| Probe | Status | One-variable action |
|-------|--------|---------------------|
| J | DONE — DECISIVE | RESULT: **NO CRASH — reached the menu** with the pointer swapped but g_kcdxVtable an exact COPY of the original (`probe_j_copy_vtable`, `kcdx_owned=0`, zero GUARD faults; boot ran ~36s further than the crashing run, to the FOREIGN_HOOK selftest). ⟹ **the pointer swap / memory location is INNOCENT; the `0xC8` is a kcdx SLOT IMPL.** Candidates 2/3 (cached-original mismatch, address/RTTI check) are DEAD. The crash is one of the 28 kcdx slot impls behaving wrong for what graphics-init dispatches. |
| (review-logs) | DONE | Read-heavy digest of the crashing run's (`22-26`) crash window (swap→fatal, tid=21192) vs the clean J run (`23-26`). FINDING: **ZERO kcdx-SERVED ops** in the window — all 30 fault-thread FS ops are `how=miss-original`/`original` (kcdx impls ran but returned the ORIGINAL's answer; the FOpen of `./system.cfg` minted a kcdx handle, result=3, succeeded). No `index-*` op. So the INDEX-RESOLUTION theory is dead — graphics-init's crash is NOT a wrong index answer. CAVEAT (load-bearing): FS_BOOT_TRACE covers only open/meta/enum slots that take a path; the READ family (38..66) is NOT traced, and slot 35/36's handle-MINTING (kcdx handle-id vs engine FILE*) is the change PROBE J reverts. The suspect narrows to: a kcdx slot the trace doesn't cover — the read family, or the open slots' kcdx-handle-id representation — that graphics-init dispatches and trips on. |
| J | (orig) in progress | Partial-swap discriminator — separate "the POINTER changed / where it points" from "a kcdx SLOT's behavior". Swap the vtable pointer as normal (same `[pCryPak+0x00]` overwrite, same g_kcdxVtable memory), but build g_kcdxVtable as a FAITHFUL COPY of the original — every slot = `originalVtable[i]`, ZERO kcdx slots. ONE variable vs PROBE G's full swap: kcdx-slot-contents present vs all-original-contents, pointer swapped either way. STILL CRASHES ⇒ the engine cares THAT the pointer changed or WHERE it points (cached-original mismatch / address-range / RTTI-identity check) — fix targets the pointer/memory. BOOTS CLEAN ⇒ a specific kcdx SLOT impl graphics-init dispatches is the cause — bisect which family next. Re-enables the swap-write (PROBE G flag back OFF), adds a copy-mode flag. |
| K | DONE — DECISIVE | Read-family boot-window trace (OBSERVATION, takeover intact). RESULT: **read family reached with CORRECT kcdx handles, all `tag=1`, full open→read→close lifecycle clean** on system.cfg + pak.cfg, then `0xC8` ~354ms later with no kcdx file op feeding it. The handle-id-straddle theory is DEAD (the engine does NOT mis-route the kcdx handle through its own pak-arm test). With ALL 28 kcdx slots now instrumented, EVERY dispatch in the crash window returns correctly — the last "kcdx serves/operates a wrong value" axis is eliminated. (First run was invalid — copy-mode left ON; re-run with `kcdx_owned=28` was decisive.) |
| L (static) | DONE — LEAD NARROWED | STATIC EVIDENCE FIRST (results-driven §4) — REUSED `_research/asset-fopen-handle-recon/FINDINGS.md` (body-read of the engine FOpen `FUN_1804614a0`, 2026-06-03). FINDING: the engine's original FOpen, on the PAK path, **writes the open entry into the pak-handle VECTOR at `param_1[8]` = `[this+0x40]`** (walks for a free slot, writes the entry at :260, returns `slot+1`). kcdx's FOpen mints into its OWN private pool and writes NOTHING into `[this+0x40]` — the vector stays empty. The engine's own FReadRaw dispatch reads that vector's element COUNT (`FUN_180427e40([RBX+0x40])` = `(end-begin)/0x18`). So `[this+0x40]` (the pak-handle vector) is the concrete engine member the original FOpen mutates and kcdx omits — the fresh-frame mechanism, body-read and OFFSET-NAMED. CAVEAT: applies only if system.cfg/pak.cfg take the engine's PAK path (loose files return a `FILE*` and write no vector entry) — L-live must confirm pak-vs-loose + that graphics-init reads `[this+0x40]`. |
| L (live) | DONE — FALSIFIED THE `[+0x40]` READING FOR THESE FILES | RESULT: across all 3 boot-window FOpens (system.cfg, pak.cfg, engine_core.thread_config), the vector triple at `[CCryPak+0x40/+0x48/+0x50]` reads **`begin=0 end=0 cap=0 count=0`, `pre`==`post`**. Re-read of the FOpen body (asset-fopen-handle-recon) shows the `[+0x40]` pak-handle vector is written ONLY on the **pak-resident** path; the **loose** path (`:200-205`) returns a `FILE*` and writes NO vector entry. These 3 files are LOOSE config files (all `how=miss-original`, not pak) → the engine's OWN original FOpen would ALSO leave `[+0x40]` at `0/0/0` for them → `[+0x40]` is the WRONG member to watch for these opens (reading (b)). The `0/0/0` falsifies the pak-vector reading for the crash-window files, it does not confirm a mechanism. **BUT** the loose path makes two ENGINE MEMBER-CALLS kcdx omits: `vtable[0x2c8](this,…)` at `:203` and `vtable[0x268](this, FILE*, …)` at `:204` — engine open-file registration for the loose handle. kcdx's FOpen makes NEITHER. The mechanism candidate REFRAMES from "the pak vector at `[+0x40]`" to "the loose-handle registration via `vtable[0x268]`/`[0x2c8]` that kcdx omits". → PROBE L2 watches what THOSE write. |
| Gate B | DONE — probe-required HALT | root-cause-verifier (WITHHELD context) REJECTED the cross-CRT-fseek root cause: KI-0019's raw-`FILE*` hazard does NOT transfer to KI-0026's tagged-int `3` (a valid-looking low fd, not an invalid one); contradicted by PROBE K (`Fileno` through kcdx, kcdx-CRT fd, tag=1); ~354ms gap rules out a sync fseek-fault; AV vs deliberate `RaiseException(0xC8)` is a real surface diff. Resolution does NOT land. Owes PROBE P. |
| P (static) | DONE — DECISIVE (outcome b) | DUMP RE-READ (results-driven §4, no launch) of `kcdx_2026-06-19_23-09-24.dmp` (the PROBE N crash). The faulting stack is a CLEAN deliberate `RaiseException(0xC8)` from inside `NVSDK_NGX_UpdateFeature+0x871b5a`, whole chain in WHGame engine code (NGX/FSR2 → CreateGameStartup → C_Game::CreateInstance → thread start). **ZERO `ucrtbase`/`fseek`/`get_osfhandle`/`invalid_parameter` frame** — NONE of KI-0019's CRT-fault signature is present. This FALSIFIES the cross-CRT-fseek mechanism directly (outcome a is DEAD): there is no file op anywhere in the fault path. It is NGX *deliberately deciding to abort* on a resource it found null (`ffxFsr2ResourceIsNull`), confirming **outcome (b)** = the swap-write's side-effect on engine/NGX state (PROBE G's direction), a DIFFERENT root than the rejected fseek story. Fault ctx: `rdi=r14=0xC8` (the code); two live NGX handles `r12=0x2b3fbb8d3c0`, `r13=0x2b47bf61cc0` carried into the raise. WHICH resource NGX found null is NGX-internal (no WHGame PDB) → the live half (P-live) at the raise site is owed for that last detail. |
| P (live) | pending — OWED | Instrument the NGX/FSR2 raise site `WHGame+0x871b5a` (or its caller) to capture WHAT `ffxFsr2ResourceIsNull` evaluated null at fault time — the specific resource/pointer NGX aborts on. The static dump settled it is a deliberate NGX raise on a null resource (not a CRT fseek); this names the resource so the swap-write→null-resource chain can be traced to its origin (does the swap-write zero/invalidate an NGX/FSR2 resource the engine cached, or break a CCryPak-object field NGX reads to locate it?). Theory-independent. NOTE: heavier (hooks an NGX RVA, not a kcdx slot) — design the hook safely. |
| N | DONE — DECISIVE (kills object-member theory) | RESULT: for ALL 3 loose config opens, **`engine diffs=0`, `kcdx diffs=0`, `revert diffs=0`** (over a `0x400` window). The engine's OWN original FOpen of a loose file writes ZERO object-member bytes — so kcdx is missing NOTHING at the object level; kcdx's open and the engine's open produce BYTE-IDENTICAL object state. The object-member-write mechanism (pak-vector AND registration-call) is DEAD. Crash still reproduced (`code=200`, same culprit) — genuine ground truth. Per the outcome map, `engine diffs=0` ⇒ the divergence is NOT an object member → global/TLS or handle-identity/consumability (fresh-frame P2/P3). CAVEAT to close: confirm the `0x400` window wasn't too small (a write at `≥0x408` would also read `diffs=0`) — though zero writes across 1KB for BOTH opens strongly indicates loose-open is object-state-free, not a windowing miss. | At slot-36, for each boot-window loose open: SNAP_A = `self[0..N)` before; run the ENGINE original FOpen (`originalVtable[36]`, forced `"rb"`) → SNAP_B; close via the ENGINE original FClose (`originalVtable[55]`) → SNAP_C; run kcdx_FOpen → SNAP_D. Report every 8-byte offset where A≠B (engine wrote) and A≠D (kcdx wrote); the MISSING-WRITE SET = `(A≠B)\(A≠D)`. Straddle-SAFE: original open+close both on the ENGINE CRT, same scope, rb-only, handle never reaches kcdx; SNAP_C==SNAP_A asserts the engine close reverted (kcdx's diff uncontaminated). Outcome: `(A≠B)\(A≠D)` non-empty ⇒ the omitted object-member write at offset X is the mechanism (read the engine FOpen store to [self+X], replicate). `(A≠B)` empty ⇒ engine writes NO object member for a loose open ⇒ divergence is global/TLS/handle-identity → P2 (global-region diff). `(A≠B)⊆(A≠D)` ⇒ kcdx already replicates ⇒ object-member theory dead → P2. Falsifying on all branches. Needs: object size N (err high, ~0x400; over-read safe read-only) + originalVtable[36]/[55] reachable from the marker (store process-lifetime at swap). |
| L (orig design) | superseded | OBSERVATION (user chose the snapshot probe over the hybrid). Snapshot the CCryPak object member window `[pCryPak+0x00..+0x100]` (covers the pak-handle vector triple) + the records the vector points at, BEFORE kcdx's FOpen, AFTER kcdx's FOpen, and after the captured ORIGINAL FOpen body run for the SAME name (its handle closed via the original FClose — engine open+close pair entirely on the engine CRT, NO kcdx CRT touch = no straddle, per the user's safety call). Log every offset where original-after ≠ before but kcdx-after == before. Built on the live full takeover, one variable = kcdx-body vs original-body object side-effect. EXACT offsets to watch (from L-static body read): `[+0x40]` vector BEGIN (`param_1[8]`/`plVar1`), `[+0x48]` END (`param_1[9]`), `[+0x50]` CAP (`param_1[10]`); the entry is a `0x18`-byte record written at `begin + index*0x18` via `FUN_1823c9004`. Original writes `[+0x40..+0x58]`/the record + kcdx does NOT ⇒ mechanism CONFIRMED (fix = kcdx FOpen replicates the vector write). No divergence ⇒ widen window / it is loose-not-pak ⇒ re-frame. |

## Fresh-frame reframe (post-PROBE-J, 2nd axis reframe → fresh-frame subagent per results-driven.md)

PROBE J killed the pointer/identity theories; the review-logs digest killed the
index-answer theory. A fresh-frame probe designer (leading theory WITHHELD) was
dispatched on the raw facts + killed theories. It surfaced the load-bearing
ground-truth fact and the surviving suspect set:

- **The kcdx handle-id encoding is `(id << 1) | 1` — a tiny ODD integer (smallest
  value = 3), NOT a 16-byte-aligned heap `FILE*`.** The one FOpen in the crash
  window minted `result=3` — that IS the kcdx handle-id `3`, not a "3=success"
  code. If graphics-init (or code it calls) operates that handle as a `FILE*`
  (dereference / range-check / fileno / hand to a CRT op), a value of `3` behaves
  catastrophically vs a real pointer. (handle rep: `src/fs_takeover/file_handle.h`)
- **The surviving suspects, reconciling PROBE J (a kcdx slot IS the cause) with the
  digest (no kcdx slot SERVED a non-original answer):** the differing kcdx behavior
  is something the open/meta trace does NOT capture — (a) the open slots' handle
  MINTING (FOpen returns a kcdx handle-id even on a `how=miss-original` open — the
  mint happens regardless of index hit), or (b) the READ family (38..66, untraced)
  operating that handle-id. Copy-of-original (PROBE J) reverted BOTH at once, which
  is why it could not separate them.

## Mechanism candidate — the handle-id straddle (the §4.4 "never reached" assertion is the probe target)

The handle-id header (`src/fs_takeover/file_handle.h` §4.4) and slot 38's body
comment (`read_slots.cpp:41`) both rest on ONE design-asserted runtime mechanism:
the engine's native read family dispatches on **`taggedHandle-1 < pakEntryCount`
→ engine pak arm, else the OS `FILE*` arm**; the contract claims this engine test
is "never reached" for a kcdx handle "because kcdx owns the read family, so the
read slots route on the kcdx tag." Per `.claude/rules/results-driven.md`, that
"never reached" is a checkable claim wearing a settled-decision costume — and it
is exactly the surface FS_BOOT_TRACE never covered.

- **The crash is in NGX/FSR2 graphics-init — engine code kcdx does NOT own**,
  holding the handle-id kcdx minted (`result=3`). If graphics-init hands that
  handle to an engine-internal helper, runs the engine's OWN `handle-1 <
  pakEntryCount` test on it, or treats the small integer as a pak index / `FILE*`
  — rather than dispatching it back through a kcdx read slot — the "never reached"
  assumption is violated. A kcdx handle `3` (3 < pakEntryCount) would route into
  the engine's PAK arm, reading garbage from pak-entry-index-2; or, deref'd as a
  `FILE*`, fault on a near-null pointer. Either is content-independent, latent, no
  kcdx frame — the EXACT observed `0xC8` signature.
- **The takeover stays TOTAL.** The fix this points at is making kcdx's handle
  rep / read family SERVE graphics-init correctly (§1: kcdx owns every file op),
  NOT handing a family back to the engine. The earlier per-family copy-of-original
  bisect (Candidate A/B below) was the WRONG instrument — it revert-to-engine, i.e.
  walks back the takeover, and "un-take-over stops the crash" is a symptom going
  away, not the mechanism (AP17). Superseded by the read-family TRACE.

## Fresh-frame reframe #2 (post-PROBE-K, 3rd axis killed → fresh-frame subagent per results-driven.md)

PROBE K killed the read-family/handle axis (the 3rd axis killed: pointer → index →
read-family). A fresh-frame probe designer (leading theory WITHHELD) was dispatched
on the raw facts + all killed theories. With my lean hidden, it independently
reached a mechanism that RESOLVES the PROBE J / PROBE K contradiction — and it is an
axis nothing had observed:

**The engine's ORIGINAL FOpen body registers each open into the CCryPak object's OWN
internal open-file / handle bookkeeping (engine member state at `[pCryPak+0xNN]`).
kcdx's FOpen mints into its OWN private pool and touches ZERO engine members.** Same
return value (a kcdx handle), OPPOSITE side effects on the object. Graphics-init /
FSR2 later reads that engine-side bookkeeping DIRECTLY off the object (no vtable
dispatch, no kcdx frame) — finds it empty/stale — and trips `ffxFsr2ResourceIsNull`
→ `0xC8`, ~354ms after the clean kcdx close.

- **Why this is the ONE thing PROBE J could not isolate:** copy-of-original runs the
  engine's REAL FOpen body (which DOES register into engine members) → boots; full
  takeover runs kcdx's body (which does NOT) → crashes. The return value is identical
  (a handle), so EVERY return-value probe (F/I/K/digest) was blind to it. The
  difference is the engine-member SIDE EFFECT, not the served value.
- **It is takeover-PRESERVING.** The fix this points at is kcdx's FOpen replicating
  the engine's open-file bookkeeping ITSELF (kcdx maintains the object's open-file
  registry), NOT handing opens back to the engine. Consistent with §1.
- **Still a THEORY — a design-asserted runtime mechanism** ("the original FOpen
  writes object members graphics-init reads"). Per results-driven.md it is OBSERVED
  before any fix is built. The probe: snapshot the CCryPak object's member bytes
  around the first kcdx FOpen, and compare against what `originalVtable[36]` writes
  into `this` for the same open. Outcome map: the original mutates a member kcdx
  leaves unchanged → mechanism identified (next: xref that offset against the
  FSR2/NGX path, fix = kcdx replicates the write); no such divergence → the delta is
  elsewhere (engine globals / CRT state the original touches), widen the snapshot.
- **Static evidence FIRST (results-driven §4):** before the live byte-snapshot,
  decompile `originalVtable[36]`'s body for its `[this+0xNN]` member writes — that
  names the exact offsets to watch (and may settle the producer half on paper).
  Reuse-first ladder: existing `_research/` FOpen dumps → predecessor sigs → Ghidra.

## Next probe — read-family boot-window trace (observation, takeover 100% intact)

The right cut is OBSERVATION on the live full-takeover build, not a revert. Extend
the FS_BOOT_TRACE coverage to the READ family (the one untraced surface): on every
read-family slot dispatch in the boot window, log the raw handle value received +
the slot + the tag-bit, so the crash window shows whether graphics-init's handle
`3` ever reaches a kcdx read slot at all — or vanishes into engine code that
operates it off our slots.

- **Theory-independent / falsifying:** records ground truth (every read-slot
  dispatch + the raw handle), does not test a theory about it. If a kcdx read slot
  fires on handle `3` before the fatal → graphics-init DOES route through kcdx and
  the handle rep is operated correctly there (handle-id-straddle theory weakened,
  look elsewhere in the read impl). If NO read slot fires on it before the fatal →
  graphics-init operates the minted handle off kcdx's slots entirely (the engine
  ran its own logic on a kcdx handle — the "never reached" assertion is FALSE, the
  mechanism is the straddle). Either outcome is decisive.
- **Superseded (do NOT run):** the per-family copy-of-original bisect — Candidate A
  (open-original, read-kcdx) and Candidate B (read-original, open-kcdx). Both revert
  a family to the engine original, which contradicts the total-takeover invariant
  (§1) and answers only "does partial un-takeover stop the crash" (a symptom, AP17),
  not the mechanism. Replaced by the read-family trace above.

## Reframe #3 (post-L, mechanism-offset hopped 2× → method decision owed)

PROBE L falsified the `[+0x40]` pak-vector reading for the crash-window files (they
are loose, not pak — the vector is written only on the pak path). The candidate
reframed to "kcdx's from-scratch FOpen omits the engine's internal open-file
REGISTRATION member-calls" — the loose path makes `vtable[0x2c8](this, resolvedName,…)`
(every open) and `vtable[0x268](this, FILE*,…)` (loose-success) that kcdx never calls.
BUT the static reads are now AMBIGUOUS / mutually inconsistent:

- `front1-full-vtable-surface.md` calls `vtable+0x268`/`+0x2c8` "handle registration"
  (within FOpenRaw's body).
- The raw vtable-surface dump lists **slot 77 (`+0x268`) = `FUN_18241c9f4`, the
  `%USER%` expansion fn (params=0, ret=void)** — which does NOT match "registers a
  FILE* handle".
- `0x268/8 = 77`, `0x2c8/8 = 89`. Whether those member-CALLS write open-file
  bookkeeping kcdx must replicate is UNREAD (AP19 — not asserted).

I have hopped the mechanism-offset 2× (`[+0x40]` pak-vector → `vtable[0x268]/[0x2c8]`
registration) and the static evidence is no longer cleanly settling it. Per
results-driven §B.5 (2+ offset hops + ambiguous static reads) the next step is a
METHOD decision: a rigorous gated body-read of the FOpen body's FULL `this`-member
write set + the registration-call bodies (`/research-disassembly`, AP19 gate), or a
fresh-frame probe designer, rather than another ad-hoc offset guess. The crash
trigger (swap-write) and the direction (kcdx omits an engine open-side effect) are
solid; the EXACT omitted write is unread.

## ROOT CAUSE — REJECTED BY GATE B (probe-required HALT). The cross-CRT-fseek mechanism does NOT hold for KI-0026.

The "cross-CRT fseek/get_osfhandle" mechanism below was proposed (research-disassembly
tier-2 reuse of KI-0019) and **HALTED by the root-cause-verifier (Gate B)** — it does
NOT meet the AP17 bar for KI-0026. The verifier's independent re-derivation (working
agent's reasoning WITHHELD) found four defeating gaps; this section is kept as a
KILLED theory, not a Resolution.

- **Handle-representation conflation (the load-bearing defect).** KI-0019's cross-CRT
  fault was on a raw **`FILE*`** (the old asset_overlay HOOK 2 returned an actual
  `FILE*` to the engine). KI-0026's takeover returns a **tagged integer `3`** =
  `(id<<1)|1`, NOT a `FILE*` (file_handle.h / open_slots.cpp; trace `result=3`). In
  the engine's ucrtbase fd table, `3` is a *valid-looking low fd*, not an invalid
  kcdx-origin fd. The KI-0019 mechanism does not transfer across this boundary.
- **Contradicted by the doc's OWN decisive PROBE K.** `Fileno` (slot 46 — the exact
  call `get_osfhandle` sources its fd from) dispatched THROUGH kcdx's read slot with
  `tag=1`, computing `_fileno` on kcdx's CRT. The fd path was traced and went through
  kcdx correctly. "PROBE K couldn't see the direct fseek" was an unbacked assertion to
  save the theory (a confirm-only rationalization — results-driven violation).
- **Timing.** A cross-CRT `fseek` fault fires SYNCHRONOUSLY during the fseek; KI-0026's
  `0xC8` fires ~354ms after a clean FClose with NO intervening kcdx file op.
- **Over-claim.** KI-0019 = `0xC0000005` AV (CRT null-write); KI-0026 = deliberate
  `RaiseException(0xC8)` from NVSDK_NGX on an `ffxFsr2ResourceIsNull` check (NGX
  reading a wrong RESULT). "Same root, different surface" papered over this.
- KI-0019 is itself OPEN / leading-diagnosis with an unconfirmed HIT-vs-MISS residual
  — reusing it imported an unverified edge as a closed root cause.

**Gate B's owed probe (one variable, theory-independent, falsifying):** instrument the
engine `ucrtbase` `_get_osfhandle`/`fseek` entry (or the NGX/FSR2 call site at
`WHGame+0x871b5a`) to log the fd/handle ARGUMENT it receives in the boot window, AND
capture at the `0xC8` raise site what `ffxFsr2ResourceIsNull` evaluated (which
resource/pointer it read null). Ground-truth first. Outcomes: (a) `get_osfhandle`/
`fseek` IS called on `3`/the kcdx handle OUTSIDE a kcdx slot and faults there →
cross-CRT-fseek confirmed FOR KI-0026 (rewrite the Resolution to cite THIS, not
KI-0019); (b) no off-vtable CRT op on the kcdx handle; `0xC8` raises on a null/wrong
resource with no kcdx file op feeding it → the mechanism is the swap-write's
side-effect on engine/NGX state (PROBE G's direction), a DIFFERENT root cause; (c) the
fd reached `get_osfhandle` THROUGH `kcdx_Fileno` (valid kcdx-CRT fd) and did not fault
→ handle path clean, look at the resource NGX reads.

---

### (KILLED) The rejected cross-CRT-fseek mechanism — kept for the trail

`/research-disassembly` hit a TIER-2 reuse answer in `_research/ki0019-inventory-av-recon/
FINDINGS.md` (2026-06-13, dump-observed + source-confirmed). The claim was that KI-0026
and KI-0019 are the SAME cross-CRT hazard (the KI-0006 family). Gate B rejected it (above).

**Mechanism (dump-observed call-edge, §3.5-grounded — a real faulting stack, not an
inference):** FSR2/DLSS init (`ffxFsr2ResourceIsNull` → `NVSDK_NGX_UpdateFeature`,
`C_Game::CreateInstance` graphics-init) calls **`fseek`/`get_osfhandle` DIRECTLY on a
handle returned by a kcdx FOpen** — bypassing the CCryPak `FRead` slot entirely. The
KI-0019 dump shows the chain `fseek → common_fseek → lseeki64_nolock →
get_osfhandle+0x55 → invalid_parameter_noinfo → invalid_parameter`. `get_osfhandle`
validates the fd in the **engine's CRT (`ucrtbase`)**, where a kcdx-origin handle is
INVALID → invalid-parameter handler → fault. KI-0019 saw it as a null-write AV
(`0xC0000005`); KI-0026 sees it as the engine's `0xC8` deliberate fatal-raise — SAME
root, the invalid-parameter handler surfaces either way depending on path/timing.

**Why every prior KI-0026 probe saw "kcdx clean" — this resolves the whole chain:**
- PROBE K (read family clean, tag=1): FSR2 does NOT use the CCryPak read slots — it
  calls `fseek`/`get_osfhandle` on the handle DIRECTLY, outside the vtable. PROBE K
  traced read-SLOT dispatches, so it could not see the direct fseek.
- PROBE N (object state byte-identical): the hazard is not object state — it is the
  RETURNED HANDLE operated by the wrong CRT. (PROBE N's own lead — "the returned
  handle value is the surviving difference" — was correct; the recon names WHY.)
- PROBE J (copy-of-original boots): the original FOpen returns a real engine-CRT
  `FILE*` (valid fd in `ucrtbase`); the takeover returns a kcdx handle `(id<<1)|1`
  (not even a real `FILE*`) — invalid in `ucrtbase` → the direct fseek faults.
- The takeover's cross-CRT safety proof is scoped to `FRead` (routes a real `FILE*`
  to its OS arm by `handle−1 ≫ pak-count`). It does NOT cover the `fseek`/
  `get_osfhandle` FSR2 calls directly. THIS is the uncovered path.
- Non-deterministic (KI-0019): FSR2/DLSS touching that specific handle is GPU/driver/
  timing-dependent — explains the earlier "sometimes no crash" runs.

**Fix shape (takeover-PRESERVING, the open question for /design):** the engine must
never operate a kcdx handle on its own CRT via a direct `fseek`/`get_osfhandle` (or
any direct CRT op outside the CCryPak read slots). Options to weigh: (a) kcdx's FOpen
returns a handle whose fd IS valid in the engine's CRT (a real OS handle the engine's
ucrtbase can `get_osfhandle`), (b) kcdx also owns the slots FSR2's `fseek` path
dispatches through (if any are CCryPak vtable slots not yet kcdx-owned), (c) the
config files FSR2 reads are served such that the engine opens them itself. This is a
DESIGN fork (the takeover's handle-representation contract) — surface to the user /
route to the fs-takeover design, NOT decided here.

## Facts

- (KILLED — Gate B HALT) The proposed "cross-CRT fseek/get_osfhandle" root cause does
  NOT hold for KI-0026: it imports KI-0019's raw-`FILE*` hazard onto KI-0026's
  tagged-integer handle (`3`, a valid-looking low fd in ucrtbase, not an invalid one);
  it is contradicted by PROBE K (`Fileno` dispatched THROUGH kcdx, fd computed on
  kcdx's CRT, tag=1); a sync fseek-fault cannot explain the ~354ms gap; and AV
  (`0xC0000005`) vs deliberate `RaiseException(0xC8)` is a real surface difference, not
  "the same root". The mechanism is UNVERIFIED; Gate B owes a fd-argument/resource
  probe (see the rejected-root-cause section). This supersedes the prior ROOT CAUSE
  fact line.
- DUMP-CONFIRMED (P-static): the `0xC8` faulting stack has NO `ucrtbase`/`fseek`/
  `get_osfhandle`/`invalid_parameter` frame — it is a clean deliberate
  `RaiseException(0xC8)` from `NVSDK_NGX_UpdateFeature` on an `ffxFsr2ResourceIsNull`
  null check, whole chain in WHGame engine code, NO file op in the fault path. The
  cross-CRT-fseek mechanism is FALSIFIED by the dump (outcome a DEAD). NGX deliberately
  aborts on a null/wrong resource. (kcdx_2026-06-19_23-09-24.dmp)
- STRONGEST EVIDENCE-BACKED DIRECTION (PROBE G+N+P-static, now DUMP-HARDENED): the
  swap-WRITE is the trigger (PROBE G), kcdx's open produces byte-identical object state
  to the engine's (PROBE N), and the fault is NGX aborting on a null resource with no
  file op feeding it (P-static). So the cause is the STATE the swap-write leaves the
  CCryPak object / engine / NGX in that graphics-init trips on — NOT a wrong per-open
  value or handle op (every one observed correct), NOT a cross-CRT file fault. The owed
  P-live probe names WHICH resource NGX finds null, to trace the swap-write→null chain
  to its origin.
- DECISIVE (PROBE L): the crash-window files (system.cfg, pak.cfg,
  engine_core.thread_config) are LOOSE (all `how=miss-original`), and the engine's
  FOpen writes the pak-handle vector at `[+0x40]` ONLY on the PAK path — so `[+0x40]`
  reads `0/0/0` under kcdx AND would under the engine original for these files. The
  pak-vector is NOT the mechanism for the crash-window opens. The loose path's
  open-side effects are the internal calls `vtable[0x2c8]`/`vtable[0x268]`, which
  kcdx's from-scratch FOpen never makes. (PROBE L + asset-fopen-handle-recon body)
- DECISIVE (PROBE K): graphics-init dispatches into kcdx for `./system.cfg` and
  `data/pak.cfg` and runs the FULL read lifecycle on each — `FOpen → result=3`
  (a valid kcdx handle-id), then `Fileno / FReadRaw_byPakIndex / FClose` all on
  `handle=3 tag=1`. Every read fire carries `tag=1` (the kcdx tag bit set) — the
  engine operates the kcdx handle THROUGH kcdx's read slots, never through its own
  `handle-1 < pakEntryCount` pak-arm test. The handle-id-straddle theory is FALSE:
  the §4.4 "never reached" contract HOLDS for the read path. (PROBE K)
- DECISIVE (PROBE K): the fault thread (tid=24152) is the SAME thread that ran the
  swap + every file op. Its last kcdx FS op is `FClose handle=3` (data/pak.cfg) at
  41.512; the `0xC8` fatal fires at 41.866 (~354ms later), same culprit
  `WHGame.DLL rva=38115914`, identical 14-frame stack. Between the clean FClose and
  the fatal there is NO kcdx file op on the fault thread (only BugSplat collection
  on tid=15744 at 41.787 — crash already in progress). Graphics-init's file I/O
  through kcdx COMPLETED CORRECTLY; the fatal is downstream engine work that ran no
  kcdx slot. (PROBE K)
- All 28 kcdx slots are now instrumented (open 1/35/36, read 38..66, meta
  13/45/67-70/92/93, enum 14). In the crash window graphics-init dispatched only
  3 opens (system.cfg, pak.cfg, + AdjustFileName resolutions, all `how=miss-original`)
  and the read lifecycle on each — ZERO metadata index hits, ZERO enum. Every kcdx
  dispatch returned the correct value through the correct path. (PROBE K)
- CONVERGENCE (post-K): every "kcdx serves or operates a WRONG VALUE" axis is now
  dead by direct evidence — index answer (PROBE I: zero hits), file resolve/open
  (PROBE F: all correct), served bytes (review-logs digest: zero kcdx-served),
  pointer-identity/memory (PROBE J: copy-of-original boots), handle-id/read-family
  (PROBE K: tag=1, clean lifecycle). The `0xC8` reproduces in a window where every
  kcdx slot graphics-init touches behaves correctly. The mechanism is NOT a wrong
  return value from any slot. (PROBE K)
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
- `FAULTED_INVENTORY` at fault time folds exactly 6 modifications: 2 plugin_hook
  (`engine.lua_pcall`, `engine.lua_newstate`), 3 engine (`lua_pcall`, `update`,
  `engine.lua_newstate`), 1 probe (`bugsplat_ctor`). The fs-takeover vtable swap
  is NOT in the inventory — the inventory tracks MinHook detours, not the
  vtable-pointer swap (a coverage note, not a contradiction; the swap IS live,
  logged separately). The faulting thread (`tid=32952`) is the same thread the
  seating hook + swap ran on. (PROBE F)
- Every file op in the crash window resolves CORRECTLY. `FS_BOOT_TRACE`: 23 ops,
  21 `how=miss-original` + 2 `how=original`, ZERO index hits. Every vpath is an
  engine config/log/save path (`kcd.log`, `./system.cfg`, `data/pak.cfg`,
  `engine/config/engine_core.thread_config`, `%engine%/config/...`, the
  saved-games dir) — none are assets, so they correctly miss the asset index and
  correctly thunk to the original engine body. kcdx serves NO wrong file answer
  in the entire boot window. (PROBE F)
- The asset index is BUILT and COMPLETE before the crash: `asset_index_built
  entries=305176 paks=41 pak_entries=305173 loose=4` at 21:08:10.544, and
  `seat_index_stored entries=305176` immediately after — ~850ms before the
  `0xC8` fatal at 21:08:11.392. The overlay-ready gate signaled
  (`overlay_map_built_signaled entries=4`), the seat acquired it, built the index
  over `<game-root>/Data`, and stored it. The index path is fully functional.
  (PROBE F)
- The `engine_core.thread_config` errno=2 (the prior-run "anomaly immediately
  preceding the fatal") is `how=miss-original result=0` — i.e. the ORIGINAL
  engine FOpen body (thunked, not kcdx's index) returns 0 for it. The file
  genuinely is not found by the engine's own resolver; the miss is correct
  behavior, not a kcdx mis-resolution. This RESOLVES the prior Open-question
  ("is `engine_core.thread_config` causal or incidental?") → incidental: kcdx
  reproduces the engine's own not-found, it does not introduce one. (PROBE F)
- The two BugSplat `GetFileStat how=original` ops (`BugSplatAttachments/...`) fire
  on a DIFFERENT thread (`tid=42808`) AT crash time (21:08:11.312, ~80ms before
  the GUARD fault line) — these are BugSplat already collecting attachments, i.e.
  the crash is already in progress, not a kcdx op feeding the fault. (PROBE F)
- Graphics-init reads NO file metadata through a kcdx index-answering slot in the
  crash window. Across the whole boot window the 8 metadata slots logged ZERO
  index hits (0 index-loose / index-pak / index-either / index; the only 2 metadata
  ops were BugSplat-thread `GetFileStat how=original` at crash time). The asset
  index has 305K entries, but graphics-init never queries existence/size of an
  asset BY NAME through a kcdx slot during the fatal window — it operates on
  already-resolved state. So kcdx's metadata ANSWERS cannot be the mechanism: there
  is no index-answered query for them to be wrong on. (PROBE I)
- CONVERGENCE: every "kcdx serves a WRONG ANSWER" axis is now eliminated by direct
  evidence — file open/resolve is correct (PROBE F: all ops resolve, index built),
  the resolution + metadata MISS thunks are P4-PASS safe (Gate A + `p2-p4` capture),
  and graphics-init reads no kcdx index-answered metadata at all (PROBE I). The
  crash reproduces in a window where kcdx serves NOTHING graphics-init consumes
  through the index. The remaining suspect is the STATE the vtable swap leaves the
  `CCryPak` object / engine in — a side-effect of the swap itself that the NGX/FSR2
  path trips on without ever calling a kcdx file slot with an asset query. (PROBE I)
- ISOLATED: the vtable swap-WRITE is the `0xC8` trigger. Bypassing ONLY the
  `memcpy` that writes kcdx's vtable pointer into `[pCryPak+0x00]` (everything else
  identical — seating hook, vtable build, captures, index) makes the game boot
  CLEAN to the menu. Re-enabling it reproduces the `0xC8`. The swap-write is
  necessary and (within the fs-takeover) sufficient for the crash. (PROBE G)
- The crash mechanism is therefore something NGX/FSR2 graphics-init reads or
  validates THROUGH the swapped vtable pointer (or via the swap disturbing
  something the engine cached/expects about the object), NOT a kcdx slot serving a
  wrong value. UNVERIFIED which: (a) graphics-init dispatches a CCryPak slot whose
  KCDX impl behaves wrong for it (but PROBE I showed no index-answered metadata; a
  NON-metadata slot — an open/read/mount during graphics-init — remains possible),
  (b) the engine cached the ORIGINAL vtable pointer somewhere and the swap creates
  a mismatch the NGX path validates, or (c) the kcdx vtable's MEMORY (g_kcdxVtable,
  a kcdx-owned array) fails an integrity/identity check the engine does on its pak
  object. Mechanism is the open question; trigger is isolated. (PROBE G)
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
