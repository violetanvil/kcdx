"""One-time slicer: carve a small, REAL excerpt of the full extractor dump into
tests/fixtures/mini-dump/ so the apply<->rebuild oracles run in milliseconds
instead of re-ingesting the 17M-row live dump every time.

WHY A REAL SLICE (not hand-typed): the function-add oracle asserts the PROMOTE
path carries a real body fingerprint (content_hash + signatures.observed_arg_slots
+ caller_reg_args.caller_reg_arg_count). Those must be genuine extractor output,
so we copy real rows rather than fabricate them.

WHAT IT INCLUDES:
  - every curated seed RVA that exists as a dump function (so the curated build
    + every PROMOTE works),
  - a pool of extra uncurated functions that carry a FULL fingerprint (so the
    add-oracle's _a_free_bulk_function_rva has free promotable RVAs to pick),
  - the signatures / caller_reg_args rows for every included function,
  - the statements / referenced_vars / call_edges rows whose endpoints are all
    inside the included function set (bulk-table coverage, kept tiny).

RUN ONCE (re-run only if the dump format changes):
    python tests/make_mini_dump.py
The output is committed; the oracles read it, never the full dump.
"""
import csv
import glob
import os

HERE = os.path.dirname(os.path.abspath(__file__))
FULL = os.path.normpath(os.path.join(HERE, "..", "dump", "refdata-1.5.1164953"))
OUT = os.path.join(HERE, "fixtures", "mini-dump", "refdata-1.5.1164953")
SEED_VERS = os.path.normpath(
    os.path.join(HERE, "..", "..", "seeds", "address_versions_seed.csv"))

EXTRA_PROMOTABLE = 40   # uncurated functions with a full fingerprint to keep
EXTRA_PLAIN = 20        # extra functions (any) for headroom / non-fp picks


def _shards(table):
    return sorted(glob.glob(os.path.join(FULL, table, f"{table}_*.csv")))


def _rows(table):
    for sh in _shards(table):
        with open(sh, newline="", encoding="utf-8", errors="replace") as f:
            yield from csv.DictReader(f)


def _hdr(table):
    with open(_shards(table)[0], newline="", encoding="utf-8") as f:
        return csv.DictReader(f).fieldnames


def _pi(s):
    s = (s or "").strip()
    if not s:
        return None
    return int(s, 16) if s.startswith("0x") else int(s)


def main():
    # 1. curated RVAs from the real seed
    curated = set()
    with open(SEED_VERS, newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            rv = (r.get("rva") or "").strip()
            if rv:
                curated.add(int(rv, 16) if rv.startswith("0x") else int(rv))

    # 2. which RVAs carry a full fingerprint (signatures + caller_reg_args)
    sig_by_rva = {_pi(r["rva"]): r for r in _rows("signatures") if _pi(r["rva"]) is not None}
    cra_by_rva = {_pi(r["rva"]): r for r in _rows("caller_reg_args") if _pi(r["rva"]) is not None}
    full_fp = set(sig_by_rva) & set(cra_by_rva)

    # 3. choose the function set: curated-present + extra promotable + extra plain
    keep = set()
    fn_rows = []
    for r in _rows("functions"):
        rva = _pi(r["rva"])
        if rva is None:
            continue
        fn_rows.append((rva, r))
    by_rva = {rva: r for rva, r in fn_rows}

    for rva in curated:
        if rva in by_rva:
            keep.add(rva)

    promotable_pool = sorted((set(by_rva) & full_fp) - keep)
    keep.update(promotable_pool[:EXTRA_PROMOTABLE])
    plain_pool = sorted(set(by_rva) - keep)
    keep.update(plain_pool[:EXTRA_PLAIN])

    os.makedirs(OUT, exist_ok=True)

    def write(table, rows):
        path = os.path.join(OUT, table, f"{table}_00000000.csv")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=_hdr(table))
            w.writeheader()
            w.writerows(rows)
        print(f"  {table}: {len(rows)} rows -> {path}")

    # functions
    write("functions", [by_rva[r] for r in sorted(keep)])
    # signatures / caller_reg_args for kept functions
    write("signatures", [sig_by_rva[r] for r in sorted(keep) if r in sig_by_rva])
    write("caller_reg_args", [cra_by_rva[r] for r in sorted(keep) if r in cra_by_rva])

    # statements whose function_rva is kept
    st = [r for r in _rows("statements") if _pi(r.get("function_rva")) in keep]
    write("statements", st)
    # referenced_vars whose function_rva is kept
    rv = [r for r in _rows("referenced_vars") if _pi(r.get("function_rva")) in keep]
    write("referenced_vars", rv)
    # call_edges with BOTH endpoints kept
    ce = [r for r in _rows("call_edges")
          if _pi(r.get("caller_rva")) in keep and _pi(r.get("callee_rva")) in keep]
    write("call_edges", ce)

    print(f"\nmini-dump built: {len(keep)} functions "
          f"({len(curated & keep)} curated, {min(EXTRA_PROMOTABLE, len(promotable_pool))} extra promotable)")


if __name__ == "__main__":
    main()
