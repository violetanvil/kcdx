"""test_report_schema.py -- the validation test for the frozen JSON verification
report schema (the cross-repo contract, design D23/D25/D28/D31b).

WHAT THIS PROVES (the load-bearing claim)
-----------------------------------------
The frozen schema (data/maintainer-tool/report-schema/verification-report.schema.json)
is a real, enforced contract:

  1. The sample VALID report (samples/report-valid.json) VALIDATES -- all required
     fields present, every verdict in-enum, schema_version present + pinned to 1.
  2. A MALFORMED report is REJECTED. Three independent defect cases, each isolated to
     ONE defect so a pass/fail attributes cleanly:
       (a) an out-of-enum verdict -- specifically the OLD PROSE SPELLING `resolves+works`
           (with the +). This is the load-bearing assertion the snake_case wire-token
           freeze is enforced: the old design-prose spelling MUST be rejected so the
           three parsers (C++ producer, JS + Python consumers) agree on one token form.
       (b) a missing required field (a row dropping `verdict`).
       (c) a bad schema_version (2, not the pinned 1).
  3. schema_version is present + pinned to the frozen value 1.

THE VALIDATOR. `jsonschema` (the standard Python JSON Schema validator) is NOT a
project dependency (a new dep is the user's call -- not added here). When it IS
present this test uses it (the schema file is then validated by the canonical
implementation); when it is ABSENT this test uses a small hand-rolled checker
(_validate) that interprets the draft-07 SUBSET this contract uses -- type,
required, enum, const, minimum/minLength, and additionalProperties:false. The schema
JSON file is the single source of truth either way; the hand-rolled checker reads
that file and enforces it, it does not duplicate the contract inline.

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
# valid fixture both carry exactly this value.
PINNED_SCHEMA_VERSION = 1


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
    if json_type == "integer":
        return isinstance(value, int) and not isinstance(value, bool)
    py = _JSON_TYPE.get(json_type)
    return py is not None and isinstance(value, py)


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
    """The sample valid report passes: all required fields, every verdict in-enum,
    schema_version pinned."""
    errors = _errors_for(VALID_REPORT)
    assert errors == [], f"valid report should validate, got errors: {errors}"


def test_schema_version_present_and_pinned():
    """schema_version is present + pinned to the frozen value in BOTH the schema
    (as a const) and the valid fixture."""
    assert SCHEMA["properties"]["schema_version"]["const"] == PINNED_SCHEMA_VERSION
    assert VALID_REPORT["schema_version"] == PINNED_SCHEMA_VERSION


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
    """A report whose schema_version is not the pinned value is rejected (proving
    the pin is enforced, not cosmetic)."""
    doc = copy.deepcopy(VALID_REPORT)
    doc["schema_version"] = 2
    assert not _is_valid(doc), "a non-pinned schema_version must be rejected"


def test_out_of_enum_verdict_is_rejected():
    """Any verdict outside the four frozen snake_case tokens is rejected (a typo,
    a stray token, the excluded `ambiguous` author-time outcome)."""
    for bad in ("resolves+works", "ambiguous", "Unchanged", "ok", ""):
        doc = copy.deepcopy(VALID_REPORT)
        doc["rows"][0]["verdict"] = bad
        assert not _is_valid(doc), f"verdict {bad!r} must be rejected (out of enum)"


def test_all_four_verdict_tokens_accepted():
    """Each of the four frozen tokens validates in a row (and only those four are
    in the schema's enum)."""
    enum = SCHEMA["properties"]["rows"]["items"]["properties"]["verdict"]["enum"]
    assert sorted(enum) == sorted(
        ["resolves_works", "wrong_target", "dead", "cannot_check"]
    ), "the report verdict enum must be exactly the four frozen tokens"
    for token in enum:
        doc = copy.deepcopy(VALID_REPORT)
        doc["rows"][0]["verdict"] = token
        assert _is_valid(doc), f"frozen token {token!r} must validate"


if __name__ == "__main__":
    test_valid_report_validates()
    test_schema_version_present_and_pinned()
    test_old_prose_verdict_spelling_is_rejected()
    test_malformed_missing_required_field_is_rejected()
    test_malformed_bad_schema_version_is_rejected()
    test_out_of_enum_verdict_is_rejected()
    test_all_four_verdict_tokens_accepted()
    print("test_report_schema: all assertions passed "
          f"(jsonschema {'present' if _HAVE_JSONSCHEMA else 'absent -- hand-rolled checker'})")
