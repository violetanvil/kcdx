"""test_report_schema.py -- the validation test for the frozen JSON verification
report schema (the cross-repo contract, design D23/D25/D28/D31b).

WHAT THIS PROVES (the load-bearing claim) -- schema v2
------------------------------------------------------
The frozen schema (data/maintainer-tool/report-schema/verification-report.schema.json)
is a real, enforced contract, now at v2 (the per-row matched_address_version_id
attribution field + the version pin bumped 1 -> 2):

  1. The sample VALID report (samples/report-valid.json) VALIDATES -- all required
     fields present (incl. matched_address_version_id on every row), every verdict
     in-enum, schema_version present + pinned to 2, and the v2 attribution invariant
     holds (resolves_works rows carry a non-null integer matched id, non-pass rows
     carry null).
  2. A MALFORMED report is REJECTED. Each defect is isolated to ONE row so a
     pass/fail attributes cleanly:
       (a) an out-of-enum verdict -- specifically the OLD PROSE SPELLING `resolves+works`
           (with the +). This is the load-bearing freeze assertion the snake_case
           wire-token freeze is enforced: the old design-prose spelling MUST be
           rejected so the three parsers (C++ producer, JS + Python consumers) agree
           on one token form. (The snake_case freeze proof carries forward unchanged.)
       (b) a missing required field (a row dropping `verdict`).
       (c) a v1-shaped report (schema_version 1) -- now the WRONG version (the pin is 2).
       (d) the v2 attribution invariant: a resolves_works row whose
           matched_address_version_id is NULL (or missing) is REJECTED (a passing
           attribution MUST name its row), and a wrong_target/dead/cannot_check row
           whose matched_address_version_id is NON-NULL is REJECTED (a non-pass row
           must NOT name one). This is the load-bearing v2 assertion.
  3. schema_version is present + pinned to the frozen value 2.

THE VALIDATOR. `jsonschema` (the standard Python JSON Schema validator) is NOT a
project dependency (a new dep is the user's call -- not added here). When it IS
present this test uses it (the schema file is then validated by the canonical
implementation); when it is ABSENT this test uses a small hand-rolled checker
(_validate) that interprets the draft-07 SUBSET this contract uses -- type (incl. a
type UNION like ["integer", "null"] and the "null" type), required, enum, const,
minimum/minLength, additionalProperties:false, and the v2 conditional (if/then/else
on the row object). The schema JSON file is the single source of truth either way;
the hand-rolled checker reads that file and enforces it, it does not duplicate the
contract inline.

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

# The frozen schema_version this step pins. The test asserts the schema and the
# valid fixture both carry exactly this value. Bumped 1 -> 2 with the per-row
# matched_address_version_id attribution field.
PINNED_SCHEMA_VERSION = 2


# ---------------------------------------------------------------------------
# Load the schema + fixtures from disk (the schema file is the source of truth).
# ---------------------------------------------------------------------------
def _load(path):
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


SCHEMA = _load(SCHEMA_PATH)
VALID_REPORT = _load(VALID_PATH)
MALFORMED_REPORT = _load(MALFORMED_PATH)


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
    # bool is a subclass of int in Python; the contract uses no boolean fields,
    # and _is_type below excludes bool from integer so a stray bool is rejected.
}


def _is_type(value, json_type):
    # `type` may be a single string OR a list of allowed types (e.g. the v2
    # matched_address_version_id is ["integer", "null"]). A list matches if the
    # value satisfies ANY listed type.
    if isinstance(json_type, list):
        return any(_is_type(value, t) for t in json_type)
    if json_type == "integer":
        return isinstance(value, int) and not isinstance(value, bool)
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
    `if` is a property-const check on the row object (verdict == "..."): a
    clean match with no error collection drives the then/else selection."""
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
        # uses it on the row object to encode the v2 attribution invariant:
        #   if verdict == resolves_works  -> matched_address_version_id is a
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


def _errors_for(document):
    """Validate a document against SCHEMA; return a list of error strings."""
    if _HAVE_JSONSCHEMA:
        validator = jsonschema.Draft7Validator(SCHEMA)
        return [e.message for e in validator.iter_errors(document)]
    errs = []
    _validate_node(document, SCHEMA, "$", errs)
    return errs


def _is_valid(document):
    return not _errors_for(document)


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
def test_valid_report_validates():
    """The sample v2 valid report passes: all required fields (incl.
    matched_address_version_id on every row), every verdict in-enum,
    schema_version pinned to 2, and the v2 attribution invariant satisfied
    (resolves_works rows carry a non-null integer id; non-pass rows carry null)."""
    errors = _errors_for(VALID_REPORT)
    assert errors == [], f"valid report should validate, got errors: {errors}"


def test_schema_version_present_and_pinned():
    """schema_version is present + pinned to the frozen value (2) in BOTH the
    schema (as a const) and the valid fixture."""
    assert SCHEMA["properties"]["schema_version"]["const"] == PINNED_SCHEMA_VERSION
    assert PINNED_SCHEMA_VERSION == 2, "v2 step pins schema_version to 2"
    assert VALID_REPORT["schema_version"] == PINNED_SCHEMA_VERSION


def test_matched_id_required_on_every_row():
    """The v2 field matched_address_version_id is a REQUIRED per-row field (the
    contract carries it on every row, integer-or-null), and the valid fixture
    populates it on every row."""
    row_required = SCHEMA["properties"]["rows"]["items"]["required"]
    assert "matched_address_version_id" in row_required, (
        "matched_address_version_id must be a required per-row field in v2"
    )
    prop = SCHEMA["properties"]["rows"]["items"]["properties"][
        "matched_address_version_id"
    ]
    assert prop["type"] == ["integer", "null"], (
        "matched_address_version_id is typed integer-or-null"
    )
    for i, row in enumerate(VALID_REPORT["rows"]):
        assert "matched_address_version_id" in row, (
            f"valid fixture row[{i}] must carry matched_address_version_id"
        )
    # A row dropping the new required field is rejected.
    doc = copy.deepcopy(VALID_REPORT)
    del doc["rows"][0]["matched_address_version_id"]
    assert not _is_valid(doc), (
        "a row missing the required matched_address_version_id must be rejected"
    )


def test_old_prose_verdict_spelling_is_rejected():
    """The load-bearing freeze assertion: the OLD design-prose verdict spelling
    `resolves+works` (with the +) is out-of-enum and REJECTED. The wire token is
    frozen to snake_case (resolves_works); the old + spelling must not parse."""
    assert "resolves+works" not in SCHEMA["properties"]["rows"]["items"][
        "properties"
    ]["verdict"]["enum"], "the old + spelling must NOT be a valid enum member"
    # The shipped malformed fixture carries exactly this defect.
    assert not _is_valid(MALFORMED_REPORT), (
        "the malformed fixture (old prose verdict spelling) must be rejected"
    )


def test_malformed_missing_required_field_is_rejected():
    """A report missing a required field (a row without `verdict`) is rejected."""
    doc = copy.deepcopy(VALID_REPORT)
    del doc["rows"][0]["verdict"]
    assert not _is_valid(doc), "a row missing the required `verdict` field must be rejected"


def test_malformed_bad_schema_version_is_rejected():
    """A report whose schema_version is not the pinned value (2) is rejected,
    proving the pin is enforced, not cosmetic. In v2 the OLD value 1 is now the
    wrong version -- a v1-shaped report must be rejected -- and so is any other
    non-2 value."""
    for bad in (1, 3, 0):
        doc = copy.deepcopy(VALID_REPORT)
        doc["schema_version"] = bad
        assert not _is_valid(doc), (
            f"schema_version {bad} (not the pinned 2) must be rejected"
        )


def test_out_of_enum_verdict_is_rejected():
    """Any verdict outside the four frozen snake_case tokens is rejected (a typo,
    a stray token, the excluded `ambiguous` author-time outcome). The matched id
    is set null on the mutated row so the SOLE defect under test is the verdict
    enum violation (an out-of-enum verdict is not resolves_works, so the else
    branch wants null -- isolating the rejection to the enum check)."""
    for bad in ("resolves+works", "ambiguous", "Unchanged", "ok", ""):
        doc = copy.deepcopy(VALID_REPORT)
        doc["rows"][0]["verdict"] = bad
        doc["rows"][0]["matched_address_version_id"] = None
        assert not _is_valid(doc), f"verdict {bad!r} must be rejected (out of enum)"


def test_all_four_verdict_tokens_accepted():
    """Each of the four frozen tokens validates in a row (and only those four are
    in the schema's enum). The matched id is set to satisfy the v2 attribution
    invariant for each verdict -- a non-null integer for resolves_works, null for
    the three non-pass verdicts -- so the row is well-formed under v2."""
    enum = SCHEMA["properties"]["rows"]["items"]["properties"]["verdict"]["enum"]
    assert sorted(enum) == sorted(
        ["resolves_works", "wrong_target", "dead", "cannot_check"]
    ), "the report verdict enum must be exactly the four frozen tokens"
    for token in enum:
        doc = copy.deepcopy(VALID_REPORT)
        doc["rows"][0]["verdict"] = token
        doc["rows"][0]["matched_address_version_id"] = (
            7 if token == "resolves_works" else None
        )
        assert _is_valid(doc), f"frozen token {token!r} must validate"


def test_pass_row_with_null_matched_id_is_rejected():
    """The load-bearing v2 assertion (the then-branch): a resolves_works row whose
    matched_address_version_id is NULL -- or MISSING -- is REJECTED. A passing
    attribution must NAME the address_version row it matched; a non-null integer is
    mandatory on a pass. The shipped malformed fixture carries exactly this defect."""
    # Missing on a resolves_works row -> rejected (required + then-branch).
    doc = copy.deepcopy(VALID_REPORT)
    assert doc["rows"][0]["verdict"] == "resolves_works"
    del doc["rows"][0]["matched_address_version_id"]
    assert not _is_valid(doc), (
        "a resolves_works row missing matched_address_version_id must be rejected"
    )
    # Explicit null on a resolves_works row -> rejected (then-branch: non-null int).
    doc = copy.deepcopy(VALID_REPORT)
    doc["rows"][0]["matched_address_version_id"] = None
    assert not _is_valid(doc), (
        "a resolves_works row with a null matched_address_version_id must be rejected"
    )
    # The shipped malformed fixture has a resolves_works row with a null id.
    assert not _is_valid(MALFORMED_REPORT), (
        "the malformed fixture (resolves_works row with null matched id) must be rejected"
    )


def test_pass_row_with_noninteger_matched_id_is_rejected():
    """A resolves_works row whose matched id is present but not a non-negative
    integer is rejected (the then-branch types it integer, minimum 0)."""
    for bad in ("42", -1, 3.5, True):
        doc = copy.deepcopy(VALID_REPORT)
        assert doc["rows"][0]["verdict"] == "resolves_works"
        doc["rows"][0]["matched_address_version_id"] = bad
        assert not _is_valid(doc), (
            f"resolves_works matched id {bad!r} must be rejected (non-negative integer only)"
        )


def test_nonpass_row_with_nonnull_matched_id_is_rejected():
    """The v2 else-branch: a wrong_target / dead / cannot_check row whose
    matched_address_version_id is NON-NULL is REJECTED. A non-pass row matched no
    candidate address_version row, so it must NOT name one -- the id is null."""
    # The valid fixture rows 2/3/4 are the three non-pass verdicts (null ids).
    nonpass = {
        row["verdict"]: i
        for i, row in enumerate(VALID_REPORT["rows"])
        if row["verdict"] != "resolves_works"
    }
    assert set(nonpass) == {"wrong_target", "dead", "cannot_check"}, (
        "the valid fixture must carry one row of each non-pass verdict"
    )
    for verdict, idx in nonpass.items():
        doc = copy.deepcopy(VALID_REPORT)
        doc["rows"][idx]["matched_address_version_id"] = 99
        assert not _is_valid(doc), (
            f"a {verdict} row with a non-null matched id must be rejected (else-branch: null)"
        )


if __name__ == "__main__":
    test_valid_report_validates()
    test_schema_version_present_and_pinned()
    test_matched_id_required_on_every_row()
    test_old_prose_verdict_spelling_is_rejected()
    test_malformed_missing_required_field_is_rejected()
    test_malformed_bad_schema_version_is_rejected()
    test_out_of_enum_verdict_is_rejected()
    test_all_four_verdict_tokens_accepted()
    test_pass_row_with_null_matched_id_is_rejected()
    test_pass_row_with_noninteger_matched_id_is_rejected()
    test_nonpass_row_with_nonnull_matched_id_is_rejected()
    print("test_report_schema: all assertions passed "
          f"(jsonschema {'present' if _HAVE_JSONSCHEMA else 'absent -- hand-rolled checker'})")
