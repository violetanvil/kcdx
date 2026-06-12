"""seeds_shared.reverify_resolver -- the bulk re-verify RESOLVE seam (design D39).

WHAT THIS IS (design data/maintainer-tool/design.md D39 / D34 / D35 / D29-rev / D17a)
-------------------------------------------------------------------------------------
The data-core RESOLVES + COMPUTES the bulk re-verify edit-specs FROM the v3
verification report (the frontend sends report rows + the maintainer's confirm,
never pre-computed edits -- D39). The v3 report row carries `kcdx_id` + resolved
`version` + `verdict` + `method_rank` + `matched_address_version_id` (a DB integer
id), but NOT `valid_from_version` / `valid_through` / `last_verified_at_version` (the
write-path identity + the current values the deltas need) -- and only the data-core
speaks DB ids + owns the interval logic (law 6, the data-core is the sole writer). So
this module reads the DB and produces the per-row edit-specs in the
`{kcdx_id, valid_from_version, edits}` shape `/confirm/batch` (the 6.2 endpoint)
already consumes -- the write MECHANISM is unchanged (D19); only the edit-spec SOURCE
moved from the FE to here.

NO WRITE -- this module READS the DB (READ-ONLY, the read_api `_open_ro` seam) and
COMPUTES the edit-specs. The batch write path (`update_version_rows_batch` /
`_apply_one_db`) transacts them; the resolver never opens a write connection (law 6 --
the data-core's existing write path is the sole writer).

THE TWO PATHS (D39)
-------------------
VERIFY-ALL (`verified_working` / `passed_not_verified` rows -- the verified block):
  reads the matched row BY `matched_address_version_id` -> its
  {kcdx_id, valid_from_version} (the write-path key) + its current
  valid_through / last_verified_at_version, and computes:
    - the AUDIT TRIO (D17a): last_verified_at_version -> the report version;
      verified_date -> today; verified_by -> the injected identity.
    - the PROOF-RANK-KEYED evidence_kind (D29-rev): a rank-1 `verified_working` row
      (OBSERVED live execution) -> `live_production`; a ranks-2-5
      `passed_not_verified` row (the in-game STATIC pass) -> `live_test_plugin`.
    - the D34 GAP-EXTENSION: valid_through_version -> the report version WHEN the
      swept version sat BEYOND the matched row's interval. An OPEN row
      (valid_through NULL) covers everything from valid_from onward (src/refdb.cpp:
      "the open row covers everything from its valid_from onward"), so its interval
      already covers the swept version -> NO valid_through edit. A CLOSED row whose
      valid_through already covers the swept version -> NO valid_through edit. Only a
      CLOSED row whose valid_through is BEFORE the swept version gets the extension.
  A row already verified at OR BEYOND the swept version
  (last_verified_at_version >= swept) is SKIPPED -- nothing to add (re-verifying at a
  version the row already covers adds nothing -- D34).

CLOSE-INTERVALS (`failed` rows -- the failing block; matched_address_version_id is
  NULL by the report's attribution invariant): resolves the target row
  DETERMINISTICALLY as the address_version row of `kcdx_id` whose interval CONTAINS
  the resolved `version` -- a `failed` row's version was definitionally covered (a
  build version in a GAP is `not_applicable`, NOT `failed`, and `not_applicable` is
  shown-no-action, never in the failing block), so exactly one row contains it
  (intervals are non-overlapping per entity). Computes valid_through_version -> that
  row's last_verified_at_version (the D35 retract -- close the over-claimed interval
  back to the last version with positive evidence).

VERDICT ROUTING (D39 / D28 / D36):
  {verified_working, passed_not_verified} -> verify-all
  failed                                  -> close-intervals
  {not_applicable, cannot_check, skipped, error} -> NO edit-spec (shown-no-action).

THE valid_through WRITE TARGET (D40): the resolver computes valid_through; D40 settles
WHERE it lands -- the AUTHORED `valid_through_version` column (in EDITABLE_VERSION_COLUMNS,
the tag form resolved to the game_versions.id FK exactly as valid_from_version
resolves). The 6.2a-fix landed the write path that ACCEPTS valid_through_version (the
_apply_one_db interval-edit branch); this resolver emits it.
"""
import datetime
import os
import sqlite3

# The read-only DB-open + FK->ordinal/tag resolution the read surface already owns
# (law 6 -- one place resolves a game_versions.id FK to its ordinal/tag). The resolver
# reuses _open_ro (the same sqlite URI mode=ro the read endpoints use) + the two FK maps
# rather than re-querying game_versions itself.
from .read_api import (
    _open_ro,
    _version_ordinals,
    _version_tags,
    _dict_decode,
    DbReadError,
)

# The verdicts that route to verify-all (the verified block -- the strongest applicable
# active attempt PASSED). The report schema's 7-state enum; these two are the
# verified-block ceiling (verified_working = observed live execution; passed_not_verified
# = a passing static/safe-read attempt, execution not observed).
_VERIFY_ALL_VERDICTS = frozenset({"verified_working", "passed_not_verified"})
# The single verdict that routes to close-intervals (the failing block).
_FAILED_VERDICT = "failed"
# The no-action set (shown, no edit). Listed for the routing audit -- a verdict in
# neither the verify-all set nor the failed verdict produces no edit-spec (D28/D36).
_NO_ACTION_VERDICTS = frozenset({"not_applicable", "cannot_check", "skipped", "error"})

# The proof-rank -> evidence_kind mapping (D29-rev). The report's verdict already
# encodes the rank split (verified_working == rank 1 == observed live execution;
# passed_not_verified == ranks 2-5 == a passing static/safe-read attempt), so the
# verdict keys the evidence tier directly -- a rank-1 row records live_production (it
# truly ran in the real game), a ranks-2-5 row records live_test_plugin (the in-game
# test plugin checked it but did not observe execution). Both are existing enum members
# (policy.md "evidence_kind enum") -- no frozen-schema churn (D29).
_EVIDENCE_KIND_VERIFIED_WORKING = "live_production"      # rank 1 (observed execution)
_EVIDENCE_KIND_PASSED_NOT_VERIFIED = "live_test_plugin"  # ranks 2-5 (static pass)


class ReverifyResolveError(RuntimeError):
    """The report row could not be resolved against the curated DB -- a structural
    mismatch the maintainer must see, NOT a silent skip (silent-success is the
    anti-pattern). Raised for: a verify-all row whose matched_address_version_id names
    no address_versions row (a stale report id), an unknown report `version` tag (no
    game_versions row), or a close-intervals row with no interval-containing target
    (the attribution invariant violated -- a `failed` row whose version no interval
    covers). The caller (the /save/reverify-batch endpoint) surfaces it; nothing is
    written either way (the resolver never writes)."""


def _evidence_kind_for_verdict(verdict):
    """The proof-rank-keyed evidence_kind (D29-rev). verified_working (rank 1, observed
    live execution) -> live_production; passed_not_verified (ranks 2-5, the in-game
    static pass) -> live_test_plugin. Only the two verify-all verdicts reach here."""
    if verdict == "verified_working":
        return _EVIDENCE_KIND_VERIFIED_WORKING
    # passed_not_verified -- the only other verify-all verdict.
    return _EVIDENCE_KIND_PASSED_NOT_VERIFIED


# The address_versions columns the resolver reads -- the interval window + the audit
# trio (the cells the verify-all edit touches, so the OLD values for the field-delta
# come from the same read; close-intervals reads the same shape for the window).
_AV_READ_COLS = ("id", "kcdx_id", "valid_from", "valid_through",
                 "last_verified_at_version", "verified_by", "verified_date",
                 "evidence_kind")
_AV_READ_SELECT = ", ".join(_AV_READ_COLS)


def _av_row_by_id(con, av_id):
    """The address_versions row BY its PRIMARY KEY id -- the matched_address_version_id
    the verify-all path reads. Returns the sqlite3.Row or None (a stale report id)."""
    return con.execute(
        f"SELECT {_AV_READ_SELECT} FROM address_versions WHERE id = ?",
        (av_id,)).fetchone()


def _interval_containing_row(con, kcdx_id, swept_ordinal, gv_ordinals):
    """The address_versions row of `kcdx_id` whose interval CONTAINS the swept version
    (the close-intervals target -- D39). An interval is [valid_from, valid_through];
    an OPEN row (valid_through NULL) covers everything from valid_from onward
    (src/refdb.cpp). Intervals are non-overlapping per entity, so AT MOST ONE row
    contains a version. Returns the sqlite3.Row or None (no interval covers it -- the
    attribution invariant violated for a `failed` row).

    Containment is on ORDINALS (policy.md "ordinal compare"): valid_from_ordinal <=
    swept AND (valid_through is NULL OR swept <= valid_through_ordinal)."""
    out = None
    for r in con.execute(
            f"SELECT {_AV_READ_SELECT} FROM address_versions WHERE kcdx_id = ?",
            (kcdx_id,)):
        vf_ord = gv_ordinals.get(r["valid_from"])
        if vf_ord is None or vf_ord > swept_ordinal:
            continue
        vt = r["valid_through"]
        if vt is None:
            # Open row -- covers everything from valid_from onward (src/refdb.cpp).
            out = r
            break
        vt_ord = gv_ordinals.get(vt)
        if vt_ord is not None and swept_ordinal <= vt_ord:
            out = r
            break
    return out


def _saved_cells(row, columns, *, gv_tags, ek_decode):
    """The OLD (current-DB) cell values for `columns`, in the SAME tag/string form the
    `edits` dict + the field-delta render -- so field_delta(saved, edits) yields the
    per-row `old -> new` the FE shows. A version FK column (last_verified_at_version /
    valid_through_version) resolves id->tag; evidence_kind decodes its dict-id->string;
    verified_by / verified_date pass through. A NULL is '' (the seed-cell convention)."""
    saved = {}
    for col in columns:
        if col == "valid_through_version":
            saved[col] = gv_tags.get(row["valid_through"]) or ""
        elif col == "last_verified_at_version":
            saved[col] = gv_tags.get(row["last_verified_at_version"]) or ""
        elif col == "evidence_kind":
            ek = row["evidence_kind"]
            saved[col] = (ek_decode.get(ek, ek) if ek is not None else "")
        else:
            v = row[col]
            saved[col] = "" if v is None else str(v)
    return saved


def _verify_all_edits(row, *, swept_tag, swept_ordinal, verdict, verified_by, today,
                      gv_ordinals):
    """The verify-all `edits` dict for ONE matched row, or None when the row is already
    covered (nothing to add). Computes the audit trio + the proof-rank evidence_kind +
    the D34 gap-extension.

    The SKIP (D34): a row already verified AT OR BEYOND the swept version
    (last_verified_at_version >= swept) needs no edit -- re-verifying at a version the
    row already covers adds nothing. Returns None.

    Otherwise the trio + evidence_kind are written; the valid_through gap-extension is
    ADDED only when the matched row's interval does NOT already cover the swept version
    -- i.e. the row is CLOSED (valid_through not NULL) AND its valid_through is BEFORE
    the swept version. An OPEN row covers forward (src/refdb.cpp), so no valid_through
    edit; a CLOSED row whose valid_through already covers swept, likewise."""
    lvv_ord = gv_ordinals.get(row["last_verified_at_version"])
    # SKIP: already verified at or beyond the swept version -- nothing to add (D34). A
    # NULL last_verified (never verified) is NOT covered, so it always gets the trio.
    if lvv_ord is not None and lvv_ord >= swept_ordinal:
        return None

    # The audit trio (D17a) + the proof-rank evidence_kind (D29-rev). The trio is
    # all-set-together (policy.md): last_verified -> swept, verified_date -> today,
    # verified_by -> the injected identity; evidence_kind -> the verdict's proof tier.
    edits = {
        "last_verified_at_version": swept_tag,
        "verified_date": today,
        "verified_by": verified_by,
        "evidence_kind": _evidence_kind_for_verdict(verdict),
    }

    # The D34 gap-extension: extend a CLOSED interval forward to the swept version when
    # the swept version sits BEYOND it. An OPEN row (valid_through NULL) already covers
    # forward -> no edit. A CLOSED row whose valid_through_ordinal < swept -> extend.
    vt = row["valid_through"]
    if vt is not None:
        vt_ord = gv_ordinals.get(vt)
        if vt_ord is not None and vt_ord < swept_ordinal:
            edits["valid_through_version"] = swept_tag

    return edits


def resolve_reverify_batch(out_dir, rows, *, action, verified_by, today=None):
    """Resolve the v3 report rows for ONE batch action into the per-row edit-specs the
    /confirm/batch endpoint transacts (design D39). READS the DB (READ-ONLY); writes
    NOTHING.

    ARGS
      out_dir      -- the checkout's data dir (where reference.sqlite lives; the same
                      out_dir the read endpoints resolve). Opened READ-ONLY.
      rows         -- the report's actionable rows for THIS action, each a mapping
                      carrying at least: kcdx_id (int), version (str -- the resolved
                      tag), verdict (str -- the 7-state enum), method_rank (int),
                      matched_address_version_id (int or None).
      action       -- "verify-all" | "close-intervals" -- which block this batch is.
                      The caller pre-filters the rows to the action's verdict set
                      (verify-all -> {verified_working, passed_not_verified};
                      close-intervals -> {failed}); a row whose verdict does not match
                      the action is a caller bug (a routing error) -> ReverifyResolveError.
      verified_by  -- the injected commit identity (D17a -- the resolved author name the
                      endpoint passes; the same identity that authors the git commit).
                      Used for the verify-all trio's verified_by cell. Ignored by
                      close-intervals (it writes no trio).
      today        -- the verified_date value (YYYY-MM-DD). Defaults to today's date;
                      injectable so a test is deterministic. Used by verify-all only.

    RETURNS the list of edit-specs in the {kcdx_id, valid_from_version, edits} shape,
    each ALSO carrying `saved` (the OLD cell values for the edited columns, so the
    endpoint computes the per-row field-delta purely via field_delta(saved, edits)):
        [{"kcdx_id": int, "valid_from_version": str, "edits": {col: new, ...},
          "saved": {col: old, ...}}, ...]
      -- the {kcdx_id, valid_from_version, edits} subset is exactly the BatchRowSpec
      shape /confirm/batch consumes (it ignores `saved`). A verify-all row whose matched
      row is already covered produces NO edit-spec (skipped -- nothing to add); a
      no-action verdict would never reach here (the caller filters it). The order
      follows `rows` (a verify-all skip drops out, preserving the rest).

    RAISES ReverifyResolveError on a structural mismatch (a stale matched id, an unknown
      version tag, a missing close-target, a verdict/action mismatch) -- surfaced loudly,
      never a silent skip (silent-success is the anti-pattern). DbReadError when no
      curated DB resolves under out_dir (the read-surface's missing-DB signal)."""
    if action not in ("verify-all", "close-intervals"):
        raise ReverifyResolveError(
            f"unknown reverify action {action!r} (expected 'verify-all' or "
            f"'close-intervals')")
    if today is None:
        today = datetime.date.today().isoformat()

    con = _open_ro(out_dir)
    try:
        gv_ordinals = _version_ordinals(con)
        gv_tags = _version_tags(con)
        # The evidence_kind dict-decode (id -> string) -- the OLD evidence_kind cell is
        # stored as a dict id; the field-delta `old` must render its string value.
        ek_decode = _dict_decode(con, "address_versions", "evidence_kind")
        # The tag -> game_versions.id reverse map (the report carries a version TAG;
        # the resolver compares ordinals + keys edit-specs by the row's valid_from TAG).
        tag_to_id = {tag: gvid for gvid, tag in gv_tags.items()}

        specs = []
        for r in rows:
            verdict = r["verdict"]
            swept_tag = r["version"]
            kcdx_id = r["kcdx_id"]

            # The report version must resolve to a known game version -- the swept
            # version the in-game sweep ran at. An unknown tag is a stale/foreign report
            # (the report was produced against a build the DB does not know).
            swept_gvid = tag_to_id.get(swept_tag)
            if swept_gvid is None:
                raise ReverifyResolveError(
                    f"report row (kcdx_id={kcdx_id}) names version {swept_tag!r}, "
                    f"which is not a known game version in the curated DB")
            swept_ordinal = gv_ordinals.get(swept_gvid)

            if action == "verify-all":
                if verdict not in _VERIFY_ALL_VERDICTS:
                    raise ReverifyResolveError(
                        f"verify-all received a {verdict!r} row (kcdx_id={kcdx_id}); "
                        f"only {sorted(_VERIFY_ALL_VERDICTS)} route to verify-all")
                av_id = r["matched_address_version_id"]
                if av_id is None:
                    raise ReverifyResolveError(
                        f"verify-all row (kcdx_id={kcdx_id}, verdict={verdict!r}) "
                        f"carries a NULL matched_address_version_id -- the report's "
                        f"attribution invariant requires a verified-block row to name "
                        f"its matched row")
                row = _av_row_by_id(con, av_id)
                if row is None:
                    raise ReverifyResolveError(
                        f"verify-all row names matched_address_version_id={av_id}, "
                        f"which is no address_versions row in the curated DB (a stale "
                        f"report id)")
                edits = _verify_all_edits(
                    row, swept_tag=swept_tag, swept_ordinal=swept_ordinal,
                    verdict=verdict, verified_by=verified_by, today=today,
                    gv_ordinals=gv_ordinals)
                if edits is None:
                    # Already covered (last_verified >= swept) -- nothing to add (D34).
                    continue
                valid_from_tag = gv_tags.get(row["valid_from"])
                specs.append({
                    "kcdx_id": row["kcdx_id"],
                    "valid_from_version": valid_from_tag,
                    "edits": edits,
                    "saved": _saved_cells(row, edits.keys(), gv_tags=gv_tags,
                                          ek_decode=ek_decode),
                })
            else:
                # close-intervals: the target is the interval-CONTAINING row of kcdx_id
                # at the resolved version (deterministic -- non-overlapping intervals).
                if verdict != _FAILED_VERDICT:
                    raise ReverifyResolveError(
                        f"close-intervals received a {verdict!r} row "
                        f"(kcdx_id={kcdx_id}); only {_FAILED_VERDICT!r} routes to "
                        f"close-intervals")
                target = _interval_containing_row(
                    con, kcdx_id, swept_ordinal, gv_ordinals)
                if target is None:
                    raise ReverifyResolveError(
                        f"close-intervals row (kcdx_id={kcdx_id}) at version "
                        f"{swept_tag!r} has no interval-containing address_versions row "
                        f"-- the attribution invariant is violated (a `failed` row's "
                        f"version must be covered by exactly one interval)")
                # The D35 retract: close valid_through to the row's last_verified_at_version
                # (the last version with positive evidence -- the sweep disproved validity
                # beyond it). A target with a NULL last_verified has never been verified;
                # there is no last-passing version to retract to -- surface it (a `failed`
                # row whose covering interval was never verified is itself a contradiction).
                lvv_id = target["last_verified_at_version"]
                lvv_tag = gv_tags.get(lvv_id) if lvv_id is not None else None
                if lvv_tag is None:
                    raise ReverifyResolveError(
                        f"close-intervals target (kcdx_id={kcdx_id}, "
                        f"address_version id={target['id']}) has no "
                        f"last_verified_at_version to retract valid_through to -- the "
                        f"covering interval was never verified")
                valid_from_tag = gv_tags.get(target["valid_from"])
                close_edits = {"valid_through_version": lvv_tag}
                specs.append({
                    "kcdx_id": target["kcdx_id"],
                    "valid_from_version": valid_from_tag,
                    "edits": close_edits,
                    "saved": _saved_cells(target, close_edits.keys(), gv_tags=gv_tags,
                                          ek_decode=ek_decode),
                })
        return specs
    finally:
        con.close()
