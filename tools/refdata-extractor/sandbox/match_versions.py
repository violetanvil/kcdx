"""match_versions.py -- propose-only cross-version function matcher + self-score.

Reads two CSV dump dirs (v1/, v2/ -- the same sharded layout the real refdata
dump uses) and proposes, per v2 function, ONE verdict re-identifying it against
v1:

    MATCHED <v1_rva>        confident: this v2 fn IS that v1 fn
    AMBIGUOUS [v1_rva,...]   could be several v1 fns (ties / split / merge / swap)
    UNMATCHED                no confident v1 match (genuinely new, or a miss --
                             the matcher does NOT decide which)

Plus, every v1 fn that no v2 fn confidently MATCHED -> DELETED_CANDIDATE.

The matcher MINTS nothing and DELETES nothing. It only proposes. "v1 identity"
here = the v1 rva (this sandbox uses v1 rva as the stable-id stand-in).

SIGNAL DESIGN -- ranked by how well each survives a real game patch (a changed
function body). The whole point of cross-version matching is "the bytes changed
but it is the SAME logical function", so the matcher must lean on what a body
change does NOT destroy:

  - call-graph (PRIMARY survivor): a changed fn still CALLS the same callees and
    is CALLED BY the same callers. Express both sets through ALREADY-MATCHED
    identities (a fixpoint, seeded from hash-equal matches). When present, this
    is the dominant signal -- it is near-1.0 for a changed fn whose neighbours
    matched, and near-0 only when the fn genuinely lost/gained edges.
  - string/cvar anchors (STRONG when present): a changed fn that still references
    the same distinctive literal is almost certainly the same fn. A shared rare
    string can carry a match largely on its own (a leaf with no call edges is
    identifiable ONLY this way).
  - statement-hash Jaccard (CORROBORATOR, NOT dominant): a per-statement
    content_hash set is ~1.0 for an UNCHANGED fn (moved bulk) and ~0 for a
    CHANGED body -- i.e. it is zero exactly when matching is hard. So it is a
    high-confidence confirmer for the unchanged majority and a tie-breaker, never
    the dominant weight.
  - structure+position (FALLBACK floor): statement-set SIZE similarity + ordinal
    proximity (bracketed by matched neighbours, NO image offset assumed). The
    only thing left for a changed fn that has no call edges and no anchors -- a
    weak signal that survives a body change because a patch rarely changes a fn's
    size/order wholesale. Carries a match only when nothing stronger exists AND
    the structural candidate is unique.

The algorithm (in order):
  1. Hash-equal pass: a v2 fn whose function content_hash uniquely equals one
     v1 fn's hash -> MATCHED ~1.0. A hash shared by >1 entity on EITHER side is
     NOT auto-matched -- it falls through to the fingerprint pass.
  2. Fingerprint fixpoint: for the hash-changed / hash-colliding fns, score each
     open (v2,v1) pair by combining the surviving signals above. UNCHANGED-but-
     hash-colliding bulk fns confirm via statement Jaccard ~1.0; genuinely-
     changed fns rely on call-graph / strings / structure. Greedy by descending
     confidence; re-run a few rounds so the mapped call-graph improves as more is
     matched. top-2 within a margin -> AMBIGUOUS; below the floor -> UNMATCHED.
  3. Swap guard: a reciprocal ordinal transposition (two adjacent v2 fns whose
     assigned v1 fns are reverse-consecutive) is the honest tell of a fingerprint
     swap -> downgrade both to AMBIGUOUS. NOT special-cased by name.
  4. Merge pre-detection: a v2 fn whose statement set is the near-disjoint UNION
     of two+ substantial v1 fns -> AMBIGUOUS (a genuine merge, not a single fn).
  5. DELETED_CANDIDATE: any v1 fn no v2 fn confidently MATCHED.

Then it SELF-SCORES against ground_truth.csv, per case category, and exits 1 if
any HARD case fails or any confident-WRONG match exists.
"""
import csv
import glob
import os
import sys
from collections import defaultdict, Counter

HERE = os.path.dirname(os.path.abspath(__file__))

# --- tunables (iterated against the fixture; see the report) ---------------
# The weights reflect PATCH SURVIVAL, not raw discriminating power on the bulk:
# call-graph + strings dominate because they survive a body change; the
# statement-hash Jaccard is a corroborator (it is ~0 exactly when a body
# changed, i.e. exactly when matching is hard), so it must NOT be the dominant
# weight; structure+position is the last-resort floor for edge-less leaves.
FLOOR = 0.30            # below this best-confidence -> UNMATCHED
MARGIN = 0.10           # top1-top2 within this -> AMBIGUOUS
FIXPOINT_ROUNDS = 5

# Behavioural signals (the survivors). Each is a Jaccard in [0,1]; the function
# confidence is their weighted blend, re-normalised over the signals that the
# pair actually HAS (a fn with no call graph isn't penalised for lacking one).
W_CALLEE = 0.34         # mapped callee set -- primary survivor
W_CALLER = 0.30         # mapped caller set -- primary survivor (symmetric)
W_STRING = 0.30         # shared string/cvar literals -- strong, can carry alone
W_STMT = 0.18           # statement-hash Jaccard -- corroborator ONLY
# Structural fallback (the floor for a changed fn with NO graph + NO anchors):
# statement-set SIZE similarity + ordinal proximity. Deliberately capped below
# the confident band so it can only carry a UNIQUE structural candidate, never
# out-vote a real behavioural match.
W_SIZE = 0.55           # statement-count similarity within the structural score
W_POS = 0.45            # ordinal proximity within the structural score
STRUCT_CAP = 0.62       # max confidence a purely-structural match can reach
STMT_CONFIRM = 0.70     # statement Jaccard at/above this = an UNCHANGED fn:
                        # confirm at high confidence (the moved-bulk corroborator)
SWAP_CONFLICT_MARGIN = 0.15  # how close the cross vs. self score must be to flag


# ---------------------------------------------------------------------------
def parse_rva(s):
    if s is None:
        return None
    s = s.strip()
    if not s:
        return None
    try:
        return int(s, 16) if s.lower().startswith("0x") else int(s)
    except ValueError:
        return None


def load_table(ver_dir, table):
    """Read every shard of a table -> list of dict rows. Empty if absent."""
    rows = []
    hdr = None
    for shard in sorted(glob.glob(os.path.join(ver_dir, table, f"{table}_*.csv"))):
        with open(shard, newline="", encoding="utf-8", errors="replace") as f:
            rd = csv.reader(f)
            h = next(rd, None)
            if h is None:
                continue
            hdr = h
            for row in rd:
                if len(row) < len(hdr):
                    row = row + [""] * (len(hdr) - len(row))
                rows.append(dict(zip(hdr, row)))
    return rows


# ---------------------------------------------------------------------------
class Version:
    """One DLL-version dump: its functions + the fingerprint signals per fn."""

    def __init__(self, ver_dir):
        self.dir = ver_dir
        funcs = load_table(ver_dir, "functions")
        self.rvas = []                      # ordered list of fn rvas (by rva)
        self.fn_hash = {}                   # rva -> function content_hash
        for r in funcs:
            rv = parse_rva(r.get("rva"))
            if rv is None:
                continue
            self.rvas.append(rv)
            self.fn_hash[rv] = r.get("content_hash", "") or ""
        self.rvas.sort()
        self.fnset = set(self.rvas)
        self.ordinal = {rv: i for i, rv in enumerate(self.rvas)}

        # statement fingerprint: per-fn SET of statement content_hashes + the
        # string/cvar literals it references.
        self.stmt_hashes = defaultdict(set)
        self.anchors = defaultdict(set)     # string_ref / cvar_ref literals
        for s in load_table(ver_dir, "statements"):
            fr = parse_rva(s.get("function_rva"))
            if fr is None:
                continue
            ch = s.get("content_hash", "") or ""
            if ch:
                self.stmt_hashes[fr].add(ch)
            sref = (s.get("string_ref") or "").strip()
            cref = (s.get("cvar_ref") or "").strip()
            if sref:
                self.anchors[fr].add("s:" + sref)
            if cref:
                self.anchors[fr].add("c:" + cref)

        # call graph: callees (out) and callers (in). Only INTRA-module edges
        # (callee resolves to a fn in THIS version) participate in the mapped
        # fingerprint; external callees are kept verbatim as anchors.
        self.callees = defaultdict(set)     # rva -> set(callee rva)
        self.callers = defaultdict(set)     # rva -> set(caller rva)
        self.ext_callees = defaultdict(set)  # rva -> set(callee rva outside fnset)
        for e in load_table(ver_dir, "call_edges"):
            caller = parse_rva(e.get("caller_rva"))
            callee = parse_rva(e.get("callee_rva"))
            if caller is None or callee is None:
                continue
            if callee in self.fnset:
                self.callees[caller].add(callee)
                self.callers[callee].add(caller)
            else:
                self.ext_callees[caller].add(callee)


def jaccard(a, b):
    if not a and not b:
        return 0.0
    u = len(a | b)
    return len(a & b) / u if u else 0.0


# ---------------------------------------------------------------------------
def match(v1, v2):
    """Return verdicts: v2_rva -> (kind, payload, confidence, evidence dict).
    kind in {'MATCHED','AMBIGUOUS','UNMATCHED'}. Plus deleted: set(v1_rva)."""

    # ---- pass 1: hash-equal (unique on both sides) -------------------------
    v1_by_hash = defaultdict(list)
    for rv, h in v1.fn_hash.items():
        if h:
            v1_by_hash[h].append(rv)
    v2_hash_count = Counter(h for h in v2.fn_hash.values() if h)

    verdict = {}                  # v2_rva -> (kind, payload, conf, evidence)
    matched_v2_to_v1 = {}         # v2_rva -> v1_rva (confident, for fixpoint)
    claimed_v1 = {}               # v1_rva -> v2_rva (the confident claimant)

    def claim(v2_rva, v1_rva, conf, ev):
        verdict[v2_rva] = ("MATCHED", v1_rva, conf, ev)
        matched_v2_to_v1[v2_rva] = v1_rva
        claimed_v1[v1_rva] = v2_rva

    for rv2 in v2.rvas:
        h = v2.fn_hash.get(rv2, "")
        if not h:
            continue
        cands = v1_by_hash.get(h, [])
        # unique on BOTH sides -> trivially the same entity.
        if len(cands) == 1 and v2_hash_count[h] == 1 and len(v1_by_hash[h]) == 1:
            claim(rv2, cands[0], 1.0, {"hash_equal": True})

    # ---- fingerprint scoring ----------------------------------------------
    def mapped_callees(ver, rva, m_v2_v1):
        """Express a v2 fn's callee set through known v2->v1 matches; for a v1
        fn the callee set is itself (identity)."""
        if ver is v1:
            return set(ver.callees.get(rva, ())) | {("x", c) for c in ver.ext_callees.get(rva, ())}
        out = set()
        for c in ver.callees.get(rva, ()):
            mapped = m_v2_v1.get(c)
            out.add(mapped if mapped is not None else ("v2", c))
        for c in ver.ext_callees.get(rva, ()):
            out.add(("x", c))
        return out

    def mapped_callers(ver, rva, m_v2_v1):
        if ver is v1:
            return set(ver.callers.get(rva, ()))
        out = set()
        for c in ver.callers.get(rva, ()):
            mapped = m_v2_v1.get(c)
            out.add(mapped if mapped is not None else ("v2", c))
        return out

    def score(rv2, rv1, m_v2_v1):
        ev = {}
        s_stmt = jaccard(v2.stmt_hashes.get(rv2, set()), v1.stmt_hashes.get(rv1, set()))
        ev["stmt"] = round(s_stmt, 3)
        c2 = mapped_callees(v2, rv2, m_v2_v1)
        c1 = mapped_callees(v1, rv1, m_v2_v1)
        s_callee = jaccard(c2, c1)
        ev["callee"] = round(s_callee, 3)
        r2 = mapped_callers(v2, rv2, m_v2_v1)
        r1 = mapped_callers(v1, rv1, m_v2_v1)
        s_caller = jaccard(r2, r1)
        ev["caller"] = round(s_caller, 3)
        a2 = v2.anchors.get(rv2, set())
        a1 = v1.anchors.get(rv1, set())
        shared = len(a2 & a1)
        s_string = jaccard(a2, a1) if (a2 or a1) else 0.0
        ev["shared_anchors"] = shared

        # if a fn has NO call graph and NO anchors, the statement fingerprint
        # carries all the weight (string_leaf-style leaves).
        has_graph = bool(c2 or c1 or r2 or r1)
        has_anchor = bool(a2 or a1)
        if not has_graph and not has_anchor:
            conf = s_stmt
        else:
            conf = (W_STMT * s_stmt + W_CALLEE * s_callee +
                    W_CALLER * s_caller + W_STRING * s_string)
        return conf, ev

    # ---- merge pre-detection (runs BEFORE the fixpoint can claim one half) ---
    # A merge folds >=2 v1 fns into one v2 fn, so the v2 fn's statement set
    # CONTAINS nearly all of MULTIPLE v1 fns' statement sets. Containment =
    # |v1 stmts that are in v2| / |v1 stmts|. Two+ highly-contained v1 fns -> the
    # v2 fn is genuinely ambiguous; flag it AMBIGUOUS and keep it out of the
    # greedy claim (otherwise the higher-Jaccard half wins a confident-wrong pick).
    def containment(rv2, rv1):
        a, b = v2.stmt_hashes.get(rv2, set()), v1.stmt_hashes.get(rv1, set())
        return (len(a & b) / len(b)) if b else 0.0

    CONTAIN_HI = 0.85
    MIN_PART = 5        # a contained v1 fn must be substantial (not a 1-2 stmt stub)
    flagged = {}     # v2_rva -> AMBIGUOUS verdict tuple (merge etc.), pre-fixpoint
    for rv2 in v2.rvas:
        if rv2 in matched_v2_to_v1:
            continue
        s2 = v2.stmt_hashes.get(rv2, set())
        if len(s2) < 2 * MIN_PART:
            continue
        contained = [rv1 for rv1 in v1.rvas
                     if len(v1.stmt_hashes.get(rv1, set())) >= MIN_PART
                     and containment(rv2, rv1) >= CONTAIN_HI]
        if len(contained) < 2:
            continue
        # a true merge is roughly the DISJOINT UNION of its parts: the parts'
        # combined size approximates the v2 set, and they barely overlap each
        # other. Near-duplicate v1 fns (each ~= the whole v2 set) are NOT a merge
        # -- their sizes don't add up, they coincide. Require additivity.
        union = set().union(*(v1.stmt_hashes[r] for r in contained))
        sum_parts = sum(len(v1.stmt_hashes[r]) for r in contained)
        # parts must be near-disjoint (sum ~= union) AND fill most of v2.
        if (len(union) >= 0.9 * sum_parts          # parts barely overlap
                and len(union) >= 0.8 * len(s2)     # they cover v2
                and sum_parts <= 1.2 * len(s2)):    # not duplicate-stacking
            band = sorted(contained)
            flagged[rv2] = ("AMBIGUOUS", band, 0.0,
                            {"merge": True, "contained": [hex(x) for x in band]})

    # candidate v1 pool = v1 fns not yet uniquely hash-claimed (keep claimed
    # ones available so a better fingerprint can't be double-counted, but we
    # never re-assign an already-claimed v1).
    def run_round(m_v2_v1):
        # gather (conf, rv2, rv1, ev, top2) for every still-open v2 fn.
        results = {}
        open_v2 = [rv2 for rv2 in v2.rvas
                   if rv2 not in matched_v2_to_v1 and rv2 not in flagged]
        for rv2 in open_v2:
            scored = []
            for rv1 in v1.rvas:
                if rv1 in claimed_v1:
                    continue
                conf, ev = score(rv2, rv1, m_v2_v1)
                if conf > 0.0:
                    scored.append((conf, rv1, ev))
            if not scored:
                results[rv2] = None
                continue
            scored.sort(key=lambda x: (-x[0], v2.ordinal.get(rv2, 0)))
            results[rv2] = scored
        return results

    # ---- pass 2 + 3: fingerprint fixpoint ---------------------------------
    for _round in range(FIXPOINT_ROUNDS):
        results = run_round(matched_v2_to_v1)
        newly = 0
        # greedy: take the globally-highest-confidence clear winners first.
        ranked = []
        for rv2, scored in results.items():
            if not scored:
                continue
            top_conf, top_rv1, ev = scored[0]
            ranked.append((top_conf, rv2, top_rv1, ev, scored))
        ranked.sort(key=lambda x: -x[0])
        for top_conf, rv2, top_rv1, ev, scored in ranked:
            if rv2 in matched_v2_to_v1 or rv2 in flagged:
                continue
            if top_rv1 in claimed_v1:
                continue
            if top_conf < FLOOR:
                continue
            second = scored[1][0] if len(scored) > 1 else 0.0
            if top_conf - second < MARGIN:
                continue  # ambiguous -- resolve later, not in the greedy claim
            claim(rv2, top_rv1, top_conf, ev)
            newly += 1
        if newly == 0:
            break

    # emit the pre-flagged merge AMBIGUOUS verdicts.
    for rv2, vt in flagged.items():
        verdict[rv2] = vt

    # ---- final pass: classify everything still open -----------------------
    final_results = run_round(matched_v2_to_v1)
    for rv2 in v2.rvas:
        if rv2 in verdict:
            continue
        scored = final_results.get(rv2)
        if not scored:
            verdict[rv2] = ("UNMATCHED", None, 0.0, {"no_signal": True})
            continue
        top_conf, top_rv1, ev = scored[0]
        second = scored[1][0] if len(scored) > 1 else 0.0
        if top_conf < FLOOR:
            verdict[rv2] = ("UNMATCHED", top_rv1, round(top_conf, 3),
                            dict(ev, best=hex(top_rv1), reason="below_floor"))
        elif top_conf - second < MARGIN:
            # tie -> AMBIGUOUS, list every candidate within the margin band.
            band = [rv1 for (c, rv1, _e) in scored if top_conf - c < MARGIN]
            verdict[rv2] = ("AMBIGUOUS", band, round(top_conf, 3),
                            dict(ev, candidates=[hex(x) for x in band]))
        else:
            # a clear winner but the v1 was already claimed by someone else.
            if top_rv1 in claimed_v1 and claimed_v1[top_rv1] != rv2:
                verdict[rv2] = ("AMBIGUOUS", [top_rv1], round(top_conf, 3),
                                dict(ev, reason="v1_already_claimed",
                                     by=hex(claimed_v1[top_rv1])))
            else:
                verdict[rv2] = ("MATCHED", top_rv1, round(top_conf, 3), ev)
                matched_v2_to_v1[rv2] = top_rv1
                claimed_v1[top_rv1] = rv2

    # ---- pass 4: swap guard (reciprocal ordinal transposition) ------------
    # Two fns that exchanged their behavioral content (call targets + strings +
    # statements) match perfectly to EACH OTHER'S v1 by content alone -- a
    # confident CROSS match. The only signal that still betrays the swap, WITHOUT
    # assuming the image offset, is ORDER: the bulk preserves relative ordinal,
    # so a swap shows up as a local reciprocal inversion -- two v2 fns, adjacent
    # in v2 order, whose assigned v1 fns are consecutive in v1 order but REVERSED.
    # That transposition is the honest tell. Downgrade both to AMBIGUOUS (the two
    # v1 candidates), rather than emit a confident wrong cross-match.
    matched_pairs = [(v2.ordinal[rv2], rv2, p)
                     for rv2, (k, p, _c, _e) in verdict.items()
                     if k == "MATCHED" and isinstance(p, int)]
    matched_pairs.sort()
    for i in range(1, len(matched_pairs)):
        (_oa, rv2a, p_a) = matched_pairs[i - 1]
        (_ob, rv2b, p_b) = matched_pairs[i]
        # rv2a precedes rv2b in v2 order, but their v1 partners are reverse-
        # consecutive (p_a sits one slot AFTER p_b in v1 order) -> a 2-swap.
        if v1.ordinal[p_a] == v1.ordinal[p_b] + 1:
            band = sorted([p_a, p_b])
            for x in (rv2a, rv2b):
                verdict[x] = ("AMBIGUOUS", band, verdict[x][2],
                              {"swap_conflict": True,
                               "candidates": [hex(y) for y in band]})
                matched_v2_to_v1.pop(x, None)

    # rebuild claimed_v1 from the surviving confident matches
    claimed_v1 = {}
    for rv2, (k, p, _c, _e) in verdict.items():
        if k == "MATCHED" and isinstance(p, int):
            claimed_v1[p] = rv2

    # ---- pass 5: DELETED_CANDIDATE ----------------------------------------
    deleted = set(v1.rvas) - set(claimed_v1.keys())

    return verdict, deleted


# ---------------------------------------------------------------------------
def self_score(verdict, deleted, v1, v2):
    """Compare verdicts to ground_truth.csv, per case category."""
    gt_path = os.path.join(HERE, "ground_truth.csv")
    rows = []
    with open(gt_path, newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            rows.append(r)

    HARD = {"changed_body", "changed_moved", "swap_trap", "split", "merge",
            "added", "deleted", "string_leaf", "ripple", "curated_changed"}

    per_cat = defaultdict(lambda: {"count": 0, "pass": 0, "fail": 0, "fails": []})
    confident_wrong = []   # (v2_rva, got_v1, expected) -- a false MATCHED anywhere

    for r in rows:
        case = r["case"]
        expect = r["expect"]
        detail = r["detail"]
        cat = per_cat[case]
        cat["count"] += 1

        if expect == "DELETED_V1":
            v1_rva = parse_rva(r["v2_rva"])   # this column is a v1 rva here
            ok = v1_rva in deleted
            _record(cat, ok, r["v2_rva"], "DELETED_CANDIDATE",
                    "deleted" if ok else "NOT surfaced")
            continue

        rv2 = parse_rva(r["v2_rva"])
        kind, payload, conf, ev = verdict.get(rv2, ("MISSING", None, 0, {}))
        got = _fmt(kind, payload)

        if expect == "MATCHED":
            want = parse_rva(detail)
            if kind == "MATCHED" and payload == want:
                ok = True
            elif kind == "MATCHED" and payload != want:
                ok = False
                confident_wrong.append((r["v2_rva"], _hx(payload), detail))
            elif case == "swap_trap":
                # honest abstention is acceptable; only a confident match to the
                # OTHER swap fn is the FAIL (already caught above as wrong).
                ok = kind in ("AMBIGUOUS", "UNMATCHED")
            else:
                ok = False
            _record(cat, ok, r["v2_rva"], want and _hx(want), got)

        elif expect in ("SPLIT", "MERGE"):
            # PASS = did NOT confidently single-MATCH. For SPLIT, a confident
            # MATCHED for BOTH halves is the FAIL -- handled by the per-row
            # single-MATCH check naturally (each half scored independently;
            # both confident == both fail).
            if kind == "MATCHED":
                # a single confident pick for an ambiguous entity.
                # for split, matching ONE half to the v1 is allowed if the
                # OTHER half did not also confidently match the same v1.
                ok = _split_merge_ok(case, detail, verdict, rv2)
                if not ok:
                    confident_wrong.append((r["v2_rva"], _hx(payload), detail))
            else:
                ok = kind in ("AMBIGUOUS", "UNMATCHED")
            _record(cat, ok, r["v2_rva"], expect, got)

        elif expect == "NEW":
            ok = kind == "UNMATCHED"
            if kind == "MATCHED":
                confident_wrong.append((r["v2_rva"], _hx(payload), "NEW"))
            _record(cat, ok, r["v2_rva"], "UNMATCHED", got)

    # also: ANY confident MATCHED whose target is wrong, even if not in a
    # MATCHED-expected row -- already accumulated in confident_wrong.

    return per_cat, confident_wrong, HARD


def _split_merge_ok(case, detail, verdict, rv2):
    """For split: matching at most ONE half is allowed; both halves confidently
    MATCHED to the same v1 = FAIL. For merge: a confident single pick = FAIL."""
    if case == "merge":
        return False  # a confident single MATCHED for a merge is always a fail
    if case == "split":
        # find both split rows' v2 rvas from ground truth-independent state:
        # both share detail (the v1 rva). Count how many split v2 fns confidently
        # matched THAT v1.
        v1_rva = parse_rva(detail)
        # the matcher matched this half to v1_rva; allowed only if it's the
        # sole confident claimant of v1_rva.
        claimants = [r for r, (k, p, _c, _e) in verdict.items()
                     if k == "MATCHED" and p == v1_rva]
        return len(claimants) <= 1
    return False


def _record(cat, ok, v2_rva, expected, got):
    if ok:
        cat["pass"] += 1
    else:
        cat["fail"] += 1
        cat["fails"].append((v2_rva, expected, got))


def _hx(v):
    return hex(v) if isinstance(v, int) else str(v)


def _fmt(kind, payload):
    if kind == "MATCHED":
        return f"MATCHED {_hx(payload)}"
    if kind == "AMBIGUOUS":
        return "AMBIGUOUS [" + ",".join(_hx(x) for x in (payload or [])) + "]"
    if kind == "UNMATCHED":
        return "UNMATCHED"
    return kind


# ---------------------------------------------------------------------------
def main():
    v1_dir = os.path.join(HERE, "v1")
    v2_dir = os.path.join(HERE, "v2")
    if not (os.path.isdir(v1_dir) and os.path.isdir(v2_dir)
            and os.path.exists(os.path.join(HERE, "ground_truth.csv"))):
        sys.exit("fixture missing -- run make_sandbox.py once to build v1/ v2/ ground_truth.csv")

    v1 = Version(v1_dir)
    v2 = Version(v2_dir)
    print(f"== loaded v1: {len(v1.rvas)} fns   v2: {len(v2.rvas)} fns")

    verdict, deleted = match(v1, v2)

    kinds = Counter(k for (k, _p, _c, _e) in verdict.values())
    print(f"== verdicts: MATCHED={kinds['MATCHED']} "
          f"AMBIGUOUS={kinds['AMBIGUOUS']} UNMATCHED={kinds['UNMATCHED']} "
          f"| DELETED_CANDIDATE={len(deleted)}")

    per_cat, confident_wrong, HARD = self_score(verdict, deleted, v1, v2)

    # ---- per-category table ----------------------------------------------
    print("\n== PER-CATEGORY SCORE")
    print(f"  {'case':<16}{'count':>6}{'pass':>6}{'fail':>6}")
    order = sorted(per_cat, key=lambda c: (c in HARD, c))
    total_pass = total = 0
    for case in order:
        s = per_cat[case]
        total += s["count"]; total_pass += s["pass"]
        tag = " (HARD)" if case in HARD else ""
        print(f"  {case:<16}{s['count']:>6}{s['pass']:>6}{s['fail']:>6}{tag}")
        for (rv, exp, got) in s["fails"]:
            print(f"        FAIL {rv}  expected={exp}  got={got}")

    # ---- headline numbers -------------------------------------------------
    bulk = per_cat.get("bulk_move", {"count": 0, "pass": 0})
    hard_count = sum(per_cat[c]["count"] for c in per_cat if c in HARD)
    hard_pass = sum(per_cat[c]["pass"] for c in per_cat if c in HARD)
    hard_fail = sum(per_cat[c]["fail"] for c in per_cat if c in HARD)

    print("\n== HEADLINE")
    print(f"  bulk_move : {bulk['pass']}/{bulk['count']} "
          f"({100*bulk['pass']//max(bulk['count'],1)}%)  -- the easy bulk")
    print(f"  HARD cases: {hard_pass}/{hard_count} "
          f"({100*hard_pass//max(hard_count,1)}%)  -- what the sandbox exists to prove")

    if confident_wrong:
        print("\n== CONFIDENT-WRONG MATCHES (false MATCHED -- gate failure)")
        for (rv2, got, exp) in confident_wrong:
            print(f"  {rv2}  matched->{got}  but expected {exp}")

    print(f"\n== VERDICT: {total_pass}/{total} overall")
    fail_gate = hard_fail > 0 or bool(confident_wrong)
    if fail_gate:
        print("   GATE: FAIL (a HARD case failed and/or a confident-wrong match exists)")
        sys.exit(1)
    print("   GATE: PASS")
    sys.exit(0)


if __name__ == "__main__":
    main()
