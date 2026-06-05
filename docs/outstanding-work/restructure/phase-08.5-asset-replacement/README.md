# Phase 8.5 — asset replacement (kcdx absorbs pak mods) — SUPERSEDED

**Status: SUPERSEDED** (2026-06-04) — re-planned and spun out into the standalone
**[`asset-system/`](../../asset-system/README.md)** tree. The original 5-step stub
below proved too coarse once the work began (the seam was a two-coordinated-hook
design, the author surface a full Lua+C++ parity surface, and the cross-mod /
runtime-store concerns needed their own steps); `/plan` re-decomposed it into a
richer 3-phase tree under `docs/outstanding-work/asset-system/`. That tree is the
live ledger; this stub is retained only as the supersession pointer.

**Where the work actually is (the `asset-system/` tree):**

| asset-system phase | covers old 8.5 steps | Status |
|---|---|---|
| [Phase 1 — resolution ownership (the two-hook seam)](../../asset-system/phase-01-resolution-ownership/README.md) | 1 (hook) + 3 (overlay-map check) | **DONE** (`2b0bd1b`) |
| [Phase 2 — author surface (Lua + C++ `kcdx.assets.*` / `kcdxAssetInterface`)](../../asset-system/phase-02-author-surface/README.md) | 4 (the surface) | **DONE** (6/6, full Lua↔C++ parity) |
| [Phase 3 — regression coverage](../../asset-system/phase-03-regression/README.md) | 5 (test plugin) | **NOT STARTED** |

The in-game register/replace SERVE of a runtime overlay is **DEFERRED → Phase 11**
(the boot-cache lifecycle gap, KI-0005). The `pak-mods.md` "deprecated; use
`[entrypoints].assets`" rewrite the original gate named lands with asset-system
Phase 3. The original step docs (`step-1`…`step-5`) remain in this directory as
historical authoring records; the `asset-system/` step docs are authoritative.
