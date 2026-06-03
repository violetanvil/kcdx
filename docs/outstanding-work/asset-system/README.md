# Asset system — implementation plan

kcdx's **asset system** — the full surface by which a mod author adds, references,
publishes, composes, and replaces game assets: one `assets/` folder, explicit
replacement declarations (sidecar or code), navigable `kcdx.plugin.<author>.<plugin>.*`
cross-plugin references, runtime registration/publishing, transparent per-class
handling. The most-touched mod-authoring surface for total conversions.

**Settled design:** [`../../design/asset-replacement.md`](../../design/asset-replacement.md)
**v2** (the canonical `§`-structured TRD, committed `9c891b1` — the seam is TWO
coordinated hooks: HOOK 1 replaces `CCryPak::AdjustFileName` (slot 1, id 152) for
the resolution DECISION, HOOK 2 returns kcdx's own CRT `FILE*` for the loose OPEN;
both install in the already-shipping ready-bracket; `sys_pakPriority` neither set
nor depended on). **Shared spec + coverage map:** [`plan-spec.md`](plan-spec.md).

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
| [1 — resolution ownership (the TWO-hook seam, probe-gated)](phase-01-resolution-ownership/README.md) | NOT STARTED | — |
| [2 — author surface (namespace + Lua + C++)](phase-02-author-surface/README.md) | NOT STARTED | — |
| [3 — regression coverage](phase-03-regression/README.md) | NOT STARTED | — |

## Phase intents

- **Phase 1 — resolution ownership (the two-hook seam).** Two probes FIRST: the
  seam-install ordering (`ModManager_ctor` vs the first asset read, §8 — findings
  captured) and the DirectStorage texture-arm bypass (§7 caveat, default-off,
  confirm before shipping). Then the two hooks: HOOK 1 replaces
  `CCryPak::AdjustFileName` (the resolution DECISION — overlay HIT decides kcdx's
  file wins, MISS calls through the leaves; removes the dead FOpen/SEAM-A residue);
  HOOK 2 returns kcdx's own CRT `FILE*` for the loose OPEN (no dependence on the
  engine's loose-search), installed alongside HOOK 1 in the ready-bracket. Then the
  per-asset sidecar declarative model + load-order conflict reporting. Ends with
  overlay replacement working in-game (add-new via the loose lane, replace-vanilla
  via the resolver redirect into the pak/mount lane), and stock paks unchanged.
- **Phase 2 — author surface.** The navigable `kcdx.plugin.<author>.<plugin>.*`
  namespace (`__index` resolver chain, the general cross-plugin primitive); the
  stale-comment sweep; the `kcdx.assets.*` Lua surface (add/reference/publish/
  register/replace); the `kcdxAssetInterface` C++ mirror (full parity). Ends with
  authors able to reference, publish, and register assets in code, cross-plugin.
- **Phase 3 — regression coverage.** The permanent `cap-NN`/`comp-NN` test plugin(s)
  exercising override (US-1), cross-plugin reference (US-3), the chain/conflict path
  (US-4), and stock-pak backward-compat (US-7).
