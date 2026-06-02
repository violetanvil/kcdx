# Step 8 — `cap-XX-asset-replace` regression plugin(s) + matrix rows

**Status: NOT STARTED.** Ledger: [`README.md`](README.md) → step 8.

## What

The permanent `test-plugins/` regression coverage for the asset surface, the
phase's end-to-end proof. Exercises override (declarative sidecar), cross-plugin
reference (the navigable namespace), and the chain/conflict path, from BOTH the
Lua and C++ surfaces (the grow-the-suite + parity rules). This is the
`cap-XX-asset-replace` plugin the design's whole-phase gate names.

## Scope

- `cap-XX-asset-replace` plugin under `test-plugins/<row-id>-asset-replace/`,
  suite-gated (`test_suite_only = true`), self-reporting via `kcdx.test.report`
  (Lua) / `ReportTestResult` (C++) (`test-suite.md`).
- Coverage rows: a known-safe **override** (a vanilla-path replacement visible
  in-game — both a memory-mapped `.dds` AND a handle-consumed `.lua`/`.xml`, to
  prove staging); a **cross-plugin reference** (one plugin resolving another's
  published-name + by-path asset via `kcdx.plugin.<a>.<p>.assets.*`); the
  **chain/conflict** path (two plugins replacing the same target → "lost to
  plugin X"). A second plugin (or sub-plugin) provides the cross-plugin/chain
  counterpart.
- Both surfaces: a Lua plugin AND a C++ plugin (or one driving both) exercise the
  capability — parity is tested, not assumed (`lua-api-surface.md`).
- Matrix rows in `test-plugins/README.md` (What / status / path / auto-pass check
  / last result / notes); strike the throwaway `cap-44-fopen-override` stub if it
  is now subsumed (surface that, don't silently delete a matrix row).
- `in-game` mode for the perceptual override (the user sees the replaced asset);
  the cross-plugin + conflict rows self-report to the dev log
  (`acceptance-signal.md` — agent reads the signal, user performs the gesture).

## Test bar

THIS is the test step — it ships the permanent regression coverage. Falsifiable per
row: the override row FAILS if the replaced asset is NOT visible / the overlay-hit
line absent; the cross-plugin row FAILS if `get_by_*` returns nil/wrong; the
conflict row FAILS if the loser is not suppressed or the "lost to" line absent
(`anti-patterns.md` AP15 — no tautological PASS).

## Dependencies

**Steps 2–7** (the full surface this exercises — resolution, sidecar, staging,
namespace, Lua + C++ verbs). Ordered last — it is the end-to-end exercise of
everything before it.

## Disassembler-test / author-burden

CLEAN — the test plugin is itself an author plugin using the surface by path/name;
if writing it requires any hex, that is a surface defect to surface, not absorb.

## Reference

[`../plan-spec.md`](../plan-spec.md); design authority
[`../../restructure/phase-08.5-asset-replacement/asset-design.md`](../../restructure/phase-08.5-asset-replacement/asset-design.md)
§3 (the US acceptance criteria each row proves). Build the rows to §3's acceptance,
not to this summary.
