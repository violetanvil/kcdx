# `kcdx.cvar.*` CVar-read surface

Read any game CVar's value by name from Lua + C++. The author supplies the CVar
string; the engine resolves the console + ICVar accessors by name and returns the
int / bool / float value. Backed by two AP18-approved Address Library entities
(`ICVar_GetIVal`, `ICVar_GetFVal`) + the existing `IConsole_GetCVar` (id 16).

**Spec:** [`plan-spec.md`](plan-spec.md) (settled decisions + coverage map).
**Evidence:** `_research/icvar-getival-recon/` (FINDINGS.md + the maintainer-tool
handoff). First consumer after this: the asset-system DirectStorage `KCDX_DSCVAR`
live read.

## Step ledger

Status: `NOT STARTED` · `BLOCKED` · `DONE` · `NEEDS REWORK`. Commit = short hash
when `DONE` (`(landed)` until backfilled), `—` otherwise. **Step 0 is EXTERNAL**
(the maintainer-tool DB-owner lane) — it is not a `/feature` build step; the build
steps below are BLOCKED until it lands.

| Step | Status | Commit |
| 0 — EXTERNAL (maintainer-tool): record `ICVar_GetIVal` + `ICVar_GetFVal` + fix id-16 prose, rebuild reference.sqlite | DONE (external — ids 156/157 in seeds, prose fixed, reference.sqlite rebuilt) | — |
| [1 — engine CVar-read core (`src/cvar.{h,cpp}`)](phase-01-cvar-read/step-1-engine-cvar-core.md) | DONE | bc71351 |
| [2 — Lua surface `kcdx.cvar.get_int/get_bool/get_float`](phase-01-cvar-read/step-2-lua-surface.md) | DONE | (landed) |
| [3 — C++ mirror `kcdxConsoleInterface::GetCVar{Int,Bool,Float}` (v2→3)](phase-01-cvar-read/step-3-cpp-mirror.md) | NOT STARTED | — |
| [4 — regression plugin `cap-71-cvar-read` (both surfaces)](phase-01-cvar-read/step-4-test-plugin.md) | NOT STARTED | — |
| [5 — docs `kcdx.cvar.*` + glossary "CVar"](phase-01-cvar-read/step-5-docs.md) | NOT STARTED | — |

## Phase verification gate

- **Build green** (`pwsh ./build.ps1` → the three artifacts) after every step.
- **Step 1** verified at step 4 (no standalone in-game surface yet) — the `cvar::`
  module compiles + resolves all four names at init (logged ready line).
- **Steps 2-3** verified at step 4 — the Lua + C++ surfaces register; exercised by
  the test plugin.
- **Step 4** verified by a live launch: `cap-71` reads a known engine CVar from
  BOTH surfaces (Lua + a C++ DLL), self-reports `ACCEPT-RESULT`, the matrix row +
  `suite: X/Y` confirm.
- **Step 5** verified by the docs gate (`docs-discipline.md`): `kcdx.cvar.*`
  reference entry + index map entry + C++ mirror entry + glossary "CVar" term,
  common-path-first, snippet copy-paste-runnable.
- Feature done when both surfaces read int/bool/float in-game (cap-71 green) and
  the docs/glossary/parity entries landed.
