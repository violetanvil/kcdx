# Phase 1 step 2 — REPLACE `CCryPak::AdjustFileName` (the production resolution seam)

**Status: NOT STARTED.** Ledger: [`README.md`](README.md) → step 2.

## What

kcdx takes ownership of asset resolution by replacing `CCryPak::AdjustFileName`
(the resolution-decision root every by-name file op funnels through, design §7).
The kcdx replacement, installed through the conflict engine
(`hook_chain::AddCEngine`, Around/Replace), consults the load-order overlay map
(built, `2588b33`): on an overlay **HIT** it returns kcdx's loose-asset path; on a
**MISS** it falls through to the engine's own leaves (pak-membership id 153,
disk-existence id 154, root-prefix id 155) so stock content — including a stock
Nexus/Workshop pak — resolves exactly as today. The seam installs inside the
ready-bracket per step 1's ordering result. This step also **removes the dead
FOpen probe + the `InstallSeamAProbe()` SEAM-A diagnostic** from
`src/asset_overlay.{h,cpp}` — the FOpen hook is NOT the mechanism; the live source
returns to the one production seam with no probe residue
(`.claude/rules/working-artifacts.md`).

## Scope

- `src/asset_overlay.{h,cpp}`: replace the FOpen-hook scaffolding with the
  `AdjustFileName` production seam — resolve the target by NAME
  (`CCryPak_AdjustFileName`, id 152) via refdb (never a literal RVA — AP1,
  `no-hardcoded-addresses.md`); install through `hook_chain::AddCEngine` (NOT raw
  MinHook — AP4, `hook-engine.md`); the body: normalize the requested vpath
  (`NormalizeVPath`, built) → overlay-map lookup → HIT returns kcdx's path / MISS
  calls through to the leaves (153/154/155) by name.
- Install the seam in the ready-bracket window (before `SetEvent(g_kcdxReadyEvent)`)
  per step 1's confirmed ordering. If step 1 surfaced the falsifying outcome, this
  step is BLOCKED on the user's resolution of that fork (do not build to a guessed
  install point).
- Remove `InstallSeamAProbe()` + its `src/dllmain.cpp` call + the dead FOpen probe
  hook; capture any still-useful wiring to `_research/probe-archive/` first.
- Resolve game facts by name/id only; **no new seed row** (ids 152–155 exist, AP18).

## Test bar

A behavior step proven live: the memory-mapped override (`.dds` from a plugin's
`assets/` dir, the live-verified case) renders in-game **through the new
`AdjustFileName` seam** (not FOpen) — the engine log emits the overlay-HIT line
(winning plugin + vpath). Build green (`pwsh ./build.ps1`). The SEAM-A/FOpen
residue is gone (grep `src/asset_overlay.{h,cpp}` for `InstallSeamAProbe` /
`FOpen` probe → none). (The handle-consumed class is step 3; this step proves the
seam + the memory-mapped path + the miss fall-through.) The permanent regression
row is step 9 (`test-suite.md`); this step's live check is the seam's own proof.

## Dependencies

**Step 1** (the ordering probe — settles the install point this step builds to; a
falsifying outcome must be user-resolved before this step). The landed overlay map
(`2588b33`). Ordered after step 1 so the install point is verified, not assumed
(`.claude/rules/incremental-delivery.md`).

## Reference

Design authority: [`../../../design/asset-replacement.md`](../../../design/asset-replacement.md)
§7 (the verified seam + call-through leaves) + §8 (install timing) + §10.1
(`src/asset_overlay` responsibility). Shared spec: [`../plan-spec.md`](../plan-spec.md)
§"Cross-step invariants".

## Disassembler-test / author-burden

The seam is engine-internal — no author-facing input. The game facts (ids 152–155)
resolve by NAME through refdb; the seam carries no hand-written RVA/offset (AP1,
AP12). No new seed row (AP18).
