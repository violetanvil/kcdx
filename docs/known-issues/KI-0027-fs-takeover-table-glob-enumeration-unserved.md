---
id: KI-0027
opened: 2026-06-20
status: open
commit_at_filing: 68cf9f6
---

# Table-DB load fails: kcdx's fs-takeover does not serve wildcard-glob directory enumeration (`<table>__*.xml` override discovery)

**Status:** open

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

## Open questions

- Which CCryPak slot(s) does the engine's table-override glob dispatch through — a
  `FindFiles`/`FindFirst`-class slot, the `ForEachFile` slots 15/101, or a different
  enumeration entry point? (A static read of the table-loader's discovery call + a live
  enum-slot trace settles it — this is the design input for serving glob enumeration
  over the unified index.)
- The unified index already holds every pak-resident entry by normalized vpath; serving
  a `<dir>/<prefix>__*.xml` glob is a prefix/pattern match over the index key space plus
  the loose + overlay layers. How does the enumeration merge pak + loose + overlay
  (mirroring `ResolveVPath`'s single-file precedence), and what does it return to the
  engine's iterator (the `ForEachFile` callback shape)? (Design — route to `/design` /
  `senior-architect-consult`; the enum-takeover subsystem was explicitly deferred.)
- Does the engine also enumerate non-table directories at boot (the glob surface may be
  broader than `Libs/Tables`)? (Scan the boot log's full `*`-path set.)
