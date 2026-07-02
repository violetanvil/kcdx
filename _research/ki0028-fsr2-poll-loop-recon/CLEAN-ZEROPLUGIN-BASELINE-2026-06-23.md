# KI-0028 — CLEAN zero-plugin baseline + the INDEXED-ONLY reframe (2026-06-23)

Run: `kcdx-dev_2026-06-23_09-36-55.log`. Live PID 38868 (invasive cdb `-p`, capture, `qd` — game left running).
Config: FS swap kFamAll (mask=15, confirmed live), **ZERO test plugins** (test-suite + the 2 top-level cap-38 dirs parked to `_test-suite-BAK` / `_toplevel-BAK`). Dev mode on. No markers.

## DECISIVE: the FS swap ALONE black-screens — test plugins EXONERATED

Black screen + sound, zero plugins, NO crash (the prior `0x01030002` crash in `C_Game::CreateInstance` was the 123-plugin run; it does NOT recur with zero plugins). So:
- The 123 test plugins (incl. rejected cap-49 manifest + crash-trigger probes) are NOT the KI-0028 cause — they caused the EARLIER crash variant, not the black screen.
- The black-screen wedge is the pure FS swap. Clean one-variable baseline finally established.

## THE REFRAME — it is NOT "no draws recorded"; it is "no INDEXED draws"

The DRAW_PROBE was built on the premise "draws HIGH swap-OFF + ~ZERO swap-ON". The clean data **overturns that premise**:

```
DRAW_PROBE summary (swap-ON, zero plugins, climbing over the run):
  create_cmdlist=19
  draw_instanced=27212      <-- HIGH and climbing (NOT zero)
  draw_indexed=0            <-- the ONLY zero
  ia_set_vb=27212           <-- vertex buffers ARE bound
  ia_set_topo=27212         <-- topology IS set
  ia_set_ib=0               <-- index buffers NEVER bound
  om_set_rt=6731  om_null_rt=0   <-- render targets ARE set, none null
PRESENT_PROBE: present_count=9627  d_present=144/s  hr_present=0   <-- presenting fine at 144fps
```

**The render pipeline is ALIVE.** The engine records 27k+ instanced draws per snapshot, binds VBs + topology + RTs, and presents at 144fps with HRESULT=0 — yet the screen is black. The ONLY thing missing is **indexed geometry**: `DrawIndexedInstanced=0` and `IASetIndexBuffer=0`.

So the wedge is NOT "the world never builds" and NOT "the render loop records nothing". It is specifically: **the engine skips every INDEXED draw (static mesh / world geometry) while still issuing instanced draws.** Indexed geometry is the bulk of visible scene meshes; instanced-without-index draws are typically particles/sprites/fullscreen/dynamic passes — which render to black with no world behind them.

## What this kills / keeps

- KILLS: "no draws recorded" / "render loop dead" / "present fails" — all false (27k instanced draws, 144fps present, hr=0).
- KILLS: the 123-plugin crash being KI-0028 — it's a separate plugin-induced variant.
- KEEPS + SHARPENS the wedge: what makes the engine abandon the INDEXED-geometry path specifically, swap-ON, when every FS OUTPUT (bytes/handle/object/enum) is measured-correct? The index-buffer creation/binding path reads something from the FS that, swap-ON, yields a state that makes the engine skip indexed submission — without erroring (no FAULTED, hr=0).

## FOLLOW-UP READ (reuse-first, same log) — the engine reads ZERO mesh files, and that correlates with BLACK not CRASH

FS_BOOT_TRACE in the clean black-screen run = **100,097 lines** over 10 min steady-state (09:37→09:47). Extension histogram: `.dds`=65,238, `.xml`=18,448, compiled-shaders(`.cfib`/`.cfxb`/`.cfi`/`.cfx`)≈5,800, `.ent`/`.lua`/`.adb`/`.gfx`/… — and **ZERO `.cgf`/`.cga`/`.skin`/`.chr` mesh files** (0 refs anywhere in the log, all categories). Trace slot coverage is full (FReadRaw_byPakIndex=36496, FOpen=14035, FSeek/FClose/FTell/AdjustFileName/FGetSize/FindFirst/… — every family the FS serves).

**The cross-run comparison overturns the simple read:**
- Clean BLACK-screen run (zero plugins): **0** `.cgf`/`.cga` refs.
- The CRASHED run (`09-06-14`, 123 plugins): **71,739** `.cgf`/`.cga` refs — it WAS deep in mesh loading when it crashed in `C_Game::CreateInstance` (the `0x01030002` object).
- All other recent black-screen runs: 0 `.cgf` refs.

So mesh-reads correlate with the CRASH variant, and their ABSENCE with the BLACK-screen variant. The difference between crash and black is **how far world-load progressed**: the crashed run reached the mesh-load phase (71k mesh reads, then died mid-load on a corrupted object); the black-screen run **never reaches mesh-load at all** — it stalls BEFORE static-geometry loading, issuing only non-mesh instanced passes (textures/particles/fullscreen) → `draw_indexed=0` → black.

## Corrected wedge (sharper)

NOT "kcdx mis-serves .cgf content" (the engine never requests a .cgf in the black run). The wedge is: **swap-ON, world-load STALLS before the static-mesh-load phase begins** — it loads 65k textures + 18k XML + shaders heavily but never advances to requesting geometry. Something gates the transition from asset-streaming into mesh/scene-geometry load. The 144fps-present + 27k-instanced-draws means the render loop runs on whatever non-mesh content exists, foreverwaiting on a world that never starts loading its meshes.

## CRUX (reuse-first trace-diff, both logs on disk) — the LEVEL-LOAD never fires swap-ON

Diffed distinct opened vpaths, crash-run (45,037) vs black-run (15,442). The 29,596 crash-only files ARE the world-geometry set: `.cgf`=14,257, `.mtl`=352, `.cgfm`=269, `.skin`=52, `.chr`=52 — none ever opened in the black run.

Traced the crash run's FIRST `.cgf` (line 127284): it is triggered by reading
`terrain.pak → levels/kutnohorsko/terrain/merged_meshes_sectors/mmrm_used_meshes.lst` (the terrain merged-mesh manifest), which then cascades into `Engine.pak → %engine%/engineassets/objects/default.cgf` and the full mesh load.

Level-chain presence, black vs crash:
```
mmrm_used_meshes      black=0       crash=7
merged_meshes_sectors black=0       crash=7
levels/kutnohorsko    black=6       crash=118,847   <-- the whole difference
leveldata.xml         black=2       crash=17
mission_              black=0       crash=6
*.lst                 black=0       crash=7
```
Black-run high-water mark (where it stalls) = **UI/menu assets**: `cursor_green.dds`, `libs/ui/textures/dynamic/pros_qr_frame.dds`, `autoexec.cfg`, `config/config.dat`.

**The corrected root-region:** swap-ON, the engine loads base assets + UI and **STALLS at the menu / level-entry boundary — it never BEGINS loading the level (`kutnohorsko`).** No level-load → no terrain mesh-list → no `.cgf` → `ia_set_ib=0` / `draw_indexed=0` → black. The 144fps + 27k instanced draws are the **menu/UI render loop** running over a level that never loads. The crash run (123 plugins) DID enter the level, then crashed mid-mesh-load on the `0x01030002` corrupted object — likely the same swap perturbation manifesting one phase deeper.

This moves KI-0028 from "render/geometry/present" (all alive) to: **the new-game/level-load transition is never triggered swap-ON.** An init-sequence/state gate, exactly the original "kcdx-perturbed init-state" hypothesis, now precisely located at the menu→level-load handoff.

## Next probe (owed) — narrowed to the level-load trigger

1. What FIRES the level load on a working (swap-OFF) boot — the new-game / level-stream / `OnEditorGameRequest`-equiv call chain that opens `leveldata.xml` then `mmrm_used_meshes.lst`? Find it swap-OFF, then check whether swap-ON the engine reaches that call at all (a present-but-never-called level-load trigger) or calls it and it early-returns (a level-load that runs but finds nothing — a served `leveldata.xml`/level-manifest that parses to an empty level swap-ON).
2. Compare the 2 `leveldata.xml` reads in the black run vs the 17 in the crash run — does the black run READ leveldata.xml and get correct bytes, then NOT proceed? (If leveldata.xml serves correctly but the level still doesn't load → the gate is AFTER the manifest read, in the engine's level-init state, not in FS content.)
3. Is the stall waiting on a menu→game user action that swap-OFF auto-advances? (Does vanilla auto-load a level to reach the menu, or does the menu itself need a level? KCD2's main menu renders over a 3D scene — so the menu-backdrop level may be what fails to load.)

## FALSE LEAD KILLED — level-existence is NOT the gate (do not re-chase)

The black run's level enum looked like a serve defect at first:
```
enum FindFirst "levels/*.*" matched=3 (klaster, kutnohorsko, trosecko)
  levels/kutnohorsko/level.pak       IsFileExist3 how=original result=1  (exists)
  levels/kutnohorsko/kutnohorsko.xml IsFileExist3 how=original result=0  (missing)
```
But the CRASH run (which DID load the level) gets the **identical** results — `level.pak`=1, `kutnohorsko.xml`=0. And ground truth: there is NO loose `kutnohorsko.xml` under `Data\levels\kutnohorsko\` (only `level.pak`), so `result=0` for the loose `.xml` is CORRECT — the engine falls back to `level.pak` (found, result=1) in both runs. **kcdx serves level-existence correctly; it is NOT the black-vs-load differentiator.** Both runs agree through level-existence, then diverge at the actual level-LOAD (which the black run never starts).

## Where the static evidence ends

Both runs agree up through "level.pak found". The divergence — what the engine does between finding `level.pak` and reading `mmrm_used_meshes.lst`, and why the black run never advances — is **engine level-init CONTROL FLOW**, which FS_BOOT_TRACE (file-ops only) cannot show. The static logs are fully mined. Next requires either a live deep-stack read of the stalled main thread, or a new probe on the level-load entry point. (Live read attempted this session but the game closed before capture; owed on next launch.)

## Next probe (owed) — the level-load entry point

The narrowed, FS-exonerated question: **after `level.pak` is found, what engine call advances into level load (reading `mmrm_used_meshes.lst` → meshes), and why does swap-ON never reach/complete it?** Approaches:
1. Live: relaunch to the black screen, invasive `cdb -p <pid> ~0 k 40` (main thread, qd) — what is the stalled main thread waiting on 10 min in? (CryEngine main = thread 0.)
2. Probe the level-load entry: find (swap-OFF/RE) the CryEngine level-system call that opens `mmrm_used_meshes.lst` (a `C3DEngine::LoadLevel` / `CTerrain` / merged-mesh-manager entry). After-hook it; does swap-ON reach it? Reached-but-early-returns vs never-reached partitions "level-load runs but finds nothing" from "level-load never triggered".
3. Menu-backdrop angle: KCD2's main menu renders over a 3D scene. Is the menu itself supposed to load a backdrop level that fails swap-ON? Check a swap-OFF run: does reaching the MENU (not new-game) already read `mmrm_used_meshes.lst`? If yes, the menu-backdrop level is the failing load and the black menu IS the symptom.

## Parked plugins
`kcdx-plugins/test-suite/` → `_test-suite-BAK` (123) and the 2 top-level cap-38 dirs → `_toplevel-BAK`. RESTORE after KI-0028 work (the FS swap reproduces black with zero plugins, so they can be restored anytime).
