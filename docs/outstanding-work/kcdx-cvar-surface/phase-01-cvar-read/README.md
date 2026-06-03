# Phase 1 — CVar-read surface (Lua + C++)

Build the `kcdx.cvar.*` read surface end to end: the engine resolve/call core, the
Lua binder, the C++ mirror, the regression plugin, the docs. One phase — the whole
feature is a single coherent build once the entities are recorded (step 0,
external). Each step is its own commit, ordered so it is independently verifiable
when it lands (`incremental-delivery.md`).

## Step ledger

| Step | Status | Commit |
| [1 — engine CVar-read core](step-1-engine-cvar-core.md) | BLOCKED (on step 0) | — |
| [2 — Lua surface](step-2-lua-surface.md) | NOT STARTED | — |
| [3 — C++ mirror](step-3-cpp-mirror.md) | NOT STARTED | — |
| [4 — regression plugin cap-71](step-4-test-plugin.md) | NOT STARTED | — |
| [5 — docs + glossary](step-5-docs.md) | NOT STARTED | — |

## Verification gate

- Build green after every step.
- Steps 1-3 are exercised at step 4 (no standalone surface until the test plugin
  drives it); step 1's init resolve is confirmed by the logged ready line.
- Step 4: live launch — `cap-71` reads a known CVar from BOTH surfaces, self-reports
  `ACCEPT-RESULT`, matrix row + `suite: X/Y` confirm (`test-suite.md`,
  `acceptance-signal.md`).
- Step 5: docs gate (`docs-discipline.md`) — reference + index + mirror + glossary,
  common-path-first.
- Phase done when both surfaces read int/bool/float in-game and docs landed.
