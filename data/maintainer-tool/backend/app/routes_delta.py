"""app.routes_delta -- the field-delta endpoint.

The confirm surface calls this to render the plain-language FIELD DELTA: given
the SAVED record + the prospective EDITED record, it returns the changed fields as
`field: old -> new` (the human's acceptance signal; the literal CSV diff is not
exposed). For a NEW version it also returns the "nothing changed" verdict (a new
version identical to its source steers the maintainer to re-verify, not duplicate).

THIN CALLER: the backend computes NOTHING. It deserializes
the body, calls the data-core's `field_delta` / `is_new_version_nothing_changed`, and
serializes the result. No delta logic, no comparison, no field-order logic here -- all the
data-core's. The field-delta is a distinct concern from the read endpoints, so it sits in
its own router (structure-by-responsibility); main.py includes it.

WHY A LIST, NOT A JSON OBJECT (the response shape decision)
-----------------------------------------------------------
The data-core returns an OrderedDict whose field ORDER is load-bearing -- the authored CSV
header order, so the confirm surface lists the changed fields in a stable layout on every call (layout
stability). A JSON object's key order is NOT guaranteed across the wire (a client's
JSON parser may reorder object keys), which would defeat that stability. A JSON ARRAY
preserves order by construction -- so the delta serializes as a LIST of {field, old, new},
one entry per changed field, in the data-core's deterministic order. The frontend renders
the list top-to-bottom as the order it receives.

WHY ONE ENDPOINT (the field delta + the nothing-changed verdict together)
-------------------------------------------------------------------------
The confirm surface's "new row" state shows the field delta AND the nothing-changed steering at once. One
POST gives the frontend both in a single call: the delta is always returned; the verdict
is folded in as an optional `nothing_changed` field, present only when the caller flags
`is_new_version=true`. Two endpoints would force the confirm surface to make two round-trips for one screen.
"""
from collections import OrderedDict
from typing import Literal

from fastapi import APIRouter
from pydantic import BaseModel

from . import data_core

router = APIRouter()

# WHY two record_kind tokens -> two field_order constants: picking the constant is a
# PARAMETER SELECTION, not computation. A version-row edit (the common confirm surface)
# orders by the address_versions header; a lifecycle (names-row) edit orders by the
# address_names header (field_delta's field_order arg, the lifecycle UPDATE path).
_FIELD_ORDER_BY_KIND = {
    "version": data_core.ADDRESS_VERSIONS_CSV_HEADER,
    "names": data_core.ADDRESS_NAMES_CSV_HEADER,
}


class FieldDeltaRequest(BaseModel):
    """The two record dicts the confirm surface compares, + the order/verdict flags.

    saved / prospective are the data-core's seed-row dict shape ({column: cell}, '' or
    None for an empty/NULL cell -- the data-core treats them as the SAME empty cell).
    record_kind selects the deterministic field order (default "version"). is_new_version
    requests the nothing-changed verdict (default false -- an UPDATE does not need it).

    record_kind is a closed Literal: an unrecognized value is a clean 422 (Pydantic), not a
    silent fall-through to the version order -- a wrong-order delta must never ship silently."""
    saved: dict
    prospective: dict
    record_kind: Literal["version", "names"] = "version"
    is_new_version: bool = False


@router.post("/field-delta")
def field_delta(req: FieldDeltaRequest):
    """The saved-vs-prospective field delta + the optional nothing-changed verdict.

    Returns {"changes": [{field, old, new}, ...]} -- a LIST (order load-bearing, see module
    docstring) holding ONLY the changed fields in the data-core's deterministic order; an
    unchanged field (incl. a None-vs-'' no-op) is absent. When is_new_version is true, also
    returns "nothing_changed": <bool> from is_new_version_nothing_changed (the re-verify steering).

    THIN: it calls the data-core and reshapes OrderedDict->list (a serialization boundary,
    like routes_read's _json_safe -- not rule logic). record_kind only picks a field_order
    constant. Computes nothing itself."""
    # record_kind is a closed Literal (Pydantic rejects anything else with a 422), so the
    # lookup always hits -- no silent fall-through to a default order.
    field_order = _FIELD_ORDER_BY_KIND[req.record_kind]

    delta: "OrderedDict" = data_core.field_delta(
        req.saved, req.prospective, field_order=field_order)

    # OrderedDict {field: (old, new)} -> ordered list of {field, old, new}. The data-core
    # already fixed the order; iterating the OrderedDict preserves it into the list.
    response = {
        "changes": [{"field": field, "old": old, "new": new}
                    for field, (old, new) in delta.items()],
    }

    if req.is_new_version:
        # Only meaningful for a NEW version (identical-except-valid_from -> steer the
        # maintainer to re-verify the existing row). The data-core owns the verdict.
        response["nothing_changed"] = data_core.is_new_version_nothing_changed(
            req.saved, req.prospective)

    return response
