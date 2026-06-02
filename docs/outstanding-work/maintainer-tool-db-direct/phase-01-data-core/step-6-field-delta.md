# Step 6 — field_delta.py: saved-vs-prospective field-delta computation (D8)

**What.** Add `data/refdata-extractor/python/seeds_shared/field_delta.py` — the
headless computation that, given a saved record and the prospective edited record,
produces the **plain-language field delta**: the set of changed fields as
`field → (old, new)`. This is the data behind the confirm surface (D8): the human
reads `last_verified_at_version: 1.4 → 1.5`, not a CSV diff. Covers every edit shape
(version-row UPDATE, INSERT, lifecycle UPDATE) — a delta over the union of an entity's
authored columns. Also computes the D12 "nothing changed" verdict for a new version
(empty delta except `valid_from_version` → steer to re-verify). Pure, Qt-free, no I/O.

**Scope.** One new module `field_delta.py` + its delta + nothing-changed functions.
Pure computation over two record dicts (the data-core's existing dict shape via
`dict_codec.py`). No GUI, no Qt, no DB write, no validation (it describes a change;
the validator gates it elsewhere). Feeds the GUI save-confirm (step 11).

**Test bar.** `data/refdata-extractor/tests/test_field_delta.py` (new). On synthetic
record pairs: an audit-trio edit yields exactly the trio's changed fields as
`old → new`; an unchanged field is absent from the delta; a new-entity INSERT yields
an all-fields-from-null delta; a new version identical to its source (except
`valid_from_version`) yields the nothing-changed verdict; ordering + formatting of the
delta is deterministic. Runs headless. (Pure function — the cheapest oracle in the
phase.)

**Test bar runnable now?** Yes — a pure function over record dicts; the oracle runs
the moment it lands, no dependency on the GUI.

**Dependencies.** `dict_codec.py` (the record-dict shape — exists). Independent of
steps 3–5's writes (it describes a prospective change, it does not perform it);
sequenced last in Phase 1 so it can be tested against every edit shape's record pair.

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§10 D8 (the field delta is the human's acceptance signal; `field: old → new`, only
changed fields) + §7 (the field-delta confirm state) + §6 US-4. The confirm-surface
rendering is `data/maintainer-tool/ui/screens/s06-save-confirm.md` (this step is its
data source).

**Disassembler-test / author-burden.** N/A — pure data computation, no author-facing
game-function input.
