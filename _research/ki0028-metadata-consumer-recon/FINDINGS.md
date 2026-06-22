# KI-0028 — metadata-slot (existence) consumer recon

**Date:** 2026-06-22 · **Trust:** primary evidence (binary xref + body reads).
**Verdict: DIVERGENCE A (existence-timing) is FALSIFIED as a DIRECT no-present-wedge driver.**

## Question

KI-0028 slot-diff (FINDING-real-rva-window-mode-loop.md) found kcdx's metadata slots (67 IsFileExist3,
70 IsFileExist2, 45 GetFileSize) report a pak vpath as EXISTS/sized from `CSystem::Init` onward,
bypassing the engine's pakPriority/location/mount-timing gates. Does a WINDOW/SWAPCHAIN/PRESENT boot
consumer branch on that premature-TRUE and fork toward the no-present wedge?

## Method (reuse-first; the load-bearing correction)

Whole-`.text` linear capstone DRIFTS (one mid-instruction byte desyncs the rest → false 0-caller
results). Robust approach (reused from `fs-takeover-pak-mount-recon/xref_indirect_bytes.py`): byte-scan
the 680 `mov r64,[rip]` loads of the pCryPak global **0x18492B850** (= gEnv 0x18492B800 + 0x50; matches
phase8.5's "680 xrefs"), then correlate each to `mov vt,[reg]; call [vt+slotoff]` to get
provenance-verified consumers (call edge AND receiver==pCryPak both read, AP19-clean). Scripts here:
`correlate_pcrypak_slots.py`, `find_provenance_callers.py`, `classify_callers.py`, `read_branch.py`.
Vtable offsets: slot 67 = +0x218, 70 = +0x230, 45 = +0x168.

## Result (VERIFIED — binary read)

- **44 provenance-verified consumers**: slot 67 = 41, slot 45 = 3, **slot 70 = 0** (no engine code calls
  the 2-arg IsFileExist through gEnv->pCryPak).
- **Every one of the 44 is an asset / level / data loader** — level paks, character/anim
  (`.cgf/.chr/.caf`), materials, particles, textures, quests, factions, Bink, `Menu.gfx`, profile paks.
  **NONE references a swapchain / display-mode / `r_Fullscreen` / DXGI / present / the window-mgr
  singleton cluster `0x492b8xx`** (where the PROBE-M loop `0x869c39` lives).
- Two strongest boot consumers body-read:
  - `0x89682d` — `Menu.gfx` REQUIRED-asset gate: `if(IsFileExist) skip-fatal else "can't run without"`.
    A premature-TRUE MASKS a missing-asset fatal; it does not fork toward a wedge.
  - `0x244dd9c` site `0x244deef` — menucommon level-cache `GetFileSize` gate:
    `if(size) use-pak else "...does not exist"` (the F6 size-mismatch shape). Gates LEVEL-pak loading,
    not present.

## Consequence

The existence-timing divergence cannot DIRECTLY drive the no-present wedge — no window/present consumer
reads slots 67/70/45. Corroborates PROBE M from the consumer side. Residual (NOT statically traceable):
a metadata premature-TRUE could make a downstream asset/level load take a wrong arm whose effect
surfaces LATER as the un-presentable swapchain (multi-hop) — needs a live swap-on/off probe of
`0x244dd9c` / `0x89682d`, not more static work.

## Reuse

- pCryPak global = `0x18492B850` (gEnv `0x18492B800` + 0x50). 680 loads. The byte-scan+correlate
  scripts here are the reusable xref instrument (linear capstone is unreliable on WHGame — use these).
