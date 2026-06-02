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

## NOT archived here (still LIVE in src/)

- `src/probes/fopen_override_probe.{cpp,h}` — LIVE Phase 8.5 asset-overlay gating diagnostic
  (`Install()` active at `dllmain.cpp`). Feature infra, not residue. Untouched.
- `src/probes/bugsplat_ctor_probe.{cpp,h}` — LIVE before_game-hook install machinery, KEEP
  for the deferred before_game-hook work (`ArmLdrInstall()` live at `dllmain.cpp`). Its
  *internal* PROBE Z `#if 0` block (loader-lock asmjit smoke test) WAS extracted — see
  [bugsplat-probe-z.md](bugsplat-probe-z.md). The live install machinery stays in src/.
