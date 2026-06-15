# RESUME — file-system-takeover, exact pickup point (2026-06-15)

The durable handoff so work resumes EXACTLY here after the KI-0025 DB-integrity
detour. Read this first on pickup; it is the source of truth, not conversation
memory (`.claude/rules/plan-persistence.md`).

## Where we are

**Phase 2 — DONE** (`63daea9`): pak reader + unified asset index. All 4 steps landed.

**Phase 1 — built + live-proven on its seating half** (`3be161a`, run 2026-06-15):
the stub-vtable swap seats, the engine dispatches into kcdx (P2), the game boots
through the 101 thunked slots (P4, cap-108 PASS). The phase-grain row reads NOT
STARTED only because its KI-0019/KI-0006 *resolution* half stays open until step
4.2 — that is the deliberate documented two-gate state, NOT unbuilt work.

**Phase 3 — step 3.1 DONE** (`4f2c32d`, backfill `4e817ed`): PROBE P3 resolved.
Handle representation SETTLED = a kcdx **handle-id** (design §4.4), safe because
the read family is kcdx-owned (never thunked). Steps 3.2–3.6 NOT STARTED.

## What we were doing when we paused (the live thread)

Driving **Phase 3 step 3.2 (open slots)**, with two user decisions already made
that RESHAPE the build:

1. **Merge 3.2 + 3.3 into ONE atomic open+read cutover** (user-chosen). Reason:
   3.2 mints kcdx handle-ids; if the read family were still thunked, a thunked
   read slot would operate a kcdx handle-id on the ENGINE's CRT (the exact
   cross-CRT crash). So open + read flip to KCDX together, in one cutover.
2. **Build only on verified ABIs** (user-chose "extract ABIs first"). That ran
   `/research-disassembly`, which produced:

### The slot-map reconciliation (DONE, committed `4ca0bae`)

`_research/fs-takeover-slot35-recon/FINDINGS.md` — the authoritative
slot→FUN→role→ABI map, front1-vs-front3 conflict resolved against the binary,
gated-verifier confirmed. Key outcomes that bind the build:

- **front1 (and design §4.5) is authoritative; front3 mislabeled read-family
  ROLES.** slot 40 = FGetCachedFileData (not FRead), slot 39 = FReadRaw (not
  FEof/FTell), slot 38 = **FReadRaw-by-pak-index (a READ, NOT an open)**.
- **Slot 38 is a READ slot** → it belongs to the read family (was-3.3), NOT the
  open slots (was-3.2). **§4.5 has a wording defect** (it groups slot 38 under
  "Open") — correct it during the cutover.
- **Open family = slots 1 + 35 + 36** (NOT 1/35/36/38). Read family = 38/39/40/
  41/53/54/55/56 (+ variants).
- **Slot 35 FOpenRaw ABI freshly dumped + verified**: 5-arg __fastcall
  `FILE*-like(this, pName, szMode, outResolvedBuf, int bufCap)`, `_wfopen`-backed
  via FUN_1809b2b28, resolves via slot 1. RVA 0x2418DE4.

### Seed-row status

- slot 1 (AdjustFileName) = seeded **id 152**; slot 36 (FOpen) = seeded **id 131**
  — resolve by name, ready.
- slot 35 (FOpenRaw) = NO seed row → **BLOCKED by KI-0025** (see below).

## THE BLOCKER — KI-0025 (why we paused)

Adding the slot-35 seed entity `CCryPak_FOpenRaw` (kcdx_id=160) via
`/add-db-entity`: the row validated clean at PREVIEW (`valid:true`) but
`/confirm` FAILED the whole-DB integrity gate and ROLLED BACK (nothing written):
`survival_derives_from kcdx_id=12 has no curated address_versions row`. This is a
PRE-EXISTING DB inconsistency (kcdx_id 9/12/23 gEnv-resolver anchor family), NOT
the FOpenRaw row — and it blocks ALL new-entity adds. Full diagnosis +
fix-direction: **`docs/known-issues/KI-0025-refdb-dangling-survival-derives-from-kcdx-id-12.md`**.

**User's decision at the pause:** "Pause DB work; investigate the kcdx_id 9/12/23
integrity defect separately, defer FOpenRaw + the build." (The CCryPak_FOpenRaw
verified fact is preserved in `4ca0bae`'s FINDINGS, seed-ready, to re-add once the
DB is consistent.)

## EXACT next action on pickup (after KI-0025 is fixed)

1. **Fix KI-0025** first (a maintainer/GUI UPDATE to the kcdx_id 9/12 linkage —
   out of `/add-db-entity` scope; investigate the integer-version-id-vs-tag
   mechanism, do NOT hand-edit CSV / force the write).
2. **Re-add CCryPak_FOpenRaw** (kcdx_id=160) via `/add-db-entity` — the row body
   is ready (`/tmp/foparaw-entity.json` shape; re-derive from FINDINGS if gone):
   kind=function, module=WHGame.dll, rva=0x2418DE4,
   signature='ptr (ptr, cstr, cstr, ptr, i32)', valid_from_version=1.5.1164953,
   audit trio = VioletAnvil / 2026-06-15 / maintainer_ghidra. AP18 user-approval
   already GIVEN this session — re-confirm on re-add.
3. **`/plan`-update the takeover tree** (user-chosen, was the next step): merge
   3.2+3.3 into one open+read cutover step; move slot 38 to the read family;
   correct §4.5's slot-38 "Open" grouping; record the slot-35 ABI + the merged-step
   decision in plan-spec/design (`.claude/rules/decision-capture.md`).
4. **Execute the cutover**: flip open (1/35/36) + read (38/39/40/41/53/54/55/56)
   slots THUNK→KCDX; wire `BuildAssetIndex` into the seating path (NOT yet called —
   `seating_hook.cpp` only swaps the vtable today); mint handle-ids + operate them
   on kcdx's CRT; full open→read regression plugin (next free = **cap-113**).
   This is the change that closes the cross-CRT read class (KI-0019/KI-0006 at 4.2).

## Owed follow-ups (don't lose these)

- **OWED test plugin** for CCryPak_FOpenRaw once seeded (the cutover's slot-35
  exercise satisfies it) — `policy.md` test-plugin requirement.
- **cap-108–112 PASS rows** still `[unverified — pending launch]` (Phase 2 + 3.1
  seating) — confirm at the next `/verification-checkpoint` launch.
- **§4.5 slot-38 wording fix** (groups a read slot under "Open").

## Commit trail (this thread)

`4f2c32d` (3.1 P3) → `4e817ed` (3.1 backfill) → `4ca0bae` (slot-map recon +
slot-35 dump). DB add: NOTHING written (KI-0025 rollback). Working tree otherwise
carries unrelated parallel-chat dirty files — stage by exact path only.
