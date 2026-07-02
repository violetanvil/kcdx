# KI-0028 — kFamAll confound self-check did NOT reproduce the black screen; it CRASHED (2026-06-23)

Run: `kcdx-dev_2026-06-23_09-06-14.log` (50MB) + dump `kcdx_2026-06-23_09-06-14.dmp` (97MB).
Build: fresh `kcdx.dll` with PROBE Z2 scratch live (parallel chat's, uncommitted). Marker: NONE (default `kFamAll`, mask=15 — confirmed `probe_z_live_mask=15` + `vtable_swapped` + `swap_live_first_open`).

## The self-check outcome — NOT the expected silent black screen

Expected (the confound guard): the default full swap reproduces the KI-0028 black-screen-with-sound.
Observed: ~1 second of sound on black, then a **"Database system error - tables can't be loaded"** fatal dialog, then bugsplat + a 97MB minidump.

So the kFamAll run did **not** cleanly reproduce the prior symptom — it crashed in a different place. Per the confound self-check's own logic, this means **the run is contaminated** — the harness/environment changed since the last black-screen run.

## The actual fault (dump ground truth, nearest-export labels IGNORED — no PDB)

- Faulting instr: `call qword ptr [rax+8]`, `rax=0x01030002` → `[rax+8]=0x0103000a` unmapped → AV read.
- `FAULTED ... plugin=(none)` — **NOT a kcdx hook fault.** Engine thread tid=37204.
- Stack is entirely WHGame frames; only real symbol = `wh::game::C_Game::CreateInstance+0x210936`. The engine is deep in game-instance / world creation.
- The faulting object @ `0x24744590F30` is **half-corrupted at the front**:
  - `+0x00`: `0x00000247_00000001` (low dword = 1)
  - `+0x08`: `0x00000000_01030002`  ← loaded into rax; `0x01030002` reads like two packed u16 fields (a version/flags pair), NOT a pointer
  - `+0x10`: `0x00000247_439c0c01` (misaligned `...0c01`)
  - `+0x18`/`+0x20`: real WHGame code pointers (so the object is partially valid)
- Shape: a structure-layout/synthesis mismatch — small integers where the front pointers belong. Same CLASS as `project_kcdx_crystringt_record_fields` (synthesized record missing its header → engine reads garbage where a pointer should be) — a LEAD, not yet attributed.

## The contamination — 123 test-suite plugins in the boot

`kcdx-plugins/test-suite/` has **123 plugins** deployed live, including:
- `cap-03-hook-lua-callback` — the 32-line FAULTED_FIRE storm in the log tail (GUARD hook-inventory dump; misleads — the real fault is `plugin=(none)`, the engine's own thread).
- `cap-49-fix-stray-table` — kcdx.toml REJECTED at boot (`unknown top-level` key) — a stale manifest vs the current engine.
- `probe-crash-trigger`, `probe-comp-crash` — crash-trigger probes deliberately in the set.

A KI-0028 measurement under 123 plugins + a rejected manifest + crash-trigger probes is **not a clean one-variable read of the FS swap.** The `0x01030002` corruption cannot be attributed to the swap vs a plugin-interaction while this set is live.

## Next (owed, before attributing anything)

Re-run the swap with the test-plugin set REMOVED (move `kcdx-plugins/test-suite/` aside, not deleted) — the one-variable isolation of FS-swap vs the 123 plugins:
- Still crashes/blacks with zero plugins → the swap is the cause (clean KI-0028; chase `0x01030002` in `C_Game::CreateInstance`).
- Reaches the menu with zero plugins → a test plugin is the contaminant; the "black screen" we chased is partly a plugin-interaction artifact, and the isolation target moves.

Do NOT delete the plugins (user's + parallel chat's test env). Move-aside + restore.
