"""seeds_shared.survival_checker -- THE Python per-kind survival REFERENCE checker
(the test-of-record, TRD D27). Given a DLL's on-disk bytes + an authored row, return
that row's per-kind static survival verdict (Unchanged / Changed / Ambiguous /
CannotCheck) for the 8 in-scope kinds. It is the CANONICAL reference the JS browser
checker (Phase 2) is pinned against -- the JS port must agree with it on the same bytes.

DESIGN AUTHORITY: data/maintainer-tool/fingerprint-per-kind.md
("Per-kind fingerprint table" + "The anchor dependency (cross-row survival)"). Each
check is built to that section's "survival datum" + "the check at survival time"; the
verdict enum is US-11 / D26. NOTHING here is invented -- a kind whose check the design
leaves silent is returned to the caller, not guessed.

ON-DISK, NOT LIVE (corrected D25): this checker reads ON-DISK DLL bytes / the fixture's
on-disk byte-slices. The reachability / live (resolve-into-live-.text) check is
engine-only -- NOT this static checker's job.

THE 6 CHECKABLE STATIC KINDS + 2 (vtable_index CannotCheck, transitive CannotCheck):
  function / function_no_sig / function_variadic  -- BODY HASH: re-hash [rva,rva+length)
      on-disk (BLAKE3), compare to the stored content_hash. Match->Unchanged; differ->Changed.
  callsite            -- AOB RE-MATCH: scan .text for the stored AOB (bytes + ? mask);
      unique hit->Unchanged; zero->Changed; multiple->Ambiguous.
  string_anchor       -- .rdata LITERAL PRESENCE: present->Unchanged; absent->Changed.
  instruction_anchor  -- DERIVATION CHAIN: decode the anchor (disp32-follow) + shape-match
      the stored AOB; chain completes + shape matches->Unchanged; else Changed.
  data_slot           -- DERIVATION: re-run the survival_rule (disp32@<kid>); reaches the
      recorded slot->Unchanged; lands elsewhere->Changed. NO content hash.
  vtable_base         -- TABLE-SHAPE: read N qwords at the rva; N valid .text pointers->
      Unchanged; shrunk/non-pointer->Changed. NOT a byte hash.
  vtable_index        -- CannotCheck (deferred within this design -- no rva, runtime slot).
  + transitive CannotCheck: a kind that derives THROUGH a row that is Changed is
      CannotCheck-with-reason (the DAG), never a silent pass.

BLAKE3: stdlib hashlib has NO blake3; the body-hash kind needs the canonical BLAKE3 the
production content_hash was computed with. The hasher is INJECTED (`body_hasher`) so the
checker has no hard import; the caller supplies one (the `blake3` PyPI package via
`default_body_hasher()`). A row that needs a body hash with no hasher supplied is
CannotCheck-with-reason, never a silent pass.
"""
from .cross_impl_fixture import (
    VERDICT_UNCHANGED, VERDICT_CHANGED, VERDICT_AMBIGUOUS, VERDICT_CANNOT_CHECK,
)
from .survival_checker_primitives import (
    parse_aob, aob_find_all, aob_matches_at,
    follow_rip_disp32, read_qwords, DecodeError,
    RIP_INSTR_LEN,
)


# ---------------------------------------------------------------------------
# The verdict object -- a (verdict, reason) pair. The verdict is the pinned
# vocabulary value; the reason is the falsifiable one-liner (what was observed vs.
# expected) -- required for every non-Unchanged verdict (the Changed/Ambiguous/
# CannotCheck "with reason"). The fixture pins on the VERDICT string; the reason is
# diagnostic.
# ---------------------------------------------------------------------------
class CheckResult:
    __slots__ = ("verdict", "reason")

    def __init__(self, verdict, reason=""):
        self.verdict = verdict
        self.reason = reason

    def __repr__(self):
        return "CheckResult(%r, %r)" % (self.verdict, self.reason)

    def __eq__(self, other):
        # Equality is on the VERDICT only -- the fixture pins the verdict string,
        # not the diagnostic reason. `result == "Unchanged"` and `result == other_result`
        # both compare the verdict.
        if isinstance(other, CheckResult):
            return self.verdict == other.verdict
        return self.verdict == other


def _ok(reason=""):
    return CheckResult(VERDICT_UNCHANGED, reason)


def _changed(reason):
    return CheckResult(VERDICT_CHANGED, reason)


def _ambiguous(reason):
    return CheckResult(VERDICT_AMBIGUOUS, reason)


def _cannot_check(reason):
    return CheckResult(VERDICT_CANNOT_CHECK, reason)


# ---------------------------------------------------------------------------
# BLAKE3 body hasher injection. The body-hash kinds need the canonical BLAKE3 the
# production content_hash was stamped with (data/refdata-extractor/ghidra/blake3 --
# the reference Jack-O'Connor algorithm). stdlib hashlib has no blake3, so the
# hasher is injected. default_body_hasher() resolves the `blake3` PyPI package
# (licensed CC0-1.0 OR Apache-2.0 -- the Apache-2.0 branch is elected, matching the
# vendored Java oracle; recorded in data/refdata-extractor/README.md), raising a clear
# error if it is not installed.
# ---------------------------------------------------------------------------
def default_body_hasher():
    """Return a callable `bytes -> 64-hex-lowercase BLAKE3` using the `blake3`
    package. Raises RuntimeError (with install guidance) if the package is absent --
    the caller decides whether that is fatal (the reference checker) or a skip (a
    test running without blake3); the checker itself maps an absent hasher to
    CannotCheck-with-reason, never a silent pass."""
    try:
        import blake3
    except ImportError as e:  # pragma: no cover - exercised by the env, not a branch
        raise RuntimeError(
            "the function body-hash kind needs BLAKE3; the `blake3` package is not "
            "installed (stdlib hashlib has no blake3). Install it "
            "(`pip install blake3`) -- it is the canonical reference algorithm the "
            "production content_hash was computed with."
        ) from e
    return lambda b: blake3.blake3(b).hexdigest()


# ===========================================================================
# Per-kind checks. Each takes the recorded survival `datum` + the on-disk bytes the
# kind operates over, and returns a CheckResult. Built to fingerprint-per-kind.md.
#
# Each kind's byte input is supplied as `body` (the bytes the check sees). For the
# real-DLL path the caller maps datum.rva -> the on-disk span via
# survival_checker_primitives.sections_from_dll; for the fixture path the slice IS
# that span (cross_impl_fixture.py header: "the check sees exactly these bytes").
# ===========================================================================

def check_function(datum, body, *, body_hasher=None):
    """function / function_no_sig / function_variadic -- BODY HASH.
    fingerprint-per-kind.md function: re-hash [rva, rva+length) on-disk (BLAKE3),
    compare to the stored content_hash. Match -> Unchanged; differ -> Changed.

    datum.content_hash -- the stored 64-hex BLAKE3.
    body               -- the on-disk function-body span [rva, rva+length).
    body_hasher        -- bytes -> 64-hex BLAKE3 (injected); absent -> CannotCheck.
    """
    stored = datum.get("content_hash")
    if not stored:
        # No recorded hash -- the row was never fingerprinted (a function with no
        # bulk baseline; survival_builder.py treats an absent hash as not-yet-
        # fingerprinted, never a forged datum). Cannot decide Unchanged/Changed.
        return _cannot_check("no recorded content_hash to compare against")
    if body_hasher is None:
        return _cannot_check(
            "function body-hash needs a BLAKE3 hasher; none supplied "
            "(stdlib hashlib has no blake3)")
    got = body_hasher(bytes(body))
    if got.lower() == stored.lower():
        return _ok()
    return _changed(
        "body BLAKE3 %s != recorded content_hash %s" % (got, stored))


def check_callsite(datum, text_bytes):
    """callsite -- AOB RE-MATCH. fingerprint-per-kind.md callsite: scan .text for the
    stored AOB (bytes + ? mask). unique hit -> Unchanged; zero -> Changed; multiple
    -> Ambiguous.

    datum.aob -- the stored AOB pattern ("48 8B 41 08 ... 3C 02", with `?` wildcards).
    text_bytes -- the .text-like span to scan.
    """
    aob = datum.get("aob")
    if not aob:
        return _cannot_check("no recorded survival_aob to re-match")
    values, mask = parse_aob(aob)
    hits = aob_find_all(text_bytes, values, mask)
    n = len(hits)
    if n == 1:
        # Unique hit -> Unchanged. (The RVA relocates to the hit if it moved; the
        # static fixture only pins the verdict, the relocation is the engine's.)
        return _ok("unique AOB hit at offset %d" % hits[0])
    if n == 0:
        return _changed("AOB not found in .text (the site is gone)")
    return _ambiguous(
        "AOB matched %d times -- no longer a unique locator" % n)


def check_string_anchor(datum, rdata_bytes):
    """string_anchor -- .rdata LITERAL PRESENCE. fingerprint-per-kind.md string_anchor:
    search .rdata for the stored literal. Present -> Unchanged; absent -> Changed.

    The optional single-xref assert (survival_expect_unique) needs a .text LEA-xref
    decode -- a DOCUMENTED LIMITATION of this static presence check
    (fingerprint-per-kind.md allows the .text-LEA-xref to be deferred to the decoder;
    the core presence check is the bar). The presence check is what is implemented;
    expect_unique is carried in the datum but the xref assertion is not run here.

    datum.anchor_string -- the stored literal.
    rdata_bytes         -- the .rdata-like span to search.
    """
    literal = datum.get("anchor_string")
    if not literal:
        return _cannot_check("no recorded survival_anchor_string to search for")
    needle = literal.encode("ascii") if isinstance(literal, str) else bytes(literal)
    if needle in rdata_bytes:
        return _ok("literal present in .rdata")
    return _changed("literal %r absent from .rdata (the anchor is gone)" % literal)


def check_instruction_anchor(datum, anchor_bytes):
    """instruction_anchor -- DERIVATION CHAIN. fingerprint-per-kind.md instruction_anchor:
    re-run the resolver chain -- verify the anchor instruction matches the stored shape
    (the stored AOB) AND the disp32-follow lands on the expected derived target. Chain
    completes + shape matches -> Unchanged; else Changed.

    (The full chain in production also re-FINDS the anchor from its string_anchor at a
    fresh version -- the backward LEA->MOV walk probe 0.2 flagged as a separate Phase-2
    re-find concern. This static check verifies the load-bearing forward primitive: the
    anchor's SHAPE matches the stored AOB and the disp32-follow re-derives the recorded
    target -- which is what the fixture pins.)

    datum.aob                 -- the stored instruction-shape AOB ("48 8B 0D ?? ?? ?? ??").
    datum.anchor_rva          -- the anchor instruction's RVA (for the disp32 arithmetic).
    datum.expected_target_rva -- the RVA the disp32-follow must re-derive (its data_slot).
    anchor_bytes              -- the on-disk bytes at the anchor RVA (>= 7).
    """
    aob = datum.get("aob")
    if not aob:
        return _cannot_check("no recorded instruction-shape AOB")
    values, mask = parse_aob(aob)
    # 1. SHAPE: the anchor bytes must match the stored AOB shape.
    if not aob_matches_at(anchor_bytes, values, mask, 0):
        return _changed("anchor bytes do not match the stored shape AOB %r" % aob)
    # 2. DERIVATION: decode the disp32 and follow it; it must land on the expected target.
    anchor_rva = datum.get("anchor_rva")
    expected = datum.get("expected_target_rva")
    if anchor_rva is None or expected is None:
        # Shape matched but the design carries no derivation endpoint to verify
        # against -- the shape match alone is the recorded check.
        return _ok("anchor shape matches the stored AOB (no derivation endpoint recorded)")
    try:
        derived = follow_rip_disp32(anchor_rva, bytes(anchor_bytes[:RIP_INSTR_LEN]))
    except DecodeError as e:
        return _changed("anchor disp32 decode failed: %s" % e)
    if derived == (expected & 0xFFFFFFFF):
        return _ok("anchor shape matches + disp32-follow lands on 0x%08X" % derived)
    return _changed(
        "disp32-follow lands on 0x%08X, not the recorded target 0x%08X"
        % (derived, expected))


def check_data_slot(datum, anchor_bytes):
    """data_slot -- DERIVATION (NO content hash). fingerprint-per-kind.md data_slot:
    re-run the survival_rule (disp32@<kid>) over its anchor's bytes; Unchanged iff the
    derivation still REACHES the recorded slot RVA. The .data bytes legitimately change
    (relocated pointers) -- a byte hash of a data_slot is an anti-signal, so there is NO
    content hash.

    Supports the two survival_rule forms fingerprint-per-kind.md names:
      "disp32@<kid>"     -- follow the RIP-relative disp32 at the anchor instruction.
      "<kid>+0xHEX" / "<kid>-0xHEX" -- a fixed RVA offset from another slot (pure
                            arithmetic on the dependency's RVA, no decode -- e.g.
                            gEnv = pConsole - 0xA8).

    datum.rule              -- the survival_rule string.
    datum.anchor_rva        -- the anchor instruction RVA (disp32@ form).
    datum.expected_slot_rva -- the recorded slot RVA the derivation must reach.
    datum.derives_from_rva  -- the dependency slot's RVA (offset form).
    anchor_bytes            -- the on-disk bytes at the anchor RVA (disp32@ form).
    """
    rule = datum.get("rule")
    if not rule:
        return _cannot_check("no recorded survival_rule to re-derive")
    expected = datum.get("expected_slot_rva")
    if expected is None:
        return _cannot_check("no recorded expected_slot_rva to verify the derivation against")
    rule = rule.strip()

    if rule.startswith("disp32@"):
        anchor_rva = datum.get("anchor_rva")
        if anchor_rva is None:
            return _cannot_check("disp32@ rule needs anchor_rva to follow from")
        try:
            derived = follow_rip_disp32(anchor_rva, bytes(anchor_bytes[:RIP_INSTR_LEN]))
        except DecodeError as e:
            return _changed("data_slot disp32 decode failed: %s" % e)
        if derived == (expected & 0xFFFFFFFF):
            return _ok("derivation reaches recorded slot 0x%08X" % derived)
        return _changed(
            "derivation lands on 0x%08X, not the recorded slot 0x%08X"
            % (derived, expected))

    # Offset form: "<kid>+0xHEX" / "<kid>-0xHEX" -- pure arithmetic on the dependency
    # slot's RVA. The caller supplies the dependency slot's resolved RVA as
    # datum.derives_from_rva (the same caller-resolves-ids discipline survival_builder
    # uses); the rule's sign + hex offset is applied to it.
    base_rva = datum.get("derives_from_rva")
    if base_rva is None:
        return _cannot_check(
            "offset rule %r needs the dependency slot's resolved RVA (derives_from_rva)"
            % rule)
    sign, hex_off = _parse_offset_rule(rule)
    if sign is None:
        return _cannot_check("unrecognized survival_rule form %r" % rule)
    derived = (base_rva + sign * hex_off) & 0xFFFFFFFF
    if derived == (expected & 0xFFFFFFFF):
        return _ok("offset derivation reaches recorded slot 0x%08X" % derived)
    return _changed(
        "offset derivation lands on 0x%08X, not the recorded slot 0x%08X"
        % (derived, expected))


def _parse_offset_rule(rule):
    """Parse a '<kid>+0xHEX' / '<kid>-0xHEX' offset rule into (sign, offset_int).
    Returns (None, 0) if `rule` is not an offset form (the caller maps that to
    CannotCheck -- an unrecognized rule is returned, never guessed)."""
    for sep, sign in (("+", 1), ("-", -1)):
        if sep in rule:
            _kid, _, off = rule.partition(sep)
            off = off.strip()
            try:
                return sign, (int(off, 16) if off.lower().startswith("0x") else int(off))
            except ValueError:
                return None, 0
    return None, 0


def check_vtable_base(datum, table_bytes):
    """vtable_base -- TABLE-SHAPE (NOT a byte hash). fingerprint-per-kind.md vtable_base:
    read N qwords at the rva; Unchanged iff there are N and each resolves into .text. A
    shrunk/grown table or non-pointer contents -> Changed. The pointers relocate every
    build, so the check is the SHAPE (N plausible .text-range pointers), never a hash.

    datum.slot_count -- N, the expected slot count (survival_slot_count).
    datum.text_range -- [lo, hi): the .text RVA window each qword must fall inside (the
                        "resolves into .text" structural assertion).
    table_bytes      -- the on-disk bytes at the vtable base RVA (>= N*8).
    """
    n = datum.get("slot_count")
    if not n:
        return _cannot_check("no recorded survival_slot_count")
    text_range = datum.get("text_range")
    if not text_range or len(text_range) != 2:
        return _cannot_check("no recorded .text range to classify the qword pointers against")
    lo, hi = text_range
    try:
        qwords = read_qwords(table_bytes, 0, n)
    except ValueError:
        return _changed(
            "table is shorter than %d qwords (the N-slot shape no longer holds)" % n)
    for i, q in enumerate(qwords):
        if not (lo <= q < hi):
            return _changed(
                "slot %d value 0x%X is not a .text-range pointer [0x%X, 0x%X)"
                % (i, q, lo, hi))
    return _ok("all %d slots resolve into .text" % n)


def check_vtable_index(datum, _body=None):
    """vtable_index -- CannotCheck. fingerprint-per-kind.md vtable_index is DEFERRED
    within this design: no RVA, the "intended method" identity lives at
    vtable_base[index] and needs a runtime slot target. Returns CannotCheck.
    """
    return _cannot_check(
        "vtable_index is deferred -- no RVA, the slot target needs a runtime resolve")


# ===========================================================================
# THE DISPATCH -- map `kind` to its check. The single dispatch entry point the
# caller (the test, the JS-port pinning, a future apply path) calls.
# ===========================================================================

# Which kinds DERIVE THROUGH another row (the anchor dependency DAG,
# fingerprint-per-kind.md "The anchor dependency"). A dependent row whose
# dependency is Changed is transitively CannotCheck-with-reason, never a silent pass.
#   data_slot         -> instruction_anchor -> string_anchor
#   instruction_anchor-> string_anchor
#   vtable_index      -> vtable_base
# The checker honors dependency order: the caller resolves each row's dependency
# verdict first and passes it in; check_row short-circuits to transitive-CannotCheck.
DERIVING_KINDS = frozenset({"instruction_anchor", "data_slot", "vtable_index"})


def check_row(kind, datum, body, *, body_hasher=None, dependency_result=None):
    """THE per-kind dispatch. Return the CheckResult for `(kind, datum)` over `body`.

    kind              -- the ADDRESS_KIND (decides the check).
    datum             -- the recorded survival datum (the kind's payload columns).
    body              -- the on-disk bytes the kind operates over (the function span /
                         .text / .rdata / anchor bytes / qword table). May be None for
                         vtable_index (no on-disk target).
    body_hasher       -- bytes -> 64-hex BLAKE3 (function kinds only); None otherwise.
    dependency_result -- the CheckResult of the row this row DERIVES FROM (the DAG edge),
                         or None when it has no dependency / the dependency is Unchanged.
                         If the dependency is Changed/Ambiguous/CannotCheck, this row is
                         transitively CannotCheck (the anchor dependency).

    TRANSITIVE CannotCheck (the DAG): before running the kind's own check, if the row
    derives from a dependency that did NOT survive (dependency_result is not Unchanged),
    return CannotCheck-with-reason -- a dependent of a Changed anchor is suspect, never a
    silent pass.
    """
    if kind in DERIVING_KINDS and dependency_result is not None:
        if not _is_unchanged(dependency_result):
            dep_v = (dependency_result.verdict if isinstance(dependency_result, CheckResult)
                     else dependency_result)
            return _cannot_check(
                "transitively suspect: the row this %s derives from is %s "
                "(the anchor dependency did not survive)" % (kind, dep_v))

    if kind in ("function", "function_no_sig", "function_variadic"):
        return check_function(datum, body, body_hasher=body_hasher)
    if kind == "callsite":
        return check_callsite(datum, body)
    if kind == "string_anchor":
        return check_string_anchor(datum, body)
    if kind == "instruction_anchor":
        return check_instruction_anchor(datum, body)
    if kind == "data_slot":
        return check_data_slot(datum, body)
    if kind == "vtable_base":
        return check_vtable_base(datum, body)
    if kind == "vtable_index":
        return check_vtable_index(datum, body)
    # An unknown kind is RETURNED, never guessed -- a new ADDRESS_KIND must declare its
    # survival check here (the same fail-loud discipline survival_builder.survival_kind_form
    # uses). This is the "do NOT invent" floor of the authority statement.
    raise ValueError(
        "survival_checker: no per-kind check for address kind %r "
        "(a new kind must declare its check here)" % (kind,))


def _is_unchanged(result):
    v = result.verdict if isinstance(result, CheckResult) else result
    return v == VERDICT_UNCHANGED
