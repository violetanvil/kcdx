# Finding — HOOK 2 serves the handle-consumed (`.lua`) class end-to-end IN-GAME; the marker's absence is a vehicle artifact, not a serve defect

Captured 2026-06-04 (live, in-game save load — the gesture FINDINGS.md said the
handle-consumed lane needed). Extends [`FINDINGS.md`](FINDINGS.md) §"Runtime
confirmation" — that run proved the `.dds` (memory-mapped) lane at boot and
flagged the handle-consumed (`.lua`/`.xml`) lane as unexercisable at boot. This
is the in-game confirmation.

## The probe (theory-independent, in `src/asset_overlay.cpp` `FOpenLooseOverlay`)

A throwaway `// === DIAGNOSTIC (PROBE FOPEN-HC)` observer, three un-latched
signals (the prior `overlay_opened` one-shot latch hid the `.lua` serve because
the `.dds` consumed it first — caught + corrected mid-investigation):

1. `probe_fopen_hc vpath=<v> map=HIT|MISS` — every DISTINCT handle-consumed
   (`.lua`/`.xml`/`.cfg`) vpath reaching `CCryPak::FOpen`, HIT/MISS in the
   overlay map. Answers "does the engine open handle-consumed assets through
   this FOpen at all, and does HOOK 2 recognize the overlay?"
2. `probe_fopen_hc_served vpath=<v> winner=<plugin>` — every DISTINCT vpath
   HOOK 2 actually returned its own `FILE*` for (un-latched). The end-to-end
   "HOOK 2 served THIS vpath" proof.
3. `KCDX_SEAMA_LUA_LOADED` in `kcd.log` (the overlay `main.lua`'s file-scope
   `System.LogAlways`) — proves the served chunk EXECUTED.

## Ground truth (three runs, the save = `playline0/autosave573` → level `kutnohorsko` → `OnGameplayStarted`)

- **The engine opens handle-consumed assets through `CCryPak::FOpen` richly on a
  save load: 28,266 DISTINCT vpaths** — 27,717 `.xml`, 364 `.lua`, 184 `.cfg`.
  The handle-consumed class is emphatically reachable through HOOK 2's seam (the
  boot-only run saw ZERO because the engine doesn't open these that early; a
  save+level load does).
- **HOOK 2 SERVES the handle-consumed `.lua` class.** `scripts/main.lua` →
  `map="HIT"` AND `probe_fopen_hc_served vpath="scripts/main.lua"
  winner="probe_asset_overlay"`. The engine opened it through FOpen, HOOK 2
  recognized the keyed overlay, opened the overlay's disk file, and returned its
  own CRT `FILE*` as FOpen's result — the SAME mechanism the `.dds` proved, now
  for the handle-consumed class. Served-set this run = 2 (`kcdlogo.dds` +
  `main.lua`).
- **`KCDX_SEAMA_LUA_LOADED` is ABSENT** — and `kcd.log` shows NO "Loading
  script … main" / compile / Lua-error line for `main.lua` this session. The
  engine OPENED `main.lua` (HOOK 2 served kcdx's bytes) but did NOT
  re-compile/execute it as the boot chunk in this MID-GAME load — the boot
  script VM was already initialized from the real boot earlier (the timing
  FINDINGS.md anticipated). So the served bytes were not run.

## What this proves (the load-bearing Phase-1 fact) vs the residual

- **PROVEN — the seam works for BOTH lanes.** Phase-1 step-4's gate is "a
  declared overlay's BYTES served end-to-end in-game — a handle-consumed
  `.lua`/`.xml` served via the own-`FILE*` open." `probe_fopen_hc_served
  vpath="scripts/main.lua"` IS that: HOOK 2 returned its own `FILE*` for a
  handle-consumed `.lua`. The cross-runtime `FILE*` serve (kcdx's `/MT` CRT
  handle read by the engine's separate CRT) holds for the handle-consumed class,
  not only the memory-mapped `.dds`.
- **RESIDUAL (NOT a serve defect, NOT load-bearing for Phase 1) — execution
  proof of a served `.lua`.** The `KCDX_SEAMA_LUA_LOADED` marker measures the
  served chunk EXECUTING; `main.lua` is the already-initialized boot script, not
  re-run on a mid-game save load, so its served bytes weren't executed THIS
  load. Confirming execution needs a `.lua` the engine actually RUNS on save
  load — a startup script the observer saw reach FOpen
  (`scripts/startup/sl_saveload.lua`, `scripts/startup/tutorials.lua`,
  `scripts/startup/*_startup.lua`), keyed as the overlay vehicle. This is the
  step-10 (Phase 3) permanent-regression home, not a Phase-1 blocker.

## Verified vs unverified (AP19 / results-driven)

- **VERIFIED (observed live, cited):** the engine opens `.lua`/`.xml`/`.cfg`
  through `CCryPak::FOpen` on a save load (28,266 distinct); HOOK 2 recognizes a
  keyed `.lua` overlay (`map=HIT`) and serves its own `FILE*` for it
  (`probe_fopen_hc_served`). The `.dds` serves identically the same run
  (served-set = 2).
- **NOT executed this run (explained, not a defect):** the served `main.lua`
  bytes did not run as a script (no compile/marker in kcd.log) — `main.lua` is
  the already-initialized boot chunk, not re-run mid-game.
- **UNVERIFIED (the residual, deferred to step-10):** end-to-end EXECUTION of a
  served handle-consumed `.lua` — owed a startup-script vehicle the engine runs
  on save load.

## Step-10 follow-up (2026-06-04, `23-11-37` run) — the `sl_saveload.lua` vehicle is FALSIFIED

cap-77 keyed an overlay on `scripts/startup/sl_saveload.lua` (the recon's first-named
candidate above) and asserted the file-scope marker `KCDX_SEAMA_LUA_LOADED cap-77`
reaches `kcd.log` on a save load (a Continue → load-into-world gesture).

- **The overlay WAS keyed** — `overlay_entry vpath="scripts/startup/sl_saveload.lua"
  winner="cap_77_serve_execute"` (the sidecar declared it; CAP-77-keyed PASS).
- **The engine NEVER opened `sl_saveload.lua` this run** — zero mentions in
  `kcd.log`, no `overlay_resolved`/`overlay_opened` for the vpath in the dev log.
  So this is NOT served-but-not-executed; the vpath was never requested on this
  gesture. The recon listed `sl_saveload.lua` as a hypothesis ("the observer saw it
  reach FOpen"); the marker run FALSIFIES it for the Continue/Load gesture.
- **GROUND TRUTH on which `.lua` the engine RUNS this save-load:** the engine emits
  `Loading file [scripts/<x>.lua] ... [DEBUG] <x> loaded` for every `.lua` it
  compiles+runs. This run ran exactly 20 `.lua` — **ALL `scripts/cheat/*.lua`**, and
  ONLY because a third-party `cheat` mod is installed (`Loading lua init script for
  mod cheat` → `Cheat:OnInit` → the cheat scripts). **ZERO base-game `.lua` emitted a
  `Loading file` line.** Base-game scripts do NOT surface a per-file execute log the
  way mod-author source `.lua` does (they are likely bytecode-precompiled in paks,
  loaded via the script system without the mod-init `Loading file` trace).
- **CONCLUSION:** the serve-AND-EXECUTE vehicle needs a `.lua` that BOTH (a) the
  engine demonstrably RE-RUNS on a save-load AND (b) emits an observable execute
  signal. The mod-init path (`Loading lua init script for mod <name>` → the mod's
  scripts run + log) is the one observed execute channel — so the vehicle is likely
  a MOD-OWNED `.lua` (a kcdx-plugin-shipped script the engine runs via the mod-init
  hook), NOT a base-game `scripts/startup/*.lua`. The next probe must OBSERVE which
  served `.lua` actually executes (instrument the serve + correlate with a run
  signal), not guess a third candidate. Tracked as a known-issue (the cap-77
  serve-execute confirmation is OPEN, owed a verified vehicle).

## Probe wiring (reconstructable; removed from live source after capture)

The observer was a `// === DIAGNOSTIC (PROBE FOPEN-HC)` block in
`FOpenLooseOverlay` (`src/asset_overlay.cpp`): an `ends_with(.lua/.xml/.cfg)`
filter + two `std::set<std::string>` distinct-vpath dedup sets
(`g_fopenHcSeen` / `g_fopenHcServed`) under one mutex, logging
`probe_fopen_hc` (with `map=HIT|MISS`) at FOpen entry after `NormalizeVPath`,
and `probe_fopen_hc_served` right after the latched `overlay_opened`. Per
`working-artifacts.md` it is removed from live source now that the question is
answered; reconstruct from this recipe if the execution-vehicle run needs it.
