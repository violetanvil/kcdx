---
id: KI-0005
opened: 2026-06-04
status: Closed
closed: 2026-06-04
closed_by_commit: 4eaa60d
commit_at_filing: 2259a761c47442c7540a301110f2b3d42ebca952
---

# KI-0005 — asset runtime-overlay serve does not fire in-game for a re-opened vanilla vpath

**Status:** closed (resolved-by-design — root cause is the Lua-VM lifecycle; the boot-asset Lua-runtime SERVE is deferred to the DllMain-VM phase per `before-game-hooks.md` §6b; the interim AP14 teaching warn shipped in `4eaa60d`)

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
| 2026-06-04 | PROBE A: un-latched per-vpath observer in AdjustFileNameResolver + FOpenLooseOverlay — log every `.dds` vpath seen (raw pName + normalized key + runtime-store HIT/MISS) across save-load + pause-menu | B killed; A inconclusive. Logo reaches HOOK 2 (FOpen) — `probeA_h2_dds key="libs/ui/textures/kcdlogo.dds" rt=MISS` at 18:22:41; 20361 `.dds` reach the resolver (not cache-only). BUT the observer is DEDUPED per-vpath: it logged the FIRST (boot) open at 18:22:41, BEFORE the register at 18:22:42 — so the logged MISS is the pre-register boot open (correct). The post-register pause-menu open was dedup-SUPPRESSED → its verdict unseen. PROBE confound. |
| 2026-06-04 | PROBE B: REMOVE the dedup for the logo vpath (log every open with a seq counter) so the POST-register pause-menu open's HIT/MISS is observed directly | **B3.** The logo opened EXACTLY ONCE all session — `probeB_logo_open seq=1 rt=MISS` at 18:31:05, BEFORE the register at 18:31:07. The save-load + TWO pause-menu opens produced NO `seq=2` — the engine never re-opened the logo FILE; it re-uses the boot-loaded texture. B1/B2 killed (no post-register open exists to hit/miss). Root cause found: a boot-cached menu asset is opened once (pre-register) + never re-opened, so the runtime overlay (take-effect="thereafter") is never consulted for it. Store is NOT buggy; the test vehicle was wrong. |

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
- Take-effect timing was the cause in the 18:12 run; re-examined under PROBE A
  (18:22 run): the logo's FIRST open (the menu logo at 18:22:41) is BEFORE
  cap-75's register (18:22:42) — plugin.lua (and the register) runs AFTER the
  menu has already loaded the logo. (PROBE A)
- The logo open REACHES the resolver via HOOK 2 (FOpen): `probeA_h2_dds
  raw="libs/ui/textures/kcdlogo.dds" key="libs/ui/textures/kcdlogo.dds"
  mode="rb" rt="MISS"`. Outcome B (never-reaches-resolver / cache-only) is
  KILLED — the resolver IS on the texture-open path. (PROBE A)
- 20,361 distinct `.dds` vpaths reached HOOK 2 (FOpen) this run; 0 reached HOOK 1
  (AdjustFileName). So textures open through FOpen, and the resolver's runtime
  consult IS reached for `.dds`. (PROBE A)
- The register-side key and the engine-open-side key are byte-identical
  (`libs/ui/textures/kcdlogo.dds` both sides) — no visible normalization
  mismatch. (PROBE A)
- PROBE A's observer is DEDUPED per vpath → it logged only the FIRST (boot,
  pre-register) logo open; the post-register pause-menu open's verdict is
  unobserved (the dedup masked it). PROBE A cannot distinguish "post-register
  re-open MISSES (a real store-lookup bug)" from "post-register re-open never
  happens (the engine re-uses the loaded texture, no file re-open)". (PROBE A)
- PROBE B (dedup removed, every logo open logged with a seq counter): the logo
  was opened EXACTLY ONCE the entire session — `probeB_logo_open seq=1 rt=MISS`
  at 18:31:05, BEFORE cap-75's register at 18:31:07. A save-load + TWO pause-menu
  opens produced NO `seq=2`. The engine does NOT re-open the logo file on a
  pause-menu display — it re-uses the texture loaded once at boot. (PROBE B)
- The runtime store is NOT buggy: its only logo open (seq=1) was genuinely
  pre-register (correct MISS); there is no post-register open for it to miss
  (B1/B2 both require a post-register open, which never occurs). (PROBE B)

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

- PROBE A killed B (the resolver IS on the `.dds` open path) and found the keys
  byte-identical, but its dedup masked the POST-register open. PROBE B (no dedup)
  settles the refined question below.
- **The refined question (PROBE B outcome map, pre-committed):** does the engine
  RE-OPEN the logo file after cap-75's register, and if so does the runtime store
  HIT?
  - **B1 — a post-register `probeA_h2_dds` for the logo with `rt=HIT`** → the
    store serves it; the not-rendering is a downstream serve/texture-refresh
    issue (the engine got kcdx's bytes but the already-loaded GPU texture wasn't
    invalidated) → a texture-cache-invalidation finding, likely out of scope for
    the file-level overlay contract.
  - **B2 — a post-register `probeA_h2_dds` for the logo with `rt=MISS`** → the
    store lookup genuinely misses for a byte-identical key after a successful
    register → a real runtime-store bug (snapshot visibility: the resolver reads
    a stale snapshot, OR a different store instance, OR a key-fold edge invisible
    in the log). The serious case.
  - **B3 — NO post-register logo open at all** (only the boot one) → the engine
    does NOT re-open the logo FILE on a pause-menu display — it re-uses the
    texture loaded at boot. The runtime overlay can never serve an asset the
    engine opened once at boot and caches → a take-effect/asset-lifecycle finding
    (the runtime register/replace contract serves assets opened AFTER the call;
    a boot-cached asset is never re-opened, so it is outside that contract). This
    is the most likely outcome and mirrors the Phase-1 asset-lifecycle residual.
- If B3: the runtime-serve confirmation needs an asset the engine opens FRESH
  after plugin.lua runs (a level/zone asset loaded on the save-load, not a
  boot-cached menu texture) — the correct test vehicle.

## Resolution

**Root cause (the mechanism, falsifiable — PROBE B).** The engine opens a
boot/menu asset (here `Libs/UI/Textures/KCDLogo.dds`) EXACTLY ONCE per session —
at boot, inside `CSystem::Init`, which is also the phase that CREATES the engine's
Lua VM. The author's Lua `kcdx.assets.replace` runs in `plugin.lua`, which fires
from a game lifecycle hook AFTER that VM exists (`src/hooks.cpp:305` `RunAll`) —
i.e. AFTER the boot asset open. PROBE B (dedup-free observer) proved it: the logo
opened once at 18:31:05 (`probeB_logo_open seq=1 rt=MISS`), the register fired at
18:31:07, and a save-load + two pause-menu opens produced NO second logo
file-open (the engine re-uses the boot-loaded GPU texture). So the runtime overlay
(take-effect="thereafter", `asset-replacement.md` §5.1) is never consulted for the
logo a second time. The wrong value is not in the store — the store keyed
`libs/ui/textures/kcdlogo.dds` correctly, byte-identical to the engine's open key;
the wrong thing is the ORDER: the author's Lua code runs after the boot open,
because the engine's Lua VM (which `plugin.lua` needs) does not exist until
`CSystem::Init`, the same phase that opens + caches the boot asset. The store made
no wrong write; there was simply no post-register open of the logo for it to
serve. This is the Lua-VM-lifecycle boundary, NOT a store/resolver defect.

**Fix (no code fix to the store — a design correction + a Phase-11 deferral +
an interim AP14 mitigation).** The store and the resolver consult are correct
(20,361 `.dds` reach the resolver; cross-mod resolves; the boot-suite is green) —
nothing to fix there. The resolution is:
- **Design corrected** (`asset-replacement.md` §5.1 take-effect bullet + new §5.4,
  changelog 2026-06-04 "latest"): take-effect="thereafter" is the
  Lua-VM-lifecycle boundary, not a hard store limit. A boot/early asset is
  replaced DECLARATIVELY (the sidecar — it parses as data in `DiscoverAndLoad`,
  pre-VM, and wins the boot open, proven Phase-1); the Lua runtime verb is for
  assets opened after `plugin.lua` runs.
- **Boot-asset Lua-runtime serve DEFERRED to Phase 11** (the user's call,
  2026-06-04), same root + trigger as before_game Lua hooks: FIX A
  (`fix-a-drop-static-lua.md`) brings a Lua VM up at DllMain so `plugin.lua` (or an
  early Lua slot) can run before the boot open. Phase 11's design explicitly
  accounts for it as a named deliverable — the early Lua slot, the order vs the
  boot open, the serve confirmation, the warn retirement (`before-game-hooks.md`
  §6b + §4 table).
- **Interim AP14 teaching — BUILT (`4eaa60d`).** A Lua runtime `register`/`replace`
  targeting a vpath the engine already boot-opened now emits a one-time teaching
  warn (`ASSET_RUNTIME runtime_overlay_boot_asset` — use the declarative sidecar;
  a Lua-runtime boot serve lands when the DllMain Lua VM is available) — never a
  silent non-serve. Mechanism: a bounded-boot-window set in `asset_namespace`
  (the resolver records boot opens under a mutex until the Lua VM is up via
  `NotifyVmReady`, then freezes; `RegisterRuntimeOverlay` checks the frozen set
  post-VM). Hot-path-clean post-boot (atomic short-circuit before the lock),
  release/acquire happens-before, RCU stores untouched (step-review commit-step;
  architect-review concurrency-approve). Regression: cap-75
  `CAP-75-replace-boot-asset-warn` (FAILS if a boot-opened target silently
  no-ops). This is what made the silent non-serve LOUD — the close's code
  deliverable Gate B required.

**Verification.** The root cause is mechanism-level + falsifiable (PROBE B's
single-open ground truth). The store's correctness is verified (PROBE B: the one
logo lookup was a genuine pre-register MISS; no post-register open exists to
mis-serve). The runtime-store LIVE serve (independent of the boot window) is
re-vehicled to an after-VM asset (`asset-replacement.md` §9) — a gameplay/on-demand
asset opened after `plugin.lua` runs; that confirms the store serves live now and
is the §9 acceptance, separate from the boot-window Phase-11 deliverable.

**Status — deferred, NOT a provisional mask.** The root cause IS known (the
mechanism above); this is not an AP17 provisional mask. The CAPABILITY (boot-asset
Lua-runtime serve) is deferred to Phase 11 with a named trigger + an explicit
Phase-11 design account + an interim AP14 mitigation — closed as resolved-by-design
(correct-as-designed today; the boot path is the declarative sidecar) with the
Phase-11 capability tracked in `before-game-hooks.md`.

The probe wiring + finding are captured at
`_research/probe-archive/ki0005-resolver-dds-observer.md`; the in-source probe was
removed (no residue — `asset_overlay.cpp` byte-identical to its pre-probe state).
