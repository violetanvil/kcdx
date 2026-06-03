# Step 1 — probe: can a code-referenced / staged non-vanilla path load via a game API?

**Status: NOT STARTED.** Ledger: [`README.md`](README.md) → step 1.

## What

The build's opening probe (`../plan-spec.md` §"Build-gated unknown";
`.claude/rules/results-driven.md`). The simple-replacement map (a vanilla path the
game itself requests) is live-verified. UNVERIFIED: can the engine LOAD an asset
at a **non-vanilla path** — one the game never auto-requests — when the author's
code hands that path to a game asset API, and/or when kcdx stages a
handle-consumed overlay under a `<game>/Data/` root? This gates the resolution
shape (step 2 for the redirect, step 4 for staging) and the path the surface hands
back (Phase 2). Resolve it before building on it.

## Scope

- A throwaway probe (a `test-plugins/` probe plugin and/or a marked
  `// === DIAGNOSTIC (PROBE …)` site within the existing engine-owned
  `OverlayFOpen` body — NO second detour, `guard-probe-stack.py`), agent-built +
  deployed, user-launched, agent-read. Leaves NO residue in live source (capture
  finding + wiring to `_research/`, then remove — `.claude/rules/working-artifacts.md`).
- Theory-independent, falsify-designed, outcome→meaning map UP FRONT
  (`.claude/rules/results-driven.md`). One variable. The map decides: does a
  staged `<game>/Data/`-relative path + the `0x10000` loose-search flag make a
  non-vanilla asset load via a game API; and does that settle the staging
  lifecycle (probe-2, `../plan-spec.md`).
- Reuse-first: `_research/phase8.5-pak-resolver/` already holds the FOpen /
  AdjustFileName decompiles + the prior probe findings — read them before any
  live launch (static evidence first).

## Test bar

The probe IS its own verification — the pre-committed `ASSET_OVERLAY_PROBE` dev-log
outcome map, read against ground truth on the user's launch. No permanent test
plugin (that is step 8); the probe is throwaway. The finding is captured to
`_research/` (durable process-output) and the in-source probe removed.

## Dependencies

The landed foundation (the FOpen hook + overlay map — `plan-spec.md`). No prior
plan step. This is correctly ordered FIRST — a probe-/evidence-first step ships no
user-facing behavior but is verifiable by its captured result
(`.claude/rules/incremental-delivery.md`).

## Disassembler-test / author-burden

None — a probe adds no author-facing surface.

## Reference

[`../plan-spec.md`](../plan-spec.md) §"Build-gated unknown" + the design's §8;
design authority [`../../restructure/phase-08.5-asset-replacement/asset-design.md`](../../restructure/phase-08.5-asset-replacement/asset-design.md)
§8.
