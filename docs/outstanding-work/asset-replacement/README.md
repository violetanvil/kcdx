# Asset replacement — implementation plan

kcdx's asset-replacement surface: one `assets/` folder, explicit replacement
declarations (sidecar or code), navigable cross-plugin references, transparent
per-class staging. The most-touched mod-authoring surface for total conversions.

**Settled design:** [`../restructure/phase-08.5-asset-replacement/asset-design.md`](../restructure/phase-08.5-asset-replacement/asset-design.md)
(committed `9bb4bb1`). **Shared spec + coverage map:** [`plan-spec.md`](plan-spec.md).

The production `CCryPak::FOpen` hook (`9e524ae`) + the `[entrypoints].assets` parse
and load-order overlay map (`2588b33`) are **already built + live** — the landed
foundation this plan builds on (see `plan-spec.md`). This tree picks up from there;
the FOpen hook body is currently pass-through and Phase 1 fills it.

This tree is the navigable plan: one phase subdir, one doc per shippable
(commit-grain) step. A landed step flips its step-grain row in the phase README;
a phase's row here flips to `DONE` when all its steps land. The ledger is the
completion record — `/execute` reads each step doc as its `Source work-item` and
flips the row.

## Phase ledger

Status: `NOT STARTED` · `BLOCKED` · `DONE` · `NEEDS REWORK`. Commit = short hash
when `DONE`, `—` otherwise.

| Phase | Status | Commit |
|---|---|---|
| [1 — resolution mechanism (probe-gated)](phase-01-resolution/README.md) | NOT STARTED | — |
| [2 — author surface (namespace + Lua + C++)](phase-02-surface/README.md) | NOT STARTED | — |
| [3 — regression coverage](phase-03-regression/README.md) | NOT STARTED | — |

## Phase intents

- **Phase 1 — resolution mechanism.** Probe the code-reference/staging unknown
  FIRST (`plan-spec.md` §"Build-gated unknown"), then fill the FOpen hook body
  with the overlay-map redirect (simple replacement, memory-mapped — live-verified),
  the per-asset sidecar declarative model, and transparent per-class staging built
  to the probe's result. Ends with overlay replacement working in-game.
- **Phase 2 — author surface.** The navigable `kcdx.plugin.<author>.<plugin>.*`
  namespace (`__index` resolvers) + the stale-comment sweep, the `kcdx.assets.*`
  Lua surface, and the `kcdxAssetInterface` C++ mirror (full parity). Ends with
  authors able to reference + register assets in code, cross-plugin.
- **Phase 3 — regression coverage.** The permanent `cap-XX` test plugin(s)
  exercising override, cross-plugin reference, and the chain/conflict path.
