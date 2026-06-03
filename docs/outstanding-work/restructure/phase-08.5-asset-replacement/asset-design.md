# Asset replacement — design moved

**Status:** SUPERSEDED — moved to the canonical home (2026-06-02).

The asset-replacement design now lives at
[`../../../design/asset-replacement.md`](../../../design/asset-replacement.md) —
the canonical, by-responsibility design doc. **Build to that doc, not to this
file.**

This file's prior content (the Phase-8.5 asset-design v1) was promoted to the
canonical home AND **corrected on its central mechanism**: the prior version named
`CCryPak::FOpen` (vtable slot 36) as the resolver hook and framed the design as
riding the engine's `sys_pakPriority` mode. The 5-front disassembly superseded
both — kcdx OWNS asset resolution by **REPLACING `CCryPak::AdjustFileName`** (the
resolution-decision root, slot 1, id 152), reusing the pak/disk/normalizer leaves
(ids 153/154/155) by calling through, **independent of `sys_pakPriority`**. The
canonical doc §7 carries the verified seam; §8 carries the seam-install timing
(the already-shipping ready-bracket + a step-1 ordering probe); §12 is the full
decision record.

A reader who followed a link here for the asset-replacement design should read the
canonical doc; this pointer exists only so the prior link target does not present a
superseded mechanism as settled.
