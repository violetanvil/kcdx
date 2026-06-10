"""test_report_schema.py -- the validation test for the frozen JSON verification
report schema (the cross-repo contract).

WHAT THIS PROVES (the load-bearing claim) -- schema v3
------------------------------------------------------
The frozen schema (data/maintainer-tool/report-schema/verification-report.schema.json)
is a real, enforced contract, now at v3 (the active-attempt model: the 7-state
verdict enum, the method_rank proof-strength ladder, the invoke_attempted /
invoke_skip_reason fields, the redefined summary.passing, and the
complete / rows_expected incremental-flush partial-report signal). v3 carries
forward v2's matched_address_version_id attribution invariant unchanged in shape
(non-null on a pass, null otherwise), now keyed to the pass set
{verified_working, passed_not_verified}:

  1. The sample VALID report (samples/report-valid.json) VALIDATES -- all required
     fields present (incl. method_rank, invoke_attempted, invoke_skip_reason,
     matched_address_version_id on every row), every verdict in the 7-state enum,
     schema_version present + pinned to 3, the partial-report signal
     (complete + rows_expected) present, and the v3 attribution invariant holds
     (verified_working / passed_not_verified rows carry a non-null integer matched
     id; every other verdict carries null).
  2. A MALFORMED report is REJECTED. Each defect is isolated so a pass/fail
     attributes cleanly:
       (a) an OLD 4-token verdict (resolves_works / wrong_target / dead /
           cannot_check) -- the v2 vocabulary, now out-of-enum in v3. The old
           prose spelling `resolves+works` (with the +) carries forward as a
           rejected token too (it was never in any enum).
       (b) a row missing one of the new required fields (method_rank /
           invoke_attempted / invoke_skip_reason).
       (c) a v2-shaped report (schema_version 2) -- now the WRONG version (the pin
           is 3).
       (d) the v3 attribution invariant: a pass-set row (verified_working /
           passed_not_verified) whose matched_address_version_id is NULL is
           REJECTED, and a non-pass row whose matched_address_version_id is
           NON-NULL is REJECTED.
  3. The D37 incremental-flush contract:
       (a) a single per-row JSONL LINE validates as one rows[] element (against
           the rows.items subschema -- the durable per-row shape the sweep flushes
           as each row resolves).
       (b) the finalized document carries complete + rows_expected.
       (c) a PARTIAL document (complete:false, rows[] length < rows_expected)
           still VALIDATES -- a crashed-mid-sweep report is ingestible, NOT
           malformed. (Paired with the truly-malformed-still-fails assertions
           above, so "partial validates" cannot be masking "everything validates".)
  4. schema_version is present + pinned to the frozen value 3.

THE VALIDATOR. `jsonschema` (the standard Python JSON Schema validator) is NOT a
project dependency (a new dep is the user's call -- not added here). When it IS
present this test uses it (the schema file is then validated by the canonical
implementation); when it is ABSENT this test uses a small hand-rolled checker
(_validate) that interprets the draft-07 SUBSET this contract uses -- type (incl.
a type UNION like ["integer", "null"] / ["string", "null"] and the "null" type),
required, enum (incl. an enum containing null), const, minimum/maximum/minLength,
additionalProperties:false, and the conditional (if/then/else on the row object,
the `if` selecting on a verdict ENUM). The schema JSON file is the single source
of truth either way; the hand-rolled checker reads that file and enforces it, it
does not duplicate the contract inline.

RUN
---
    python tests/test_report_schema.py
    pytest tests/test_report_schema.py
"""
import copy
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
SCHEMA_DIR = os.path.join(
    REPO_ROOT, "data", "maintainer-tool", "report-schema"
)
SCHEMA_PATH = os.path.join(SCHEMA_DIR, "verification-report.schema.json")
VALID_PATH = os.path.join(SCHEMA_DIR, "samples", "report-valid.json")
MALFORMED_PATH = os.path.join(SCHEMA_DIR, "samples", "report-malformed.json")
PARTIAL_PATH = os.path.join(SCHEMA_DIR, "samples", "report-partial.json")

# The frozen schema_version this step pins. The test asserts the schema and the
# valid fixture both carry exactly this value. Bumped 2 -> 3 with the
# active-attempt model (7-state enum + method_rank + invoke fields) and the
# complete / rows_expected partial-report signal.
PINNED_SCHEMA_VERSION = 3

# The frozen v3 verdict enum (the 7-state set) + the pass set the attribution
# invariant keys on. Both are read back from the schema file in the tests below;
# these are the expected values the test asserts the schema carries.
V3_VERDICTS = [
    "verified_working",
    "passed_not_verified",
    "failed",
    "not_applicable",
    "cannot_check",
    "skipped",
    "error",
]
PASS_VERDICTS = ["verified_working", "passed_not_verified"]
NONPASS_VERDICTS = ["failed", "not_applicable", "cannot_check", "skipped", "error"]
# The old v2 4-token vocabulary -- every one of these is now out-of-enum in v3.
OLD_V2_VERDICTS = ["resolves_works", "wrong_target", "dead", "cannot_check"]


# ---------------------------------------------------------------------------
# Load the schema + fixtures from disk (the schema file is the source of truth).
# ---------------------------------------------------------------------------
def _load(path):
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


SCHEMA = _load(SCHEMA_PATH)
VALID_REPORT = _load(VALID_PATH)
MALFORMED_REPORT = _load(MALFORMED_PATH)
PARTIAL_REPORT = _load(PARTIAL_PATH)

# The per-row subschema -- a single JSONL line validates against THIS, not the
# whole-document schema (D37: one per-row line == exactly one rows[] element).
ROW_ITEM_SCHEMA = SCHEMA["properties"]["rows"]["items"]


# ---------------------------------------------------------------------------
# Validator -- jsonschema if present, else a hand-rolled draft-07 subset checker.
# Returns a list of error strings (empty == valid).
# ---------------------------------------------------------------------------
try:
    import jsonschema  # type: ignore

    _HAVE_JSONSCHEMA = True
except ImportError:
    jsonschema = None  # type: ignore
    _HAVE_JSONSCHEMA = False


_JSON_TYPE = {
    "object": dict,
    "array": list,
    "string": str,
    "integer": int,
    "boolean": bool,
}


def _is_type(value, json_type):
    # `type` may be a single string OR a list of allowed types (e.g.
    # matched_address_version_id is ["integer", "null"], invoke_skip_reason is
    # ["string", "null"]). A list matches if the value satisfies ANY listed type.
    if isinstance(json_type, list):
        return any(_is_type(value, t) for t in json_type)
    if json_type == "integer":
        # bool is a subclass of int in Python; an integer field must reject a
        # stray bool (True is not a valid method_rank / id).
        return isinstance(value, int) and not isinstance(value, bool)
    if json_type == "boolean":
        return isinstance(value, bool)
    if json_type == "null":
        return value is None
    py = _JSON_TYPE.get(json_type)
    return py is not None and isinstance(value, py)


def _validate_branch_properties(value, subschema, path, errors):
    """Apply a typeless `properties`-only subschema (an `if`/`then`/`else`
    branch in this contract) to `value`. It validates ONLY the named
    properties present on the instance -- it does NOT re-apply required /
    additionalProperties (those belong to the row object's own schema, not to
    the conditional branch). A branch that names no `properties` is a no-op."""
    if not isinstance(value, dict):
        return
    for key, sub in subschema.get("properties", {}).items():
        if key in value:
            _validate_node(value[key], sub, f"{path}.{key}", errors)


def _instance_matches(value, subschema):
    """True iff `value` satisfies the `if` subschema. The verification-report
    `if` is a property check on the row object (verdict in the pass-set enum):
    a clean match with no error collection drives the then/else selection. The
    branch property's subschema is the `if`'s -- here an ENUM membership, which
    _validate_node enforces -- so the match reads the pass set from the schema
    file, never hardcoded here."""
    probe = []
    _validate_branch_properties(value, subschema, "$if", probe)
    return not probe


def _validate_node(value, schema, path, errors):
    """Enforce the draft-07 subset the verification-report contract uses."""
    if "const" in schema and value != schema["const"]:
        errors.append(f"{path}: expected const {schema['const']!r}, got {value!r}")
    if "enum" in schema and value not in schema["enum"]:
        errors.append(f"{path}: {value!r} not in enum {schema['enum']}")

    json_type = schema.get("type")
    if json_type is not None and not _is_type(value, json_type):
        errors.append(f"{path}: expected type {json_type}, got {type(value).__name__}")
        return  # type mismatch -- deeper checks would be noise

    if json_type == "object":
        for req in schema.get("required", []):
            if req not in value:
                errors.append(f"{path}: missing required field {req!r}")
        props = schema.get("properties", {})
        if schema.get("additionalProperties") is False:
            for key in value:
                if key not in props:
                    errors.append(f"{path}: unexpected field {key!r}")
        for key, sub in props.items():
            if key in value:
                _validate_node(value[key], sub, f"{path}.{key}", errors)

        # Draft-07 conditional (if/then/else). The verification-report contract
        # uses it on the row object to encode the attribution invariant:
        #   if verdict in {verified_working, passed_not_verified}
        #                                 -> matched_address_version_id is a
        #                                    non-null integer (minimum 0)
        #   else                          -> matched_address_version_id is null
        # Minimal evaluation: test the `if` subschema against the instance; on
        # a match apply `then`, otherwise apply `else`. The branch subschemas
        # are read from the schema FILE -- the contract is not hardcoded here.
        if "if" in schema:
            branch = schema["then"] if _instance_matches(value, schema["if"]) else schema.get("else")
            if branch is not None:
                _validate_branch_properties(value, branch, path, errors)

    elif json_type == "array":
        item_schema = schema.get("items")
        if item_schema is not None:
            for i, item in enumerate(value):
                _validate_node(item, item_schema, f"{path}[{i}]", errors)

    elif json_type == "string":
        if "minLength" in schema and len(value) < schema["minLength"]:
            errors.append(f"{path}: string shorter than minLength {schema['minLength']}")

    elif json_type == "integer":
        if "minimum" in schema and value < schema["minimum"]:
            errors.append(f"{path}: {value} below minimum {schema['minimum']}")
        if "maximum" in schema and value > schema["maximum"]:
            errors.append(f"{path}: {value} above maximum {schema['maximum']}")


def _errors_against(document, schema):
    """Validate a document against an explicit schema; return error strings.
    The schema defaults to the whole-document SCHEMA, but a per-row JSONL line
    is validated against the rows.items subschema (D37)."""
    if _HAVE_JSONSCHEMA:
        validator = jsonschema.Draft7Validator(schema)
        return [e.message for e in validator.iter_errors(document)]
    errs = []
    _validate_node(document, schema, "$", errs)
    return errs


def _errors_for(document):
    """Validate a document against the whole-document SCHEMA."""
    return _errors_against(document, SCHEMA)


def _is_valid(document):
    return not _errors_for(document)


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
def test_valid_report_validates():
    """The sample v3 valid report passes: all required fields (incl. method_rank,
    invoke_attempted, invoke_skip_reason, matched_address_version_id on every
    row), every verdict in the 7-state enum, schema_version pinned to 3, the
    complete / rows_expected signal present, and the attribution invariant
    satisfied (pass-set rows carry a non-null integer id; non-pass rows null)."""
    errors = _errors_for(VALID_REPORT)
    assert errors == [], f"valid report should validate, got errors: {errors}"


def test_schema_version_present_and_pinned():
    """schema_version is present + pinned to the frozen value (3) in BOTH the
    schema (as a const) and the valid fixture."""
    assert SCHEMA["properties"]["schema_version"]["const"] == PINNED_SCHEMA_VERSION
    assert PINNED_SCHEMA_VERSION == 3, "v3 step pins schema_version to 3"
    assert VALID_REPORT["schema_version"] == PINNED_SCHEMA_VERSION


def test_verdict_enum_is_the_seven_state_set():
    """The schema's verdict enum is EXACTLY the 7-state set -- and none of the
    old v2 4-token vocabulary survives in it (the deliberate v2 -> v3 break)."""
    enum = SCHEMA["properties"]["rows"]["items"]["properties"]["verdict"]["enum"]
    assert sorted(enum) == sorted(V3_VERDICTS), (
        "the report verdict enum must be exactly the 7 frozen v3 tokens"
    )
    # The old 4-token vocabulary that is unique to v2 must be gone. (cannot_check
    # is shared between v2 and v3, so it is intentionally still present -- assert
    # only the dropped tokens.)
    for old in ("resolves_works", "wrong_target", "dead"):
        assert old not in enum, f"old v2 verdict {old!r} must NOT survive into the v3 enum"


def test_new_required_fields_present_in_schema_and_fixture():
    """method_rank, invoke_attempted, invoke_skip_reason are REQUIRED per-row
    fields in v3, with the contracted types, and the valid fixture carries them
    on every row."""
    item = SCHEMA["properties"]["rows"]["items"]
    for field in ("method_rank", "invoke_attempted", "invoke_skip_reason"):
        assert field in item["required"], f"{field} must be a required per-row field in v3"
    assert item["properties"]["method_rank"]["type"] == "integer"
    assert item["properties"]["method_rank"]["minimum"] == 1
    assert item["properties"]["method_rank"]["maximum"] == 5
    assert item["properties"]["invoke_attempted"]["type"] == "boolean"
    assert item["properties"]["invoke_skip_reason"]["type"] == ["string", "null"]
    for i, row in enumerate(VALID_REPORT["rows"]):
        for field in ("method_rank", "invoke_attempted", "invoke_skip_reason"):
            assert field in row, f"valid fixture row[{i}] must carry {field}"


def test_row_missing_a_new_required_field_is_rejected():
    """A row dropping any one of the three new v3 fields is REJECTED (the
    required-set is enforced). FALSIFIABLE: were the field optional, the
    delete-and-validate would still pass and this test would fail."""
    for field in ("method_rank", "invoke_attempted", "invoke_skip_reason"):
        doc = copy.deepcopy(VALID_REPORT)
        del doc["rows"][0][field]
        assert not _is_valid(doc), (
            f"a row missing the required {field} must be rejected"
        )


def test_old_v2_verdict_tokens_are_rejected():
    """The deliberate v2 -> v3 break: an OLD 4-token verdict (resolves_works /
    wrong_target / dead) is now out-of-enum and REJECTED. The id is set to a
    state matching whichever branch the (out-of-enum) verdict drives so the SOLE
    defect under test is the enum violation. FALSIFIABLE: an old-enum verdict
    validating fails this test."""
    for bad in ("resolves_works", "wrong_target", "dead"):
        doc = copy.deepcopy(VALID_REPORT)
        doc["rows"][0]["verdict"] = bad
        # An out-of-enum verdict is not in the pass set, so the else-branch wants
        # null; set null to isolate the failure to the enum check.
        doc["rows"][0]["matched_address_version_id"] = None
        assert not _is_valid(doc), f"old v2 verdict {bad!r} must be rejected (out of v3 enum)"


def test_old_prose_verdict_spelling_is_rejected():
    """The carried-forward freeze assertion: the OLD design-prose verdict
    spelling `resolves+works` (with the +) is out-of-enum and REJECTED."""
    enum = SCHEMA["properties"]["rows"]["items"]["properties"]["verdict"]["enum"]
    assert "resolves+works" not in enum, "the old + spelling must NOT be a valid enum member"
    doc = copy.deepcopy(VALID_REPORT)
    doc["rows"][0]["verdict"] = "resolves+works"
    doc["rows"][0]["matched_address_version_id"] = None
    assert not _is_valid(doc), "the old prose verdict spelling `resolves+works` must be rejected"


def test_out_of_enum_verdict_is_rejected():
    """Any verdict outside the seven frozen tokens is rejected (a typo, a stray
    token, the excluded `ambiguous` author-time outcome)."""
    for bad in ("ambiguous", "Unchanged", "ok", "verified working", ""):
        doc = copy.deepcopy(VALID_REPORT)
        doc["rows"][0]["verdict"] = bad
        doc["rows"][0]["matched_address_version_id"] = None
        assert not _is_valid(doc), f"verdict {bad!r} must be rejected (out of enum)"


def test_all_seven_verdict_tokens_accepted():
    """Each of the seven frozen tokens validates in a row. The matched id is set
    to satisfy the attribution invariant for each verdict -- a non-null integer
    for the pass set, null otherwise -- so the row is well-formed under v3."""
    enum = SCHEMA["properties"]["rows"]["items"]["properties"]["verdict"]["enum"]
    for token in enum:
        doc = copy.deepcopy(VALID_REPORT)
        doc["rows"][0]["verdict"] = token
        doc["rows"][0]["matched_address_version_id"] = 7 if token in PASS_VERDICTS else None
        assert _is_valid(doc), f"frozen token {token!r} must validate"


def test_method_rank_out_of_range_is_rejected():
    """method_rank is bounded 1..5; a value below 1 or above 5, a non-integer,
    or a bool is rejected. FALSIFIABLE: were the maximum unenforced, rank 6 would
    validate and this test would fail."""
    for bad in (0, 6, -1, 3.5, "1", True):
        doc = copy.deepcopy(VALID_REPORT)
        doc["rows"][0]["method_rank"] = bad
        assert not _is_valid(doc), f"method_rank {bad!r} must be rejected (integer 1..5 only)"


def test_invoke_skip_reason_enum_and_null():
    """invoke_skip_reason accepts exactly the three reason strings OR null, and
    rejects any other string. null and each reason validate; a stray string is
    rejected."""
    item_enum = SCHEMA["properties"]["rows"]["items"]["properties"]["invoke_skip_reason"]["enum"]
    assert None in item_enum, "invoke_skip_reason enum must permit null"
    for good in ("unsafe_to_call", "uncontainable", "not_a_callable_kind"):
        assert good in item_enum, f"invoke_skip_reason must permit {good!r}"
    # null validates (an invoke_attempted:true row); set invoke_attempted true to
    # keep the row coherent (no cross-field schema constraint, but keep it honest).
    doc = copy.deepcopy(VALID_REPORT)
    doc["rows"][0]["invoke_attempted"] = True
    doc["rows"][0]["invoke_skip_reason"] = None
    assert _is_valid(doc), "invoke_skip_reason null must validate"
    # A stray reason string is rejected.
    for bad in ("gave_up", "dunno", "unsafe", ""):
        doc = copy.deepcopy(VALID_REPORT)
        doc["rows"][0]["invoke_skip_reason"] = bad
        assert not _is_valid(doc), f"invoke_skip_reason {bad!r} must be rejected (out of enum)"


def test_invoke_attempted_must_be_boolean():
    """invoke_attempted is a strict boolean; a stray int/string is rejected."""
    for bad in (1, 0, "true", "false"):
        doc = copy.deepcopy(VALID_REPORT)
        doc["rows"][0]["invoke_attempted"] = bad
        assert not _is_valid(doc), f"invoke_attempted {bad!r} must be rejected (boolean only)"


def test_v2_schema_version_is_rejected():
    """A report whose schema_version is not the pinned value (3) is rejected,
    proving the pin is enforced. In v3 the OLD value 2 is now the wrong version
    -- a v2-shaped report must be rejected -- and so is any other non-3 value.
    (A real v2 report ALSO fails on its old verdicts + missing v3 fields; this
    isolates the version pin by bumping only schema_version on a v3 body.)"""
    for bad in (1, 2, 4, 0):
        doc = copy.deepcopy(VALID_REPORT)
        doc["schema_version"] = bad
        assert not _is_valid(doc), (
            f"schema_version {bad} (not the pinned 3) must be rejected"
        )


def test_summary_passing_counts_the_verified_block():
    """The redefined summary.passing == count of rows whose verdict is
    verified_working OR passed_not_verified (the D36 verified block). The valid
    fixture's passing field matches that count over its own rows."""
    pass_count = sum(
        1 for row in VALID_REPORT["rows"] if row["verdict"] in PASS_VERDICTS
    )
    assert VALID_REPORT["summary"]["passing"] == pass_count, (
        "summary.passing must equal the count of verified_working + "
        f"passed_not_verified rows ({pass_count}), got "
        f"{VALID_REPORT['summary']['passing']}"
    )
    assert VALID_REPORT["summary"]["total"] == len(VALID_REPORT["rows"])


def test_matched_id_required_on_every_row():
    """matched_address_version_id is a REQUIRED per-row field (integer-or-null),
    and the valid fixture populates it on every row. A row dropping it is
    rejected."""
    item = SCHEMA["properties"]["rows"]["items"]
    assert "matched_address_version_id" in item["required"]
    assert item["properties"]["matched_address_version_id"]["type"] == ["integer", "null"]
    for i, row in enumerate(VALID_REPORT["rows"]):
        assert "matched_address_version_id" in row, (
            f"valid fixture row[{i}] must carry matched_address_version_id"
        )
    doc = copy.deepcopy(VALID_REPORT)
    del doc["rows"][0]["matched_address_version_id"]
    assert not _is_valid(doc), (
        "a row missing the required matched_address_version_id must be rejected"
    )


def test_pass_row_with_null_matched_id_is_rejected():
    """The attribution then-branch: a pass-set row (verified_working /
    passed_not_verified) whose matched_address_version_id is NULL -- or MISSING
    -- is REJECTED. A passing attribution must NAME the row it matched."""
    for token in PASS_VERDICTS:
        # Explicit null on a pass row -> rejected (then-branch: non-null int).
        doc = copy.deepcopy(VALID_REPORT)
        doc["rows"][0]["verdict"] = token
        doc["rows"][0]["matched_address_version_id"] = None
        assert not _is_valid(doc), (
            f"a {token} row with a null matched_address_version_id must be rejected"
        )
        # Missing on a pass row -> rejected (required + then-branch).
        doc = copy.deepcopy(VALID_REPORT)
        doc["rows"][0]["verdict"] = token
        del doc["rows"][0]["matched_address_version_id"]
        assert not _is_valid(doc), (
            f"a {token} row missing matched_address_version_id must be rejected"
        )


def test_pass_row_with_noninteger_matched_id_is_rejected():
    """A pass-set row whose matched id is present but not a non-negative integer
    is rejected (the then-branch types it integer, minimum 0)."""
    for bad in ("42", -1, 3.5, True):
        doc = copy.deepcopy(VALID_REPORT)
        doc["rows"][0]["verdict"] = "verified_working"
        doc["rows"][0]["matched_address_version_id"] = bad
        assert not _is_valid(doc), (
            f"verified_working matched id {bad!r} must be rejected (non-negative integer only)"
        )


def test_nonpass_row_with_nonnull_matched_id_is_rejected():
    """The attribution else-branch: a failed / not_applicable / cannot_check /
    skipped / error row whose matched_address_version_id is NON-NULL is REJECTED.
    A non-pass row matched no candidate row, so it must NOT name one."""
    for token in NONPASS_VERDICTS:
        doc = copy.deepcopy(VALID_REPORT)
        doc["rows"][0]["verdict"] = token
        doc["rows"][0]["matched_address_version_id"] = 99
        assert not _is_valid(doc), (
            f"a {token} row with a non-null matched id must be rejected (else-branch: null)"
        )


def test_top_level_partial_signal_required():
    """The D37 partial-report signal -- complete (boolean) + rows_expected
    (integer) -- is a required top-level field. A report missing either is
    rejected. FALSIFIABLE: were they optional, the delete-and-validate would
    pass and this test would fail."""
    assert "complete" in SCHEMA["required"]
    assert "rows_expected" in SCHEMA["required"]
    assert SCHEMA["properties"]["complete"]["type"] == "boolean"
    assert SCHEMA["properties"]["rows_expected"]["type"] == "integer"
    for field in ("complete", "rows_expected"):
        doc = copy.deepcopy(VALID_REPORT)
        del doc[field]
        assert not _is_valid(doc), f"a finalized report missing {field} must be rejected"


def test_finalized_valid_report_is_complete():
    """The finalized valid fixture is a whole-set sweep: complete is true and
    rows[] length == rows_expected (the consumer reads this as 'swept the whole
    curated set')."""
    assert VALID_REPORT["complete"] is True
    assert VALID_REPORT["rows_expected"] == len(VALID_REPORT["rows"]), (
        "a complete report's rows length must equal rows_expected"
    )


def test_per_row_jsonl_line_validates_as_one_rows_element():
    """D37 (a): a single per-row line == exactly one rows[] element. Each row of
    the valid fixture, taken on its own, validates against the rows.items
    subschema (the durable per-row shape the sweep flushes as the row resolves).
    A truly-broken line (an old-enum verdict) does NOT validate against the same
    subschema -- so 'a line validates' is a real check, not 'anything validates'."""
    for i, row in enumerate(VALID_REPORT["rows"]):
        errors = _errors_against(row, ROW_ITEM_SCHEMA)
        assert errors == [], f"per-row line {i} should validate as a rows[] element, got {errors}"
    # FALSIFIABLE control: a line carrying an old v2 verdict fails the row schema.
    bad_line = copy.deepcopy(VALID_REPORT["rows"][0])
    bad_line["verdict"] = "resolves_works"
    bad_line["matched_address_version_id"] = None
    assert _errors_against(bad_line, ROW_ITEM_SCHEMA), (
        "a per-row line with an old v2 verdict must fail the rows.items subschema"
    )
    # FALSIFIABLE control: a line missing method_rank fails the row schema.
    short_line = copy.deepcopy(VALID_REPORT["rows"][0])
    del short_line["method_rank"]
    assert _errors_against(short_line, ROW_ITEM_SCHEMA), (
        "a per-row line missing method_rank must fail the rows.items subschema"
    )


def test_partial_report_validates():
    """D37 (c): a PARTIAL document -- complete:false, rows[] length < rows_expected
    (a sweep that died mid-run) -- still VALIDATES. A crashed-mid-sweep report is
    ingestible, NOT malformed. This is the falsifiable case the step bar names: a
    partial report being REJECTED fails the test."""
    # Property assertions on the shipped partial fixture (it IS partial).
    assert PARTIAL_REPORT["complete"] is False, "the partial fixture must carry complete:false"
    assert len(PARTIAL_REPORT["rows"]) < PARTIAL_REPORT["rows_expected"], (
        "the partial fixture must carry fewer rows than rows_expected"
    )
    errors = _errors_for(PARTIAL_REPORT)
    assert errors == [], f"a partial report must VALIDATE (it is ingestible), got: {errors}"


def test_partial_validation_is_not_masking_a_broken_check():
    """AP14 guard: prove 'a partial report validates' is NOT 'everything
    validates'. The same partial document, mutated into a genuinely-malformed
    one (an old v2 verdict on a row), MUST now be REJECTED. Partial-but-well-formed
    validates; partial-AND-malformed does not -- so the validator still
    discriminates on a partial report, it has not been loosened into a pass-all."""
    truly_malformed_partial = copy.deepcopy(PARTIAL_REPORT)
    truly_malformed_partial["rows"][0]["verdict"] = "resolves_works"  # old v2 token
    truly_malformed_partial["rows"][0]["matched_address_version_id"] = None
    assert not _is_valid(truly_malformed_partial), (
        "a partial report that is ALSO malformed (old v2 verdict) must still be rejected"
    )
    # And a partial report missing a required top-level field is still rejected.
    short_partial = copy.deepcopy(PARTIAL_REPORT)
    del short_partial["rows_expected"]
    assert not _is_valid(short_partial), (
        "a partial report missing rows_expected must still be rejected"
    )


def test_shipped_malformed_fixture_is_rejected():
    """The shipped malformed fixture (samples/report-malformed.json) carries its
    isolated v3 defects and must be REJECTED as a whole."""
    assert not _is_valid(MALFORMED_REPORT), (
        "the shipped malformed fixture must be rejected"
    )


if __name__ == "__main__":
    test_valid_report_validates()
    test_schema_version_present_and_pinned()
    test_verdict_enum_is_the_seven_state_set()
    test_new_required_fields_present_in_schema_and_fixture()
    test_row_missing_a_new_required_field_is_rejected()
    test_old_v2_verdict_tokens_are_rejected()
    test_old_prose_verdict_spelling_is_rejected()
    test_out_of_enum_verdict_is_rejected()
    test_all_seven_verdict_tokens_accepted()
    test_method_rank_out_of_range_is_rejected()
    test_invoke_skip_reason_enum_and_null()
    test_invoke_attempted_must_be_boolean()
    test_v2_schema_version_is_rejected()
    test_summary_passing_counts_the_verified_block()
    test_matched_id_required_on_every_row()
    test_pass_row_with_null_matched_id_is_rejected()
    test_pass_row_with_noninteger_matched_id_is_rejected()
    test_nonpass_row_with_nonnull_matched_id_is_rejected()
    test_top_level_partial_signal_required()
    test_finalized_valid_report_is_complete()
    test_per_row_jsonl_line_validates_as_one_rows_element()
    test_partial_report_validates()
    test_partial_validation_is_not_masking_a_broken_check()
    test_shipped_malformed_fixture_is_rejected()
    print("test_report_schema: all assertions passed "
          f"(jsonschema {'present' if _HAVE_JSONSCHEMA else 'absent -- hand-rolled checker'})")
