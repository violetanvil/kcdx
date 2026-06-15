# Plan spec — file-system takeover

Shared spec for the `file-system-takeover` plan. Every step doc leans on this;
steps cross-link here rather than restating shared context.

## Goal

kcdx takes TOTAL ownership of the engine's `CCryPak` file object — every file
call in WHGame dispatches into kcdx, every handle is minted/read/sought/closed on
kcdx's own CRT cradle-to-grave — eliminating the cross-CRT `FILE*` crash class
(KI-0019/KI-0006) structurally and reading both vanilla paks and loose mod files
itself.

## Settled design (the authority)

**`docs/design.md`'s sibling design doc: [`docs/design/file-system-takeover.md`](../../design/file-system-takeover.md)** (committed `29a21c5`)
is the authoritative spec. Every step builds to THAT doc's relevant section, not
to this plan's prose summary (`.claude/rules/spec-conformance.md`). The settled
decisions, verbatim with their design source:

- **Total takeover** (design §1) — kcdx IS the filesystem; no line straddled.
- **Full vtable-pointer swap** (design §4.1) — kcdx builds its own `CCryPak`
  vtable and swaps the pointer in the live object at `*(gEnv+0x50)`.
- **Per-slot declarative vtable table** (design §4.3) — each of the 102 slots is
  one row, `KCDX(&fn)` or `THUNK(original)`; flipping a thunk→kcdx is a one-line
  edit (the reversibility constraint — do not code into a box).
- **kcdx serves every read on its own CRT** (design §4.4) — the read family is
  kcdx's; the engine never operates a kcdx handle.
- **kcdx's own PKZIP/DEFLATE reader** (design §6) — vanilla + mod paks read by
  kcdx, no engine ZipDir in the path.
- **One unified asset index built at load** (design §5) — vpath → ByteSource;
  one O(1) lookup per open, precedence decided once at index-build.
- **Own track; home for KI-0019/KI-0006** (design §9) — decoupled from Phase 11.

## Settled plan-decomposition decisions (this plan's structuring calls, user-decided)

- **Stub-vtable spike FIRST** (Phase 1) — a minimal all-thunks vtable + one
  marker proves the seating (P1/P2) and thunk-compat (P4) before the ~30 real
  file slots are built. The real slots build on proven ground.
- **The plan owns the in-flight cleanup** — KI-routing correction (1.1) +
  PROBE-F removal (1.2) are early steps; the `asset_overlay.cpp` seam subsumption
  (3.6) is its own step in the phase that lands the replacement (no coverage gap —
  the seam is removed only once its kcdx-slot replacement is live).
- **Dedicated DEFLATE-dependency step** (2.1) — the zlib-vs-miniz pick is
  surfaced + decided AT that step (license-checked, recorded per
  `.claude/rules/dependencies.md`), not pre-baked into this plan.

## The four probes (design §8) — provisional mechanisms proven before their dependent phase

Each probe is an early step ordered before the phase that rests on it
(`.claude/rules/incremental-delivery.md` + `.claude/rules/results-driven.md`). The
design is provisional on each until its probe lands.

- **P1 — CCryPak construction timing** (step 1.3) — when `*(gEnv+0x50)` is first
  constructed + that kcdx's ready-bracket runs in the window [constructed, first
  file call]. Gates the swap-seating step.
- **P2 — vtable-swap acceptance** (step 1.4) — the swap holds + every consumer
  dispatches into kcdx. Gates the real-slot build.
- **P3 — off-vtable raw-handle access** (step 3.1) — RESOLVED (static binary read,
  outcome 1): no off-vtable access to a `FOpen`-class handle (the read family
  dispatches on the handle tag through the vtable; the one off-vtable raw op is on an
  engine-minted pak-MOUNT handle, not `FOpen`). Settled the kcdx handle
  representation = a kcdx handle-id (design §4.4). Capture:
  `_research/probe-archive/p3-off-vtable-handle-rep.md`.
- **P4 — thunked-slot `this`-compat** (step 1.4, proven alongside P2) — a thunked
  original slot runs correctly against the swapped object (the object layout is
  preserved because kcdx swaps only the vtable pointer). Gates the thunk approach.

## Cross-step invariants

- **No kcdx handle is ever operated by the engine's CRT** — the load-bearing
  invariant the whole takeover protects (design §9). A step that mints a kcdx
  handle the engine could `fseek`/`fclose` reintroduces the crash class.
- **The kcdx handle is a handle-id honoring the engine's tagged-union tag; the read
  family is kcdx-owned, never thunked** (P3-resolved, design §4.4). 3.2 mints a kcdx
  handle-id distinguishable by the engine's dispatch test (`index+1` = pak entry;
  else = real-`FILE*`-class); every handle-operating read slot (3.3/3.5) stays
  `KCDX`, never `THUNK` — a thunked read slot would `fread` the kcdx handle-id on the
  ENGINE's CRT (the cross-CRT straddle). This is the one §4.3 thunk-flip the per-slot
  table forbids while the handle is a kcdx-minted id.
- **The per-slot table is the single point of slot ownership** — no code outside
  the table assumes "slot N is the engine's" (design §4.3). Reviewed each slot
  step.
- **Every byte kcdx serves is on kcdx's CRT** — pak (kcdx's PKZIP reader) and
  loose (kcdx's `_wfopen`) alike.
- **The author-facing contract is unchanged** — the `assets/` folder, sidecars,
  published names, cross-mod references all resolve identically; only the
  engine-side seam changes (design §7, carried from `asset-replacement.md`).
- **No hardcoded game addresses** — every game-binary target resolves by
  name/id through the Address Library (AP1); a new entity is AP18-gated (user
  approval before a seed row lands).

## Coverage map — every design element → its step

Design elements enumerated by walking `docs/design/file-system-takeover.md`
section by section (the raw artifact, per `.claude/rules/spec-conformance.md`).

| Design element | Covered by | Notes |
|---|---|---|
| E1 — DEFLATE inflater dependency (zlib/miniz pick, license-checked) | Step 2.1 | design §10, §6; library pick surfaced at the step |
| E2 — kcdx's own PKZIP central-directory parser | Step 2.2 | design §6 |
| E3 — kcdx's DEFLATE read path, every byte on kcdx CRT | Step 2.3 | design §6 |
| E4 — the per-slot declarative vtable table (102 rows) | Step 3.5 | design §4.3 (finalized when the last real slots land) |
| E5 — the vtable-pointer swap at init | Step 1.4 | design §4.1 (stub-vtable spike) |
| E6 — the unified asset index (vpath → ByteSource) | Step 2.4 | design §5 |
| E7 — index-build: vanilla-pak discovery + CDR population | Step 2.4 | design §5, §6 |
| E8 — index-build: loose-override + mod-pak sources | Step 2.4 | design §5, §7 |
| E9 — kcdx slot-1 AdjustFileName impl (index lookup) | Step 3.2 | design §4.5, §5 |
| E10 — kcdx open slots (36/35/38) minting kcdx handles | Step 3.2 | design §4.5, §4.4 |
| E11 — kcdx read family operating handles on kcdx CRT | Step 3.3 | design §4.5, §4.4 — the cross-CRT class dies here |
| E12 — kcdx handle representation (settled by P3) | Step 3.1 | design §4.4 |
| E13 — kcdx existence/metadata slots (13/45/67/68/69/70/92/93) | Step 3.4 | design §4.5 |
| E14 — kcdx directory-enum slots (14/15/101) | Step 3.4 | design §4.5 |
| E15 — kcdx pak/archive-mgmt slots (7/17/32/33/34/71/72/91/100) | Step 3.5 | design §4.5 |
| E16 — kcdx search-path/alias/mods slots (19–24/94) | Step 3.5 | design §4.5 |
| E17 — kcdx delete/copy slots (49/50/52) | Step 3.5 | design §4.5 |
| E18 — thunk-to-original wiring for the pure-internal slots | Step 3.5 | design §4.3, §4.5 |
| E19 — P1 CCryPak construction-timing probe | Step 1.3 | design §8 |
| E20 — P2 vtable-swap-acceptance probe | Step 1.4 | design §8 |
| E21 — P3 off-vtable raw-handle-access probe | Step 3.1 | design §8 |
| E22 — P4 thunked-slot this-compat probe | Step 1.4 | design §8 (alongside P2) |
| E23 — KI-0019/KI-0006 resolution + closure | Step 4.2 | design §9 |
| E24 — author-facing contract preserved | Step 4.1 | design §7 (verified by the regression rows) |
| E25 — subsume the asset_overlay.cpp two-hook seam | Step 3.6 | design §11, §1 |
| E26 — PROBE F removal + capture | Step 1.2 | design §11 |
| E27 — vanilla-pak format-uniformity static check | Step 2.2 | design §6 |
| E28 — regression test plugin(s) + matrix rows | Step 4.1 | `.claude/rules/test-suite.md` |
| E29 — file-system subsystem reference doc | Step 4.1 | `.claude/rules/structure-by-responsibility.md` |
| E30 — KI-0019/KI-0006 routing correction | Step 1.1 | design §11 (repoint the KI routing from Phase-11 to this design) |

No element is DEFERRED or OUT-OF-SCOPE — every one (E1–E30) resolves to a step. The
design's own v1 deferrals (reimplementing thunked slots; pak-writing — design
§10) are outside the design's v1 scope and so are not plan elements; this plan
covers exactly the design's v1.

## Reference

- Design: [`docs/design/file-system-takeover.md`](../../design/file-system-takeover.md) + its changelog.
- Recon substrate: `_research/phase8.5-pak-resolver/` (102-slot vtable map, handle-tag read path, PKZIP format), `_research/ki0019-inventory-av-recon/` (the crash mechanism), `_research/init-cycle-recon/` (the C_ModManager timing reference — note: CCryPak timing is P1, a different object).
- Bugs this closes: [KI-0019](../../known-issues/KI-0019-ccrypak-fopen-reentrancy-av-on-inventory-open.md), [KI-0006](../../known-issues/KI-0006-serve-execute-vehicle-not-found.md).
