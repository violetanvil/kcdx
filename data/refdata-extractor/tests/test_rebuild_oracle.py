"""test_rebuild_oracle.py -- the behaviour-preserving gate for the seeds_shared
extraction (db-updator Phase 1, step 1).

WHAT THIS PROVES
----------------
This is the rebuild-output gate: a fresh rebuild of reference.sqlite +
reference-dev.sqlite from the local dump at
  data/refdata-extractor/dump/refdata-1.5.1164953
must match, per-table (content hash + row count, including the _dict_* lookup
tables), the recorded baseline snapshot (tests/oracle_baseline.json). It is the
oracle the incremental `apply` path is checked against: apply must produce the
same rows a rebuild would.

BASELINE PROVENANCE
-------------------
The baseline was FIRST captured from the pre-`seeds_shared`-extraction code, to
prove the extraction (db-updator step 1) was byte-for-byte behaviour-preserving
-- which it was. It was RE-CAPTURED at step 4 when the rebuild's promote path was
corrected to gate on kind-class: a non-function curated row whose rva coincides
with a bulk function entry (today: id 8 callsite @ 0x566040, id 17 data_slot @
0xB99098) now MINTS with a NULL fingerprint instead of inheriting the host
function's content_hash+length (a body-hash is not a meaningful survival datum
for a callsite/data_slot -- see data/maintainer-tool/fingerprint-per-kind.md).
That correction intentionally changed the output for those 2 rows (+2 dev rows;
their USER/DEV curated rows lose the wrongly-inherited fingerprint), so the
baseline was re-recorded from the corrected rebuild.

It was RE-CAPTURED AGAIN at step 5.1 when the `survival` sibling table was added:
the rebuild now emits one `survival` row per curated address_versions row (the
per-kind survival datum, kind-discriminated; function kinds reuse the av row's
body fingerprint, the search/derivation kinds carry their seed survival columns
which step 5.2 fills -- empty payload until then). The new table appears in BOTH
DBs (USER + DEV ship the curated survival datum) plus its autoincrement
sqlite_sequence bump, so the baseline gained the `survival` table entry and the
sqlite_sequence hash changed; nothing else moved. The re-capture was deliberate
and inspected (survival rows == curated entity count, correct kind_form per kind,
empty payloads where 5.2 has not filled) before being recorded.

It was RE-CAPTURED AGAIN at step 5.2 on two coordinated changes: (1) a new
`survival_expect_unique` INTEGER column was appended to the `survival` table
(the AOB-unique / unique-xref assertion for the search-locating kinds), and
(2) the 14 verified survival values were transcribed into the seed -- callsite
AOBs (ids 5-8), the `exec autoexec.cfg` string anchor (12), the
instruction-anchor shape + its derives_from (9), the data_slot derivation rules
+ their derives_from chain (10/11/132), and the vtable_base slot counts
(119/138/139/140). So the `survival` table's content hash + column set changed
(the new column + the now-non-empty payload cells on 14 rows); the vtable_index
rows (19-24) stayed empty and nothing outside the `survival` table moved (the
address_versions audit columns were untouched). The re-capture was deliberate
and inspected (the expect_unique column present; the 14 rows carry their values;
vtable_index still empty; function rows still carry the hash) before recording.

It was RE-CAPTURED AGAIN at importer-no-prose-derivation Phase 2 step 2 when the
four AUTHORED per-kind datum seed columns were established: `value` / `offset` /
`vtable_slot` gained an explicit authored seed-CSV column (they already existed
in the DB schema), and a NEW `struct_offset` INTEGER column was added to the
address_versions schema + the engine SELECT. The columns are EMPTY for every
current row (value authoring is Phase 3), so only the `address_versions` table's
content hash moved -- the new NULL `struct_offset` cell on all 143 curated rows
(USER) and all 321140 rows (DEV) -- with NO row-count change, NO table-set
change, and no other table touched. The re-capture was deliberate and inspected
(only address_versions drifted, both DBs, content-hash only; the offset/
vtable_slot/value values are byte-identical because every authored cell is empty)
before recording.

It was RE-CAPTURED AGAIN when the curated set grew 143 -> 157: fourteen
RE-verified curated entities were added across prior cycles (ids 144-157 -- the
SaveGame family 144-148, CLocalizedStringsManager_ctor 149, IConsole_PrintLine/
PrintLinePlus 150-151, the four CCryPak asset-resolution entities 152-155, and
ICVar_GetIVal/GetFVal 156-157), and the `vtable_slot` authored column was filled
on the vtable_index rows (ids 19-24, 152) from the binary. The committed seeds
are ground truth; the rebuild reproduces them, so the baseline was re-recorded at
157. The drift was inspected + fully explained before recording: USER
address_names/address_versions/survival 143 -> 157 (the 14 added curated rows;
survival is 1:1 with curated), the sqlite_sequence high-water bump, DEV
address_versions +4 (minted-no-rva 20 -> 24), and -- the only initially-surprising
lines -- DEV statements/referenced_vars/call_edges content-hash drift at UNCHANGED
row counts: those bulk tables carry a nullable `kcdx_id` cell set from
`rva_to_kcdx_id`, which flips NULL -> id for the 14 newly-curated functions
(present in the bulk dump), a mechanical consequence of curating dump functions.
NO table that should be untouched moved (modules / meta / game_versions / every
_dict_* table are byte-identical). Coordinated with a one-time seed normalization:
`address_versions_seed.csv` ids 156/157 carried cosmetic quotes around a
comma-free signature (`"i32 (ptr self)"`) that QUOTE_MINIMAL does not emit; the
seed was re-exported to the canonical form (the four quote chars dropped, RE facts
byte-identical) so the export/round-trip oracles round-trip.

It was RE-CAPTURED AGAIN at the schema-flatten-survival-fold Phase 1 step 1 when
the six folded survival columns were added to the `address_versions` schema
(D22 / design §11.2 -- the additive FIRST move of folding the `survival` sibling
table into `address_versions`): `aob` / `anchor_string` / `rule` TEXT,
`slot_count` / `expect_unique` INTEGER, and `derives_from` INTEGER (a nullable
self-FK -> `address_versions.id`, the same shape as the existing valid_through /
superseded_by self-FK columns). The SQL types match the former `survival` SCHEMA
entry's same-named columns exactly. The six columns are NULL for EVERY row this
step (no populate logic -- that is step 2; no reader consumes them yet), so ONLY
the `address_versions` table's content hash moved -- the six new NULL cells on
all 157 curated rows (USER) and all 321144 rows (DEV) -- with NO row-count change,
NO table-set change, and -- critically for an additive-fold step -- the `survival`
table BYTE-IDENTICAL (its content hash + row count unchanged in both DBs, the fold
not yet touching it) and no other table touched. The verify was run FIRST and
confirmed the ONLY drift was the two `address_versions` content hashes (USER +
DEV); the re-capture was deliberate and inspected (only address_versions drifted,
both DBs, content-hash only; survival + every other table byte-identical; the new
cells are all NULL because every build_curated_row caller passes None) before
recording.

It was RE-CAPTURED AGAIN at the schema-flatten-survival-fold Phase 1 step 2 when
the six folded survival columns were POPULATED on the curated address_versions
rows (D22 / design §11.2 -- the dual-write step that proves the fold equivalent).
The importer's rebuild + _apply_one_db now write each curated row's
aob/anchor_string/rule/slot_count/expect_unique/derives_from cells from the SAME
per-kind dispatch (survival_builder._KIND_TO_FORM) that builds the (STILL-written)
survival row -- so each av folded cell equals its survival row's cell, row-for-row
(the equivalence the Phase-3 survival-table delete rests on, asserted by
test_survival_table.py::test_av_folded_cells_equal_survival). The DUAL-WRITE is the
point of this step: the `survival` table is written UNCHANGED in parallel. So the
ONLY drift is the two `address_versions` content hashes (USER + DEV) -- the folded
cells flip from NULL (step 1's additive add) to their per-kind values on the rows
whose kind populates them (callsite/instruction_anchor -> aob+expect_unique,
string_anchor -> anchor_string+expect_unique, data_slot -> rule+derives_from,
vtable_base -> slot_count, function/vtable_index rows stay NULL except a seeded
derives_from) -- with NO row-count change, NO table-set change, and -- critically
for a dual-write step -- the `survival` table BYTE-IDENTICAL (its content hash +
row count unchanged in both DBs: the fold writes the av columns, it does NOT touch
the survival table this step) and no other table moved. The verify was run FIRST
and confirmed the ONLY drift was the two `address_versions` content hashes (USER +
DEV) -- survival + every other table byte-identical; the re-capture was deliberate
and inspected (only address_versions drifted, both DBs, content-hash only; the av
folded cells now equal the survival cells row-for-row; the survival table did NOT
move) before recording.

It was RE-CAPTURED AGAIN at the schema-flatten-survival-fold Phase 3 step 6 -- the
DELETE + finalize -- when the `survival` sibling table was folded onto
address_versions and DELETED (D22 / design §11.2). Every consumer (exporter, engine,
read seam) had already been migrated to read the folded av columns over Phases 1-2;
the folded cells were proven equal to the survival rows row-for-row (157/157). So
deleting the table loses nothing. The drift is EXACTLY two coordinated changes,
verified FIRST before recording: (1) the `survival` table is GONE from BOTH DBs (the
table-set shrinks by one in each, USER 14 -> 13 tables, DEV 17 -> 16; the
sqlite_sequence count drops by one and its content hash changes as the survival
autoincrement entry is removed) and (2) the `address_versions` content hash moves in
BOTH DBs because the 156/157 ICVar accessor rows gained their RE-verified
vtable_slot/struct_offset in the structured columns (id156 slot 2 / +0x10; id157 slot
4 / +0x20 -- moved from the notes prose to their first-class cells per the §11
convention). NO row-count change on any surviving table; address_names / game_versions
/ meta / modules / statements / referenced_vars / call_edges / every _dict_* table
BYTE-IDENTICAL. The verify was run first and confirmed the ONLY drift was the survival
table removal (both DBs) + the two address_versions content hashes (the 156/157
cells); the re-capture was deliberate and inspected (survival gone, the table set
shrunk by one, the 156/157 cells carry their slot/offset, nothing else moved) before
recording.

It was RE-CAPTURED AGAIN (KI-0009, 2026-06-08) for two coordinated, already-committed
output changes the prior baseline predated, verified FIRST before recording (the
complete drift set mapped 1:1 onto the two commits with nothing left over): (1) `3cc6a67`
-- a prose-only correction to `address_names_seed.csv` row id 152's (`CCryPak_AdjustFileName`)
`notes` column (the v1 "every by-name consumer calls slot 1" model rewritten to the
corrected two-lane/two-hook model; NO row/id/column/count change, 157 rows both before
and after). The `notes` column ships to USER and DEV identically, so the rebuilt
`address_names` content hash moves in BOTH DBs (2e046c36.. -> 77b6fab9..). (2) the
curated statement-subset (`a9b0e8a`) added `statements` + `referenced_vars` to the USER
projection (USER_TABLES / USER_COLUMNS) -- so the USER table set gains those two entries
(statements 2385, referenced_vars 5595, the curated address_version_id subset of the DEV
tables), and because both are autoincrement, USER `sqlite_sequence` bumps 3 -> 5 (count +
hash). NO table that should be untouched moved (address_versions / game_versions / meta /
modules / call_edges / every _dict_* table BYTE-IDENTICAL; DEV's statements/
referenced_vars/call_edges unchanged). The re-capture was deliberate and inspected (only
the four explained drifts: USER address_names + the two new USER tables + USER
sqlite_sequence, plus DEV address_names; the drift fully attributed to `3cc6a67` +
`a9b0e8a`, root-cause-verifier `land-fix`) before recording.

It was RE-CAPTURED AGAIN (seeds-to-tracked-csv migration, 2026-06-10) for one
already-committed maintainer-tool edit the prior baseline (KI-0009, 2026-06-08)
predated, verified FIRST before recording. During the migration the oracle's seed
source moved from `data/seeds/` to the tracked `data/db-export/` export (P2.1,
`076834e`) -- which only changed WHERE build_rows reads, not the data. The drift is
EXACTLY one cell, fully attributed to `baae0a9` (2026-06-09, "maintainer-tool: save
version-row kcdx_id=144"): the `address_versions` row id 144 (the function at RVA
0x03581B04) `signature` return type was normalized `char (...)` -> `i8 (...)` (the
same 8-bit type, vocabulary-normalized). So the `address_versions` content hash moves
in BOTH DBs (USER ccd21220.. -> 3a7903dd..; DEV 5f85f958.. -> db2b5418..). NO
row-count change (USER 157, DEV 321144 both before and after); address_names /
game_versions / meta / modules / statements / referenced_vars / call_edges / every
_dict_* table BYTE-IDENTICAL. The drift was verified first (a CSV diff isolated the
single id-144 char->i8 cell; the post-capture baseline diff confirmed ONLY the two
address_versions hashes moved, counts unchanged) before recording -- a legitimate
re-capture of an explained, committed edit, NOT a paper-over.

Re-capture with --capture ONLY for a deliberate, reviewed output change like
those -- never to paper over an unexplained drift.

The hash is over the ORDERED rows of each table exactly as the importer emits
them (the importer fixes row order: address_versions by id, dump-driven tables
in dump-iteration order, dict tables in first-seen order). Order is part of the
contract, so we do NOT sort -- a re-ordering would itself be a behaviour change.
Every cell is canonicalized (BLOB -> hex, None -> a sentinel) so the hash is
stable across the sqlite driver's Python type mapping.

RUN
---
    # one-time baseline capture from the current code (DESTRUCTIVE: overwrites
    # tests/oracle_baseline.json):
    python tests/test_rebuild_oracle.py --capture

    # the gate (used by pytest or run directly):
    python tests/test_rebuild_oracle.py
    pytest tests/test_rebuild_oracle.py

The real dump is ~321K functions; one rebuild takes tens of seconds. That is
expected.
"""
import hashlib
import json
import os
import sqlite3
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PYDIR = os.path.normpath(os.path.join(HERE, "..", "python"))
DUMP_DIR = os.path.normpath(
    os.path.join(HERE, "..", "dump", "refdata-1.5.1164953"))
BASELINE_PATH = os.path.join(HERE, "oracle_baseline.json")

sys.path.insert(0, PYDIR)
import import_to_sqlite as imp  # noqa: E402


def _canon(v):
    """Stable string form of one cell, independent of the sqlite driver's
    Python type choices. BLOB -> 'b:<hex>'; None -> a NULL sentinel; everything
    else -> repr-ish text with a type tag so 1 (int) and '1' (text) differ."""
    if v is None:
        return "\x00NULL\x00"
    if isinstance(v, (bytes, bytearray, memoryview)):
        return "b:" + bytes(v).hex()
    if isinstance(v, int):
        return "i:" + str(v)
    if isinstance(v, float):
        return "f:" + repr(v)
    return "t:" + str(v)


def _table_names(con):
    rows = con.execute(
        "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name"
    ).fetchall()
    return [r[0] for r in rows]


def _hash_db(db_path):
    """Return {table_name: {"count": N, "hash": sha256hex}} for every table in
    the db, INCLUDING the _dict_* lookup tables. Rows are read in the table's
    natural rowid order (the order the importer inserted them); columns in
    declared order. No sorting -- order is part of the behaviour contract."""
    con = sqlite3.connect(db_path)
    try:
        out = {}
        for t in _table_names(con):
            cols = [c[1] for c in con.execute(
                f'PRAGMA table_info("{t}")').fetchall()]
            h = hashlib.sha256()
            n = 0
            cur = con.execute(
                f'SELECT {",".join(chr(34)+c+chr(34) for c in cols)} FROM "{t}"')
            for row in cur:
                h.update(("\x1e".join(_canon(c) for c in row) + "\x1d").encode(
                    "utf-8", "surrogatepass"))
                n += 1
            out[t] = {"count": n, "hash": h.hexdigest()}
        return out
    finally:
        con.close()


def rebuild_and_snapshot():
    """Run the importer's REBUILD path against the local dump into a temp dir,
    then snapshot both DBs. Returns {"user": {...}, "dev": {...}}."""
    if not os.path.isdir(DUMP_DIR):
        raise SystemExit(
            f"dump dir not found: {DUMP_DIR}\n"
            f"  this oracle needs the local refdata-1.5.1164953 dump present.")
    tmp = tempfile.mkdtemp(prefix="rebuild_oracle_")
    try:
        imp.run_rebuild(DUMP_DIR, tmp)
        user_db = os.path.join(tmp, "reference.sqlite")
        dev_db = os.path.join(tmp, "reference-dev.sqlite")
        return {"user": _hash_db(user_db), "dev": _hash_db(dev_db)}
    finally:
        # Best-effort cleanup; leave nothing behind in the repo.
        import shutil
        shutil.rmtree(tmp, ignore_errors=True)


def _load_baseline():
    if not os.path.isfile(BASELINE_PATH):
        raise SystemExit(
            f"no baseline at {BASELINE_PATH}\n"
            f"  capture it from the current code first:\n"
            f"    python {os.path.relpath(__file__)} --capture")
    with open(BASELINE_PATH, encoding="utf-8") as f:
        return json.load(f)


def _compare(expected, actual):
    """Return a list of human-readable mismatch strings ([] == identical)."""
    problems = []
    for db in ("user", "dev"):
        exp_tables = set(expected[db])
        act_tables = set(actual[db])
        if exp_tables != act_tables:
            problems.append(
                f"[{db}] table set differs: "
                f"missing={sorted(exp_tables - act_tables)} "
                f"extra={sorted(act_tables - exp_tables)}")
        for t in sorted(exp_tables & act_tables):
            e, a = expected[db][t], actual[db][t]
            if e["count"] != a["count"]:
                problems.append(
                    f"[{db}.{t}] row count {a['count']} != baseline {e['count']}")
            if e["hash"] != a["hash"]:
                problems.append(
                    f"[{db}.{t}] content hash differs "
                    f"(baseline {e['hash'][:12]}.., got {a['hash'][:12]}..)")
    return problems


def test_rebuild_matches_baseline():
    """The behaviour-preserving gate: a fresh rebuild reproduces the recorded
    pre-refactor per-table row counts + content hashes, for every table in both
    the USER and DEV DBs (including the _dict_* lookup tables)."""
    expected = _load_baseline()
    actual = rebuild_and_snapshot()
    problems = _compare(expected, actual)
    assert not problems, "rebuild output drifted from baseline:\n  " + \
        "\n  ".join(problems)


def _capture():
    snap = rebuild_and_snapshot()
    with open(BASELINE_PATH, "w", encoding="utf-8") as f:
        json.dump(snap, f, indent=2, sort_keys=True)
    print(f"\nbaseline written -> {BASELINE_PATH}")
    for db in ("user", "dev"):
        print(f"\n== {db} db ==")
        for t in sorted(snap[db]):
            print(f"  {t:24s} count={snap[db][t]['count']:>8d} "
                  f"hash={snap[db][t]['hash']}")


def _verify():
    expected = _load_baseline()
    actual = rebuild_and_snapshot()
    problems = _compare(expected, actual)
    if problems:
        print("\nFAIL: rebuild output drifted from baseline:")
        for p in problems:
            print("  " + p)
        sys.exit(1)
    print("\nPASS: rebuild output is byte-identical to the recorded baseline.")
    for db in ("user", "dev"):
        print(f"  {db}: {len(actual[db])} tables, all hashes match")


if __name__ == "__main__":
    if "--capture" in sys.argv[1:]:
        _capture()
    else:
        _verify()
