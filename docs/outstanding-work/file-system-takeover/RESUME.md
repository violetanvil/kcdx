# RESUME — file-system-takeover, exact pickup point (updated 2026-06-16)

The durable handoff so work resumes EXACTLY here. **Next action: EXECUTE step
3.4** (pak-mgmt/search-path/delete slots + finalize the per-slot table). Steps
3.1/3.2/3.3 are DONE — the open/read/existence/metadata/enumeration families are
kcdx-owned. The phase-03 ledger ([phase-03-file-slots/README.md](phase-03-file-slots/README.md))
is the live per-step status; this doc's lower sections are the historical
rationale digest for how 3.1–3.3 landed (kept as the record, not the next step).
Read this + the ledger first on pickup; they are the source of truth, not
conversation memory (`.claude/rules/plan-persistence.md`).

## Where we are

**Phase 2 — DONE** (`63daea9`): pak reader + unified asset index. All 4 steps landed.

**Phase 1 — built + live-proven on its seating half** (`3be161a`, run 2026-06-15):
the stub-vtable swap seats, the engine dispatches into kcdx (P2), the game boots
through the 101 thunked slots (P4, cap-108 PASS). The phase-grain row reads NOT
STARTED only because its KI-0019/KI-0006 *resolution* half stays open until step
4.2 — that is the deliberate documented two-gate state, NOT unbuilt work.

**Phase 3 — steps 3.1, 3.2, 3.3 DONE; 3.4–3.5 remain.**
- 3.1 (`4f2c32d`, backfill `4e817ed`): PROBE P3 resolved — handle rep SETTLED = a
  kcdx **handle-id** (design §4.4), safe because the read family is kcdx-owned.
- 3.2 (`842e5d5`): open+read cutover — open (1/35/36) + read (38/39/40/41/53/54/
  55/56 + variants) flipped THUNK→KCDX together; the cross-CRT class structurally
  removed (KI-0019 clean live, cap-113 PASS).
- 3.3 (`247d295`): existence/metadata (13/45/67/68/69/70/92/93) + enumeration
  (14 ForEachFile) slots own the unified index — index HIT from the ByteSource,
  index MISS thunks the slot's captured original (engine pak-dir AND disk). Slots
  15 + 101 stay THUNK (probe-confirmed clean — the engine CCryPakFindData iterator
  walks the pak-dir itself; `_research/fs-takeover-slot101-callers-recon/`).
  cap-114 regression plugin landed (index core headless; live dispatch pending
  the next launch).
- 3.4 NOT STARTED (next): pak-mgmt/search-path/alias/delete slots + finalize the
  per-slot table. 3.5 NOT STARTED: subsume the `asset_overlay.cpp` seam.

## Decisions that reshaped step 3.2 — now LANDED in the tree

Two user decisions reshaped the build; both are now recorded in the plan tree +
design (this plan-update commit), so the step docs are the source of truth — the
notes below are the rationale digest:

1. **Merge 3.2 + 3.3 into ONE atomic open+read cutover** (user-chosen). Reason:
   3.2 mints kcdx handle-ids; if the read family were still thunked, a thunked
   read slot would operate a kcdx handle-id on the ENGINE's CRT (the exact
   cross-CRT crash). So open + read flip to KCDX together, in one cutover. LANDED:
   merged step doc [step-2-open-read-cutover.md](phase-03-file-slots/step-2-open-read-cutover.md);
   downstream renumbered (existence/enum 3.3, mgmt+table 3.4, seam 3.5).
2. **Build only on verified ABIs** (user-chose "extract ABIs first"). That ran
   `/research-disassembly`, which produced:

### The slot-map reconciliation (DONE, committed `4ca0bae`)

`_research/fs-takeover-slot35-recon/FINDINGS.md` — the authoritative
slot→FUN→role→ABI map, front1-vs-front3 conflict resolved against the binary,
gated-verifier confirmed. Key outcomes that bind the build:

- **front1 (and design §4.5) is authoritative; front3 mislabeled read-family
  ROLES.** slot 40 = FGetCachedFileData (not FRead), slot 39 = FReadRaw (not
  FEof/FTell), slot 38 = **FReadRaw-by-pak-index (a READ, NOT an open)**.
- **Slot 38 is a READ slot** → it belongs to the read family, NOT the open slots.
  §4.5's "Open" grouping of slot 38 was a wording defect — **FIXED in this
  plan-update** (design §4.5 v1.5; slot 38 now in the read family). The merged
  step 3.2 flips it with the read family.
- **Open family = slots 1 + 35 + 36** (NOT 1/35/36/38). Read family = 38/39/40/
  41/53/54/55/56 (+ variants).
- **Slot 35 FOpenRaw ABI freshly dumped + verified**: 5-arg __fastcall
  `FILE*-like(this, pName, szMode, outResolvedBuf, int bufCap)`, `_wfopen`-backed
  via FUN_1809b2b28, resolves via slot 1. RVA 0x2418DE4.

### Seed-row status

- slot 1 (AdjustFileName) = seeded **id 152**; slot 36 (FOpen) = seeded **id 131**
  — resolve by name, ready.
- slot 35 (FOpenRaw) = seeded **kcdx_id 160** ✅ (added `5527f2b`, AP18-approved) —
  resolve by name, ready. (Was blocked by KI-0025; now RESOLVED — see below.)

## THE BLOCKER — KI-0025 (RESOLVED 2026-06-15)

KI-0025 is **CLOSED** (`05f2ff7`; closed/KI-0025-...md). Adding CCryPak_FOpenRaw
tripped a PRE-EXISTING whole-DB integrity break (`survival_derives_from kcdx_id=12
has no curated address_versions row`). Root cause was TWO coupled defects (the
filing's "integer-version-id-vs-tag" guess was FALSIFIED): (1) a batch UPDATE
closed kcdx_id=12's sole interval (a live entity → no open current form), failing
kcdx_id=9's `survival_derives_from=12` edge; (2) the apply path's no-op comparison
eagerly resolved that edge against the open DB and raised, so the reopen couldn't
self-heal. Fixed (`79ea49a` code + validator, `8ec66e3` repair), Gate-B verified.
**Acceptance: CCryPak_FOpenRaw (kcdx_id=160) is now IN the DB (`5527f2b`)** — the
slot-35 seed row is ready to resolve by name. No DB work remains.

## EXACT next action on pickup

KI-0025 is fixed, the slot-35 row is seeded, AND the plan tree is updated —
**execute step 3.2, the open+read cutover.** (Former steps "fix KI-0025",
"re-add FOpenRaw", and "/plan-update the tree" are all DONE.)

**Execute the cutover** ([step-2-open-read-cutover.md](phase-03-file-slots/step-2-open-read-cutover.md)),
built to design §4.5/§5/§4.4/§4.3 (`.claude/rules/spec-conformance.md`):

- Wire `BuildAssetIndex` into the seating path (NOT yet called — `seating_hook.cpp`
  only swaps the vtable today) so slot 1 has an index to look up.
- Flip open (1/35/36) + read (38/39/40/41/53/54/55/56 + variants) slots THUNK→KCDX
  TOGETHER (one atomic cutover — a thunked read slot would operate a kcdx
  handle-id on the engine's CRT; that's why open+read are one step).
- Mint kcdx handle-ids (the P3-settled rep, step 3.1) + operate them on kcdx's CRT.
- Full open→read→close regression plugin, BOTH a Loose and a Pak source (next
  free = **cap-113**) — this also satisfies the OWED CCryPak_FOpenRaw test plugin.
- This is the change that closes the cross-CRT read class; KI-0019/KI-0006 formally
  CLOSE at step 4.2.

Route via `/feature` (the merged 3.2 spans the index-wiring + the slot flips + the
test plugin — a multi-part build in one motion) or `/execute` if it lands as one
commit. The step doc is the `Source work-item`.

## Owed follow-ups (don't lose these)

- **OWED test plugin** for CCryPak_FOpenRaw (kcdx_id=160, now seeded) — the
  cutover's slot-35 exercise satisfies it; `policy.md` test-plugin requirement.
- **cap-108–112 PASS rows** still `[unverified — pending launch]` (Phase 2 + 3.1
  seating) — confirm at the next `/verification-checkpoint` launch.
- **KI-0025 pre-existing reds (NOT this work):** `test_rebuild_oracle` stale
  baseline (157≠159 rows) + `test_importer_blank_signature` kcdx_id=159 fingerprint
  — surfaced during the KI-0025 fix, confirmed outside its blast radius; separate
  seed-drift items, not owed by the takeover.

## Commit trail (this thread)

`4f2c32d` (3.1 P3) → `4e817ed` (3.1 backfill) → `4ca0bae` (slot-map recon +
slot-35 dump). Then the KI-0025 detour: `79ea49a` (fix) → `8ec66e3` (kcdx_id=12
repair) → `5527f2b` (**CCryPak_FOpenRaw kcdx_id=160 ADDED** — the slot-35 row) →
`05f2ff7` (KI-0025 closed). Working tree otherwise carries unrelated parallel-chat
dirty files — stage by exact path only.
