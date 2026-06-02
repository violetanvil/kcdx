# Phase 1 — resolution mechanism (probe-gated)

Fill the production `CCryPak::FOpen` hook body (currently pass-through) with the
overlay-map redirect, the per-asset sidecar declarative model, and transparent
per-class staging — probe-gated: the code-reference/staging unknown
(`../plan-spec.md` §"Build-gated unknown") is resolved FIRST.

Shared spec: [`../plan-spec.md`](../plan-spec.md). Design authority:
[`../../restructure/phase-08.5-asset-replacement/asset-design.md`](../../restructure/phase-08.5-asset-replacement/asset-design.md).

## Step ledger

| Step | Status | Commit |
|---|---|---|
| [1 — probe: can a code-referenced/staged non-vanilla path load via a game API?](step-1-probe-new-path-load.md) | NOT STARTED | — |
| [2 — simple-replacement resolution (fill the FOpen hook body)](step-2-replacement-resolution.md) | NOT STARTED | — |
| [3 — per-asset sidecar declarative model (replace + name)](step-3-sidecar-model.md) | NOT STARTED | — |
| [4 — transparent per-class staging](step-4-transparent-staging.md) | NOT STARTED | — |

## Verification gate (whole phase)

A TOML-only plugin whose asset sidecar declares `replaces = "<vanilla path>"`
shows the replacement **in-game** (the user's eyes confirm the perceptual part)
AND the engine log emits the overlay-hit line (winning plugin + vpath — the
machine signal the agent reads); a plugin with the file but no sidecar replaces
nothing; a second plugin replacing the same target loses by load order with the
"lost to plugin X" conflict line; a handle-consumed overlay (`.lua`/`.xml`)
declared the same way also applies (proving staging works). Build green is
necessary, not sufficient — the matrix is confirmed by the user's launch
(`acceptance-signal.md`).
