# Finding — Data/-relative staging FAILS for handle-consumed at sys_pakPriority 2

Captured 2026-06-02 (live, `kcdx-dev_2026-06-02_16-06-35.log` + `kcd.log` @ 16:06:55).
The asset-replacement plan's step-1 residual probe. This FALSIFIES the design's
transparent-staging assumption (asset-design.md §4.3) for handle-consumed classes.

## The probe (one variable from the prior failed launches)

Staged a byte-exact `scripts/main.lua` + a file-scope
`System.LogAlways("KCDX_S1_DATA_RELATIVE_LOADED")` marker under
`<game>/Data/kcdx_overlay_staging/scripts/main.lua`; on the boot read-open of
`scripts/main.lua`, redirected `pName` → `kcdx_overlay_staging/scripts/main.lua`
(a `<game>/Data/`-relative path) + `nFlags |= 0x10000`. ONE variable vs the prior
launch: the path form (absolute `assets/`-dir → `Data/`-relative). Flag held 0x10000.

## Result: FALSIFIED — the marker did NOT appear

- `redirect_armed` fired: `vpath="scripts/main.lua" redirect_to="kcdx_overlay_staging/scripts/main.lua" nFlags=65536` — the redirect happened, the staged path + flag were handed to the engine.
- `KCDX_S1_DATA_RELATIVE_LOADED` count in `kcd.log` = **0** — the engine did NOT load+execute the staged substitute; it fell back to the pak's `main.lua`.
- The staged file EXISTS at the target (`Test-Path` true) — NOT a missing-file confound.

## Mechanism (why — corroborated, not just observed)

`kcd.log` this run logged `CVar sys_PakPriority value is 2`. The published game
runs **`sys_pakPriority 2` = pak-only**. The sub-resolver decompile
(`subresolver-decompiled-mechanism.md`, `CCryPak::AdjustFileName` slot 1) showed
the per-search-path-entry existence test at mode 2 is **pak-membership ONLY**
(`FUN_1804631f0`, the pak directory-index binary search) — the loose-disk check
(`FUN_1819c9cb4`) is reached for a search-path entry only at mode 0 (or under
`nFlags & 0x1000_0000`, bit 28 — NOT 0x10000). So a loose file, even
`Data/`-relative, even with 0x10000, is never consulted for a class that resolves
through the search-path arm at the published default. The redirect points at a
loose path the mode-2 resolver structurally skips.

## What this means

The per-open FOpen-redirect-to-a-loose-file mechanism works for **memory-mapped**
classes (`.dds` — live-confirmed, the menu logo) because that caller/consume path
reaches the loose-disk check; it does NOT work for **handle-consumed** classes
(`.lua`/`.xml`/scripts) at `sys_pakPriority 2`, by ANY loose-file path form tested
(absolute+0, absolute+0x10000, Data/-relative+0x10000). The design's transparent
stage-to-`Data/` mechanism (§4.3) does not deliver handle-consumed override at the
published default.

The U.4 cheat-script override worked through a DIFFERENT path (the cheat
subsystem's direct handle consumption), not the general search-path arm — so it
did not predict this flow.

## Open (a design fork — NOT to be guessed; surface to the user)

How does kcdx override a handle-consumed asset at `sys_pakPriority 2`? Candidate
mechanisms (each unverified — a probe each, after the user picks a direction):
1. **Return-our-own-handle** (Around/Replace mode, not Before): the hook opens the
   loose file itself and returns that handle, bypassing the resolver's pak-only
   search. The design-fork the earlier dispatch flagged (Before can't return a
   handle).
2. **Pack the overlay into a `.pak`** kcdx mounts (so it IS pak-resident, passing
   the mode-2 pak test) — but that fights the "loose file" author UX.
3. **Force `sys_pakPriority 0/1`** for the kcdx mount — a global resolution-order
   change (out of scope per the design's §7 rejection of the CVar route; re-open?).

Memory-mapped override is unaffected (works). This finding scopes to the
handle-consumed class.
