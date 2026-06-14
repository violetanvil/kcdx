# File-system takeover — design changelog

Newest-first. The canonical spec is [`file-system-takeover.md`](file-system-takeover.md);
this records its revisions.

## 2026-06-14 — v1 settled (initial authoring)

- **Initial design.** kcdx takes TOTAL ownership of the engine's `CCryPak` file
  system via a full vtable-pointer swap; kcdx becomes the one filesystem, reading
  vanilla paks and loose mod files itself, operating every handle on its own CRT
  cradle-to-grave.
- **Settled decisions (all user-decided in the design dialogue):**
  - Takeover extent: TOTAL (kcdx IS the filesystem) — rejected the recon's own
    PARTIAL overlay-subset recommendation (it straddles two systems).
  - Seating mechanism: full vtable-pointer swap — rejected per-function MinHook
    detours and the cvar-flip+staging mechanism.
  - Handle operation: kcdx serves every read on its own CRT — rejected handing the
    engine an OS HANDLE (unverified engine adoption) and the engine-operates-the-
    handle status quo (the crash).
  - Pak reader: kcdx's own PKZIP/DEFLATE reader — rejected driving the engine's
    ZipDir (re-threads engine-CRT memory, the straddle).
  - Non-file slots: thunk to the original, via a per-slot DECLARATIVE table so any
    slot is a one-line flip to a kcdx impl (the user's explicit reversibility
    constraint — "don't code us into a box") — rejected reimplementing all 102
    slots (own new crash surface, no UX gain).
  - Resolution model: ONE unified asset index built at load, O(1) lookup per open
    — rejected per-call search-path walk (the hotpath overhead ownership
    eliminates).
  - Phase relationship: its OWN track; this design is the home for KI-0019 +
    KI-0006 — rejected folding into Phase 11 / FIX A (couples to unrelated
    DllMain/VM work).
- **Four runtime-mechanism probes marked provisional** (§8, per
  `results-driven.md`): P1 CCryPak construction timing, P2 vtable-swap acceptance,
  P3 residual off-vtable raw-handle access, P4 thunked-slot `this` compatibility.
  The design is provisional on each; each is ordered before its dependent build
  phase.
- **Supersedes** the asset-resolution SEAM in `asset-replacement.md` §7 (the
  two-hook partial takeover); preserves that design's author-facing surface
  verbatim (§7).
- **In-flight state captured** (§11) so the design write does not orphan our place:
  PROBE F owed-removal, the `asset_overlay.cpp` seam subsumed, KI-0019/KI-0006
  routing correction owed, Phase 10 unaffected, Phase 11 decoupled.
**Integrated in:** `file-system-takeover.md` (all sections — initial authoring).
**Why:** the user settled that kcdx must own the entire engine filesystem rather
than straddle two systems, eliminating the cross-CRT `FILE*` crash class
(KI-0019/KI-0006) structurally; prerelease is the time to make the change.
