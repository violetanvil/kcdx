---
id: KI-0006
opened: 2026-06-04
status: open
commit_at_filing: 20873682510339e83f92afac6cf2f6e84fb0382f
---

# KI-0006 — serve-AND-EXECUTE confirmation owed a verified vehicle (which served `.lua` does the engine re-run on a save load?)

**Status:** open — **BUNDLED → Phase 11 (FIX A / the DllMain Lua VM)** (user-approved deferral, 2026-06-05; trigger = Phase 11). The serve-execute confirmation AND the heap-corruption root-cause are re-attempted AFTER Phase 11, not now.

## Resolution path (settled 2026-06-05) — re-attempt under Phase 11, do NOT root-cause against the pre-Phase-11 architecture

The crash is NOT root-caused (3 theories falsified; the cross-CRT FClose confirmed-real but not the trigger; the crash tracks cap-78's keyed-but-unopened `overlay_entry`, an unexplained shape). Rather than keep probing the current architecture, KI-0006 is bundled into Phase 11 because:

- **Phase 11 / FIX A retires the confirmed-real hazard underneath it.** The cross-CRT `FILE*` free (PROBE D confirmed WHGame's `fclose` frees kcdx's `/MT` handle) exists because kcdx statically links its own Lua/CRT runtime alongside WHGame's. FIX A drops the static vendored Lua and routes through WHGame's symbols — collapsing the dual-runtime that *creates* the cross-CRT-free hazard class (`lua-bridge.md`: "Both FIX C and this fix retire under FIX A / Phase 11").
- **Phase 11 reworks the exact serve-execute / VM-lifecycle area** (`before-game-hooks.md` §6b is the named owner of the serve-execute confirmation) — so a fix attempted now risks being rebuilt by Phase 11.
- **Phase 11 gives a kcdx-CONTROLLED, instrumentable execution slot** — the serve-execute test no longer needs to ride the engine's opaque mod-init loader (whose handle-close behavior is unobservable).

NOT a guaranteed fix: KI-0006's specific corrupting write is unidentified and not provably in Phase 11's path. The bundle means the re-attempt happens against the architecture KI-0006 will ship on, with the confirmed hazard structurally addressed and a far more instrumentable execution path — not that Phase 11 auto-fixes it.

**Phase-11 re-attempt plan (when the trigger fires):** (1) with FIX A's single runtime, re-run the serve-execute confirmation via the early kcdx-owned Lua slot (not a mod-init overlay); (2) if a crash still reproduces, root-cause it against the post-FIX-A architecture with the cross-CRT variable eliminated — the surviving facts (the `WHGame+0xB2DBA0` mangled-pointer victim, the cap-78-`overlay_entry`-keyed correlation) carry forward as the starting evidence; (3) the falsified theories below (record-synth, re-entrancy, mod-init-serve) stay falsified — do not re-test them.

**Status:** open (the serve mechanism is PROVEN; the EXECUTE-leg confirmation + the heap-corruption root-cause are Phase-11-gated).

## Symptom

`CAP-77-serve-execute` (the Phase-3 step-10 regression row that closes the Phase-1
handle-consumed-lane residual) cannot confirm. The row keys an overlay on a `.lua`
the engine is expected to RE-RUN on a save load, with a file-scope marker
(`KCDX_SEAMA_LUA_LOADED cap-77`); the marker reaching `kcd.log` would prove the
served chunk EXECUTED. The chosen vehicle (`scripts/startup/sl_saveload.lua`) was
FALSIFIED — the engine does not open or run it on a Continue/Load-into-world gesture.

This is NOT a serve defect. The serve mechanism is independently proven
(`CAP-73-handle-consumed-serve` PASS, live 2026-06-04: HOOK 2 returns its own
`FILE*` for a handle-consumed `.lua`; the `.dds` memory-mapped lane proven at boot).
What is unconfirmed is only that a SERVED `.lua` then EXECUTES — and that needs a
`.lua` the engine demonstrably re-runs on a save load with an observable run signal.

## Trail

| Date       | Action                                   | Result |
|------------|------------------------------------------|--------|
| 2026-06-04 | Phase-1 acceptance: key the overlay on `scripts/main.lua`, assert the marker | Served (own-`FILE*` returned) but NOT executed — `main.lua` is the already-init'd boot chunk, opened-not-rerun mid-game. Residual deferred to step-10. |
| 2026-06-04 | Step-10 (`23-11-37` run): re-vehicle to `scripts/startup/sl_saveload.lua` (the FOPEN recon's first candidate), Continue→load-into-world | Overlay KEYED (`overlay_entry vpath="scripts/startup/sl_saveload.lua" winner="cap_77_serve_execute"`; `CAP-77-keyed` PASS) but the engine NEVER OPENED `sl_saveload.lua` — no marker, no `overlay_opened` for it. Vehicle FALSIFIED for the Continue/Load gesture. |
| 2026-06-04 | Same run: grep `kcd.log` for every `.lua` the engine actually RAN (`Loading file [scripts/<x>.lua] ... loaded`) | 20 `.lua` ran — ALL `scripts/cheat/*.lua`, and ONLY because a third-party `cheat` mod is installed (`Loading lua init script for mod cheat` → `Cheat:OnInit`). ZERO base-game `.lua` emitted a `Loading file` execute trace. |
| 2026-06-04 | STATIC re-ground (results-driven §4 — reuse the recon before a live probe): read the loadpath-map recon + the Warhorse wiki + the `23-11-37` mod-init log | RE-FRAMED. (1) The engine RELIABLY runs `scripts/mods/<modid>.lua` via mod-init for EVERY installed mod, every boot (`Loading lua init script for mod <modid>` → `Loading and executing script file 'scripts/mods/<modid>.lua'`; wiki KM-A-15; 14+ mods did it the `23-11-37` run incl. kcdx's own `kcdx_test_paklua` / `lua_memory_verify`). (2) Recon F2 (verified static): a LOOSE `.lua` opens via `CCryFile::Open` → `ICryPak::FOpen` slot 36 — HOOK 2's exact lane; a PAK-resident `.lua` uses the mount lane (bypasses HOOK 2). (3) kcdx's mod fixtures keep `<modid>.lua` INSIDE a pak → mount lane → that is why `[KCDX_PROBE]` proved execution but NOT via HOOK 2. The vehicle is a mod whose `<modid>.lua` is LOOSE. |
| 2026-06-05 | PROBE A (`00-10-46` run): loose `Mods/ki6_loose_modinit/scripts/mods/ki6_loose_modinit.lua` + cap-78 overlay of that vpath + HOOK-2 serve instrumentation; Continue→world | **CRASHED before the probe fired** (markers + `probe_ki6` count = 0). Heap corruption: AV `mov rax,[rdx]` at `WHGame+0xB2DBA0` (renderer init, `C_Game::CreateInstance`), `rdx` = mangled heap pointer; renderer is the victim, corruption committed earlier; NO kcdx frames on stack. Unique fault across all log history. NOT re-entrancy (clean run had 7090 re-entrant dispatches, no crash), NOT the probe (never fired). |
| 2026-06-05 | PROBE B (`00-27-30` run, BISECT cut 1): ki6 loose mod ALONE — clean engine (probe removed), NO cap-78 overlay. Continue→world | **NO CRASH** — reached `OnGameplayStarted`. The ki6 mod loaded IDENTICALLY (`build_record record_count=16`, `enabled_list_record kind="pak_mod"`, `takeover_record` — the exact same record synthesis as the crash run), `enabled_n=103`. So the **no-pak pak_mod / its record synthesis is EXONERATED** — it happens identically and is harmless. The remaining variable is the cap-78 OVERLAY of the mod-init vpath. |
| 2026-06-05 | PROBE C (`08-43-18` run, BISECT cut 2): add ONLY cap-78 (overlay of `scripts/mods/ki6_loose_modinit.lua`) — clean engine, ki6 already deployed | **CRASHED — identical signature to PROBE A** (`FAULTED ACCESS_VIOLATION module=WHGame.DLL module_rva=11723296`; faulting stack byte-for-byte identical: `C_Game::CreateInstance+0x306c3` → the renderer victim site). The overlay keyed (`overlay_entry vpath="scripts/mods/ki6_loose_modinit.lua" winner="cap_78_loose_modinit"`), `enabled_n=104` (vs PROBE B's clean 103). **BISECT CONCLUSIVE: ki6-alone (B) = clean; ki6 + the mod-init-vpath overlay (C) = crash.** The corruption is caused by kcdx SERVING its own overlay for a `scripts/mods/<modid>.lua` mod-init vpath. Trigger undeployed to restore launch. |

## Root cause (mechanism-level, confirmed by bisect 2026-06-05)

**Serving a kcdx overlay (HOOK 2's own `FILE*`) for a `scripts/mods/<modid>.lua` mod-init script corrupts the process heap.** Bisect: the loose no-pak mod alone loads clean to the world (PROBE B, `00-27-30`); adding the kcdx overlay of that exact mod-init vpath crashes with the identical heap-corruption signature (PROBE C `08-43-18` = PROBE A `00-10-46`: `mov rax,[rdx]` at `WHGame+0xB2DBA0`, a mangled heap pointer read in renderer init, no kcdx frames on the faulting stack — corruption committed earlier, during the mod-init script load). The serve MECHANISM is proven safe for the `.dds` memory-mapped lane (Phase-1) and was proven to SERVE a handle-consumed `.lua` (CAP-73) — but serving the MOD-INIT `.lua` specifically corrupts the heap.

This is the answer to the reframed KI-0006 question: **kcdx cannot currently safely overlay a mod-init script (`scripts/mods/<modid>.lua`) via HOOK 2's own-`FILE*` serve** — it is a real capability constraint, not a missing test vehicle. The serve-AND-EXECUTE confirmation via a mod-init script is BLOCKED on this.

### PROBE D (the mechanism probe — user picked "mechanism only, test waits", 2026-06-05)

**Leading hypothesis (from static grounding — to be PROBED, not assumed): a cross-CRT `FILE*` free.** HOOK 2 returns a `_wfopen_s` `FILE*` allocated by kcdx's `/MT` CRT (`src/asset_overlay.cpp` — "return kcdx's OWN CRT FILE*"). The engine CLOSES the served handle via `CCryPak::FClose` (seed id 131: sibling slot +0x1B8 FClose), which routes to WHGame's CRT `fclose`. A `FILE*` is a CRT-internal struct (its own malloc'd buffer); allocating it in kcdx's CRT and freeing it via WHGame's CRT `fclose` is a cross-CRT alloc/free mismatch → heap corruption — the same hazard CLASS as `lua-bridge.md` (an allocation crossing the CRT/module boundary, freed by the wrong allocator). The mod-init loader fully opens→reads→**closes** the script (reaching `FClose` on kcdx's handle); the safe lanes do not close kcdx's handle the same way (`.dds` is memory-mapped/streamed; CAP-73's `main.lua` was opened-not-fully-consumed mid-game).

PROBE D (theory-INDEPENDENT — observes ground truth, can FALSIFY the cross-CRT theory):
- Instrument the served `FILE*`'s lifecycle: in HOOK 2, record the returned `FILE*` pointer + its source-CRT identity for the mod-init vpath; hook/observe `CCryPak::FClose` (id 131 +0x1B8) to record whether the engine closes THAT pointer + via which CRT. ALSO instrument the SAFE lanes (`.dds`, a non-mod-init `.lua`) the same way — the DISCRIMINATOR: does the mod-init path reach a cross-CRT `FClose`/free that the safe lanes don't?
- Outcome→meaning map (flat, committed up front):
  - Engine calls `FClose` on kcdx's `/MT` `FILE*` on the mod-init path AND the safe lanes do NOT (or close via kcdx's CRT) → **cross-CRT free CONFIRMED** as the mechanism → the fix is kcdx owning the close (hook FClose to free its own handles via its own CRT), or not returning a raw CRT FILE* (return a kcdx-owned handle the engine's read family accepts but whose close kcdx intercepts).
  - Engine does NOT call FClose on kcdx's handle (or closes it identically on the safe lanes that don't corrupt) → cross-CRT free FALSIFIED → re-observe (a different write during the mod-init read; the loader does something else to the handle/struct). Do NOT hop to a fix theory — re-probe ground truth.
  - A read-only fingerprint of the corrupted heap region (the `0x70000001…`-mangled pointer's neighborhood) vs kcdx's `/MT` arena corroborates which allocator owns the corrupted block.

**PROBE D RESULT (`10-28-55` run) — partial; the cross-CRT FClose is REAL but NOT the crash trigger.** Observed:
- `probe_ki6d_served vpath="scripts/startup/sl_saveload.lua" fp=2409943846640 crt="kcdx_MT"` THEN `probe_ki6d_fclose handle=2409943846640 matched_served=true crt="kcdx_MT"` — **WHGame's `CCryPak::FClose` DID receive kcdx's `/MT` `_wfopen` FILE*.** The cross-CRT free is a REAL phenomenon, confirmed.
- BUT the crashing matrix falsifies it as the TRIGGER: `sl_saveload.lua` is served (and per D, cross-CRT-FClosed) in EVERY run — including the CLEAN PROBE B (`00-27-30`, 3 serves, reached world, no crash). A phenomenon present in a non-crashing run is not the crash's cause.
- The ki6 mod-init `.lua` (cap-78's overlay vpath) was NEVER served in ANY run (no `overlay_opened`/`probe_ki6d_served` for it) — the crash precedes it. So PROBE C's "serving the mod-init overlay corrupts" is WRONG about the mechanism: the overlay's vpath is keyed but never opened before the crash.
- **The crash correlates with cap-78 DEPLOYED (its `overlay_entry` keyed in the map), NOT with any serve.** Matrix: A (ki6+cap78)=crash, B (ki6 alone)=clean, C (ki6+cap78)=crash, D (ki6+cap78)=crash. The ONLY thing cap-78 adds that ki6-alone doesn't is the keyed `overlay_entry vpath="scripts/mods/ki6_loose_modinit.lua"` — a map entry whose vpath is never opened.
- **PROBE D CONFOUND:** D added a new MinHook detour on `CCryPak::FClose` (a new variable) and crashed at BOOT (`10:29:07`, ~12s earlier than A/C's world-load crashes) — so D's timing is not directly comparable; its own instrument may perturb. D confirmed the cross-CRT FClose exists but cannot cleanly attribute the crash.

**STATE: 3 theories falsified (record-synth, re-entrancy, mod-init-serve), 1 phenomenon confirmed-but-not-the-trigger (cross-CRT FClose). The crash tracks "cap-78's overlay_entry keyed in the map" — a keyed-but-never-opened mod-init vpath. Mechanism still not root-caused. SURFACED to the user (not a 4th autonomous probe): this is a hard heap-corruption bug, the probe added a confounding variable, and the next step is the user's call.**

### Open question (superseded by PROBE D above — kept for history) — the mechanism, routes to a design decision
The faulting stack names the renderer victim, not the corrupting writer (heap corruption's nature). WHY does serving a mod-init `.lua` corrupt the heap when serving a `.dds` / a non-mod-init handle-consumed `.lua` does not? Candidate mechanisms (NOT yet probed — surfaced as a design fork, not theorized into a fix): the mod system's script loader consumes the served `FILE*` via a different read/close path than the generic FRead the `.dds`/CAP-73 lanes use; or the cross-CRT `FILE*` (kcdx `/MT` handle read by WHGame's CRT) breaks on the mod-init close/free specifically; or the overlay disk path's lifetime/encoding differs. Resolving "how should kcdx serve (or decline to serve) a mod-init script" is a design-surface call (the seam's interaction with the mod-system script loader) — Gate A.

## Reframe (2026-06-05) — the crash is HEAP CORRUPTION, NOT re-entrancy and NOT serve-execute; likely a separate mod-absorb defect

A fresh-frame crash analysis (dump `k 40` + a clean-run diff) re-grounded the PROBE-A crash. TWO theories were FALSIFIED:
- **NOT re-entrancy** — the clean `23-11-37` run had 7090 `re-entrant dispatch depth=2` lines and did NOT crash; the crash run had only 52 (it crashed early). Re-entrancy is normal kcdx behavior.
- **NOT the probe** — the HOOK 2 PROBE-KI6 instrumentation never fired (count 0); the crash preceded any `mods/`-`.lua` open through HOOK 2.

**Verified mechanism (from the dump):** the AV is `mov rax,[rdx]` at `WHGame+0xB2DBA0` (renderer init in `wh::game::C_Game::CreateInstance`) with `rdx=0x70000001a311b6b1` — a MANGLED HEAP POINTER (low half matches the live heap arena `0x000001a3…`; high half stomped `→0x70000001`). The renderer fault site is the VICTIM; the corruption was committed earlier. NO kcdx frames on the faulting stack.

**Unique correlation:** this is the ONLY `FAULTED` line across all log history; the clean run is identical in every respect (same overlay serve-set, same re-entrancy) EXCEPT `enabled_n=104` (crash) vs `102` (clean) — the +2 ki6 mod entries.

**Strongest structural suspect (MEDIUM-HIGH, not yet directly observed):** the no-pak `pak_mod` record synthesis. The ki6 fixture is a LOOSE mod registered by the engine as `kind="pak_mod"` (`build_record mod_id="ki6_loose_modinit" record_count=16`; `[Mod] Opening paks in mods/ki6_loose_modinit/data/*.pak` — a `data/*.pak` glob over a mod with NO pak). kcdx synthesizes a `C_ModManager` record for it (`src/mod_absorb/record_synth.cpp`). This repo has a DOCUMENTED prior crash of EXACTLY this class — a synthesized record with a malformed CryStringT field → the engine reads a garbage pointer/nLength → corruption (the mod-absorb keystone crash, memory `project_kcdx_crystringt_record_fields`).

**So the crash is probably a SEPARATE kcdx defect** (mod-absorb mis-synthesizing a record for a no-pak pak_mod / a loose mod), exposed by the KI-0006 probe's loose-mod fixture — NOT a property of serve-AND-EXECUTE. The serve-execute question is still OPEN behind it.

### PROBE B (the deploy-bisect — one variable, falsifiable)
Deploy the ki6 loose-mod fixture ALONE — WITHOUT the cap-78 overlay, WITHOUT the HOOK-2 probe (the engine DLL is clean of the probe now). One variable: the no-pak pak_mod + its synthesized record, overlay removed.
- **Crashes again (same `C_Game::CreateInstance` mangled-pointer AV)** → the no-pak pak_mod ALONE corrupts the heap; the overlay is exonerated → second cut: ki6 with a real (dummy) `.pak` vs loose, to split "record synth" from "loose-mod takeover". This is a SEPARATE mod-absorb defect to fix.
- **Boots clean** → the no-pak pak_mod is not sufficient → redeploy adding ONLY cap-78 (still no probe); if THAT crashes, the corruption is the overlay of a mod-init vpath (a serve-execute capability constraint after all).
- **Different fault** → re-observe; do not assume same bug.

## Probe plan (persisted — `plan-persistence.md`)

| # | Probe | Status |
|---|-------|--------|
| A | Loose-mod-init vehicle: ship a test mod whose `<modid>.lua` is LOOSE (`Mods/<modid>/<modid>.lua`, NOT in a pak). The engine runs it via mod-init AND opens it via the FOpen loose lane (recon F2). Overlay that vpath via kcdx's sidecar with a marker `.lua` (`System.LogAlways`). Instrument HOOK 2 (`FOpenLooseOverlay`) to log the serve for the vpath. ONE variable: does the engine's mod-init open of a loose `<modid>.lua` route through HOOK 2's FOpen seam? | NOT STARTED |

**Outcome→meaning map (committed up front, theory-independent):**
- Marker PRESENT **AND** HOOK 2 logs a serve (`overlay_resolved`/`overlay_opened`) for the vpath → the mod-init open routed through FOpen, HOOK 2 served kcdx's bytes, they EXECUTED → **SERVE-AND-EXECUTE PROVEN, KI-0006 closes** (re-vehicle cap-77 to this form; step 10 → DONE).
- Marker PRESENT **but NO** HOOK 2 serve for the vpath → the mod-init open BYPASSED FOpen (the engine read the original loose file some other way, or the overlay didn't win) → a CAPABILITY/serve finding → re-observe (which open path did mod-init use?), surface the design fork if real.
- NO marker → the engine did not run this test mod's `<modid>.lua` → the mod fixture's structure is wrong (check `mod.manifest` / `mod_order` / the loose-`.lua` placement) → fix the fixture, re-run.

## Facts

- The serve is PROVEN for the handle-consumed `.lua` class (`CAP-73-handle-consumed-serve`, live).
- `scripts/main.lua` and `scripts/startup/sl_saveload.lua` are both opened-not-rerun (or not even opened) on a mid-game Continue/Load gesture.
- The engine emits `Loading file [scripts/<x>.lua] ... [DEBUG] <x> loaded` for source `.lua` it compiles+runs — but ONLY for MOD-init scripts (`Loading lua init script for mod <name>` → the mod's scripts run+log). No base-game `.lua` emitted that trace on this save-load.
- Base-game scripts likely run as bytecode precompiled in paks, via the script system without the per-file `Loading file` log the mod-author-source path emits — so a base-game vpath has no clean "did it execute?" observable even when it does run.

## Open questions

- Which served `.lua` does the engine RE-RUN on a save load, AND with an observable execution signal? The mod-init path (`Loading lua init script for mod <name>`) is the one observed execute channel — so the vehicle is likely a MOD-OWNED `.lua` (a kcdx-plugin-shipped script run via the mod-init hook), not a base-game `scripts/startup/*.lua`.
- THE NEXT PROBE (results-driven — observe, do not guess a third candidate): instrument the resolver to log every `.lua` it SERVES on a save load, and correlate against an execution signal — either (a) the served `.lua` ITSELF self-reports (a kcdx-plugin-owned script the engine runs via mod-init, the cap-77 plugin's own asset tree), or (b) add a transient in-overlay-`.lua` `System.LogAlways` that survives bytecode-compile. The cap-77 overlay vehicle is then re-keyed onto a PROVEN-re-run vpath.
- Reuse the recon recipe at `_research/asset-fopen-handle-recon/seamA-handle-consumed-served-LIVE.md` (the `// === DIAGNOSTIC (PROBE FOPEN-HC)` observer + the new §"Step-10 follow-up" finding).
