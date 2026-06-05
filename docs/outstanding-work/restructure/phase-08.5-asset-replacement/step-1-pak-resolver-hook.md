# Phase 8.5 step 1 — hook the game's pak resolver (production overlay hook)

> **SUPERSEDED (2026-06-04) — historical record.** This 5-step stub was re-planned into the standalone [`../../asset-system/`](../../asset-system/README.md) tree (see this dir's [`README.md`](README.md)); the asset-system step docs are authoritative. Kept as the pre-spinout authoring record; the status line below is where it stood at spinout, NOT live work.

**Status: DONE (at spinout).** Ledger row: [`README.md`](README.md) → step 1.

## What

Install the PRODUCTION asset-overlay hook on the game's pak resolver
(`CCryPak_FOpen`), so a virtual-path open can be redirected to a loose
overlay file before the pak-resident asset is read.

## As-built

- The PRODUCTION asset-overlay hook is installed on `CCryPak::FOpen` through the
  conflict engine (`hook_chain::AddCEngine`) in `src/asset_overlay.cpp` — not a
  raw `MH_CreateHook`. The target resolves by the canonical name `CCryPak_FOpen`
  (existing seed kcdx_id 131, verified ABI).
- The hook body is **pass-through**: it calls the original unchanged. The
  overlay-map redirect decision (virtual-path → loose file) is step 3; this step
  lands the production hook site so step 3 can fill the decision.
- The earlier observe-only FOPEN probe (whose runtime unknowns U.1 read-fires and
  U.4 override-acceptance were resolved) was captured to
  `_research/probe-archive/fopen-override.md` and removed from `src/` — the live
  source carries no diagnostic residue.

## Test bar

Exercised at step 5 (the phase's `cap-XX-asset-replace` plugin). This step's
own check: the production hook installs and the game still boots (the resolver is
hot — a bad hook here AVs at startup).

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 8.5" → "Phase 8.5a".
