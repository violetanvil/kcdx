# cap-ki6-loose-modinit — PROBE A loose mod-init vehicle (KI-0006)

A test-fixture mod for the KI-0006 serve-AND-EXECUTE probe. Unlike every other
fixture under `test-fixtures/pak-mods/`, this mod is **deliberately NOT a pak** —
its mod-init script is LOOSE so the engine opens it through HOOK 2's FOpen lane.

## Why loose (the whole point)

The engine runs `scripts/mods/<modid>.lua` for every installed mod right after
`scripts/main.lua` (wiki KM-A-3). A PAK-resident `.lua` reads via the mount lane
(CreateFileA at pak-mount) and BYPASSES HOOK 2 — which is why kcdx's existing pak
fixtures proved `.lua` execution but never through HOOK 2. A LOOSE `.lua` opens
via `CCryFile::Open` → `ICryPak::FOpen` slot 36 — HOOK 2's exact lane
(recon F2, `_research/asset-loadpath-map-recon/LEDGER.md`). So this fixture ships
its `<modid>.lua` LOOSE, at the mod-init vpath, mirroring the layout an installed
pak would carry internally (`scripts/mods/<modid>.lua`).

## Layout (deploys to `<game>/Mods/ki6_loose_modinit/`)

```
ki6_loose_modinit/                 <- deploys to <game>/Mods/ki6_loose_modinit/
  mod.manifest                     <- XML, <modid> = ki6_loose_modinit
  scripts/mods/ki6_loose_modinit.lua   <- LOOSE mod-init; file scope logs
                                          KCDX_KI6_MODINIT_RAN (the baseline)
```

The inner `ki6_loose_modinit/` dir IS the deployable mod folder; this outer dir
holds dev assets (this README). The mod must also be listed in the live
`<game>/Mods/mod_order.txt` so the engine loads it.

## The two markers (theory-independent)

- `KCDX_KI6_MODINIT_RAN ki6_loose_modinit` — emitted by THIS original loose file
  → the engine ran the original (HOOK 2 did NOT win the open).
- `KCDX_KI6_OVERLAY_SERVED_AND_RAN` — emitted by the kcdx overlay copy of the
  SAME vpath (shipped by `test-plugins/cap-78-loose-modinit-serve-execute/`) →
  HOOK 2 served kcdx's overlay bytes AND they executed.

Which marker reaches `kcd.log` is the observation. See
`_research/probe-archive/ki0006-probe-a-loose-modinit.md` for the full
outcome→meaning map and the open-vpath checkable unknown the HOOK 2 probe
observes (whether the engine reaches a loose `scripts/mods/<modid>.lua` at the
mod-dir root, and the exact normalized vpath it opens).

## Lifecycle

Scratch / verification fixture — remove once the KI-0006 serve-execute question
is answered (the finding is captured in the probe-archive note + the KI-0006
Resolution).
