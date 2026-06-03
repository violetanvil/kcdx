# Asset system — implementation plan

kcdx's **asset system** — the full surface by which a mod author adds, references,
publishes, composes, and replaces game assets: one `assets/` folder, explicit
replacement declarations (sidecar or code), navigable `kcdx.plugin.<author>.<plugin>.*`
cross-plugin references, runtime registration/publishing, transparent per-class
handling. The most-touched mod-authoring surface for total conversions.

**Settled design:** [`../../design/asset-replacement.md`](../../design/asset-replacement.md)
(the canonical `§`-structured TRD, committed `eea0fdb` — kcdx OWNS resolution by
REPLACING `CCryPak::AdjustFileName`, `sys_pakPriority`-independent, seam installed
in the already-shipping ready-bracket). **Shared spec + coverage map:**
[`plan-spec.md`](plan-spec.md).

The `[entrypoints].assets` parse + the load-order overlay map (`2588b33`) are
**already built + live** — the landed foundation (see `plan-spec.md`). The
superseded FOpen-based plan is archived at
[`../../archive/asset-replacement-fopen-plan/`](../../archive/asset-replacement-fopen-plan/README.md).

This tree is the navigable plan: one phase subdir, one doc per shippable
(commit-grain) step. A landed step flips its step-grain row in the phase README; a
phase's row here flips to `DONE` when all its steps land. The ledger is the
completion record — `/execute` reads each step doc as its `Source work-item` and
flips the row.

## Phase ledger

Status: `NOT STARTED` · `BLOCKED` · `DONE` · `NEEDS REWORK`. Commit = short hash
when `DONE`, `—` otherwise.

| Phase | Status | Commit |
|---|---|---|
| [1 — resolution ownership (the AdjustFileName seam, probe-gated)](phase-01-resolution-ownership/README.md) | NOT STARTED | — |
| [2 — author surface (namespace + Lua + C++)](phase-02-author-surface/README.md) | NOT STARTED | — |
| [3 — regression coverage](phase-03-regression/README.md) | NOT STARTED | — |

## Phase intents

- **Phase 1 — resolution ownership.** Probe the seam-install ordering FIRST
  (`ModManager_ctor` vs the first asset read, design §8), then replace
  `CCryPak::AdjustFileName` with the kcdx-owned resolver (overlay HIT → kcdx's
  path, MISS → call through to the engine leaves), removing the dead FOpen/SEAM-A
  residue. Probe the handle-consumed resolution + build transparent staging to its
  result. Then the per-asset sidecar declarative model + load-order conflict
  reporting. Ends with overlay replacement working in-game for both asset classes,
  and stock paks resolving unchanged.
- **Phase 2 — author surface.** The navigable `kcdx.plugin.<author>.<plugin>.*`
  namespace (`__index` resolver chain, the general cross-plugin primitive); the
  stale-comment sweep; the `kcdx.assets.*` Lua surface (add/reference/publish/
  register/replace); the `kcdxAssetInterface` C++ mirror (full parity). Ends with
  authors able to reference, publish, and register assets in code, cross-plugin.
- **Phase 3 — regression coverage.** The permanent `cap-NN`/`comp-NN` test plugin(s)
  exercising override (US-1), cross-plugin reference (US-3), the chain/conflict path
  (US-4), and stock-pak backward-compat (US-7).
