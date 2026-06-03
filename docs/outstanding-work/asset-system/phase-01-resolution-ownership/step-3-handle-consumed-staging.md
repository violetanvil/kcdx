# Phase 1 step 3 — probe + build: handle-consumed resolution + transparent staging

**Status: NOT STARTED.** Ledger: [`README.md`](README.md) → step 3.

## What

The memory-mapped class (`.dds`/textures) is live-verified to override from the
plugin's `assets/` dir (step 2). UNVERIFIED: does the **handle-consumed** class
(`.lua`/`.xml`/scripts — read through the returned file handle) also resolve
end-to-end from the plugin's `assets/` dir through the replaced seam, OR does that
class need kcdx to **stage** the file under a kcdx-managed root first (design §4.3,
§9 probe-2)? This step PROBES that (one variable), then BUILDS the resolution to
the probe's result — either direct (no staging) or with transparent staging. The
author never sees the class distinction either way (the disassembler test).

## Scope

- **Probe first** (`.claude/rules/results-driven.md`): overlay one handle-consumed
  asset (`.lua` or `.xml`) from a plugin's `assets/` dir through the step-2 seam;
  log whether the engine reads back kcdx's bytes. Throwaway, captured + removed.
  Outcome→meaning map, pre-committed:
  - **handle-consumed reads back kcdx's bytes from `assets/` directly** → no
    staging needed → build the direct path; §4.3 staging is a no-op.
  - **it does NOT** → staging required → kcdx transparently stages the file under a
    kcdx-managed `<game>/Data/`-relative root and the seam returns the staged path.
    Then the staging LIFECYCLE (ephemeral-regenerate vs tracked-invalidate) is
    decided on the probe's mechanism — surfaced if a real fork (`design-authority.md`),
    not guessed.
- **Then build** the resolution the probe settled, in `src/asset_overlay.{h,cpp}`
  (the seam's HIT path handles both classes uniformly — the author surface stays
  class-agnostic). Staging code (if needed) lands here.

## Test bar

A handle-consumed overlay (`.lua`/`.xml`) applies in-game through the seam — the
engine consumes kcdx's bytes (proven by an observable: the overlaid script's
effect, or a logged read-back marker). Build green. The probe's finding is captured
to `_research/`; its in-source diagnostic is removed (no residue). The permanent
regression row is step 9.

## Dependencies

**Step 2** (the production seam — this step exercises and extends its HIT path for
the handle-consumed class). Ordered after step 2 so there is a live seam to probe
through (`.claude/rules/incremental-delivery.md`).

## Reference

Design authority: [`../../../design/asset-replacement.md`](../../../design/asset-replacement.md)
§4.3 (transparent staging) + §9 (build-gated unknown 2). Shared spec:
[`../plan-spec.md`](../plan-spec.md) §"Build-gated unknowns".

## Disassembler-test / author-burden

The staging mechanism is engine-internal — the author drops a file in `assets/` and
never sees the class distinction or any staging root (the disassembler test,
`cornerstones.md`). No author-facing input added.
