"""seeds_shared.validators -- seed-CSV readers + the cross-row integrity checks.

Holds every rule that decides whether the seed triple is well-formed, shared by
the rebuild path and the future incremental `apply` path so a seed that is valid
for one is valid for the other:

  Per-file readers (fail-loud on a malformed row):
    - read_module_seed
    - read_address_names_seed
    - read_address_versions_seed
    - _read_canonical_seed     (the shared canonical-id reader)

  Cross-row checks (run after all three files are loaded -- they need the whole
  seed state, not one row):
    - resolve_and_check_name_refs   -- supersession/deprecation pair integrity +
                                       name/tag FK resolution (mutates the rows,
                                       replacing seed strings with resolved ids)
    - check_supersession_acyclic    -- the superseded_by graph has no cycle
    - check_kcdx_id_known           -- a versions-seed kcdx_id has a names row
    - check_every_entity_covered    -- every named entity has >=1 versions row
                                       for the baseline version

Moved verbatim out of import_to_sqlite.py (db-updator Phase 1, step 1). The
audit-trio integrity, the (kcdx_id, valid_from_version) uniqueness, the
verified_date format, and the evidence_kind enum check were ALREADY enforced
inside read_address_versions_seed; they stay there (per-file, per-row rules) and
are not duplicated here. The cross-row checks that were INLINE in build_rows are
factored into the named functions above so build_rows (and a future apply path)
call them rather than re-implementing them.

Imports the EVIDENCE_KIND_ENUM from schema.py (one-way: schema holds the enums,
validators consume them -- no circular import).
"""
import csv
import re

from .schema import EVIDENCE_KIND_ENUM

# Allow very large quoted CSV fields (seed notes can be long). Set on import so
# any consumer of this module gets the raised limit without re-stating it.
csv.field_size_limit(1 << 24)

_VERIFIED_DATE_RE = re.compile(r"^\d{4}-\d{2}-\d{2}$")

# An AOB token is either a 2-hex byte (e.g. 48, 8B, ff) or a wildcard (? or ??).
# The wildcard mask is FOLDED INTO survival_aob (a '?' token = "this byte is a
# don't-care"), so there is NO separate mask column. Tokens are whitespace-
# separated; an empty value is allowed (step 5.2 hasn't filled it yet).
_AOB_TOKEN_RE = re.compile(r"^(?:[0-9A-Fa-f]{2}|\?\??)$")


# ---------------------------------------------------------------------------
# Per-file seed readers.
# ---------------------------------------------------------------------------
def _read_canonical_seed(path, kind):
    """Read a seed CSV with a canonical maintainer-supplied `id` column. Skips
    '#'-prefixed comment lines. Fails LOUD on:
      - any row whose `id` is empty or non-integer (no autoincrement; the id
        must be supplied by the maintainer)
      - any duplicate id within the file

    Returns list[dict] of rows. `kind` is "address" or "module" -- only used in
    error messages.
    """
    rows = []
    with open(path, newline="", encoding="utf-8", errors="replace") as f:
        lines = [ln for ln in f if not ln.lstrip().startswith("#")]
    rd = csv.DictReader(lines)
    seen_ids = {}
    for lineno, r in enumerate(rd, start=2):     # header is line 1
        rid_raw = (r.get("id") or "").strip()
        if not rid_raw:
            raise RuntimeError(
                f"{kind}_seed.csv:{lineno}: empty id (canonical ids are "
                f"maintainer-supplied; no autoincrement)")
        try:
            rid = int(rid_raw)
        except ValueError:
            raise RuntimeError(
                f"{kind}_seed.csv:{lineno}: id={rid_raw!r} is not an integer")
        if rid in seen_ids:
            raise RuntimeError(
                f"{kind}_seed.csv:{lineno}: duplicate id={rid} (first seen "
                f"at line {seen_ids[rid]})")
        seen_ids[rid] = lineno
        rows.append(r)
    return rows


def read_module_seed(path):
    """Read module_seed.csv. Schema: id, name, path. The id is the canonical
    modules.id; name + path are the maintainer-supplied module identity. Both
    name and path are required (empty = HARD ERROR)."""
    rows = _read_canonical_seed(path, "module")
    for r in rows:
        name = (r.get("name") or "").strip()
        modpath = (r.get("path") or "").strip()
        if not name:
            raise RuntimeError(
                f"module_seed.csv: row id={r['id']} has empty `name`")
        if not modpath:
            raise RuntimeError(
                f"module_seed.csv: row id={r['id']} has empty `path`")
    return rows


def read_address_names_seed(path):
    """Read address_names_seed.csv. Schema:
      id, name,
      superseded_by, superseded_at_version,
      is_deprecated, deprecated_at_version, deprecation_replacement,
      notes

    The id is the canonical address_names.id (== kcdx_id, the stable cross-
    version handle plugins reference). `name` is required (empty = HARD ERROR).
    Supersession + deprecation pair integrity is resolved later (after the
    versions seed is read; the cross-row pass resolve_and_check_name_refs runs
    the cross-row validation).

    The legacy `source` column was DROPPED 2026-05-28 -- it carried no
    information ("verified" duplicating status, which is now derived from
    last_verified_at_version + the entity-level supersession/deprecation
    flags, not an authored column)."""
    rows = _read_canonical_seed(path, "address_names")
    for r in rows:
        name = (r.get("name") or "").strip()
        if not name:
            raise RuntimeError(
                f"address_names_seed.csv: row id={r['id']} has empty `name`")
    return rows


def read_address_versions_seed(path):
    """Read address_versions_seed.csv. Schema:
      kcdx_id, valid_from_version, module, rva, signature,
      last_verified_at_version, verified_by, verified_date, evidence_kind

    No `id` column -- row identity is the (kcdx_id, valid_from_version) tuple
    (a 1.5 row + a 1.6 row for the same entity is two rows, each with its own
    valid_from_version). `kcdx_id`, `valid_from_version`, `module` are REQUIRED
    (empty = HARD ERROR). `rva`/`signature` MAY be empty (vtable_index kind has
    no RVA; un-verified rows may lack a signature).

    Verification audit pair:
      - When last_verified_at_version is set, verified_by + verified_date +
        evidence_kind ALL three must be set. (Else "verified by what / when /
        how?" is unanswerable.)
      - When last_verified_at_version is NULL/empty, all three of verified_by
        / verified_date / evidence_kind MUST be empty.
      - last_verified_at_version >= valid_from_version (you can't verify a
        row for a version older than the version the row claims to start at).
      - verified_date format: YYYY-MM-DD.
      - evidence_kind: must be in EVIDENCE_KIND_ENUM when set.

    The `status` column was REMOVED 2026-05-28 -- status is derived at query
    time from (current_version, valid_from_version, last_verified_at_version)
    plus the entity-level supersession/deprecation flags on address_names. See
    policy.md for the derivation rule.

    Fails LOUD on:
      - missing required column
      - duplicate (kcdx_id, valid_from_version) tuple
      - audit-pair integrity violation
      - last_verified_at_version < valid_from_version
      - malformed verified_date
      - unknown evidence_kind

    Returns list[dict] in file order."""
    rows = []
    with open(path, newline="", encoding="utf-8", errors="replace") as f:
        lines = [ln for ln in f if not ln.lstrip().startswith("#")]
    rd = csv.DictReader(lines)
    seen = {}    # (kcdx_id:int, valid_from_version:str) -> lineno
    for lineno, r in enumerate(rd, start=2):
        kid_raw = (r.get("kcdx_id") or "").strip()
        vfv     = (r.get("valid_from_version") or "").strip()
        module  = (r.get("module") or "").strip()
        lvv     = (r.get("last_verified_at_version") or "").strip()
        vby     = (r.get("verified_by") or "").strip()
        vdate   = (r.get("verified_date") or "").strip()
        ekind   = (r.get("evidence_kind") or "").strip()

        if not kid_raw:
            raise RuntimeError(
                f"address_versions_seed.csv:{lineno}: empty kcdx_id")
        try:
            kid = int(kid_raw)
        except ValueError:
            raise RuntimeError(
                f"address_versions_seed.csv:{lineno}: kcdx_id={kid_raw!r} "
                f"is not an integer")
        if not vfv:
            raise RuntimeError(
                f"address_versions_seed.csv:{lineno}: empty valid_from_version "
                f"(kcdx_id={kid})")
        if not module:
            raise RuntimeError(
                f"address_versions_seed.csv:{lineno}: empty module "
                f"(kcdx_id={kid}, valid_from_version={vfv!r}); reference a "
                f"module_seed.csv row by id or name")
        key = (kid, vfv)
        if key in seen:
            raise RuntimeError(
                f"address_versions_seed.csv:{lineno}: duplicate "
                f"(kcdx_id={kid}, valid_from_version={vfv!r}) -- first at "
                f"line {seen[key]}")
        seen[key] = lineno

        # Audit-pair integrity: last_verified_at_version IS NULL <=> all three
        # audit columns NULL.
        audit_present_count = sum(1 for x in (vby, vdate, ekind) if x)
        if lvv:
            if audit_present_count != 3:
                raise RuntimeError(
                    f"address_versions_seed.csv:{lineno}: "
                    f"last_verified_at_version={lvv!r} is set but the audit "
                    f"trio (verified_by, verified_date, evidence_kind) is "
                    f"incomplete -- got verified_by={vby!r}, "
                    f"verified_date={vdate!r}, evidence_kind={ekind!r}")
            # last_verified >= valid_from (string compare is fine for the
            # release_M_N.BUILD tag format; lexicographic order = real order).
            if lvv < vfv:
                raise RuntimeError(
                    f"address_versions_seed.csv:{lineno}: "
                    f"last_verified_at_version={lvv!r} < "
                    f"valid_from_version={vfv!r} (you can't verify a row at a "
                    f"version older than the version the row starts at)")
            # Date format check.
            if not _VERIFIED_DATE_RE.match(vdate):
                raise RuntimeError(
                    f"address_versions_seed.csv:{lineno}: "
                    f"verified_date={vdate!r} must be YYYY-MM-DD")
            # Evidence kind enum check.
            if ekind not in EVIDENCE_KIND_ENUM:
                raise RuntimeError(
                    f"address_versions_seed.csv:{lineno}: "
                    f"evidence_kind={ekind!r} is not in the enum "
                    f"{EVIDENCE_KIND_ENUM}")
        else:
            if audit_present_count != 0:
                raise RuntimeError(
                    f"address_versions_seed.csv:{lineno}: "
                    f"last_verified_at_version is empty but at least one of "
                    f"the audit trio is set -- got verified_by={vby!r}, "
                    f"verified_date={vdate!r}, evidence_kind={ekind!r}")

        # Survival columns (db-updator step 5.1). All OPTIONAL / NULL-valid -- an
        # empty value is fine (the kind doesn't use the column, or step 5.2 has
        # not filled it yet). A malformed PRESENT value is a HARD ERROR. The
        # survival_derives_from FK closure is a CROSS-row check
        # (check_survival_derives_from_known), not done here per-row.
        _validate_survival_cols(r, lineno, kid, vfv)

        rows.append(r)
    return rows


def _validate_survival_cols(r, lineno, kid, vfv):
    """Format-validate the per-version survival columns on ONE address_versions
    seed row (db-updator step 5.1). EMPTY is always allowed; a malformed PRESENT
    value is a HARD ERROR. Columns:

      survival_aob            -- whitespace-separated AOB tokens, each a 2-hex
                                 byte or a '?'/'??' wildcard (the mask is folded
                                 into the pattern). Any other token is malformed.
      survival_anchor_string  -- free text (the literal bytes for string_anchor);
                                 no format check (any non-empty value accepted).
      survival_derives_from   -- an INTEGER kcdx_id (the dependency entity). Must
                                 parse as int when present (FK closure checked
                                 cross-row in check_survival_derives_from_known).
      survival_rule           -- a structured derivation-rule string (the grammar
                                 is documented in data/seeds/policy.md). The
                                 importer does NOT parse the grammar here -- it is
                                 the future engine consumer's job; any non-empty
                                 string is accepted at author time.
      survival_slot_count     -- an INTEGER slot count (vtable_base). Must parse
                                 as a non-negative int when present.
    """
    where = (f"address_versions_seed.csv:{lineno} (kcdx_id={kid}, "
             f"valid_from_version={vfv!r})")

    aob = (r.get("survival_aob") or "").strip()
    if aob:
        toks = aob.split()
        for t in toks:
            if not _AOB_TOKEN_RE.match(t):
                raise RuntimeError(
                    f"{where}: survival_aob token {t!r} is malformed -- each "
                    f"token must be a 2-hex byte (e.g. 48, 8B) or a wildcard "
                    f"('?' / '??'); got {aob!r}")

    dfrom = (r.get("survival_derives_from") or "").strip()
    if dfrom:
        try:
            int(dfrom)
        except ValueError:
            raise RuntimeError(
                f"{where}: survival_derives_from={dfrom!r} is not an integer "
                f"kcdx_id")

    sc = (r.get("survival_slot_count") or "").strip()
    if sc:
        try:
            if int(sc) < 0:
                raise ValueError
        except ValueError:
            raise RuntimeError(
                f"{where}: survival_slot_count={sc!r} is not a non-negative "
                f"integer")


# ---------------------------------------------------------------------------
# Cross-row checks (run after the whole seed state is loaded).
# ---------------------------------------------------------------------------
def resolve_and_check_name_refs(address_names_rows, name_to_id, tag_to_id):
    """RESOLVE supersession / deprecation references + integrity checks on the
    in-memory address_names rows (post-load; the seed strings are replaced in
    place by resolved ids).

    address_names_seed.csv records the DIRECT supersession edge by NAME ("at this
    version, b supersedes a") + the version it became active. The engine walks
    the chain at query time, applying the version filter at each hop -- no
    compaction. deprecation_replacement is an advisory pointer (engine surfaces
    it in the warning, does NOT auto-follow).

    All validation runs here, fail-loud: unknown names, unknown version tags,
    paired-fields integrity (XOR), deprecation_replacement-requires-deprecation.
    (Cycle detection is a separate pass -- check_supersession_acyclic.)

    `name_to_id` / `tag_to_id` are the resolved lookups built from the loaded
    address_names + game_versions rows.
    """
    def _resolve_name(rid, rname, col, val):
        nid = name_to_id.get(val)
        if nid is None:
            raise RuntimeError(
                f"address_names_seed.csv row id={rid} name={rname!r}: {col}={val!r} but no "
                f"seed row has that name")
        if nid == rid and col == "superseded_by":
            raise RuntimeError(
                f"address_names_seed.csv row id={rid} name={rname!r}: {col} points at itself")
        return nid

    def _resolve_tag(rid, rname, col, val):
        gvid = tag_to_id.get(val)
        if gvid is None:
            raise RuntimeError(
                f"address_names_seed.csv row id={rid} name={rname!r}: {col}={val!r} but no "
                f"game_versions row has that tag")
        return gvid

    for r in address_names_rows:
        rid, rname = r["id"], r["name"]

        # Supersession pair integrity: both-or-neither.
        sb, sbv = r["superseded_by"], r["superseded_at_version"]
        if (sb is None) != (sbv is None):
            raise RuntimeError(
                f"address_names_seed.csv row id={rid} name={rname!r}: superseded_by and "
                f"superseded_at_version must be set together "
                f"(got superseded_by={sb!r}, superseded_at_version={sbv!r})")
        if sb is not None:
            r["superseded_by"]         = _resolve_name(rid, rname, "superseded_by", sb)
            r["superseded_at_version"] = _resolve_tag(rid, rname, "superseded_at_version", sbv)

        # Deprecation pair integrity: is_deprecated=1 <=> deprecated_at_version set.
        dep, depv = r["is_deprecated"], r["deprecated_at_version"]
        if bool(dep) != (depv is not None):
            raise RuntimeError(
                f"address_names_seed.csv row id={rid} name={rname!r}: is_deprecated and "
                f"deprecated_at_version must be set together "
                f"(got is_deprecated={dep}, deprecated_at_version={depv!r})")
        if depv is not None:
            r["deprecated_at_version"] = _resolve_tag(rid, rname, "deprecated_at_version", depv)

        # deprecation_replacement only valid when is_deprecated=1.
        dr = r["deprecation_replacement"]
        if dr is not None and not dep:
            raise RuntimeError(
                f"address_names_seed.csv row id={rid} name={rname!r}: deprecation_replacement="
                f"{dr!r} requires is_deprecated=1")
        if dr is not None:
            r["deprecation_replacement"] = _resolve_name(rid, rname, "deprecation_replacement", dr)


def check_supersession_acyclic(address_names_rows):
    """Cycle detection on the supersession graph -- a cycle is wrong regardless
    of which versions gate which edges (the version-ignorant graph reachable via
    superseded_by must be acyclic). Walk each chain to a terminal; abort if we
    revisit any node. Returns the edge count (for the caller's progress print).

    Must run AFTER resolve_and_check_name_refs has turned superseded_by into an
    id.
    """
    n_superseded = sum(1 for r in address_names_rows if r["superseded_by"] is not None)
    if n_superseded:
        direct = {r["id"]: r["superseded_by"] for r in address_names_rows}
        id_to_name = {r["id"]: r["name"] for r in address_names_rows}
        for r in address_names_rows:
            seen = [r["id"]]
            cur = direct.get(r["id"])
            while cur is not None:
                if cur in seen:
                    seen.append(cur)
                    chain = " -> ".join(id_to_name.get(i, f"#{i}") for i in seen)
                    raise RuntimeError(f"address_names_seed.csv supersession cycle: {chain}")
                seen.append(cur)
                cur = direct.get(cur)
    return n_superseded


def check_kcdx_id_known(kid, vfv_tag, valid_kcdx_ids):
    """A versions-seed row's kcdx_id must reference an address_names_seed row
    (FK closure, versions -> names direction)."""
    if kid not in valid_kcdx_ids:
        raise RuntimeError(
            f"address_versions_seed.csv: kcdx_id={kid} "
            f"(valid_from_version={vfv_tag!r}) has no row in "
            f"address_names_seed.csv (every versions row must reference "
            f"an existing entity)")


def check_every_entity_covered(valid_kcdx_ids, covered_kids, game_version_tag):
    """Every kcdx_id in address_names_seed.csv must have a matching row in
    address_versions_seed.csv for the baseline import version -- else the entity
    has a name but no resolve facts (FK closure, names -> versions direction)."""
    uncovered = valid_kcdx_ids - covered_kids
    if uncovered:
        sample = sorted(uncovered)[:5]
        raise RuntimeError(
            f"address_names_seed.csv has {len(uncovered)} kcdx_id(s) with no "
            f"address_versions_seed.csv row for "
            f"valid_from_version={game_version_tag!r} "
            f"(first 5: {sample}); every named entity needs at least one "
            f"resolve fact for the baseline version")


def check_survival_derives_from_known(versions_seed, valid_kcdx_ids):
    """A non-empty survival_derives_from on an address_versions_seed row must
    reference an existing address_names_seed entity (the same FK closure as
    check_kcdx_id_known, applied to the survival DAG edge). The survival check
    walks data_slot -> instruction_anchor -> string_anchor (and vtable_index ->
    vtable_base) in dependency order, so a dangling derives_from would break the
    walk; catch it at validation time. EMPTY survival_derives_from = fine (no
    dependency / not yet authored). Runs over the FULL versions seed, shared by
    rebuild and apply.
    """
    for r in versions_seed:
        dfrom = (r.get("survival_derives_from") or "").strip()
        if not dfrom:
            continue
        kid = int(r["kcdx_id"])   # already validated as int by the reader
        vfv = r["valid_from_version"].strip()
        ref = int(dfrom)          # already validated as int by _validate_survival_cols
        if ref not in valid_kcdx_ids:
            raise RuntimeError(
                f"address_versions_seed.csv (kcdx_id={kid}, "
                f"valid_from_version={vfv!r}): survival_derives_from={ref} has "
                f"no row in address_names_seed.csv (the survival dependency "
                f"must reference an existing entity)")
