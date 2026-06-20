---
id: KI-0027
opened: 2026-06-20
status: closed
commit_at_filing: 68cf9f6
closed: 2026-06-20
closed_by_commit: 4befc07
---

# Table-DB load fails: kcdx's fs-takeover does not serve wildcard-glob directory enumeration (`<table>__*.xml` override discovery)

**Status:** closed

The engine's table-database load aborts with a fatal dialog — *"Database system error -
tables can't be loaded. Possibly caused by outdated or corrupted mods. See kcd.log"* —
because kcdx's file-system takeover serves single-file reads but NOT the engine's
**directory-enumeration / wildcard-glob** queries. The table system discovers its
override-patch files by globbing `Libs/Tables/**/<table>__*.xml` (the `__` =
table-merge override naming); the engine opens a path containing a literal `*` and
expects the FS layer to enumerate the matching pak-resident entries. kcdx's slot-1
`AdjustFileName` treats `<table>__*.xml` as a literal filename → miss; no enum slot
(`ForEachFile`, slots 15/101 — deferred THUNK) ever fires → the engine gets zero
override files where it expects an enumeration → the DB load aborts (`err_id=259`).

This is the **`ForEachFile`/`FindFiles` directory-enumeration takeover surface**, which
the prior fs-takeover work deliberately DEFERRED (cap-114 notes: slots 15/101 stayed
THUNK, "a separable concern surfaced as decisions"). The table-DB load is its first
hard consumer.

## Relationship to KI-0026

KI-0026 (graphics-init `0xC8` CryFatalError) was filed for the NGX/FSR2 graphics-init
crash. That crash is now FIXED + verified by three landed fixes (Data+Engine index
coverage + `%engine%` alias ownership → cap-115; slot-46 `FGetSize` returns size →
cap-116; `rbx` loose-open mode sanitize → cap-117), and the boot now passes graphics-init
entirely. KI-0026's three fixes unblocked the boot far enough to REACH this table-DB
layer — a distinct subsystem (enumeration vs single-file read), a different fatal
(`err_id=259` ≠ `0xC8`), so it is tracked here as its own issue rather than as a fourth
mechanism under KI-0026.

## Symptom

- Engine fatal dialog: "Database system error - tables can't be loaded. Possibly caused
  by outdated or corrupted mods. See kcd.log for more details."
- `kcd.log` tail: full engine init completes (Cry3DEngine, machine-spec autodetect,
  CryScriptSystem, CryEntitySystem, AI, DRS, 1623 prefab templates, Pros init, Ansel,
  "Sign in the default user"), `'Tables.pak' 7210465 B` / `Pak 'data\tables.pak' is
  opened, root: 'data\'`, then the DB error.
- `kcdx-dev.log` (`kcdx-dev_2026-06-20_13-30-23.log`): `fatalerror_fired err_id=259
  err_id_hex=259 boot_window=yes` (the engine `CSystem::FatalError`, distinct from the
  `0xC8`=200 KI-0026 family).

## Facts

- The single-file read path is CORRECT: 288 `read_entry pak="Tables.pak"` succeeded;
  base tables (e.g. `libs/tables/rpg/gender.xml`, `libs/tables/minigame/blacksmithrecipes.xml`)
  resolve `how=index-pak-serve` and read clean. cap-111 PASS (a Tables.pak DEFLATE entry
  inflates CRC-correct). (PROBE — `kcdx-dev_2026-06-20_13-30-23.log`)
- The GLOB path is UNSERVED: **292** opens of the form `libs/tables/**/<table>__*.xml`
  (a literal `*` in the path) ALL hit `how=miss-original result=0`, and there are **ZERO
  `FS_ENUM`/`ForEachFile` enum-slot fires** in the entire boot. (PROBE — same log)
- The override-discovery pattern is the engine globbing `<base>__*.xml` after loading
  `<base>.xml` (the `__`-suffixed table-merge override convention — confirmed by the
  sequence: `gender.xml` HIT → `gender__*.xml` miss-original; `blacksmithrecipes.xml`
  HIT → `blacksmithrecipes__*.xml` miss → `blacksmithrecipes__autotests.xml` HIT when
  named literally). (PROBE — same log)
- Slots 15 (`ForEachFile` callback dispatcher) and 101 (iterator lifecycle) were left
  THUNK in the step-3.3 metadata/enum work, surfaced as deferred decisions (cap-114
  matrix notes). (FACT — `test-plugins/README.md` CAP-114 Notes)
- **The override-glob dispatches through the `FindFirst`/`FindNext`/`FindClose` handle
  triplet at vtable +0x1F8 / +0x200 / +0x208 (slots 63 / 64 / 65) — NOT slot 14
  `ForEachFile` (already kcdx-owned) and NOT slot 101 `FindFirst`/`CCryPakFindData`
  (the iterator-OBJECT factory).** Read directly from the table-loader glob body
  `FUN_180974484`: it builds the `<base>__*.<ext>` pattern, then
  `(**(*pak+0x1F8))(pak, pattern, findData, 0)` = FindFirst → `(**(*pak+0x200))(pak,
  handle, findData)` = FindNext loop → `(**(*pak+0x208))(pak, handle)` = FindClose. The
  CCryPak singleton is `DAT_18492b850` (== the `*(gEnv+0x50)` object). (PROBE — fresh
  Ghidra, `_research/ki0027-table-glob-dispatch-recon/FINDINGS.md`)
- The triplet (slots 63/64/65) is the engine's GENERAL by-name directory enumeration
  (a second consumer `FUN_18041d238` uses the same triplet for a generic dir listing) —
  so kcdx owning it serves ALL directory enumeration, not only `Libs/Tables`. (FACT —
  same recon)

## Resolution route

This is the deferred `FindFirst`/`FindNext`/`FindClose` directory-enumeration takeover
surface (slots 63/64/65) — already named in the file-system-takeover design §4.5
(directory enumeration) under the §1 totalizing invariant (kcdx IS the filesystem). The
fix is a `/design` revision making the iterator-triplet a settled in-v1 part of the
takeover (kcdx mints + owns the full `FindFirst`-handle lifecycle, walking the unified
pak+loose+overlay set), then a `/plan`+`/feature` build. Design dialogue in progress
(2026-06-20). The slot-14 `ForEachFile` impl already proves the unified-enum MODEL; the
triplet is the same model behind the engine's stateful handle API the table loader uses.

## Open questions (remaining — for the design)

- The engine impl of the +0x1F8 triplet (`0x180973058` / `0x18041d640` / `0x18097383c`)
  was not decompiled — whether engine FindFirst walks pak dirs + disk vs disk-only is a
  separate unread fact. (Not load-bearing for the kcdx design: kcdx's own impl walks the
  unified set regardless; this only affects the engine-behavior DESCRIPTION.)
- The find-data buffer ABI the triplet fills (`local_158`, 36+ bytes — name + attrs the
  caller reads, e.g. the `& 0x10` directory bit + the name bytes at +0x36ish) needs its
  exact layout read before kcdx mints a compatible find-data the engine consumes.

## Resolution

**Root cause (the mechanism):** the engine's table-database loader discovers per-table
override patches by globbing `Libs/Tables/<base>__*.<ext>` and dispatching that glob
through the CCryPak `FindFirst`/`FindNext`/`FindClose` handle-iterator triplet at vtable
+0x1F8/+0x200/+0x208 (slots 63/64/65 — body-verified in the loader `FUN_180974484`,
`_research/ki0027-table-glob-dispatch-recon/`). kcdx's file-system takeover swapped the
CCryPak vtable but left those three slots THUNKED to the engine original (Phase 3's
step 3.3 owned only slot 14 `ForEachFile`). The engine's own FindFirst walks only its
on-disk view — it cannot see kcdx-served pak-resident entries — so for every
`<base>__*.xml` glob it returned zero matching override files. The table loader's worker,
finding no overrides where the merge convention expects an enumeration result, returned
false; `CSystem::FatalError` then raised `err_id=259` ("Database system error - tables
can't be loaded"). The original code path made this inevitable because the takeover owns
single-file reads (slot 1/36) but the *enumeration* slots the glob dispatches through
were never flipped to kcdx — a takeover-incompleteness, not a read defect.

A SECOND mechanism surfaced at the first live launch (captured
`_research/ki0027-find-data-abi-recon/LIVE-GATE-1-mask-blind-index-arm.md`): once kcdx
owned the triplet, `BuildUnifiedFindEntries`'s index arm matched candidate pak vpaths
against the directory PREFIX only and dropped the pattern's filename glob mask, so
`Libs\Tables\rpg\gender__*.xml` returned all 528 `Tables.pak` entries under
`libs/tables/rpg/` instead of the `gender__*`-matching subset. The engine then tried to
merge 528 unrelated tables as gender overrides → corrupt merge → the same DB error. The
disk arm always filtered correctly (it passed the full pattern to `_wfindfirst64`); the
index arm was asymmetric — it never applied the mask.

**Fix:** kcdx mints + owns the full `FindFirst`-handle lifecycle over the unified
pak+loose+overlay set (slots 63/64/65 flipped THUNK→KCDX,
`src/fs_takeover/find_slots.cpp`), filling the find-data to the verified P5 ABI (attr
byte @0x00 with bit 0x10 = directory, entry name inline NUL-terminated C-string @0x24).
The index arm now applies the pattern's filename glob mask (`IndexNameMask` +
`WildcardMatch`), so both the disk and index arms honor the same glob — the symmetry the
over-match broke. fs-takeover Phase 5, design `docs/design/file-system-takeover.md` §5.1
(v1.9). Commits: `a414d75` (the triplet) + `4befc07` (the mask fix). cap-118
regression plugin asserts the unified-enumeration core + the find-data ABI + the
restrictive-mask filter (assertion f — a `<base>__*` glob returns only the matching
subset, FAILS on any non-match emitted).

**Verification (live, `kcdx-dev_2026-06-20_15-58-02.log` + `kcdx_2026-06-20_15-58-02.log`):**
ZERO "Database system error" / `err_id=259` (the fatal is gone); 194+ engine `FindFirst`
dispatches through kcdx's slot 63 with CORRECT match counts (`matched=0` for vanilla
`__*` globs, not the pre-fix 528 whole-directory over-match); the test suite ran to
`passing=320/343`. The table-database load SUCCEEDS — the named mechanism is resolved.

The boot now proceeds past the table-DB layer and hangs at a DISTINCT downstream point
(UI/render bring-up — sound loads, no video, no input), tracked separately as
**[KI-0028](KI-0028-fs-takeover-boot-hang-ui-render-init.md)**. That hang is a new
subsystem (render/UI, not filesystem) the KI-0027 fix unblocked the boot far enough to
reach — the KI-0026→KI-0027→KI-0028 chain — NOT a regression of this fix (the
enumeration is verified functioning). KI-0027's closure is on its named mechanism (the
unserved `__*` glob), now resolved; reaching the menu is KI-0028's concern.

The two design `## Open questions` above are both DISCHARGED: the find-data buffer ABI
was read (P5, `_research/ki0027-find-data-abi-recon/FINDINGS.md` — attr@0, name@0x24);
the engine-FindFirst pak-vs-disk behavior was design-marked non-load-bearing (kcdx's impl
walks the unified set regardless).
