"""make_sandbox.py -- build the two-version cross-version-matcher sandbox.

The matcher (match_versions.py) reads CSV DUMP dirs, not DLLs. So the "two DLLs"
are two dump dirs:
  - v1/  = a real ~200-function SLICE of the actual WHGame.dll dump (real
           functions, call_edges, statements, signatures, caller_reg_args -- NOT
           toy data). This is "v1 of the DLL".
  - v2/  = an AUTHORED MUTATION of v1 (this script applies a hand-written recipe).
           Because we author every change, we KNOW the ground-truth v1->v2 mapping
           and emit it as ground_truth.csv.

The mutation is COMPREHENSIVE on the cases that trip up naive matching (each is a
labeled ground-truth row): identical; moved-unchanged; changed-body-same-identity;
changed+moved; deleted; added; split (1->2); merge (2->1); fingerprint-swap TRAP;
string-only-anchor leaf; renamed-callee ripple; curated-entity changed.

The fixture DATA (v1/, v2/, ground_truth.csv, backups) is gitignored -- it is a
real-WHGame.dll-derived slice. THIS recipe is tracked; the data regenerates from
it + the local dump.

Run:
  python make_sandbox.py [dump_dir] [n_slice]
    dump_dir : a refdata-<version>/ dir produced by the extractor. Omit to use
               the highest-version dir under ../dump/refdata-<version>/.
    n_slice  : how many functions to slice for v1 (default 200)
  -> writes sandbox/v1/, sandbox/v2/, sandbox/ground_truth.csv, + .backup/ copies.

GROUND TRUTH semantics (ground_truth.csv: v2_rva, expect, detail, case):
  expect = MATCHED   detail = the v1 rva this v2 entity IS (same logical fn)
         = NEW       detail = '' (genuinely new in v2; matcher must NOT silently mint)
         = SPLIT     detail = the v1 rva it was split from (ambiguous; flag, don't guess)
         = MERGE     detail = 'rva1|rva2' the v1 rvas merged into it (ambiguous)
  case   = the LABELED trip-up category (identical / moved / changed_body /
           changed_moved / bulk_move / split / merge / added / swap_trap /
           string_leaf / ripple / curated_changed) -- so the matcher self-score is
           PER-CATEGORY, not a single MATCHED bucket that hides whether the hard
           cases worked.
  (v1 entities with NO v2 row are DELETED -- listed separately in the DELETED block.)
The matcher keys on a STABLE id, not rva; the sandbox uses v1 rva as the stand-in
for "the v1 identity" since match_versions assigns v1 kcdx_ids from the v1 slice.
"""
import csv
import glob
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SANDBOX = HERE

# The extractor writes one dump dir per game version into a `dump/` sibling of
# `sandbox/`. Resolve relative to this script so it works on any maintainer's
# clone.
DUMP_ROOT = os.path.normpath(os.path.join(HERE, "..", "dump"))
_VERSION_DIR_RE = re.compile(r"^refdata-(\d+\.\d+\.\d+)$")


def find_latest_dump():
    """Return (path, version_tag) of the newest dump dir under DUMP_ROOT, or
    raise SystemExit with a helpful message if none exists."""
    if not os.path.isdir(DUMP_ROOT):
        sys.exit(
            f"no dump root at {DUMP_ROOT}\n"
            f"  -> create it and place a refdata-<version>/ dir there, or pass\n"
            f"     an explicit dump_dir as the first argument.")
    candidates = []
    for entry in os.listdir(DUMP_ROOT):
        full = os.path.join(DUMP_ROOT, entry)
        if not os.path.isdir(full):
            continue
        m = _VERSION_DIR_RE.match(entry)
        if not m:
            continue
        ver = m.group(1)
        try:
            ordinal = int(ver.split(".")[-1])
        except ValueError:
            ordinal = -1
        candidates.append((ordinal, ver, full))
    if not candidates:
        sys.exit(
            f"no `refdata-<version>/` dirs under {DUMP_ROOT}\n"
            f"  -> run the extractor (data/refdata-extractor/run-parallel.ps1)\n"
            f"     or pass an explicit dump_dir as the first argument.")
    candidates.sort(reverse=True)
    _, ver, path = candidates[0]
    return path, ver

TABLES = ["functions", "statements", "referenced_vars", "call_edges",
          "signatures", "caller_reg_args"]

# How each table references a function rva (the column the mutation rewrites).
RVA_COLS = {
    "functions":       ["rva"],
    "statements":      ["function_rva", "callee_rva"],
    "referenced_vars": ["function_rva"],
    "call_edges":      ["caller_rva", "callee_rva"],
    "signatures":      ["rva"],
    "caller_reg_args": ["rva"],
}

SHARD_SPAN = 0x100000


def hexs(v):
    return "0x%x" % v


def parse_rva(s):
    try:
        return int(s, 16) if s.startswith("0x") else int(s)
    except (ValueError, AttributeError):
        return None


# ---------------------------------------------------------------------------
# Read a contiguous slice of the real dump: the first n functions by rva, plus
# every row in the other tables that references one of those rvas.
# ---------------------------------------------------------------------------
def read_dump_table(dump_dir, table):
    rows, hdr = [], None
    for shard in sorted(glob.glob(os.path.join(dump_dir, table, f"{table}_*.csv"))):
        with open(shard, newline="", encoding="utf-8", errors="replace") as f:
            rd = csv.reader(f)
            h = next(rd)
            if hdr is None:
                hdr = h
            for row in rd:
                rows.append(row)
    return hdr, rows


def slice_v1(dump_dir, n_slice):
    """Return {table: (header, rows)} for the first n_slice functions (by rva) +
    all dependent rows. Also returns the sorted list of sliced function rvas."""
    fhdr, frows = read_dump_table(dump_dir, "functions")
    ri = fhdr.index("rva")
    frows = [r for r in frows if parse_rva(r[ri]) is not None]
    frows.sort(key=lambda r: parse_rva(r[ri]))
    sliced = frows[:n_slice]
    keep = {parse_rva(r[ri]) for r in sliced}

    out = {"functions": (fhdr, sliced)}
    for t in TABLES:
        if t == "functions":
            continue
        hdr, rows = read_dump_table(dump_dir, t)
        # keep a row if its PRIMARY function rva is in the slice (callee may point
        # outside -- that's a real external edge, keep the row, leave callee as-is).
        prim = RVA_COLS[t][0]
        pi = hdr.index(prim)
        kept = [r for r in rows if parse_rva(r[pi]) in keep]
        out[t] = (hdr, kept)
    return out, sorted(keep)


# ---------------------------------------------------------------------------
# Write a dump dir (sharded, matching the real layout the matcher reads).
# ---------------------------------------------------------------------------
def write_dump(out_dir, data):
    if os.path.exists(out_dir):
        shutil.rmtree(out_dir)
    for t, (hdr, rows) in data.items():
        tdir = os.path.join(out_dir, t)
        os.makedirs(tdir, exist_ok=True)
        # shard by the primary rva col, matching shardOf(rva)=rva//0x100000.
        prim = RVA_COLS[t][0]
        pi = hdr.index(prim)
        shards = {}
        for r in rows:
            rv = parse_rva(r[pi])
            idx = (rv // SHARD_SPAN) if rv is not None else 0
            shards.setdefault(idx, []).append(r)
        for idx, rws in shards.items():
            name = "%s_%08x.csv" % (t, idx * SHARD_SPAN)
            with open(os.path.join(tdir, name), "w", newline="", encoding="utf-8") as f:
                w = csv.writer(f)
                w.writerow(hdr)
                w.writerows(rws)


# ---------------------------------------------------------------------------
# The MUTATION: build v2 from v1 by applying the authored recipe. Returns the v2
# data dict + the ground-truth list [(v2_rva, expect, detail)] + deleted v1 rvas.
#
# The recipe targets v1 functions BY SLICE POSITION (0-based index into the sorted
# slice) so it is stable across dump refreshes. We relocate v2 into a high rva
# band (+0x800000) so v2 rvas never collide with v1 rvas -- a real version move.
# ---------------------------------------------------------------------------
V2_BASE = 0x800000   # v2 functions live at v1_rva + this (a wholesale image move)


def mutate(v1, v1_rvas):
    fhdr, frows = v1["functions"]
    ri = fhdr.index("rva")
    chi = fhdr.index("content_hash")
    ni = fhdr.index("auto_name")
    li = fhdr.index("length")

    # index v1 function rows by rva
    by_rva = {parse_rva(r[ri]): list(r) for r in frows}
    n = len(v1_rvas)

    # Pick distinct slice positions for each case (spread across the slice, all < n).
    # Each maps to a concrete v1 rva.
    def at(pos):
        return v1_rvas[pos]

    # Assign roles (require the slice to be at least ~30 fns; default 200 is plenty).
    roles = {
        "identical":        at(5),
        "moved":            at(10),
        "changed_body":     at(15),
        "changed_moved":    at(20),
        "deleted":          at(25),
        "split_src":        at(30),
        "merge_a":          at(35),
        "merge_b":          at(36),
        "swap_a":           at(40),
        "swap_b":           at(41),
        "string_leaf":      at(45),
        "ripple_callee":    at(50),   # this one changes; its callers' fingerprints shift
        "curated_changed":  at(55),
    }

    ground = []      # (v2_rva, expect, detail, case)
    deleted = []     # v1 rvas with no v2 successor

    # The v2 function set, keyed by NEW v2 rva. Start from EVERY v1 function moved
    # wholesale to the v2 band (the common case: image shifted, bytes identical),
    # then apply the special mutations on top.
    v2_funcs = {}      # v2_rva -> v2 function row (list)
    rva_map = {}       # v1_rva -> v2_rva (for rewriting dependent tables)
    changed_bodies = set()  # v1 rvas whose BYTES changed -> their statement hashes
                            # must change too (real changed bytes change statement
                            # bytes; a fixture that left statement hashes identical
                            # would let the matcher cheat on a signal that does not
                            # survive a real patch).

    def move(v1_rva, v2_rva, mutate_hash=False, new_name=None):
        if mutate_hash:
            changed_bodies.add(v1_rva)
        row = list(by_rva[v1_rva])
        row[ri] = hexs(v2_rva)
        if mutate_hash:
            # flip the hash to a different (still-valid 64-hex) value: rotate it.
            h = row[chi]
            row[chi] = (h[1:] + h[0]) if len(h) == 64 else h
        if new_name is not None:
            row[ni] = new_name
        else:
            # auto_name is FUN_<rva>; v2 name reflects the new rva (it's rva-derived).
            row[ni] = "FUN_%08x" % v2_rva
        v2_funcs[v2_rva] = row
        rva_map[v1_rva] = v2_rva
        return v2_rva

    special = set(roles.values())

    # 1. the bulk: every non-special v1 fn -> moved wholesale, bytes identical.
    #    expect MATCHED (the matcher must match across the rva move via hash/fp).
    for rv in v1_rvas:
        if rv in special:
            continue
        v2 = move(rv, rv + V2_BASE, mutate_hash=False)
        ground.append((hexs(v2), "MATCHED", hexs(rv), "bulk_move"))

    # 2. IDENTICAL (same rva, same bytes) -- a function that did NOT move at all.
    rv = roles["identical"]
    v2_funcs[rv] = list(by_rva[rv])          # keep v1 rva, untouched
    rva_map[rv] = rv
    ground.append((hexs(rv), "MATCHED", hexs(rv), "identical"))

    # 3. MOVED, unchanged bytes (rva changes, hash identical).
    rv = roles["moved"]
    v2 = move(rv, rv + V2_BASE, mutate_hash=False)
    ground.append((hexs(v2), "MATCHED", hexs(rv), "moved"))

    # 4. CHANGED BODY, same identity (rva moved AND hash changed; call-graph/strings
    #    intact -> must match by fingerprint, not hash).
    rv = roles["changed_body"]
    v2 = move(rv, rv + V2_BASE, mutate_hash=True)
    ground.append((hexs(v2), "MATCHED", hexs(rv), "changed_body"))

    # 5. CHANGED + MOVED to an unusual band (rva far away, hash changed).
    rv = roles["changed_moved"]
    v2 = move(rv, rv + V2_BASE + 0x40000, mutate_hash=True)
    ground.append((hexs(v2), "MATCHED", hexs(rv), "changed_moved"))

    # 6. DELETED: no v2 row. (matcher must surface deleted_candidate, not auto-remove.)
    rv = roles["deleted"]
    deleted.append(hexs(rv))
    # (intentionally no rva_map entry, no v2 func)

    # 7. ADDED: a brand-new v2 function with no v1 counterpart. Clone a v1 row's
    #    SHAPE but give it a fresh hash + a v2-only rva far out of band.
    added_rva = 0xf00000
    base = list(by_rva[v1_rvas[60]])
    base[ri] = hexs(added_rva)
    base[ni] = "FUN_%08x" % added_rva
    base[chi] = "a" * 64                     # a hash matching nothing in v1
    v2_funcs[added_rva] = base
    ground.append((hexs(added_rva), "NEW", "", "added"))

    # 8. SPLIT: one v1 fn -> two v2 fns (both inherit part of its fingerprint).
    rv = roles["split_src"]
    s1 = move(rv, rv + V2_BASE, mutate_hash=True)            # first half keeps rva-band
    s2_rva = rv + V2_BASE + 0x50000
    s2 = list(by_rva[rv]); s2[ri] = hexs(s2_rva); s2[ni] = "FUN_%08x" % s2_rva
    s2[chi] = (by_rva[rv][chi][2:] + by_rva[rv][chi][:2])    # different hash
    v2_funcs[s2_rva] = s2
    # both v2 rows came from the one v1 rva -> ambiguous; matcher should flag.
    ground.append((hexs(s1), "SPLIT", hexs(rv), "split"))
    ground.append((hexs(s2_rva), "SPLIT", hexs(rv), "split"))
    # NOTE rva_map[rv] points at s1 (the dependent-table rewrite picks one half).

    # 9. MERGE: two v1 fns -> one v2 fn (inherits both fingerprints).
    ra, rb = roles["merge_a"], roles["merge_b"]
    m_rva = ra + V2_BASE
    mrow = list(by_rva[ra]); mrow[ri] = hexs(m_rva); mrow[ni] = "FUN_%08x" % m_rva
    mrow[chi] = "b" * 64
    v2_funcs[m_rva] = mrow
    rva_map[ra] = m_rva
    rva_map[rb] = m_rva          # both v1 fns now point at the merged v2 fn
    changed_bodies.add(ra); changed_bodies.add(rb)   # merged body differs from both
    ground.append((hexs(m_rva), "MERGE", "%s|%s" % (hexs(ra), hexs(rb)), "merge"))

    # 10. FINGERPRINT-SWAP TRAP: swap_a and swap_b exchange their call-targets and
    #     string refs in v2, to bait the matcher into cross-assigning them. Bytes
    #     change for both. Ground truth: each still IS itself (same rva-band move).
    sa, sb = roles["swap_a"], roles["swap_b"]
    va = move(sa, sa + V2_BASE, mutate_hash=True)
    vb = move(sb, sb + V2_BASE, mutate_hash=True)
    # the swap of edges/strings happens in the dependent-table rewrite below.
    ground.append((hexs(va), "MATCHED", hexs(sa), "swap_trap"))
    ground.append((hexs(vb), "MATCHED", hexs(sb), "swap_trap"))

    # 11. STRING-ONLY LEAF: a fn with NO call edges, identifiable only by string_ref.
    #     Moves + hash changes; only its string anchors it.
    rv = roles["string_leaf"]
    v2 = move(rv, rv + V2_BASE, mutate_hash=True)
    ground.append((hexs(v2), "MATCHED", hexs(rv), "string_leaf"))

    # 12. RIPPLE CALLEE: this fn changes (hash), which shifts the fingerprints of
    #     everything that CALLS it -- a test that neighbor churn doesn't break the
    #     callers' matches. The callee itself still matches.
    rv = roles["ripple_callee"]
    v2 = move(rv, rv + V2_BASE, mutate_hash=True)
    ground.append((hexs(v2), "MATCHED", hexs(rv), "ripple"))

    # 13. CURATED-ENTITY CHANGED: a fn that (in the real seed) is curated -- here we
    #     just mark it changed; the reconcile/trigger path is exercised separately.
    rv = roles["curated_changed"]
    v2 = move(rv, rv + V2_BASE, mutate_hash=True)
    ground.append((hexs(v2), "MATCHED", hexs(rv), "curated_changed"))

    # --- rewrite the dependent tables through rva_map (+ the swap trap) ---
    sa_v2, sb_v2 = rva_map[roles["swap_a"]], rva_map[roles["swap_b"]]

    def remap(rv):
        return rva_map.get(rv)   # None if the v1 fn was deleted/has no successor

    # statement content_hash column index (statements table only) -- changed
    # bodies must rotate their per-statement hashes too.
    st_hdr = v1["statements"][0]
    st_ch_i = st_hdr.index("content_hash")

    v2_data = {"functions": (fhdr, list(v2_funcs.values()))}
    for t in TABLES:
        if t == "functions":
            continue
        hdr, rows = v1[t]
        cols = [hdr.index(c) for c in RVA_COLS[t]]
        prim_i = hdr.index(RVA_COLS[t][0])
        new_rows = []
        for r in rows:
            prv = parse_rva(r[prim_i])
            # drop rows owned by a deleted/split-away function
            if remap(prv) is None and prv != roles["identical"]:
                # identical kept its own rva (remap returns it); others w/o map are gone
                if prv not in rva_map:
                    continue
            nr = list(r)
            # CHANGED BODY: rotate this statement's content_hash (a real body change
            # changes the statements' bytes -> their hashes). This denies the matcher
            # the statement-hash-identity shortcut on changed functions, forcing it
            # onto call-graph + string + position signals that DO survive a patch.
            if t == "statements" and prv in changed_bodies:
                h = nr[st_ch_i]
                if isinstance(h, str) and len(h) == 64:
                    nr[st_ch_i] = h[3:] + h[:3]
            for ci in cols:
                old = parse_rva(nr[ci])
                if old is None:
                    continue
                # the swap trap: on swap_a/swap_b owned rows, exchange the OWNER's
                # identity so their call targets/strings move to the other fn.
                mapped = remap(old)
                if mapped is not None:
                    nr[ci] = hexs(mapped)
                # else: external edge (callee outside the slice) -- leave as-is.
            # apply the fingerprint swap: rows owned by swap_a now belong to swap_b's
            # v2 fn and vice-versa (their behavioral content trades places).
            if cols and parse_rva(r[prim_i]) == roles["swap_a"]:
                nr[prim_i] = hexs(sb_v2)
            elif cols and parse_rva(r[prim_i]) == roles["swap_b"]:
                nr[prim_i] = hexs(sa_v2)
            new_rows.append(nr)
        v2_data[t] = (hdr, new_rows)

    return v2_data, ground, deleted


def main():
    if len(sys.argv) > 1:
        dump_dir = sys.argv[1]
        if not os.path.isdir(dump_dir):
            sys.exit("dump dir not found: " + dump_dir)
    else:
        dump_dir, ver = find_latest_dump()
        print(f"== using dump for game version {ver}: {dump_dir}")
    n_slice = int(sys.argv[2]) if len(sys.argv) > 2 else 200

    print("== slicing v1 (%d functions) from %s" % (n_slice, dump_dir))
    v1, v1_rvas = slice_v1(dump_dir, n_slice)
    print("   v1 functions: %d  (rva %s .. %s)"
          % (len(v1_rvas), hexs(v1_rvas[0]), hexs(v1_rvas[-1])))

    print("== authoring v2 mutation")
    v2, ground, deleted = mutate(v1, v1_rvas)
    print("   v2 functions: %d  ground-truth rows: %d  deleted: %d"
          % (len(v2["functions"][1]), len(ground), len(deleted)))

    v1_dir = os.path.join(SANDBOX, "v1")
    v2_dir = os.path.join(SANDBOX, "v2")
    write_dump(v1_dir, v1)
    write_dump(v2_dir, v2)
    # backups
    for d in ("v1", "v2"):
        b = os.path.join(SANDBOX, d + ".backup")
        if os.path.exists(b):
            shutil.rmtree(b)
        shutil.copytree(os.path.join(SANDBOX, d), b)

    gt = os.path.join(SANDBOX, "ground_truth.csv")
    with open(gt, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["v2_rva", "expect", "detail", "case"])
        for row in sorted(ground, key=lambda x: parse_rva(x[0])):
            w.writerow(row)
        for rv in deleted:
            w.writerow([rv, "DELETED_V1", rv, "deleted"])   # a v1 rva, no v2 successor

    print("== wrote sandbox/v1, sandbox/v2, sandbox/ground_truth.csv (+ backups)")
    print("   ground_truth.csv: %d v2 rows + %d deleted-v1 rows"
          % (len(ground), len(deleted)))


if __name__ == "__main__":
    main()
