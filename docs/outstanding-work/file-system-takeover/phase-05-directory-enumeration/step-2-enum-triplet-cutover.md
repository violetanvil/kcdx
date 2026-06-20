# Step 5.2 — build slots 63/64/65 + cut over the table-DB glob + cap-118

**What.** Build kcdx's `FindFirst`/`FindNext`/`FindClose` triplet (slots 63/64/65,
vtable +0x1F8/+0x200/+0x208) as real KCDX impls over the unified set, flip the three
per-slot table rows `THUNK→KCDX`, and ship the cap-118 regression plugin. This is
the surface the table-DB override-glob (`Libs/Tables/<base>__*.<ext>`) and the
engine's general by-name directory listing dispatch through — the gap that fails the
table-database load (KI-0027).

- **slot-63 `FindFirst`** — resolve the pattern's directory prefix (slot-1
  resolution, §5), open a kcdx-owned find-handle (an iterator state in a kcdx handle
  pool — the SAME handle-id discipline as `FOpen`, §4.4), seed it with the UNIFIED
  entry set for the prefix: the engine's on-disk entries (kcdx's own
  `_wfindfirst64`-class walk on kcdx's CRT) UNION the unified index's pak-resident
  vpaths under the prefix the disk walk cannot see (loose overrides are real disk
  files the walk already surfaced — the loose-skip is the de-dup, exactly as slot 14
  does it). Fill the caller's find-data (the layout step 5.1 settled) with the first
  entry; return the handle.
- **slot-64 `FindNext`** — advance the kcdx find-handle to the next unified-set
  entry, fill the caller's find-data, return the continue/exhausted signal the
  engine consumer loops on.
- **slot-65 `FindClose`** — release the kcdx find-handle.

The engine NEVER operates the find-handle — it holds the kcdx-minted handle and
passes it back to kcdx's `FindNext`/`FindClose`, the cradle-to-grave ownership the
read family has (§4.4). NO engine `CCryPakFindData`, NO engine-CRT iterator state in
kcdx's path (the thunk-and-augment shape is REJECTED, design §5.1 — it re-threads
engine-CRT-allocated iterator state back into kcdx's path, the cross-runtime sharing
the takeover eliminates).

**Scope.** The slot-63/64/65 impls + their kcdx find-handle pool, the three per-slot
table-row flips, and the cap-118 `test-plugins/` regression plugin + its
`test-plugins/README.md` matrix row. One commit. The find-data is filled to the
layout step 5.1 captured (`.claude/rules/spec-conformance.md` — build to the read
ABI, not a guess).

**Touches existing code** — the per-slot vtable table (`src/fs_takeover/vtable_table.cpp`)
+ the enumeration impl unit (`src/fs_takeover/enum_slots.*`, which already carries
slot 14's union-enum). Grep every caller/reader of the table rows + the enum unit's
public surface before changing; the impact-analysis surfaces whether step 3.3's
slot-15/101 THUNK rows are touched (they are NOT — 101 stays THUNK per design §4.5;
3.3 already probe-confirmed 101's engine iterator walks the pak-dir itself,
`_research/fs-takeover-slot101-callers-recon/`). The find-handle pool reuses the
handle-id discipline from step 3.2's read-family handle pool, not a new mechanism.

**Design authority.** Built to `docs/design/file-system-takeover.md` §5.1 (the
iterator-triplet ownership over the unified set, the kcdx find-handle lifecycle, the
rejected thunk-and-augment), §4.5 (the slot set — 63/64/65 KCDX, 101 THUNK), §4.4
(the handle-id discipline the find-handle reuses), §4.3 (the per-slot table flip).
Builds to those sections, not this doc's summary (`.claude/rules/spec-conformance.md`).

**Test bar.** cap-118 regression plugin: a directory glob over a prefix that has BOTH
a pak-resident entry and (where applicable) a loose override resolves the expected
unified entry set — a falsifiable matrix row over the iterator (the entry set
enumerated == the union, loose-skip de-duped; the `__*`-style override the table
loader globs IS surfaced). `.claude/rules/test-suite.md` (next free = cap-118).
Build green is necessary, not sufficient — the live gate is the boot reaching the
world with no `err_id=259` (the gate below, agent-read from `kcdx-dev.log`).

**Dependencies.** Step 5.1 (the find-data layout this mints to) + Phase 2 step 2.4
(the unified index the iterator walks) + Phase 3 step 3.2 (the kcdx handle-pool
discipline + slot 14's union model this mirrors). Ordered AFTER 5.1 per
`.claude/rules/incremental-delivery.md` — the build cannot mint a correct find-data
until 5.1 reads its layout. Independently verifiable once it lands: cap-118 + the
KI-0027 repro both exercise it at this step.

**Game-function evidence.** Slots 63/64/65 dispatch offsets (+0x1F8/+0x200/+0x208)
are read from the binary (`_research/ki0027-table-glob-dispatch-recon/FINDINGS.md`,
gated-verifier confirmed); the find-data layout is step 5.1's captured ABI. No
hardcoded address — the CCryPak object resolves via `*(gEnv+0x50)` as the rest of
the takeover does (AP1). No NEW Address Library seed row is needed (the triplet is
reached by vtable slot off the existing CCryPak object, not by a named address
resolve) — if one IS proposed during the build, it is AP18-gated (user approval
before the row lands), surfaced, never auto-written.

**Reference.** [`../plan-spec.md`](../plan-spec.md); design §5.1, §4.5, §4.4, §4.3;
`_research/ki0027-table-glob-dispatch-recon/FINDINGS.md` (the verified dispatch
through 63/64/65 + the `DAT_18492b850` == CCryPak singleton proof + the front1
slot-label correction); the find-data layout from step 5.1's recon dump.

**Disassembler-test / author-burden.** N/A — engine-internal slots; no author-facing
input (the modder declares nothing; kcdx serves every directory enumeration
transparently, the §1 invariant).
