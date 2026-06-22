# KI-0028 — Boot Black-Screen Investigation: Full Rundown

**Status:** OPEN. Root cause not yet found. Bug pinned to the narrowest layer reached so far.
**Date of this session's work:** 2026-06-22.
**Discipline note:** Every number below is quoted verbatim from a probe log or a crash dump on disk. Where a claim is an interpretation rather than a measurement, it is labelled. Where a prior conclusion was overturned, that is stated plainly — several were.

---

## 1. The symptom

kcdx (our SKSE-class extender for Kingdom Come: Deliverance 2) installs a **full filesystem takeover**: it swaps the engine's `CCryPak` filesystem object for kcdx's own, so kcdx serves every file the engine reads.

With the takeover active ("**swap-ON**"), the game boots to a **black screen** — no menu, **but audio plays and the game keeps running**. With the takeover suppressed via a `kcdx-noswap` marker ("**swap-OFF**", kcdx's other init runs but the filesystem swap does not), the game **reaches the main menu normally**.

So: the filesystem swap is the differentiator. Something kcdx changes about file serving breaks rendering — but only the *visible output*, not the run itself.

**The A/B method used throughout.** Every probe is armed *before* the swap decision, so it runs identically swap-ON and swap-OFF. We compare the two. This is the only honest way to isolate "what the swap changes" — because kcdx's own file-trace is blind on swap-OFF (the engine uses its own filesystem there), a direct file-level diff is impossible by construction. Render-side and engine-side hooks fire in both modes, so they give the A/B.

---

## 2. What we knew going in (from prior sessions, re-confirmed this session)

Two earlier probes had already ruled out the obvious explanations. Both were re-confirmed from their logs this session.

| Prior finding | Source (verbatim) | What it rules out |
|---|---|---|
| **The game is NOT hung.** It ticks ~35×/sec the whole time the screen is black. | PROBE W (prior) — heartbeat advanced continuously, no stall. | Deadlock / freeze. |
| **Present SUCCEEDS on the black path.** The swapchain flips at the display rate with GPU scanout. | PROBE K, `kcdx-dev_2026-06-22_15-53-22.log`: `present_delta d_present=120 d_refresh=120 … present_count=9681 … hr_present=0`. | "The frame never reaches the screen." It does — the frame is **presented but black**. |

So before this session, the bug was already pinned to: **the game runs, builds frames, and presents them — but the presented frames are black.** The question was *why the frame content is empty*.

The leading theory entering this session, from a fresh reverse-engineering (RE) pass, was that **the shader system fails** under the swap — the engine can't load/build its shaders, so it has nothing to draw with.

---

## 3. The reverse-engineering pass that set the agenda

We disassembled WHGame.dll (fresh Ghidra, 3 parallel read-only fronts, every cited fact read in the function body). This mapped the engine's shader pipeline and produced a **specific, falsifiable hypothesis**:

> The engine reads its compiled shader cache, but under the swap a **cache-validation check rejects the cache** and disables it, so the engine never builds its render pipelines.

The RE identified the exact two functions that gate this: `FUN_180b04984` (the `lookupdata.bin` cache loader — its return value is the accept/reject verdict) and `FUN_180b04478` (the validate driver that logs *"Disabling read-only shader cache!"* on a reject).

**This was a hypothesis, not a finding.** The RE correctly mapped the architecture; whether the validation actually *fails* under the swap is a runtime fact only a live probe could settle. We built that probe. The next six sections are what it found — and every one of them **killed** a theory.

---

## 4. The probe chain — what we thought, and how each was disproven

Each probe below was armed before the swap decision and read live. The decisive numbers are quoted from the log.

### PROBE R — "The shader cache is rejected under the swap." → **FALSIFIED**

**What we thought:** The cache-validation loader rejects the cache swap-ON (returns 0 → cache disabled → no pipelines → black).

**What the probe did:** After-hooked the loader (`FUN_180b04984`) and the validate driver (`FUN_180b04478`); logged the loader's return value live.

**Result, swap-ON (black screen), `kcdx-dev_2026-06-22_15-53-22.log`:**
```
loader_calls=2  loader_reject=0  loader_accept=2  driver_calls=1
```
Both cache copies (`%USER%` and `%ENGINE%`) returned **1 = ACCEPTED**.

**Verdict:** The cache is **not** rejected under the swap. The strongest lead the RE produced is **dead** — by direct measurement. The header read the RE worried about returns valid; the cache is not disabled. The wedge is *downstream* of cache validation.

---

### PROBE R2 — "The shader-list precache submit never runs." → **FALSIFIED (as the cause)**

**What we thought:** Validation passes, but the next stage — load the shader list, submit it for pipeline build — doesn't run swap-ON.

**What the probe did:** Extended the tracer to the precache-submit chain: `mfLoadShaderList`, the compile-orchestrator (`FUN_18250dd8c`), and `_PrecacheShaderList` (`FUN_1825091e0`).

**Result, swap-ON (black), `kcdx-dev_2026-06-22_16-00-16.log`:**
```
loadlist_calls=1   orch_calls=0   precache_calls=0
```
`mfLoadShaderList` ran once; the orchestrator and `_PrecacheShaderList` **never ran**.

**Cross-check (live debugger, invasive cdb on the still-black process, PID 7164):** 192 threads, **none** in shader code. `mfLoadShaderList` entered, returned, and the sequence simply stopped — it is **not** stuck inside a function. The main thread is ticking in the message pump; the job workers are idle.

**This looked like the answer** — a "missing trigger." But the rule is: a swap-ON observation is only conclusive against a swap-OFF baseline. We ran it.

---

### SWAP-OFF BASELINE — disproves PROBE R2's apparent answer

**Result, swap-OFF (reached the MENU), `kcdx-dev_2026-06-22_16-11-48.log`** (swap suppression confirmed in-log):
```
loadlist_calls=1   orch_calls=0   precache_calls=0
```
**Byte-identical to the black swap-ON run.** The orchestrator and `_PrecacheShaderList` **never run on the working menu either** — yet the menu renders fine.

**Verdict:** This entire `_PrecacheShaderList` chain is **NOT how the menu renders**. It is the *offline* shader-compile path, idle at runtime both ways. The "missing trigger" PROBE R2 found was a **red herring** — it was equally absent on the path that works. **A swap-ON-only observation, treated as the cause, would have been wrong.** The baseline caught it.

---

### PROBE R3 — "The *runtime* PSO-precache is the missing trigger." → **FALSIFIED**

**What we thought:** OK, not the offline path — but the *runtime* PSO precache (`PipelineStateCacheManager`, the function that logs *"Precached %u Graphics PSOs"*) is the real menu build path, and *that* fails swap-ON.

**What the probe did:** Hooked the runtime PSO-precache (`FUN_180bb2ad8` Graphics + `FUN_180bb23c0` Compute).

**Result, swap-ON (black), `kcdx-dev_2026-06-22_16-15-50.log`:** `pso_gfx_calls=0 pso_comp_calls=0`.
**Result, swap-OFF (menu), `kcdx-dev_2026-06-22_16-20-51.log`:** `pso_gfx_calls=0 pso_comp_calls=0`.

**Identical — zero on BOTH paths.** The runtime precache runs on neither, yet the menu renders.

**Cross-check (live cdb on the black process, PID 47024):** the engine spawns dedicated `PSOCompilationWorker_0/_1` threads — **both parked at `NtWaitForSingleObject`**, idle, waiting for work that never arrives. The PSO-compile infrastructure is alive but unused.

**Verdict:** The runtime PSO-precache is **not** the menu's path either. Precache (offline and runtime) is exonerated.

---

### PROBE R4 — "Then the menu builds PSOs lazily, per-draw." → **FALSIFIED**

**What we thought:** Precache is off (a config default); so the menu must build pipelines *lazily*, on demand, through the per-PSO-create leaf (`FUN_180bb42c8`). *That* lazy path fails swap-ON.

**What the probe did:** Hooked the lazy per-PSO-create leaf.

**Result, swap-OFF (the WORKING menu), `kcdx-dev_2026-06-22_16-23-56.log`:** `pso_leaf_calls=0`.

**Zero — even on the working menu.** The lazy-create leaf **never fires when the menu renders.**

**Verdict:** The entire shader/PSO-build subsystem we'd been chasing is idle on the path that works. Whatever builds the menu's pipelines, it is **not** any function in this subsystem.

---

### PROBE P swap-OFF — the measurement that overturned the founding premise

This is the most important result in the session, and the most humbling.

**Background:** PROBE P (an earlier probe) hooks the *actual D3D12 device* PSO-creation call (`ID3D12Device::CreateGraphicsPipelineState`). Run swap-ON earlier, it had reported **`gfx_calls=1`** — exactly one pipeline created — and we had interpreted that as *"the engine never builds its scene/UI pipelines"* (catalogued as outcome "O5"). That interpretation drove the entire shader-cache investigation above.

**The problem:** PROBE P had **only ever been run swap-ON.** Its outcome map assumed *"a CryEngine menu creates dozens-to-hundreds of PSOs."* Nobody had checked that assumption against the working menu.

**This session we read PROBE P on the swap-OFF / working-menu run** (same run as PROBE R4, `kcdx-dev_2026-06-22_16-23-56.log`):
```
gfx_calls=1  gfx_failed=0  gfx_null_pso=0  blob_bad_magic=0
```
**`gfx_calls=1` on the WORKING menu too.** Identical to the black screen.

**Verdict — the premise was false.** This engine's menu creates **exactly one** graphics pipeline through that call, on both paths. `gfx_calls=1` is **normal**, not a symptom. Our "O5: the engine never builds its pipelines" was a **misread** — it never builds them swap-OFF either, and the menu renders fine regardless. (The menu's pipelines load from an on-disk cache through a different API the probe doesn't hook.)

**Consequence:** the entire shader/PSO axis — six probes' worth — is **exonerated as the differentiator.** Cache validation (R), offline precache (R2), runtime precache (R3), lazy create (R4), device PSO-create (P): **every one runs identically swap-ON vs swap-OFF.** The bug was never in shader/PSO building. We had been chasing a layer that behaves the same on both paths.

This is a real, if negative, result: it closes a whole subsystem definitively, and it was found by finally measuring the control case nobody had measured.

---

### PROBE S — the fresh axis: are the draws recorded into the frame?

With PSO-build exonerated and present already proven to succeed, the only remaining place for the divergence is **what gets recorded into the frame between them** — the actual draw calls. PROBE S hooks the D3D12 command list: `DrawInstanced`, `DrawIndexedInstanced`, `OMSetRenderTargets` (which render target is bound).

**One honest setback, fully owned.** The first PROBE S build **crashed the game.** The crash dump (`kcdx_2026-06-22_16-35-19.dmp`, read with cdb) pinned it exactly:
```
ExceptionCode: c0000005 (Access violation)
kcdx!…HookedOMSetRT+0x11
FAULTING_SOURCE_FILE: …\src\fs_takeover\drawcall_probe.cpp
```
**This was my bug, not the engine's** — I had `OMSetRenderTargets` at vtable slot 47; it is slot 46 (slot 47 is a different method whose argument I dereferenced as a pointer). Re-verified against the D3D12 SDK header, corrected the slot, rebuilt. The crash dump made this a one-shot fix — no guessing, no wasted launches. (This is exactly why we read the dump first.)

**The corrected probe ran clean. The A/B (both numbers verbatim from log):**

| metric | swap-OFF (MENU) `…16-37-33.log` | swap-ON (BLACK) `…16-39-40.log` |
|---|---|---|
| `draw_instanced` | **1383** | **9500** |
| `draw_indexed` | **96** | **0** |
| `om_set_rt` | 346 | 2376 |
| `om_null_rt` | 0 | **0** |

**What this proves — and what it kills:**
- The black path is **NOT "no draws."** It records **more** draws than the working menu (9500 vs 1383). The render loop is busy.
- The black path is **NOT "null render target."** `om_null_rt=0` on both — every bind is to a valid target.

Both simple explanations are falsified by direct measurement. **The draws execute, to valid targets, yet the frame is black.**

**The one sharp, sourced lead:** `draw_indexed=0` swap-ON vs **96** swap-OFF. The working menu draws **indexed geometry** (96 indexed draws — meshes, the real visible content). The black path does **zero indexed draws** — but 9500 non-indexed ones. The actual content-geometry path is **absent** under the swap; what runs instead is thousands of non-indexed passes that produce no visible image.

---

## 5. Where the bug is now pinned (sourced, not inferred)

By a chain of seven measurements, each disproving a theory:

**Exonerated — proven identical or absent on BOTH paths, by measurement:**
- Hang / deadlock (PROBE W: ticks continuously).
- Present failure (PROBE K: `present_count` in the thousands, `hr_present=0`).
- Shader-cache validation (PROBE R: `loader_accept=2` swap-ON).
- Offline shader precache (PROBE R2 + baseline: identical, both paths).
- Runtime PSO precache (PROBE R3: `pso_gfx_calls=0` both paths).
- Lazy PSO create (PROBE R4: `pso_leaf_calls=0` on the working menu).
- D3D12 device PSO-create count (PROBE P: `gfx_calls=1` both paths).
- Draw submission / null render target (PROBE S: 9500 draws, `om_null_rt=0` swap-ON).

**The surviving, measured fact:** under the swap, the engine issues **9500 draws to valid render targets but ZERO indexed-geometry draws** (vs 96 on the working menu), and the frame composites black.

**What that means (labelled as interpretation, the only one in this document):** the visible content the menu draws as indexed geometry is **not being drawn** under the swap; the divergence is in *what the draws produce or where the content geometry goes* — a render-resource / render-target-routing question, not a "pipelines aren't built" question. This is the narrowest the bug has ever been localized.

---

## 6. Honest assessment for the record

- **We chased the wrong subsystem for six probes.** The shader/PSO axis was the RE's strongest lead and it was wrong — not because the RE was sloppy (it read every fact in the binary), but because the *runtime behavior* (`gfx_calls=1` is normal) could only be known by measuring the working control case, which the prior session never did. The cost was real; the correction was decisive and is now locked in by measurement.
- **Every kill was a measurement, not an argument.** No theory was abandoned on a hunch; each died to a logged number, and the swap-OFF baseline twice caught a swap-ON-only result that would have been a false conclusion (R2 most clearly).
- **One self-inflicted crash**, caused by my vtable-slot error in a probe; diagnosed from the dump in one pass and fixed. It cost one launch and did not touch the product code path.
- **The bug is not yet solved.** It is pinned to "draws execute to valid targets but produce a black frame, with the indexed-content path absent swap-ON." The next step is a heavier instrument: capture, per draw, *which* render target the content is routed to and *what* it samples — a render-graph/resource investigation, distinct from everything tried so far.

---

## 7. Source index (every claim above traces to one of these)

All under `kcdx-engine/logs/` on the live install, plus the recon dir `_research/ki0028-cshaderman-pso-consumer-recon/`.

| Probe / event | Log file | Decisive line |
|---|---|---|
| PROBE K (present) | `kcdx-dev_2026-06-22_15-53-22.log` | `present_delta d_present=120 … present_count=9681 hr_present=0` |
| PROBE R (validation) | `kcdx-dev_2026-06-22_15-53-22.log` | `loader_accept=2 loader_reject=0` |
| PROBE R2 (precache submit, swap-ON) | `kcdx-dev_2026-06-22_16-00-16.log` | `orch_calls=0 precache_calls=0` |
| Swap-OFF baseline | `kcdx-dev_2026-06-22_16-11-48.log` | same tallies, menu reached |
| PROBE R3 (runtime precache, swap-ON) | `kcdx-dev_2026-06-22_16-15-50.log` | `pso_gfx_calls=0` |
| PROBE R3 swap-OFF | `kcdx-dev_2026-06-22_16-20-51.log` | `pso_gfx_calls=0` |
| PROBE R4 + PROBE P swap-OFF | `kcdx-dev_2026-06-22_16-23-56.log` | `pso_leaf_calls=0` ; `gfx_calls=1` |
| PROBE S crash | `kcdx_2026-06-22_16-35-19.dmp` | AV `HookedOMSetRT+0x11` (probe bug) |
| PROBE S swap-OFF baseline | `kcdx-dev_2026-06-22_16-37-33.log` | `draw_instanced=1383 draw_indexed=96 om_null_rt=0` |
| PROBE S swap-ON (black) | `kcdx-dev_2026-06-22_16-39-40.log` | `draw_instanced=9500 draw_indexed=0 om_null_rt=0` |
| Full chronological record | `_research/ki0028-cshaderman-pso-consumer-recon/FINDINGS.md` | — |
