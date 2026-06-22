# KI-0028 — CShaderMan cache-load → PSO-build path: validation ACCEPTS; the gate is DOWNSTREAM (PROBE R)

**Date:** 2026-06-22
**Method:** fresh Ghidra 12.1 disassembly (KCD2.rep, WHGame.dll `release_1_5_1164953_841`, image base 0x180000000), 3 parallel read-only fronts + synthesizer re-ground of the load-bearing edge. THEN PROBE R live (the validation-reject hypothesis, FALSIFIED).
**Trust:** PRIMARY EVIDENCE for every body cited (decompiled + the call site read).

## ⚠ PROBE R RESULT (RAN 2026-06-22, swap-ON) — the validation-reject theory is FALSIFIED

The dispatch tracer (`src/fs_takeover/dispatch_probe.{h,cpp}`) after-hooked the two validation RVAs and read the loader's return live, swap-ON (user-confirmed black screen). Ground truth (`DISPATCH_PROBE` lines, `kcdx-dev_2026-06-22_15-53-22.log`):
- `driver_enter call_n=1` — the validate driver `FUN_180b04478` was REACHED.
- `lookupdata_loader call_n=1 ret=1 rejected=0 user_flag=1` — the `%USER%` lookupdata.bin copy **VALIDATED** (return 1).
- `lookupdata_loader call_n=2 ret=1 rejected=0 user_flag=0` — the `%ENGINE%` copy **VALIDATED** (return 1).
- Final tally: `loader_calls=2 loader_reject=0 loader_accept=2 driver_calls=1`, frozen for the whole ~90s run.

**This is PROBE R's third pre-committed outcome (loader returns 1 swap-ON → cache VALIDATES → gate is PAST validation). The validation-reject mechanism — the strongest lead the RE produced — is DEAD.** The engine reads the lookupdata.bin cache index, BOTH copies pass validation under the swap, and the read-only cache is NOT disabled. So:
- The `FUN_180b04984` unchecked-bytes-read header read is NOT corrupted by the swap (it returns 1, magic+version matched).
- The 36 swap-on `data/gameshaders/*.ext` probes are NOT driven by `mfCreateCommonGlobalFlags`'s globals.txt-fail arm in a way that gates the build — OR the globals.txt arm fires but is not the wedge. (The `.ext` reprobe theory is now also suspect: validation passed, yet boot is still black.)
- The wedge is **DOWNSTREAM of cache validation**: in the precache→pipeline-build dispatch. The cache validates; something AFTER that (the shader-list build / `_PrecacheShaderList` submit / the job-deferred PSO creation) does not run swap-ON.

**Honest status (AP17):** the RE correctly mapped the architecture and the validation gate, and PROBE R correctly KILLED the validation-reject hypothesis by direct measurement (not a guess). The localization is now: validation passes (PROBE R) but `gfx_calls=1` (PROBE P) — the gate is in the **span between cache-validation-accept and PSO-build dispatch**: `mfLoadShaderList` (fills the precache list from `%USER%/shaders/shaderlist.txt`) → `_PrecacheShaderList` (`FUN_1825091e0`, submits the list) → the per-shader submit slots → `LaunchPSOCreationJobsGraphics`. The next probe targets THAT span (below), theory-independent — NOT another validation-side fix.

---

## The RE that LED to PROBE R (validation-reject, now falsified — kept for the architecture map)

The RE hunted for "the `.cfxb`-cache CONSUMER." It correctly found the architecture but its CONCLUSION (validation-reject is the gate) was the hypothesis PROBE R killed. The body-read facts below are still VERIFIED and load-bearing for the next probe; only the "validation reject IS the wedge" framing is dead.

## The reframe this RE delivers

The next-probe spec hunted for "the `.cfxb`-cache CONSUMER — the fn that takes a read cache blob and calls CreateGraphicsPipelineState." That fn exists but is the WRONG target: the engine never gets that far. The gate is UPSTREAM, at **shader-cache VALIDATION** — the engine reads the cache INDEX (`lookupdata.bin`), the validation fails under the swap, the engine **disables the read-only shader cache**, falls into the source-`.ext`-enumeration/rebuild path (the swap-on-induced 36 `data/gameshaders/*.ext` probes), and never precaches the pipeline set (`gfx_calls=1`). This is the KI-0026 read-return-contract class, one subsystem over.

## The engine's shader-cache architecture (all body-read)

D3D12 confirmed: `NCryDX12::CShader::vftable` @ 0x180b20a68 (front 1). Render-API path picks `Shaders/Cache/D3D12/` (`FUN_180b033a0` line 256, READ).

### The cache-VALIDATION gate (the KI-0028 mechanism — front 3 + synthesizer re-ground)

- **`FUN_180b033a0`** (per-render-API cache setup, READ): picks `Shaders/Cache/D3D12/`, then calls **`FUN_180b04478`** (the validate driver) at line 288.
- **`FUN_180b04478`** (cache open/validate driver, front-3 READ): builds `%ENGINE%/.../lookupdata.bin` + `staticmacrolist.bin`, calls the loader `FUN_180b04984` on the ENGINE copy; **loader returns 0 → `FUN_180a607a4(..."Disabling read-only shader cache!")` + `FUN_1824f72dc(param_1,0)`** (READ). Then validates the `%USER%` copy; 0 there → `"Deleting USER folder shader cache!"`.
- **`FUN_180b04984`** = the lookupdata.bin LOADER; its bool return IS the disable-cache gate (synthesizer body-read, `_f3` + re-read here):
  - line 97: opens lookupdata.bin via `vtable[0x120]`; `==0` → return 0.
  - **line 104: `vtable[0x130](buf, 4, 1)` reads 4-byte magic — BYTES-READ RETURN DISCARDED.**
  - **line 106: `vtable[0x130](&hdr, 0x14, 1)` reads 20-byte header — RETURN DISCARDED.**
  - line 107: `if ((magic == 0x4b435043 /*"CPCK"*/) && (version - 10U < 3 /*∈{10,11,12}*/))` — tested IMMEDIATELY on whatever landed in the stack buffers; then a `"Ver: %.1f"` string compare (line 108-117).
  - **any mismatch → fall through → return 0** (line 171). Full pass → reads entry tables → return 1 (line 166).
- **The second gate arm: globals.txt** (`FUN_180bb058c`, front-3 READ): `vtable[0x120]` open of `Shaders/Cache/globals.txt`; **`==0` (open fail — the `got=-1` tell) → `FUN_180bb190c`** = `CShaderMan::mfCreateCommonGlobalFlags`.
- **`FUN_180bb190c`** (the swap-on `.ext`-reprobe source, READ here lines 341-409): `vtable[0x1f8]` **FindFirst** on `Shaders/` → loop `vtable[0x200]` **FindNext** → each `*.ext` non-dir entry → `vtable[0x120]` **open** `Shaders/<name>.ext` → `vtable[0x1a8]` seek-end + `vtable[0x1b0]` **tell/size** + read + strstr "UsesCommonGlobalFlags"/"Name". **This IS the 36 swap-on `data/gameshaders/*.ext` probes** — fired exactly when globals.txt validation fails.

### The list-build → precache path (front 2, READ — the downstream that never runs)

- **`FUN_180da342c`** = `CShaderMan::mfInit` (READ here, "CShaderMan" string line 462): sets `Shaders/HWScripts/`, `/GameShaders/HWScripts/`, `/GameShaders/` (the gameshaders alias prefix, line 463-474), mounts `%ENGINE%/Shaders.pak` + `%ENGINE%/shadercache.pak`, then in init-order calls `FUN_180b03570` → **`FUN_180bb058c`** (globals.txt gate, line 511) → … → `FUN_1819dfb54` (ShaderCacheMisses reader, line 518).
- **`FUN_18250dd8c`** (compile-orchestrator, front-2 READ): `mfLoadShaderList` (`FUN_1819d4a54`, fills the list at `this+0x300` from `%USER%/shaders/shaderlist.txt`) → `FUN_18171a644` → **`_PrecacheShaderList`** (`FUN_1825091e0`).
- **`_PrecacheShaderList`** (`FUN_1825091e0`, front-2 READ): iterates `this+0x300`; "Nothing to precache" log fires when `FUN_18040d420(this+0x300)` reports empty (the O3 outcome). Per-item submit via shader-object vtable slots `+0x78`/`+0x48`.

### The PSO-create end (front 1, READ — confirmed job-deferred, NOT the gate)

- `mfUploadHW` (`FUN_18252f3ac`) → `FUN_1807b18b4` → **`FUN_180b209fc`** = blob → `NCryDX12::CShader` create (CPU-side DXIL container). NO device-vtable call.
- The device `CreateGraphicsPipelineState` is JOB-DEFERRED: `PSOPrecachingInternal::LaunchPSOCreationJobsGraphics` (CGenericJob vftable string @ 0x180bb44b0), dispatched via JobManager `[*DAT_18492b928+0x10]`. Runs on a worker thread, decoupled from the cache read. (The PipelineStateCacheManager.cpp fns FUN_180bb315c/2844/2ad8/23c0 all ENQUEUE; none calls the device directly.)

## The mechanism (falsifiable, AP17-grade candidate — ONE unverified link)

Under the swap, the shader-cache VALIDATION (`FUN_180b04984` lookupdata.bin loader, and/or `FUN_180bb058c` globals.txt open) gets a result it rejects → the engine **disables the read-only shader cache** → falls into `mfCreateCommonGlobalFlags`'s `Shaders/*.ext` enumeration/rebuild (the swap-on 36 `.ext` probes) → the precache list / pipeline build never completes → `gfx_calls=1`, workers idle, black. Swap-off, validation passes → cache used → menu.

**The two swap-sensitive validation reads, both keyed to `DAT_18492b850` (the object kcdx swaps):**
1. `FUN_180b04984` line 104/106: `vtable[0x130]` (FReadRaw) reads magic+header, **does NOT check the bytes-read count**, tests magic/version on the buffer immediately. If kcdx's FReadRaw lands the cursor differently (the FINDING's 28-byte header-peek → close → reopen → read pattern is a tell), the header is wrong → cache disabled.
2. `FUN_180bb058c` line 823: `vtable[0x120]` open of globals.txt; `==0` → the `.ext` enumeration. (globals.txt is genuinely absent from paks → read from `%user%` only → this may be NORMAL; the `lookupdata.bin` reject is the stronger lead.)

**THE ONE UNVERIFIED LINK (do NOT assert without it — AP17):** WHICH validation read diverges under the swap, and HOW. The loader checks magic/version but NOT the byte count, so the divergence is in WHERE the read cursor lands / WHAT bytes the kcdx handle returns for the lookupdata.bin header — a kcdx FReadRaw/FSeek/FTell return-contract difference vs the engine's native CCryPak. This is the same FS-handle-behavior bridge the FINDING checked for the CRT read family — but lookupdata.bin is served from `%engine%` as a PAK entry (`how=index-pak`), so the relevant contract is kcdx's PAK-handle FReadRaw, on the EXACT multi-read sequence (4 bytes, then 20 bytes, from a pak-minted handle).

## Owed next (the probe — narrowest hook now identified)

The dispatch-tracer Layer-B target is NO LONGER "the .cfxb→PSO consumer" — it is **the cache-VALIDATION gate**:
- **Hook `FUN_180b04984` (lookupdata.bin loader)** — log its return (0 = cache disabled = the wedge) + the magic/version it read, swap-on vs swap-off. Pre-committed: return-0 swap-on + return-1 swap-off → CONFIRMS the validation-reject mechanism, and the logged magic/version says whether the header bytes were wrong (read-contract divergence) or a real version mismatch.
- **Hook `FUN_180b04478` (validate driver)** — log which "Disabling…"/"Deleting…" branch fires swap-on vs off.
- Bracket with the existing PROBE P (gfx_calls) to confirm the causal edge (validation-reject → no precache).
- These are 2 concrete RVAs (0x180b04984, 0x180b04478) hookable by kcdx's existing hook engine — far narrower than the original "shader-list-submit" target.

**Before the launch — the cheap re-ground owed:** read kcdx's own pak-handle `FReadRaw` (vtable[0x130]) impl on the multi-read sequence (4 then 20 bytes) and confirm its cursor/return matches native CCryPak — code kcdx owns (`src/fs_takeover/`), reuse-first off `fs-takeover-readslot-abi-recon`. If kcdx's FReadRaw advances the cursor or returns differently across two consecutive small reads from a pak handle, that is the mechanism without a launch.

## Provenance / reuse pointers
- Anchors: `_anchors.txt` (47 referenced shader-system strings + xref fns).
- Front dumps: `_f_*` / `_g_*` / `_h_*` / `_i_*` (front 1 PSO-create), `_f2_*` (front 2 cache-load), `_f3_*` (front 3 cfxb/lookup). Scripts: `Ki28PsoCreateDecomp{,2,3,4}.java`, `Ki28CacheLoadDecomp.java`, `Ki28CfxbLookupDecomp{,2}.java` in `third-party-ghidra/ghidra_scripts/`.
- Key RVAs: lookupdata loader `FUN_180b04984` (0xb04984); validate driver `FUN_180b04478` (0xb04478); per-API setup `FUN_180b033a0`; globals.txt gate `FUN_180bb058c`; `.ext` reprobe `mfCreateCommonGlobalFlags` `FUN_180bb190c`; mfInit `FUN_180da342c`; _PrecacheShaderList `FUN_1825091e0`; mfLoadShaderList `FUN_1819d4a54`; blob→CShader `FUN_180b209fc`.
- The `vtable[0x130]` = FReadRaw read-N-bytes slot; `[0x120]` open; `[0x1b0]` tell/size; `[0x1a8]` seek; `[0x1c0]` eof; `[0x1b8]` close — the slots kcdx's takeover owns.
