# File-system takeover — design changelog

Newest-first. The canonical spec is [`file-system-takeover.md`](file-system-takeover.md);
this records its revisions.

## 2026-06-15 — v1.5 §4.5 slot-38 reclassified to the read family + slot-35 ABI recorded

- **§4.5 wording defect fixed: slot 38 is `FReadRaw-by-pak-index`, a READ, not an
  open.** §4.5 had grouped slot 38 under "Open" as "FOpen-by-pak-index". The
  slot-map reconciliation against the binary
  (`_research/fs-takeover-slot35-recon/FINDINGS.md`, recon committed `4ca0bae`)
  body-read slot 38 leafing into the read-raw leaf + CRT `fread` with NO `fopen`
  call. Moved slot 38 from the Open group to the Read family. The open family is
  now slots 1 (resolution) + 35 + 36; the read family gains 38.
  - **Build consequence:** slot 38 flips THUNK→KCDX in the read cutover, not the
    open step — folded into the merged open+read cutover (plan step 3.2; see the
    plan tree).
- **Slot 35 `FOpenRaw` verified ABI recorded** in §4.5: 5-arg `__fastcall` member
  `FILE*-like(this, pName, szMode, outResolvedBuf, int bufCap)`, `_wfopen`-backed
  (open primitive RVA 0x9B2B28), resolves via slot 1. Seeded kcdx_id 160 (AP18-
  approved, `5527f2b`), RVA 0x2418DE4 — resolve by name, ready for the cutover.
- **Stale slot-label legend corrected** below: the v1.4 entry's parenthetical
  "(FRead 40 / FSeek 38 / FEof 39 / …)" carried front3's superseded role labels.
  The recon settled: slot 38 = FReadRaw-by-pak-index, slot 39 = FReadRaw, slot 40
  = FGetCachedFileData; FSeek = 53, FEof = 56. Corrected in place so the
  changelog does not preserve a body-falsified mapping.

## 2026-06-15 — v1.4 P3 resolved (static binary read): kcdx handle-id is safe; read family stays kcdx-owned (step 3.1)

- **P3 resolved by a STATIC binary read** from the primary-evidence asset-resolution
  recon already on disk — not a live launch. P3 (does any engine code operate a
  handle off-vtable, bypassing the read slots?) is a static call-graph question;
  static evidence settles it and precedes a live probe
  (`.claude/rules/results-driven.md` §4), so the step-3.1 Scope's "then a live
  probe" was not needed. Outcome 1 holds: **no off-vtable access to a
  `FOpen`-class (slot 36) handle exists.**
  - The read family (FReadRaw 38/39, FGetCachedFileData 40, FWrite 41, FSeek 53,
    FClose 55, FEof 56 — corrected per the v1.5 entry above; this entry's
    original labels were front3's superseded mapping) dispatches purely on the
    handle tag THROUGH the vtable; the loose-vs-pak decision
    bites at `FOpen`-time, never off-vtable
    (`front3-handle-consume-read-path.md`).
  - The ONE off-vtable raw-handle operation (the streaming engine's
    `SetFilePointer`/`ReadFile` on `m_zipFile`) operates an ENGINE-minted pak-MOUNT
    handle (`CreateFileA`, archive factory slot 72), NEVER a `FOpen` per-file handle
    (`F5-streaming-engine-bypass.md`); DirectStorage is default-OFF and dead at the
    shipped default (`step2-directstorage-bypass-finding.md`).
- **Handle-representation decision (settled):** a kcdx `FOpen` handle is a lightweight
  **kcdx handle-id** — opaque to the engine, operated only by kcdx's own read slots.
  Outcome 2 (forced to a real `FILE*`-shaped object) is FALSIFIED. The handle-id MUST
  honor the engine's tagged-union dispatch contract (`index+1` = pak entry; else =
  real-`FILE*`-class) so any reused/thunked read slot dispatches it correctly.
- **The load-bearing constraint P3 imposes:** the read family is **kcdx-owned, never
  thunked** — a thunked read slot's OS arm would `fread` the kcdx handle-id on the
  ENGINE's CRT (the cross-CRT straddle §9 removes). Every handle-operating slot stays
  `KCDX`; this is the one §4.3 thunk-flip the per-slot table forbids while the handle
  is a kcdx-minted id. Binds 3.2 (mints the id), 3.3 (the kcdx-owned read family),
  3.5 (the table keeps handle-operating slots `KCDX`).
- **Integrated in:** §4.4 (handle-representation settled + the tagged-union contract +
  the kcdx-owns-the-read-family constraint), §8 P3 (outcome recorded), §8 closing line.
- **Capture:** `_research/probe-archive/p3-off-vtable-handle-rep.md` (the finding +
  the cited recon; no in-source probe was written, so no residue to remove).

## 2026-06-15 — v1.3 §6 assumes-discharge: vanilla pak format CONFIRMED standard PKZIP (step 2.2)

- **§6 `assumes — vanilla pak format uniformity` is DISCHARGED** — an assumption
  became confirmed evidence. The clause previously asserted vanilla game paks are
  standard PKZIP from the engine's ZipDir being a standard-PKZIP parser, without
  having read a vanilla pak's on-disk bytes. A fresh static on-disk read of real
  vanilla `<game>/Data/*.pak` files this session confirms it directly: 8 paks all
  begin `PK\x03\x04`; GeomCaches.pak head + EOCD decode to standard PKZIP
  (`PK\x05\x06` EOCD, no zip64, no encryption, STORED + DEFLATE, unsigned CDR).
  Capture: `_research/probe-archive/vanilla-pak-format-confirmed.md`.
- **Made a STANDING assertion:** the `cap-110-pak-cdr-parse` test plugin's
  format-uniformity check scans several vanilla paks at boot — a future game
  version that changes the pak format trips the regression row rather than
  silently corrupting a read. The one-time static format-confirm read is no longer
  the only guard.
- **Integrated in:** §6 (the `assumes` clause marked CONFIRMED with the capture
  cite + the standing-assertion note).

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
