# Phase 1 — resolution ownership (the AdjustFileName seam, probe-gated)

kcdx takes ownership of asset resolution by replacing `CCryPak::AdjustFileName`,
the single decision-root every by-name file op funnels through (design §7). The
phase is probe-gated: the seam-install ordering and the handle-consumed resolution
are checkable unknowns settled by a probe before the dependent code is built
(`.claude/rules/results-driven.md`, `incremental-delivery.md`).

## Step ledger

Status: `NOT STARTED` · `BLOCKED` · `DONE` · `NEEDS REWORK`. Commit = short hash
when `DONE`, `—` otherwise.

| Step | Status | Commit |
|---|---|---|
| [1 — probe: seam-install ordering (`ModManager_ctor` vs first read)](step-1-probe-install-ordering.md) | NOT STARTED | — |
| [2 — REPLACE `AdjustFileName` production seam (remove FOpen/SEAM-A residue)](step-2-adjustfilename-seam.md) | NOT STARTED | — |
| [3 — probe + build: handle-consumed resolution + transparent staging](step-3-handle-consumed-staging.md) | NOT STARTED | — |
| [4 — sidecar declarative model + load-order conflict report](step-4-sidecar-model.md) | NOT STARTED | — |

## Phase verification gate

- **Build green** (`pwsh ./build.ps1` → the three artifacts) after every step.
- **Step 1** is a probe — its gate is its pre-committed outcome→meaning map read
  against the live log (the ordering fact captured; a falsifying outcome surfaced,
  not silently worked around).
- **Step 2** is verified by the memory-mapped override rendering in-game
  end-to-end through the new seam (the `.dds` overlay live-test, now via
  `AdjustFileName` not FOpen) AND the dead FOpen/SEAM-A residue gone from
  `src/asset_overlay.{h,cpp}`.
- **Step 3** is verified by the handle-consumed probe's outcome (does `.lua`/`.xml`
  resolve from `assets/` or need staging) + the staging (or no-staging) built to it.
- **Step 4** is verified by a TOML-only declarative replacement applying in-game,
  a missing-target sidecar failing LOUD (AP14), and the load-order conflict report
  line appearing for two declarations of the same target.
- Phase done when overlay replacement works in-game for BOTH asset classes and a
  stock pak still resolves unchanged (the US-7 fall-through, fully proven in P3).
