# Finding — `sys_pakPriority` is NOT a viable overlay mechanism for the shipped game

Captured 2026-06-02 during Phase 8.5 step-3 reuse-first sweep (asset-overlay
mechanism). Primary-source + prior-dump evidence; no live probe needed.

## The question

Could kcdx achieve loose-file-over-pak asset replacement via the engine's own
`sys_pakPriority` CVar (set it to `0` = files-over-paks, stage overlays as loose
files), instead of the `CCryPak::FOpen` resolver hook?

## Verdict: NO — the published game is paks-only and pins it pre-launch

- **Warhorse wiki (primary source), "Publishing a mod" (`_research/warhorse_wiki/KM-A-58.json`):**
  - "The published version of the game does not load loose files — it only loads files from PAKs."
  - `sys_pakPriority 2` (only paks, loose ignored) is *"the default **and only possible behavior** for the published version of the game."*
  - The CVar "has to be set **before the engine launches**" (via `user.cfg`) — it is a pre-launch config knob, not a runtime override.
- **Binary corroboration (prior dump `_u5_worker2.txt`):** the CVar string `"sys_PakPriority"` (@183a93c00) and `"CVar sys_PakPriority value is %d"` (@183dcc770) exist in WHGame.dll — so the CVar is real and read, but the wiki's published-game restriction governs.

So the engine will not natively honor loose files on the shipped game regardless
of the CVar. The `CCryPak::FOpen` resolver hook (`src/asset_overlay.cpp`) is the
**correct and necessary seam** — not a workaround to reconsider. This retires the
"is there a simpler native CVar mechanism?" question.

## Corroborated, same sweep: the `0x10006` resolvable-loose-path shape

The archived U.2c finding (`_research/probe-archive/fopen-override.md`) — that
only specific loose roots resolve, and `0x10006` (OS-search flag) + a
`Data/`-relative path is the resolvable shape — is independently corroborated by
a verified DB row: `address_names_seed.csv` id 136 (`ModManager_ReadModOrder`)
documents the engine's own shared `CCryFile` helper (`FUN_1804605bc`) opening
`mod_order.txt` at `nFlags 0x10006`. The `0x10006` flag is the engine's own
file-search convention, not a probe artifact.

## What remains open (the step-3 probe targets exactly this)

Neither the DB nor the wiki settles the per-asset-class override mechanism:
1. Does redirecting `pName` to the plugin's actual `assets/`-dir loose file
   resolve, or must the file be staged to a `Data/`-relative location?
2. Does the override extend to a memory-mapped class (`.dds`/`.gfx`) or only the
   handle-consumed class (`.lua`/`.xml`) U.4 confirmed?

Phase 8.5 overlays BOTH classes (settled design decision, 2026-06-02). The
step-3 probe overlays one of each in a single launch to settle path-location +
class-coverage on evidence.
