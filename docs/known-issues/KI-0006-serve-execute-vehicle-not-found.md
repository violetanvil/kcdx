---
id: KI-0006
opened: 2026-06-04
status: open
commit_at_filing: 20873682510339e83f92afac6cf2f6e84fb0382f
---

# KI-0006 — serve-AND-EXECUTE confirmation owed a verified vehicle (which served `.lua` does the engine re-run on a save load?)

**Status:** open (investigation — the serve mechanism is PROVEN; only the EXECUTE-leg test vehicle is unfound)

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
| 2026-06-05 | PROBE A (`00-10-46` run): loose `Mods/ki6_loose_modinit/scripts/mods/ki6_loose_modinit.lua` + cap-78 overlay of that vpath + HOOK-2 serve instrumentation; Continue→world | **CRASHED before the probe fired** (markers + `probe_ki6` count = 0). NOT the probe: the AV (`c0000005` INVALID_POINTER_READ in WHGame.dll) is preceded by `lua_pcall.new_L_seen` (a NEW Lua state, `prev_g_L=0x0`) → 52 **re-entrant dispatch depth=2** on BOTH `engine.ccrypak_fopen` AND `engine.lua_pcall` → AV. The overlay KEYED (`overlay_entry vpath="scripts/mods/ki6_loose_modinit.lua" winner="cap_78_loose_modinit"`); `sl_saveload.lua` (cap-77's leftover overlay) ALSO got served this run (`overlay_opened` @00:10:57). Re-entrant-hook + new-Lua-state hazard during the mod-init / Lua-VM-init window — serving a `.lua` overlay there re-enters the FOpen+pcall hooks. Trigger fixture undeployed to restore launch. |

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
