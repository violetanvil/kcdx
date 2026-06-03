# Step 4 — transparent per-class staging

**Status: NOT STARTED.** Ledger: [`README.md`](README.md) → step 4.

## What

Make the author's rule class-agnostic: a handle-consumed overlay (`.lua`/`.xml`/
scripts) the engine will NOT resolve from the plugin's own `assets/` dir is
transparently staged by kcdx into a `<game>/Data/`-relative kcdx-managed root (with
the `0x10000` loose-search flag), so it resolves — exactly as a memory-mapped
`.dds` already does from `assets/` directly. The author NEVER sees this; they drop
the file in `assets/` and declare the replacement, regardless of class (the
disassembler test — the engine carries the WHERE).

## Scope

- Detect (or assume per the step-1 probe) which classes need staging; stage those
  overlay files into the `<game>/Data/` kcdx root at load; point the resolution
  (step 2) at the staged path for those.
- The staging LIFECYCLE (ephemeral-regenerate each launch vs tracked-invalidate) is
  pinned by step-1's probe result (`../plan-spec.md` §"Build-gated unknown" probe-2)
  — built to that, not guessed.
- Every staging failure logged (`logging.md`); a path that can't stage is a named
  error, not a silent drop (AP14).
- Path safety: a staged path is engine-rooted; reject a `..`-escaping source
  (`input-validation.md`).

## Test bar

Exercised at step 8 (a handle-consumed `.lua`/`.xml` overlay applying in-game is
the proof). This step's own check: a declared handle-consumed overlay stages +
resolves (the overlay-hit line fires for it), confirmed on the user's launch +
the dev-log. The memory-mapped path (step 2) is unaffected.

## Dependencies

**Step 1** (the probe pins the staging mechanism + lifecycle); **step 2** (the
resolution this points at the staged path); **step 3** (the sidecar declares the
replacement a handle-consumed overlay needs). Ordered after all three — its
behavior (a handle-consumed overlay applying) is exercisable when it lands.

## Disassembler-test / author-burden

CLEAN — staging is entirely engine-side; the author's surface is unchanged (drop
in `assets/`, declare). The per-class detail never reaches the author
(`cornerstones.md`, the disassembler test — design §4.3).

## Reference

[`../plan-spec.md`](../plan-spec.md); design authority
[`../../restructure/phase-08.5-asset-replacement/asset-design.md`](../../restructure/phase-08.5-asset-replacement/asset-design.md)
§4.3 (transparent staging) + §7 (the `0x10000` flag + `<game>/Data/` root facts).
Build to §4.3/§7, not to this summary.
