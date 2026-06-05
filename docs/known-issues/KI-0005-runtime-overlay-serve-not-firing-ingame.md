---
id: KI-0005
opened: 2026-06-04
status: Open
commit_at_filing: 2259a761c47442c7540a301110f2b3d42ebca952
---

# KI-0005 — asset runtime-overlay serve does not fire in-game for a re-opened vanilla vpath

**Status:** open

## Symptom

A `kcdx.assets.register`/`replace` runtime overlay is provably KEYED but does not
SERVE when the engine re-opens the overlaid vpath in-game.

Observed live 2026-06-04 (dev log `kcdx-dev_2026-06-04_18-12-28.log`): cap-75
runtime-replaces `Libs/UI/Textures/KCDLogo.dds` with a placeholder `.dds`. The
runtime overlay is keyed — engine log: `ASSET_RUNTIME runtime_overlay_registered
vpath="libs/ui/textures/kcdlogo.dds" plugin="cap_75_assets_get_by_path" entries=3`
at 18:12:40. The user then loaded a save AND opened the pause menu (which
re-displays the logo — an open AFTER the register). The logo rendered VANILLA
(not the placeholder), and the dev log shows ZERO `ASSET_OVERLAY
overlay_resolved`/`overlay_opened` lines for ANY source this run. The resolver
fired no overlay HIT at all for the re-opened, runtime-keyed vpath.

This is a RUNTIME serve-path question, distinct from the (proven + gated)
Phase-2 asset capability: the seam (HOOK 1 + HOOK 2), the two-pass cross-mod
resolver, and the RCU runtime store are all built and acceptance-verified at the
contract level (the store keys correctly; cross-mod resolves end-to-end;
boot-suite all PASS). What is unconfirmed is the in-game SERVE of a runtime
overlay — the resolver consulting the runtime store for a re-opened vpath.

## Trail

| Date       | Action                                                                 | Result |
|------------|------------------------------------------------------------------------|--------|
| 2026-06-04 | Filed from the 18:12:28 run; latch-not-consumed caveat ruled out (below) | symptom + facts captured; PROBE A pending |
| 2026-06-04 | PROBE A: un-latched per-vpath observer in AdjustFileNameResolver + FOpenLooseOverlay — log every `.dds` vpath seen (raw pName + normalized key + runtime-store HIT/MISS) across save-load + pause-menu | pending |

## Facts

- The runtime overlay for the logo IS keyed: `runtime_overlay_registered
  vpath="libs/ui/textures/kcdlogo.dds" ... entries=3` fired at 18:12:40 (the
  register half works).
- ZERO `ASSET_OVERLAY overlay_resolved`/`overlay_opened` lines this run, for ANY
  source (`overlay_resolved` count = 0; `overlay_opened` count = 0).
- The one-shot latch is NOT a confound this run: `g_loggedFirstHit` /
  `g_loggedFirstFOpen` were both UNconsumed (no HIT of any source fired — the
  probe-asset-overlay build-time `.dds` HIT that consumed them in prior runs was
  removed from the live deploy this run). So absence-of-log IS conclusive that no
  resolver HIT fired for the logo.
- The runtime-store consult IS wired into both hooks at the build-time-map MISS
  point: `src/asset_overlay.cpp:181` (HOOK 1 AdjustFileNameResolver) +
  `:294` (HOOK 2 FOpenLooseOverlay), both call `asset_namespace::LookupRuntimeOverlay`.
- Take-effect timing is NOT the cause this run: the register fired at boot
  (18:12:40), the pause-menu logo open was AFTER it (post-save-load); the open
  was genuinely after the register (take-effect="thereafter" is satisfied).

## Outcome → meaning map (PROBE A, pre-committed — theory-independent)

PROBE A observes ground truth: does the logo open REACH the resolver, and with
what key + HIT/MISS? The three outcomes, flat:

- **A — the logo vpath REACHES the resolver, runtime-store lookup MISSES** (a
  `.dds` vpath logged by the observer, with a normalized key ≠ the registered
  `libs/ui/textures/kcdlogo.dds`, HIT/MISS=MISS) → a key-normalization mismatch
  (the register-side key and the engine-open-side key disagree) → fix the
  normalization so both fold identically.
- **B — the logo vpath NEVER reaches the resolver** (the observer logs no `.dds`
  vpath matching the logo across the whole save-load + pause-menu session) → the
  engine serves the pause-menu logo from a memory/texture CACHE populated at boot
  (before the register), so it never re-opens the FILE → a cache-coverage finding
  (the asset class / open-path the resolver does not see), the same class as the
  Phase-1 handle-consumed residual. Surface as a design fork (which open-paths the
  seam must cover).
- **C — the logo reaches the resolver and HITS** (observer logs the logo vpath
  with HIT/MISS=HIT) but no `overlay_opened` followed → the serve is suppressed
  downstream of the HIT (a return-path issue) → read the actual HIT path.

## Open questions

- Which of A/B/C holds (PROBE A settles it).
- If B: is the runtime-overlay serve viable AT ALL for a memory-mapped `.dds` the
  engine caches, or is the runtime serve only reachable for handle-consumed
  classes / first-open assets? (bears on the design — what the runtime
  register/replace contract can promise per asset class).
