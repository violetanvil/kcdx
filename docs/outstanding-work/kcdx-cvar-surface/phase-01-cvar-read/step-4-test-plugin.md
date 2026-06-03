# Step 4 — regression plugin `cap-71-cvar-read` (both surfaces)

## What

A permanent regression plugin under `test-plugins/cap-71-cvar-read/` that exercises
the CVar-read surface from BOTH languages (full-parity test, `lua-api-surface.md` +
`test-suite.md`):

- **Lua side** (`plugin.lua`) — `kcdx.cvar.get_int(name)` / `.get_bool(name)` /
  `.get_float(name)` against a known engine CVar (a stable one, e.g.
  `sys_pakPriority` (int) and an `e_*` float CVar), assert non-garbage, report via
  `kcdx.test.report(...)`.
- **C++ side** — a sibling DLL plugin reading the same CVar via
  `K.console->GetCVarInt/GetCVarFloat`, asserting parity with the Lua read, report
  via `ReportTestResult(...)`.

Both report the canonical `ACCEPT-RESULT` signal (`acceptance-signal.md`) rolled
into `suite: X/Y passing`. Suite-gated (`[kcdx] test_suite_only = true`).

This step is where steps 1-3 are first exercised end-to-end. It also UPGRADES the
two seed entities' `evidence_kind` from `maintainer_ghidra` to `live_test_plugin`
(per `data/seeds/policy.md` §"Test plugin requirement") — that seed UPDATE is the
maintainer-tool lane (surface it to the user as a post-acceptance follow-up, not a
hand-edit here).

## Scope

`test-plugins/cap-71-cvar-read/` (Lua `plugin.lua` + `kcdx.toml`; the C++ sibling
DLL + its manifest) and the matrix row in `test-plugins/README.md`. Single-commit.

## Test bar

cap-71 itself IS the test. Mode: pick the lightest that proves it — likely
`boot-only` if a known CVar reads at boot, else `console` (the user types a command
that triggers the read). The plugin self-reports PASS on its passive checks. Matrix
row records: What / Engine status / plugin path / auto-pass check / last result.

## Dependencies

Steps 1 (core), 2 (Lua surface), 3 (C++ mirror) — all three must exist for the
plugin to drive both surfaces. Ordered last of the build steps.

## Design authority

`plan-spec.md` §"Cross-step invariants" (parity tested from both surfaces). Pick a
STABLE known CVar to read (a checkable fact — read `data/seeds` / the game's CVar
list / probe one that exists at boot; do NOT invent a CVar name). If no boot-stable
CVar is confidently known, that is a checkable unknown → a probe, not a guess
(`results-driven.md`).

## Disassembler-test / author-burden

The plugin author writes CVar names only. The test doubles as the worked example
for the docs (step 5).

## Rules

`test-suite.md` (permanent regression plugin, both surfaces get rows),
`acceptance-signal.md` (the `ACCEPT-RESULT` / `suite:` signal the agent reads),
`lua-api-surface.md` (parity tested), `pak-mods.md` if a pak fixture is involved
(not expected here), `results-driven.md` (probe a CVar name rather than guess).
