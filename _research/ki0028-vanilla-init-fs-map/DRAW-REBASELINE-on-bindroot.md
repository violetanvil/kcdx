# KI-0028 — DRAW re-baseline on the bind-root build (2026-06-22 22:41 run)

**Verdict:** `draw_indexed=0` / `ia_set_ib=0` HOLDS IDENTICALLY on the bind-root-fixed build. The render
divergence is INDEPENDENT of FS resolution — the FS is now fully correct (cap-112 (c) PASS, 4435 pak serves,
zero level misses) yet the indexed-geometry path is STILL abandoned upstream of command-list recording.

## The re-baseline (the DrawcallProbe / PROBE S+X is in the deployed bind-root dll, fired automatically)

swap-ON, black screen, full ~2.5-min run (40 DRAW_PROBE summary reads, `kcdx-dev_2026-06-22_22-41-11.log`):

```
draw_indexed = 0          (ENTIRE run — zero indexed draws, every read)
ia_set_ib    = 0          (ENTIRE run — IASetIndexBuffer NEVER called)
draw_instanced -> 19678   (climbs; the non-indexed fullscreen/scaffolding passes)
ia_set_vb    -> 19678     (== draw_instanced — full IA vertex setup per non-indexed draw)
ia_set_topo  -> 19678     (== draw_instanced — full topology setup)
om_set_rt    -> 4918      (render targets bound throughout)
om_null_rt   = 0          (ENTIRE run — RTs always VALID, never null)
create_cmdlist = 18       (command lists created)
first_ib_*   = 0          (no index buffer ever bound, so no first-IB VA/size/fmt captured)
```

## What this confirms (theory-independent, vs the prior pre-fix PROBE S/X baseline)

- Pre-fix PROBE S (recursive-walk build, swap-ON): `draw_indexed=0`, `draw_instanced=9500`. Pre-fix PROBE X:
  `ia_set_ib=0`. **Post-fix (bind-root build): `draw_indexed=0`, `ia_set_ib=0` — IDENTICAL.** The bind-root FS
  fix did not move the render metric. (draw_instanced is higher here, 19678 vs 9500, only because this run ran
  longer; the RATIO and the zeros are what matter, and they match.)
- The swap-OFF menu baseline (PROBE S) was `draw_indexed=96`, `ia_set_ib>0` — the working menu DOES draw indexed
  geometry. So the swap-ON `draw_indexed=0`/`ia_set_ib=0` is the divergence, and it is NOT FS-caused.
- `om_null_rt=0`: not a null-render-target failure. The non-indexed passes draw to valid targets.

## Localization (unchanged, now on a correct-FS substrate — strengthened)

The wedge is the indexed-geometry path being abandoned UPSTREAM of command-list recording (`ia_set_ib=0` means
the engine never even binds an index buffer, so the indexed draw is dropped before it reaches the command list).
With the FS now fully correct, this is purely a kcdx-perturbed render STATE/init-ORDER — exactly CORRECTION 4 +
5's verdict. The next probe (PROBE X's pre-designed branches) holds:

- **(a)** the index-buffer RESOURCES are never CREATED swap-ON (hook `ID3D12Device::CreateCommittedResource` /
  `CreatePlacedResource` — are index-buffer resources made swap-ON vs swap-OFF?), OR
- **(b)** the scene/UI mesh render PASS that issues indexed draws is never ENTERED (a higher-level engine
  decision — visibility / scene-graph / render-list population — drops all real geometry before D3D12).

The 19678 non-indexed draws are the frame scaffolding (fullscreen post/clear/sky — vertex+topology+
DrawInstanced, no index); the real content geometry path never runs. This is the narrowest the bug has been,
now on a verified-correct FS.

## Provenance
- Probe: `src/fs_takeover/drawcall_probe.{h,cpp}` (PROBE S + PROBE X, slots 12/13/43/47), fired by
  `seating_hook.cpp:171 DrawcallProbeStart()` unconditionally at seat. In the deployed bind-root `kcdx.dll`.
- Log: `kcdx-dev_2026-06-22_22-41-11.log` DRAW_PROBE lines. Prior baseline:
  `_research/ki0028-cshaderman-pso-consumer-recon/FINDINGS.md` §PROBE S + `ROOT-CAUSE-existence-overreport.md`
  §PROBE X RESULT.
