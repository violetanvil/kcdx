# File-system takeover — design changelog

Newest-first. The canonical spec is [`file-system-takeover.md`](file-system-takeover.md);
this records its revisions.

## 2026-06-15 — v1.2 P1 resolved (static binary read): swap seats at the construction site, not the ready-bracket

- **P1 resolved by a STATIC binary read** (WHGame.dll `release_1_5_1164953_841`),
  not a live launch — static evidence precedes a live probe
  (`.claude/rules/results-driven.md` §4). Outcome (c): inside `CSystem::Init`, the
  `CCryPak` object is constructed + published to gEnv+0x50 via
  `CSystem_pCryPak_construct_store` (id 158, RVA `0x9B3C0C`, call @ `0x1807A71CA`)
  BEFORE the first `*(gEnv+0x50)` file call (@ `0x1807A723A`) AND BEFORE the
  `ModManager_ctor` ready-bracket (@ `0x1807A76FE`). The CCryPak ctor itself is
  id 159 (RVA `0x00D2A570`).
- **Seating decision (settled):** the vtable swap seats at the CCryPak
  **construction site** (id 158, the gEnv+0x50 store point), NOT the late
  ModManager ready-bracket — seating at the ready-bracket would miss the file
  calls `CSystem::Init` makes between the publish and ModManager_ctor. SUPERSEDES
  the earlier "swap in the ready-bracket" assumption (§4.1's prior provisional
  clause + §8 P1 outcome (a)).
- **Integrated in:** §4.1 (P1 block + the swap text), §8 P1 (outcome recorded),
  §2 glossary "the ready-bracket" (no longer the swap point), §8 closing line.
- **Capture:** `_research/probe-archive/p1-ccrypak-construction-order.md` (the
  finding + reusable recon scripts at `_research/ccrypak-init-order-recon/`); the
  moot live-launch PROBE_P1 instrumentation removed from `src/asset_overlay.cpp` +
  `src/mod_absorb/ctor_bracket.cpp` (no residue).

## 2026-06-14 — v1.1 corrected the gEnv Address Library id citation

- corrected the gEnv Address Library id citation (1010 → 11; 1010 was a stale
  prior-scheme number) in §2/§4.1; no design change.

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
