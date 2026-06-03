# Asset replacement — implementation plan [ARCHIVED — SUPERSEDED]

> **ARCHIVED 2026-06-02 — SUPERSEDED, do not build from this tree.** This plan was
> decomposed around the superseded `CCryPak::FOpen`-redirect mechanism and framed
> the scope as "asset replacement". The live plan is the full **asset system** at
> [`../../outstanding-work/asset-system/`](../../outstanding-work/asset-system/README.md),
> decomposed against the canonical design
> [`../../design/asset-replacement.md`](../../design/asset-replacement.md) (kcdx OWNS
> resolution by REPLACING `CCryPak::AdjustFileName`, `sys_pakPriority`-independent,
> seam installed in the ready-bracket). Kept here for history only.

kcdx's asset-replacement surface: one `assets/` folder, explicit replacement
declarations (sidecar or code), navigable cross-plugin references, transparent
per-class staging. The most-touched mod-authoring surface for total conversions.

**Settled design:** [`../../design/asset-replacement.md`](../../design/asset-replacement.md)
(the canonical, corrected design). **Shared spec + coverage map:** [`plan-spec.md`](plan-spec.md).

> **⚠ This tree NEEDS RE-PLANNING against the corrected seam (2026-06-02).** It
> was decomposed around the superseded `CCryPak::FOpen`-redirect mechanism. The
> canonical design REPLACES `CCryPak::AdjustFileName` (slot 1, id 152) — reusing
> the pak/disk/normalizer leaves (153/154/155) by calling through, **independent
> of `sys_pakPriority`** — and installs the seam inside the already-shipping
> ready-bracket (canonical doc §7–§8). The phase intents + step docs below still
> describe the old FOpen mechanism; re-run `/plan` against the canonical doc
> before `/execute` consumes any step. The `[entrypoints].assets` parse +
> load-order overlay map (`2588b33`) remain valid landed foundation; the earlier
> FOpen probe site is removed in favor of the production `AdjustFileName` seam.

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
