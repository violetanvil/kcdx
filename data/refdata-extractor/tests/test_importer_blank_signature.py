"""test_importer_blank_signature.py -- the importer NULLs a blank seed signature
on a curated function-kind row, never the bulk-dump abi_walker floor.

WHAT THIS PROVES
----------------
A REAL --rebuild from the committed mini-dump fixture produces REAL DB rows; this
test reads them back and asserts the design.md round-trip-contract consequence
(§4: "the importer must persist NULL for any authored field the seed left blank
-- it must not promote a bulk-dump value onto a curated row's blank cell"):

  (a) a curated row WITH an authored signature keeps it (the 'function' rows);
  (b) a curated function_no_sig / function_variadic row whose seed `signature`
      cell is BLANK has DB signature NULL -- NOT the bulk floor `? (...)` it used
      to inherit on the promote path;
  (c) an uncurated bulk-dump row (kcdx_id IS NULL, DEV DB) keeps its floor
      signature unchanged -- the fix touches ONLY curated rows.

This exercises the actual importer (import_to_sqlite.run_rebuild ->
seeds_shared.build_curated_row), not a re-implementation of the NULL logic: the
rows asserted are whatever a from-scratch rebuild wrote to disk.

WHY (b) IS SAFE
---------------
The survival/fingerprint path keys these kinds on the body-hash
(survival_builder.py: function_no_sig / function_variadic -> 'function_hash',
payload = content_hash + length), NOT on the signature -- so NULLing the floor
signature changes no survival behaviour. This test additionally asserts every
function_no_sig / function_variadic curated row still carries its content_hash +
length fingerprint after the signature is NULLed.

FIXTURE
-------
Runs against the small committed REAL dump excerpt
(tests/fixtures/mini-dump/refdata-1.5.1164953, built by make_mini_dump.py) for a
fast rebuild, the same fixture test_apply_reverify.py uses. The seed has 119
authored-signature `function` rows, 10 blank `function_no_sig`, and 2 blank
`function_variadic` (12 blank function-kind rows total) whose RVAs match a bulk
entry and therefore PROMOTE -- the exact rows the floor used to leak onto.

RUN
---
    python tests/test_importer_blank_signature.py
    pytest tests/test_importer_blank_signature.py
"""
import os
import shutil
import sqlite3
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PYDIR = os.path.normpath(os.path.join(HERE, "..", "python"))
DUMP_DIR = os.path.normpath(
    os.path.join(HERE, "fixtures", "mini-dump", "refdata-1.5.1164953"))

sys.path.insert(0, PYDIR)
import import_to_sqlite as imp  # noqa: E402

FUNCTION_NO_SIG_KINDS = ("function_no_sig", "function_variadic")


def _require_dump():
    if not os.path.isdir(DUMP_DIR):
        raise SystemExit(
            f"dump dir not found: {DUMP_DIR}; this oracle needs the committed "
            f"mini-dump fixture present.")


def _rebuild():
    """Run the importer's REBUILD path against the mini-dump into a temp dir.
    Returns (tmp_dir, user_db_path, dev_db_path); caller cleans up tmp_dir."""
    _require_dump()
    tmp = tempfile.mkdtemp(prefix="blank_sig_oracle_")
    imp.run_rebuild(DUMP_DIR, tmp)
    return (tmp, os.path.join(tmp, "reference.sqlite"),
            os.path.join(tmp, "reference-dev.sqlite"))


def _kind_id_to_val(con):
    return {r[0]: r[1] for r in con.execute(
        "SELECT id, val FROM _dict_address_versions_kind")}


def _curated_function_rows(con):
    """Yield (kcdx_id, kind_val, signature, content_hash, length) for every
    curated (kcdx_id NOT NULL) function-class address_versions row."""
    kinds = _kind_id_to_val(con)
    for kid, kindid, sig, ch, length in con.execute(
            "SELECT kcdx_id, kind, signature, content_hash, length "
            "FROM address_versions WHERE kcdx_id IS NOT NULL"):
        k = kinds.get(kindid)
        if k in ("function", "function_no_sig", "function_variadic"):
            yield (kid, k, sig, ch, length)


def _check(user_db, dev_db):
    """Return a list of human-readable problems ([] == all three behaviours hold)."""
    problems = []

    for label, db in (("user", user_db), ("dev", dev_db)):
        con = sqlite3.connect(db)
        try:
            n_authored = 0
            n_blank = 0
            for kid, kind, sig, ch, length in _curated_function_rows(con):
                if kind == "function":
                    # (a) authored signature is preserved (never NULLed).
                    n_authored += 1
                    if not sig:
                        problems.append(
                            f"[{label}] curated 'function' kcdx_id={kid} lost its "
                            f"authored signature (got {sig!r})")
                else:
                    # (b) blank-seed function_no_sig / function_variadic -> NULL,
                    #     never the bulk floor `? (...)`.
                    n_blank += 1
                    if sig is not None:
                        problems.append(
                            f"[{label}] curated {kind} kcdx_id={kid} has DB "
                            f"signature {sig!r}, expected NULL (the bulk "
                            f"abi_walker floor must not be promoted onto a blank "
                            f"seed cell)")
                    # why-safe: the body-hash fingerprint is the survival datum
                    # for these kinds and must SURVIVE the signature NULLing.
                    if ch is None or length is None:
                        problems.append(
                            f"[{label}] curated {kind} kcdx_id={kid} lost its "
                            f"body fingerprint (content_hash={ch!r} "
                            f"length={length!r}); NULLing the signature must not "
                            f"touch the survival fingerprint")

            # The fixture's seed carries the rows these checks need; a zero count
            # means the fixture/seed changed out from under the test.
            if n_authored == 0:
                problems.append(
                    f"[{label}] no curated 'function' (authored-signature) rows "
                    f"found -- fixture/seed drift")
            if n_blank == 0:
                problems.append(
                    f"[{label}] no curated function_no_sig/function_variadic "
                    f"(blank-signature) rows found -- fixture/seed drift")
        finally:
            con.close()

    # (c) Uncurated bulk rows (DEV only -- USER ships curated-only) keep their
    #     floor signature: the fix must not touch them.
    con = sqlite3.connect(dev_db)
    try:
        n_bulk = con.execute(
            "SELECT COUNT(*) FROM address_versions WHERE kcdx_id IS NULL").fetchone()[0]
        n_bulk_floor = con.execute(
            "SELECT COUNT(*) FROM address_versions "
            "WHERE kcdx_id IS NULL AND signature LIKE '? (%'").fetchone()[0]
        if n_bulk == 0:
            problems.append("[dev] no uncurated bulk rows found -- fixture drift")
        elif n_bulk_floor != n_bulk:
            problems.append(
                f"[dev] uncurated bulk rows lost their floor signature: "
                f"{n_bulk_floor}/{n_bulk} still carry `? (...)` "
                f"(the curated-row fix must leave bulk rows unchanged)")
    finally:
        con.close()

    return problems


def test_importer_nulls_blank_curated_signature():
    """The importer NULLs a blank seed signature on a curated function-kind row,
    keeps an authored one, and leaves uncurated bulk rows' floor untouched."""
    tmp, user_db, dev_db = _rebuild()
    try:
        problems = _check(user_db, dev_db)
        assert not problems, "blank-signature importer contract violated:\n  " + \
            "\n  ".join(problems)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    tmp, user_db, dev_db = _rebuild()
    try:
        problems = _check(user_db, dev_db)
        if problems:
            print("\nFAIL: blank-signature importer contract violated:")
            for p in problems:
                print("  " + p)
            sys.exit(1)
        print("\nPASS: curated blank-signature function rows are NULL; authored "
              "signatures preserved; uncurated bulk floor untouched.")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
