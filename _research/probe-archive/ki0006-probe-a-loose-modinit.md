# PROBE A (KI-0006) — loose-mod-init serve-AND-EXECUTE vehicle

Tracks the build of PROBE A for the KI-0006 serve-execute confirmation. The
question: does the engine's mod-init open of a LOOSE `scripts/mods/<modid>.lua`
route through HOOK 2's FOpen seam (so kcdx's overlay serves it), AND do the
served bytes EXECUTE? Theory-independent by construction — two distinct markers
+ the HOOK 2 serve log = direct observation.

## Static ground (cited, not re-derived)

- **The mod-init open vpath is `scripts/mods/<modid>.lua`.** Wiki KM-A-3
  ("Structure of a Mod"): the special file `scripts/mods/<modid>.lua` "is
  loaded and executed right after scripts/main.lua". The two existing pak
  fixtures (`lua_memory_verify.pak`, `lua_sandbox_probe.pak`) contain exactly
  `scripts/mods/<modid>.lua` as their internal path. So the engine OPENS the
  vpath `scripts/mods/<modid>.lua` — that is what the overlay keys.
- **A LOOSE `.lua` opens via `CCryFile::Open` → `ICryPak::FOpen` slot 36 —
  HOOK 2's exact lane** (recon F2, `_research/asset-loadpath-map-recon/LEDGER.md`).
  A PAK-resident `.lua` uses the mount lane (CreateFileA at pak-mount, F5) and
  bypasses HOOK 2 — which is why kcdx's pak fixtures proved execution but NOT
  through HOOK 2. The vehicle must ship `<modid>.lua` LOOSE.
- **Every installed mod runs its `scripts/mods/<modid>.lua` via mod-init**
  (`Loading lua init script for mod <modid>` → `Loading and executing script
  file 'scripts/mods/<modid>.lua'`; KI-0006 trail, the 23-11-37 run) — but the
  installed mods all keep that file inside a pak (mount lane).

## The keyed vpath + reasoning

The overlay keys **`scripts/mods/<modid>.lua`** (with `<modid> = ki6_loose_modinit`,
so the literal key is `scripts/mods/ki6_loose_modinit.lua`). This is the engine's
virtual open path per the wiki + the pak-fixture internal layout — NOT `<modid>.lua`
and NOT `Mods/<modid>/<modid>.lua`.

The probe is self-correcting on the vpath: the HOOK 2 instrumentation logs the
ACTUAL vpath of every `mods/`-containing `.lua` open with its map HIT/MISS and
whether HOOK 2 served it. So even if the keyed vpath is wrong (e.g. the engine
opens the loose file at a different normalized path), the probe REVEALS the real
vpath from the open log, and the overlay re-keys on the next iteration. The
keyed-best-guess is grounded; the observe-the-real-vpath design is the backstop.

There is one residual checkable unknown the probe also OBSERVES rather than
guesses: whether the engine reaches a loose `scripts/mods/<modid>.lua` placed in
the mod-dir root (loose mirror) vs only inside `Data/`. The wiki frames mod
files as pak-resident; the loose lane is the engine's general loose-search.
The HOOK 2 open log resolves it — if no `mods/`-containing `.lua` open is logged
for this modid, the loose file was never reached (fixture-placement finding).

## The three pieces

| # | Piece | Path | Status |
|---|-------|------|--------|
| 1 | Loose-mod-init fixture (modid `ki6_loose_modinit`, marker `KCDX_KI6_MODINIT_RAN`) | `test-fixtures/pak-mods/cap-ki6-loose-modinit/` | DONE |
| 2 | kcdx overlay plugin (cap-78, overlay marker `KCDX_KI6_OVERLAY_SERVED_AND_RAN`, keys `scripts/mods/ki6_loose_modinit.lua`) | `test-plugins/cap-78-loose-modinit-serve-execute/` | DONE |
| 3 | HOOK 2 PROBE KI6 instrumentation block | `src/asset_overlay.cpp` `FOpenLooseOverlay` | DONE |

## Outcome→meaning map (committed up front, theory-independent)

Two distinct file-scope markers in `kcd.log` (the game log), plus the HOOK 2
`probe_ki6_lua_open` lines in `kcdx-dev.log`:

- **`KCDX_KI6_OVERLAY_SERVED_AND_RAN` present** (the overlay's bytes ran) →
  HOOK 2 served kcdx's overlay bytes for the mod-init vpath AND they executed →
  **SERVE-AND-EXECUTE PROVEN, KI-0006 closes.** The `probe_ki6_lua_open
  vpath=<v> map=HIT served=yes` line corroborates the serve directly.
- **only `KCDX_KI6_MODINIT_RAN` present** (the original loose file ran) → the
  engine ran the ORIGINAL loose file; HOOK 2's overlay did NOT win the open →
  the mod-init open BYPASSED HOOK 2 (a capability/serve finding). Re-observe:
  the `probe_ki6_lua_open` line tells which vpath the engine actually opened +
  whether the map HIT — re-key on the real vpath, or surface the design fork if
  the mod-init open genuinely bypasses FOpen.
- **neither marker present** → the engine did not run this mod's init at all →
  the fixture structure is wrong (check `mod.manifest` modid / `mod_order.txt` /
  the loose-`.lua` placement). The `probe_ki6_lua_open` log says whether ANY
  `mods/`-containing `.lua` open reached HOOK 2 for this modid — absent ⇒ the
  loose file was never reached (placement); present-MISS ⇒ keyed-vpath mismatch.

This FALSIFIES "the mod-init open routes through HOOK 2": the
"only KCDX_KI6_MODINIT_RAN, no overlay serve" outcome is a real, designed-for
result that kills the theory.

## Probe wiring (reconstructable; removed from live source after capture)

A `// === DIAGNOSTIC (PROBE KI6)` block in `FOpenLooseOverlay`
(`src/asset_overlay.cpp`): a cheap `.lua` + (`mods/` OR the test modid) string
filter, an UN-latched `std::set<std::string>` distinct-vpath dedup, logging
`probe_ki6_lua_open vpath=<key> map=HIT|MISS served=yes|no` for every matching
open. Un-latched (the existing `overlay_opened` one-shot latch would hide the
mod-init serve). Per `working-artifacts.md` it is removed from live source once
the question is answered; reconstruct from this block if a re-run needs it.
