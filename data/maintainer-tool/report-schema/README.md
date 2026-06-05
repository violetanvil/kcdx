# Verification report schema — the cross-repo contract

The frozen JSON contract for the in-game verification report. This is the single
seam crossing the two-repo split (design D23): the report is **produced** by the
in-game kcdx verification test-suite plugin and **consumed** by the maintainer-tool
frontend's verification worklist.

- **Schema:** [`verification-report.schema.json`](verification-report.schema.json)
  — JSON Schema draft-07, language-neutral so the producer (C++) and the two
  consumers (JS + Python) all read the one contract.
- **Sample valid report:** [`samples/report-valid.json`](samples/report-valid.json)
- **Sample malformed report:** [`samples/report-malformed.json`](samples/report-malformed.json)
  (an out-of-enum verdict — the old prose spelling `resolves+works` — proving the
  snake_case freeze is enforced, plus a v2 attribution defect: a `resolves_works`
  row carrying a null `matched_address_version_id`).
- **Validator + test:** `data/refdata-extractor/tests/test_report_schema.py`
  (pytest; the canonical home for this sub-system's Python tests, beside the
  `seeds_shared` suite). It validates both fixtures against the schema.

## Where this fits

- **Producer (Phase 4, not yet built):** a kcdx test-suite plugin runs both
  startup checks (design D25 — the on-disk version-applicability hash + the
  loaded-image reachability check) over every DB row at engine startup and writes
  one report file alongside `kcdx-dev.log`.
- **Consumer (Phase 5, not yet built):** the frontend reads the report file
  **client-side via the File API** (design D31b — no backend read seam) and
  renders the verification worklist (screen `ui/screens/s08-verification-worklist.md`).

The schema lands kcdx-side (the producer's tree) because the producer is kcdx and
the consumer's repo is the gitignored frontend; the frontend vendors/validates
against this canonical copy.

## The verdict tokens (settled — JSON wire form)

The design prose labels the verdicts with `+` and hyphens
(`resolves+works` / `wrong-target` / `cannot-check`, design D25/D28). The JSON
wire form is FROZEN to **snake_case** tokens, because this contract is parsed by
three languages (a C++ producer, a JS consumer, a Python validator) and a wire
token with `+`/`-` is awkward and error-prone to compare across them. The four —
and ONLY these four — report verdicts are:

| Design prose label              | Frozen JSON token | Meaning (design D25)                                                                 |
|---------------------------------|-------------------|--------------------------------------------------------------------------------------|
| `resolves+works`                | `resolves_works`  | on-disk hash matches (right version for this build) AND resolves into live `.text`.   |
| `wrong-target` / `changed`      | `wrong_target`    | on-disk hash mismatches — the build diverged from the DB's recorded version → avoid.  |
| `dead`                          | `dead`            | does not resolve into live `.text` (unreachable) → avoid.                             |
| `cannot-check`                  | `cannot_check`    | no `content_hash` / a non-byte kind / a deferred kind (e.g. `vtable_index`).          |

`ambiguous` is an **author-time static (s04) outcome, NOT a report verdict**
(s08: "The static Ambiguous verdict is an s04 author-time outcome, not a report
one"). It is deliberately excluded from the report enum.

## The shape (frozen)

Top-level (all four required):

| Field            | Type     | Source        | Notes                                                            |
|------------------|----------|---------------|------------------------------------------------------------------|
| `schema_version` | const 2  | step + test bar | Pinned; a consumer rejects an unrecognized value. (v1 → v2: the matched-id field.) |
| `game_version`   | string   | D28 version stamp | The resolved game/DLL version the report was produced against. |
| `summary`        | object   | D28 + s08     | `{ passing, total }` — backs the `M/N passing` header.           |
| `rows`           | array    | D28           | One entry per checked DB row.                                    |

Per row (all five required):

| Field                        | Type            | Source  | Notes                                                                        |
|------------------------------|-----------------|---------|------------------------------------------------------------------------------|
| `kcdx_id`                    | integer         | D28     | The entity id; the consumer resolves the entity name from its own DB by this. |
| `version`                    | string          | D28     | The resolved version the row was checked at.                                  |
| `verdict`                    | enum            | D25/D28 | One of the four snake_case tokens above.                                      |
| `detail`                     | string          | D28     | Free-text cause line; required, the empty string permitted.                  |
| `matched_address_version_id` | integer-or-null | D34     | The `address_version` row the swept bytes matched. Non-null integer (≥ 0) on a `resolves_works` row; null on `wrong_target` / `dead` / `cannot_check`. Enforced by a row-level conditional. |

## Recorded interpretations (where the design did not fully determine a field)

The design fixes the field SET (D28: per row `kcdx_id` + resolved version + verdict
+ detail; top-level a version stamp + summary). Where the design did not pin a
field's exact type or required/optional status, the reading built here is:

- **`name` is NOT in the report.** D28 enumerates the per-row fields as
  `kcdx_id`, resolved version, verdict, detail — `name` is not among them. US-11
  (§6) repeats this. The consumer (s08) renders `name (mono)` by resolving it from
  its own loaded DB by `kcdx_id` — its "row `<id>` is not in the current database
  (stale report?)" case is exactly the name-resolution-by-id miss. So `name` is
  **frontend-resolved, not carried in the report**. (Carrying `name` in the report
  would also let a stale report's name disagree with the live DB — the consumer
  must resolve from its DB regardless.)
- **`detail` is required-with-empty-allowed**, not optional. D28 names the row as
  "verdict + detail", so `detail` is part of the row contract; making it required
  (empty string allowed) keeps every row's shape uniform for the three parsers
  while letting a clean `resolves_works` row carry no extra cause. (The alternative
  — optional/absent — would force each consumer to branch on presence; a required
  field with an empty-string sentinel is the simpler cross-language contract.)
- **`kcdx_id` is an integer.** It is the `address_names.id` (design §11.1 — the
  stable integer `id` plugins reference), so the report carries it as an integer,
  not a string.
- **`schema_version` is an integer `const 2`.** The step bar requires it present +
  pinned; an integer is the simplest pinned form three languages compare exactly.
  (Bumped 1 → 2 with the matched-id field — see "The v1 → v2 bump" below.)
- **`game_version` / `version` are free-form strings** in the project's resolved-tag
  form (e.g. `release_1_5_1164953_841` or `1.5.1164953`). The design does not fix a
  single canonical string form for the report stamp, so the contract accepts the
  resolver's string output as-is rather than imposing a stricter pattern the
  producer might not emit.
- **`matched_address_version_id` is integer-or-null, required on every row, with a
  verdict-conditional invariant.** D34 names the field; it is `["integer", "null"]`
  with `minimum: 0` when non-null, and it is in the per-row `required` set (present
  on every row for a uniform shape across the three parsers — the same
  required-with-a-sentinel reasoning `detail` uses, where the sentinel here is
  `null`). The non-null-vs-null choice is not free per row: it is determined by the
  verdict (the conditional below), so the schema encodes it rather than leaving it to
  the producer.

## The v1 → v2 bump — the matched-id attribution field (D34)

**What changed.** v2 adds one per-row field, `matched_address_version_id`, and bumps
`schema_version` `1 → 2`. Nothing else in the shape moved (the four verdict tokens,
the strict `additionalProperties: false` at every level, the const-pinned version,
and the top-level/summary fields are unchanged).

**Why.** The in-game sweep does not just say pass/fail per `kcdx_id` — it resolves
WHICH `address_version` row the swept bytes belong to, by matching them against each
candidate row's fingerprint (the `content_hash` for a function, the per-kind datum
otherwise). The report carries that matched row id so the importer can attribute an
**uncovered version** to the right row: when the swept version falls in a gap between
an entity's intervals, a passing re-verify extends THAT row's `valid_through` forward
to the swept version (D34). Without the matched id, the importer cannot know which
row to extend.

**The conditional invariant (the real attribution rule).** The schema encodes, on
the row object, a draft-07 `if`/`then`/`else`:

- `if verdict == resolves_works` → `then` `matched_address_version_id` is a **non-null
  integer** (`minimum: 0`). A passing attribution ALWAYS names the row it matched.
- `else` (a `wrong_target` / `dead` / `cannot_check` row) → `matched_address_version_id`
  is **null**. A `wrong_target` row matched NO candidate row; a `dead` / `cannot_check`
  row was not byte-matched at all — so none names a row.

A pass always names its row; a non-pass never names one. A `resolves_works` row with a
null/missing matched id, or a non-pass row with a non-null one, is a malformed report
and is rejected.

## Adding a verdict / field later

The contract is FROZEN at `schema_version` 2. A new verdict token, a new field, or
a changed required-set is a `schema_version` bump (a new pinned value), surfaced as
a contract change — not a silent edit, because both repos parse this file.
