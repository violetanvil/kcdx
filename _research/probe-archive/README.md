# Probe archive — extracted-and-removed probe findings + reusable wiring

Durable process-output (`working-artifacts.md`). When a probe's question is answered,
its finding + reusable instrumentation wiring is captured HERE, then the probe is
removed from live source — the live source returns to pure production logic (no
`#if 0` block, no dormant branch, no commented-out corpse). The next investigation
reconstructs a probe from this tree, never from `src/`.

Each entry preserves: the 4-line archive header (verdict / root cause / known-issue
backlink / revival hint) AND the probe code itself, so a future investigation can
reconstruct the wiring without it living in `src/`.

## Entries

| Probe | Source it left | Verdict | Backlink |
|-------|----------------|---------|----------|
| [hooks-cpp-probes.md](hooks-cpp-probes.md) — PROBE α (lua_pcall/update fire) + PROBE A (WHGame-image dummynode classify) | `src/hooks.cpp` inline `#if 0` strips | VERIFIED / CONFIRMED | cap-59 KI (closed); KI-0001 |
| [console-printline-overlay.md](console-printline-overlay.md) — PROBE PRINTLINE_OVERLAY | `src/console.cpp` inline `#if 0` strip | CONFIRMED — PrintLine paints the ~ overlay | Phase 9.2 print surface |
| [scan-argv-and-ki2.md](scan-argv-and-ki2.md) — PROBE SCAN_ARGV (2 blocks) + PROBE KI2-RESOLVE | `src/console_commands_scan.cpp` + `src/scan_engine.cpp` inline `#if 0` strips | argv/parse byte-clean; KI-0002 fixture defect | KI-0002 (closed) |
| [post_bracket_probe.cpp.txt](post_bracket_probe.cpp.txt) + [.h.txt](post_bracket_probe.h.txt) — PROBE B (frame-4 modMgr dispatch) | `src/mod_absorb/post_bracket_probe.{cpp,h}` (whole files `git rm`'d) | engine dispatched on C_ModManager vtable VA, not kcdx heap obj | post-step-4 AV KI |
| [loc_dump_probe.cpp.txt](loc_dump_probe.cpp.txt) + [.h.txt](loc_dump_probe.h.txt) — LOC-DUMP probe (ctor capture + slot 21/22 LocalizeString) | `src/probes/loc_dump_probe.{cpp,h}` (whole files `git rm`'d) | loc RE phase COMPLETE; find{text=} settled; text→gameplay-fn proven impossible via loc path | cap-43 |
| [bugsplat-probe-z.md](bugsplat-probe-z.md) — PROBE Z (loader-lock asmjit smoke test) | `src/probes/bugsplat_ctor_probe.cpp` INTERNAL `#if 0` strip (file's live install machinery KEPT) | VERIFIED — codegen + branch_pool VirtualAlloc + dtor all loader-lock-safe | cap-59 KI; TD-0003 |
| [fopen-override.md](fopen-override.md) — FOPEN probe (U.1 read-fires + U.4 override acceptance) | `src/probes/fopen_override_probe.{cpp,h}` (whole files `git rm`'d) | unknown #1 PASS; U.4 override CONFIRMED (`KCDX_U4_OVERRIDE_ACTIVE` reached kcd.log) | Phase 8.5; superseded by `src/asset_overlay.cpp` |
| [pdb-autoload-symenum-internals.md](pdb-autoload-symenum-internals.md) — PROBE PDB-AUTOLOAD (SymEnumSymbols on a foreign release-PDB) | `src/plugin_loader.cpp` working-tree block (never committed; removed in-place) | Outcome B — `symload_ok=yes` but `internal_enumerated=no` (717 CRT-privates enumerated, the plugin's own non-exported fn ABSENT); the "PDB carries every internal address" assumption is FALSE for a release `/Zi`+`/DEBUG` PDB | Phase 9.3 step 3b (PDB auto-load — internal-address source must be re-designed) |
| [cvar-existence-recon.md](cvar-existence-recon.md) — CVAR_PROBE (which candidate CVars exist on the live build) | `test-plugins/cvar-probe-existence/` throwaway plugin (never committed; whole dir removed) | RESOLVED — 23 of 39 candidate CVars exist on build release_1_5_1164953_841; the 6 chosen catalog CVars all resolve + are readable | Phase 9.5 P3 s2 (the shipped behavior-catalog entries) |
| [ki0019-hook2-hit-vs-miss.md](ki0019-hook2-hit-vs-miss.md) — PROBE F (HOOK 2 hit-vs-miss; vpath + returned kcdx-CRT fp per HIT) | `src/asset_overlay.cpp` PROBE_F block (removed, no residue) | CONFIRMED — HOOK 2 HIT serves a kcdx-CRT FILE* on the inventory path | KI-0019 |

## NOT archived here (still LIVE in src/)

- `src/early_hook.{cpp,h}` — LIVE before_game-hook install machinery, relocated +
  generalized out of the original `src/probes/bugsplat_ctor_probe.{cpp,h}` into the
  permanent engine home (an author-parameterized install primitive +
  `early_hook::bugsplat::Arm`, live at `dllmain.cpp`). Its *internal* PROBE Z
  `#if 0` block (loader-lock asmjit smoke test) WAS extracted — see
  [bugsplat-probe-z.md](bugsplat-probe-z.md). The live install machinery stays in src/.
