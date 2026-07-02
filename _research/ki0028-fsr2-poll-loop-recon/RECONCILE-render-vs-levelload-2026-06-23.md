# KI-0028 — Reconciliation: the render-routing frontier was one layer too high

**Date:** 2026-06-23. **Status:** OPEN. **Effect:** pivots the investigation off the render-graph axis onto the level-load axis.

This record reconciles two KI-0028 documents written 24 hours apart that reached the SAME observation (`draw_indexed=0` swap-ON) at DIFFERENT depths and were never connected. It states which reading is correct, why, and the pivot decision (user-approved 2026-06-23). Every number below is quoted verbatim from a probe log or a document already on disk — no new measurement was taken to write this; it is a cross-document reconciliation of evidence that already existed.

---

## The two documents and where they stopped

| | `ki0028-cshaderman-pso-consumer-recon/KI-0028-MANAGER-RUNDOWN.md` (06-22) | `ki0028-fsr2-poll-loop-recon/CLEAN-ZEROPLUGIN-BASELINE-2026-06-23.md` (06-23) |
|---|---|---|
| **Layer worked at** | D3D12 draw-call layer (command-list hooks) | FS-trace layer (kcdx's own file-op log, swap-ON) |
| **Reads `draw_indexed=0` as** | the FRONTIER — "content geometry is not routed to the frame" (a render-resource / render-target-routing question) | a SYMPTOM — "no level loaded → no meshes → nothing indexed to draw" |
| **Proposed next step** | a heavier render-graph instrument: capture per-draw which RT the content routes to | find the level-load trigger; check if it fires swap-ON at all |
| **Evidence for its read** | draw counts: `draw_instanced=9500 draw_indexed=0 om_null_rt=0` swap-ON vs `1383 / 96 / 0` swap-OFF | FS trace-diff: `mmrm_used_meshes=0`, `.cgf`=0, `levels/kutnohorsko` black=6 vs crash=118,847 |

Both are honest and internally rigorous. The rundown simply did not have the FS-trace data — it worked purely at the draw-call layer, which can see "no indexed draws happened" but is structurally blind to WHY, because the "why" is upstream in file/level-load control flow.

---

## The reconciliation — the baseline reading is correct; the rundown's is superseded

**`draw_indexed=0` has ONE cause, and the FS trace already identifies it: the level's geometry was never loaded.**

The rundown's §5 interpretation — "the divergence is in what the draws produce or where the content geometry goes — a render-resource / render-target-routing question" — is **falsified by the baseline's own data**, quoted verbatim from `CLEAN-ZEROPLUGIN-BASELINE-2026-06-23.md`:

```
mmrm_used_meshes      black=0       crash=7
merged_meshes_sectors black=0       crash=7
levels/kutnohorsko    black=6       crash=118,847   <-- the whole difference
leveldata.xml         black=2       crash=17
```

Swap-ON (black), the engine reads base assets + UI (high-water mark = `cursor_green.dds`, `libs/ui/textures/dynamic/pros_qr_frame.dds`, `autoexec.cfg`, `config/config.dat` — all menu/UI), then **STALLS before requesting any level geometry.** Zero `.cgf` model reads in 100,097 FS_BOOT_TRACE lines. The level-mesh manifest (`mmrm_used_meshes.lst`) that triggers the first `.cgf` cascade is never read.

So the frame is black **because there is no loaded world geometry to draw** — not because loaded geometry is being misrouted. The 9500 non-indexed draws swap-ON are the menu/UI compositor running over a level that never loaded. There is nothing for the "heavier render-graph instrument" to find: it would instrument a render graph that is entirely downstream of the actual break.

**Why the rundown's read was the least-supported claim in that document:** it is a D3D12-layer INFERENCE about *why* indexed draws are absent, made without the FS data that answers it directly. The rundown even flags it as "the only interpretation in this document." The baseline is the ground-truth OBSERVATION that governs it (`.claude/rules/results-driven.md` §"theory-INDEPENDENT" — a confirm-only interpretation yields to the direct measurement).

---

## What still stands from the rundown (do NOT re-open these)

The rundown's §1–§4 are DIRECT MEASUREMENTS, not the overturned interpretation. All remain valid and CLOSE their subsystems:

- **Not a hang** — PROBE W: ticks ~35×/s continuously.
- **Present succeeds** — PROBE K: `present_count=9681 hr_present=0`, swapchain flips at refresh rate. The frame is presented-but-black.
- **Shader/PSO axis fully exonerated** — cache validation (PROBE R: `loader_accept=2` swap-ON), offline precache (R2 + baseline: identical both paths), runtime precache (R3: `pso_gfx_calls=0` both), lazy create (R4: `pso_leaf_calls=0` on the working menu), device PSO-create (PROBE P: `gfx_calls=1` both paths — the founding "O5" premise was a misread; `gfx_calls=1` is NORMAL). Every one runs identically swap-ON vs swap-OFF.
- **Draws execute to valid targets** — PROBE S: `om_null_rt=0` both paths.

These are load-bearing and were achieved at real cost (six probes). The pivot does not discard them — it re-reads their terminal symptom (`draw_indexed=0`) at the correct layer.

---

## The false lead the baseline already killed (do NOT re-chase)

Level-EXISTENCE is NOT the gate. Both runs get identical enum results:
```
enum FindFirst "levels/*.*" matched=3 (klaster, kutnohorsko, trosecko)
  levels/kutnohorsko/level.pak       IsFileExist3 how=original result=1  (exists)
  levels/kutnohorsko/kutnohorsko.xml IsFileExist3 how=original result=0  (missing)
```
The crash run (which DID enter the level) gets the SAME `level.pak=1 / kutnohorsko.xml=0`. Ground truth: there is no loose `kutnohorsko.xml` under `Data\levels\kutnohorsko\` (only `level.pak`), so `result=0` for the loose xml is CORRECT — the engine falls back to `level.pak` in both runs. kcdx serves level-existence correctly; both runs agree THROUGH level-existence, then diverge at the actual level-LOAD (which the black run never starts).

---

## Where the static evidence ends — and why a new probe is required

Both runs agree up through "level.pak found." The divergence — what the engine does between finding `level.pak` and reading `mmrm_used_meshes.lst`, and why the black run never advances — is **engine level-init CONTROL FLOW**, which FS_BOOT_TRACE (file-ops only) cannot show. The static logs are fully mined. The next step is a new after-hook on the level-load entry.

---

## The pivot (user-approved 2026-06-23)

**Drop the render-graph / resource-routing direction. Pursue the level-load trigger — where the FS swap actually bites.**

Rationale: the FS swap changes *file serving*. A level-load that silently never starts is far more plausibly a file-serving / enumeration divergence (a level-existence probe, a manifest lookup, or an enumeration the engine gates level-load on that returns subtly wrong under kcdx) than a D3D12 render-target bug — and it is squarely in kcdx's blast radius, which render-target routing is not.

---

## The next probe — level-load-entry after-hook (armed before the swap decision, A/B)

**Reuse-first: the entry points are ALREADY RE'd and body-read** in `_research/ki0028-vanilla-init-fs-map/` (image base `0x180000000`; WHGame.dll `release_1_5_1164953_841`). No fresh Ghidra needed for the primary targets:

| Function | RVA | Role | Source (on disk) |
|---|---|---|---|
| `C_Game::CreateInstance` | — (abort-stack orchestrator) | the level-load orchestrator; the residual wedge sits inside it | `VANILLA-MAP.md` Stage 6 |
| `CResourceList::Load` | `0x4dcb60` | first level-resource read; opens `Levels/<lvl>/auto_resourcelist.txt` | `LOADER-TRACE.md` §1–§2 (body in `_bodies.txt` line 1) |
| pathbuilder | `0x4dd384` | builds `<root>/Levels/<lvl>/leveldata.xml` etc. | `LOADER-TRACE.md` §2 |
| slot-4 reader → open helper | `0x4dd5e4` → `0x4605bc` | → `CCryPak::FOpen` slot 36 `0x4614A0` | `LOADER-TRACE.md` §2 |
| `/LevelInfo.xml` getter | `0x178b86c` | alternative record-name source | `_bodies.txt` line 142 |
| current-level record reader | `0x66bbf0` | `Game->[0x88]->[0x58]` — the null the `0xD2` abort checked | `VANILLA-MAP.md` Stage 7 |

**NOT on disk (do not assume — fresh Ghidra if needed):** `mmrm_used_meshes.lst` has ZERO hits anywhere in `_research/` or `docs/` — that string and the `terrain.pak → merged_meshes_sectors` chain were surface observations from the FS log diff, never traced to a function. A named `CLevelSystem::LoadLevel` / `SetCurrentLevel` WRITER is also unpinned (`LOADER-TRACE.md` §4 flags it as the one edge still unverified — the record READER `0x66bbf0` is pinned, the WRITER is not).

**Probe design (pre-committed outcome→meaning map):**

Arm an after-hook on `CResourceList::Load @ 0x4dcb60` (the first thing level-load does that is already resolved) BEFORE the swap decision, so it fires identically swap-ON and swap-OFF. This is the A/B the whole investigation uses.

> Probe asks: *does the engine BEGIN loading the level swap-ON?*
> - **Outcome A** — `CResourceList::Load` fires swap-OFF but NOT swap-ON → an UPSTREAM gate stops level-load before it begins. The divergence is in whatever the engine checks to DECIDE to load the level — a file/enum kcdx serves differently. **← the bet.** Next: hook the caller (`C_Game::CreateInstance` region) to find the gate.
> - **Outcome B** — fires swap-ON too, then early-returns / finds nothing → level-load starts but a served manifest (`leveldata.xml`) parses empty under the swap. Next: dump the bytes `CResourceList::Load` reads swap-ON vs swap-OFF.
> - **Outcome C** — fires identically both paths and reads identical bytes → the gate is DOWNSTREAM of the resource-list read, deeper in level-init. Next: hook the next stage (record-writer / `mmrm` trigger — needs the unpinned writer, fresh Ghidra).

One variable (does this one function fire). Falsifiable (Outcome A kills "level-load runs but produces nothing"; B/C kill "level-load never starts"). Ground-truth first (log the raw fire-count + the vpath argument, not a theory about it).

**Bind-root caveat (already-fixed context):** the `83a9279` bind-root-prefix fix cleared the `0xD2` abort (kcdx was storing `leveldata.xml` bare, engine requested `Levels/<lvl>/leveldata.xml` → every level-resource MISSED). That fix is IN. The black screen persists AFTER it — so the level-load gate this probe hunts is a SEPARATE mechanism from the bind-root miss, downstream of the abort that fix cleared. Confirm the fix is still live in the swap-ON run before concluding Outcome A.

---

## Cross-references

- Superseded conclusion: `ki0028-cshaderman-pso-consumer-recon/KI-0028-MANAGER-RUNDOWN.md` §5/§6 (now carries a SUPERSEDED banner pointing here).
- Governing evidence: `ki0028-fsr2-poll-loop-recon/CLEAN-ZEROPLUGIN-BASELINE-2026-06-23.md`.
- RE'd entry points: `ki0028-vanilla-init-fs-map/LOADER-TRACE.md`, `VANILLA-MAP.md`, `_bodies.txt`.
- Full chronological handoff: `ki0028-fsr2-poll-loop-recon/KI-0028-FULL-HANDOFF.md`.
- Known-issue: `docs/known-issues/KI-0028-fs-takeover-boot-hang-ui-render-init.md` (OPEN; AP17 — no root-cause mechanism yet).
