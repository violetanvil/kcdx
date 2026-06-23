# Verification report schema — the cross-repo contract

The frozen JSON contract for the in-game verification report. This is the single
seam crossing the two-repo split: the report is **produced** by the in-game kcdx
verification test-suite plugin and **consumed** by the maintainer-tool frontend's
verification worklist.

- **Schema:** [`verification-report.schema.json`](verification-report.schema.json)
  — JSON Schema draft-07, language-neutral so the producer (C++) and the two
  consumers (JS + Python) all read the one contract.
- **Sample valid report:** [`samples/report-valid.json`](samples/report-valid.json)
  (a finalized, whole-set sweep — `complete: true`, every verdict in the 7-state
  enum, the attribution invariant satisfied).
- **Sample partial report:** [`samples/report-partial.json`](samples/report-partial.json)
  (a sweep that died mid-run — `complete: false`, fewer rows than `rows_expected`;
  it still VALIDATES, because a crashed-mid-sweep report is ingestible, not malformed).
- **Sample malformed report:** [`samples/report-malformed.json`](samples/report-malformed.json)
  (isolated defects, each on its own row: the old prose spelling `resolves+works`,
  an old 4-token verdict, and a verified-block row carrying a null
  `matched_address_version_id`).
- **Validator + test:** the report-schema validation test in the data-core
  test suite (pytest). It validates the fixtures against the schema.

## Where this fits

- **Producer (not yet built):** a dev-mode kcdx test-suite plugin runs a
  console-triggered sweep after a save loads, giving every curated row its
  strongest applicable active attempt, and flushes each row's result to disk as
  the row resolves (the incremental-flush contract below) alongside `kcdx-dev.log`.
- **Consumer (not yet built):** the frontend reads the report file
  **client-side via the File API** (no backend read seam) and renders the
  verification worklist.

The schema lands kcdx-side (the producer's tree) because the producer is kcdx and
the consumer's repo is the gitignored frontend; the frontend vendors/validates
against this canonical copy.

## The verdict enum (settled — JSON wire form)

The wire form is FROZEN to **snake_case** tokens, because this contract is parsed
by three languages (a C++ producer, a JS consumer, a Python validator). v3 carries
the **7-state active-attempt verdict enum** — every curated row gets its strongest
applicable active attempt and one of these seven, and ONLY these seven, verdicts:

| Token                 | Meaning                                                                                            |
|-----------------------|---------------------------------------------------------------------------------------------------|
| `verified_working`    | observed executing correctly this session (a kcdx hook fired with correct pass-through, or kcdx's own production call already ran) — the top rung. |
| `passed_not_verified` | the strongest applicable attempt PASSED (bytes / resolution / wiring) but execution could not be observed — passing tests, not proof-of-working. |
| `failed`              | an attempt the row should pass returned wrong (diverged bytes / dead resolve / wrong target / a thrown exercise). |
| `not_applicable`      | the running build's version is not covered by this row (the version check ran and found non-coverage — a gap). |
| `cannot_check`        | the attempt ran but the row lacks the inputs the check needs (no fingerprint, or a deferred kind such as a vtable slot index). |
| `skipped`             | a precondition was not met this run (needs a loaded save, run from the menu, module absent) — the only "did not run", and it names why. |
| `error`               | the harness itself faulted on this row (the row is fine, the test blew up — distinct from `failed`). |

A verdict is the **ceiling of the strongest method that ran**: only observed live
execution (rank 1) earns `verified_working`; every other passing attempt caps at
`passed_not_verified`. `ambiguous` is an author-time static outcome, NOT a report
verdict — it is deliberately excluded from this enum.

## The proof-strength rank (`method_rank`)

Each row carries the rank of the strongest method that actually ran, so the report
records BOTH the verdict and the evidence strength behind it:

| `method_rank` | Method (weakest → strongest)                                              |
|---------------|--------------------------------------------------------------------------|
| 5             | existence / resolution                                                    |
| 4             | on-disk version-applicability hash                                        |
| 3             | loaded-image reachability                                                 |
| 2             | safe-read exercise (a live value read with zero mutation)                 |
| 1             | observed live execution (the function fired in the running process and passed through correctly) |

A passing rank 2–5 caps at `passed_not_verified`; only rank 1 can award
`verified_working`.

## The shape (frozen)

Top-level (all six required):

| Field            | Type     | Notes                                                                            |
|------------------|----------|----------------------------------------------------------------------------------|
| `schema_version` | const 3  | Pinned; a consumer rejects an unrecognized value.                                |
| `game_version`   | string   | The resolved game/DLL version the report was produced against.                   |
| `summary`        | object   | `{ passing, total }` — `passing` is the verified block (see below).              |
| `rows`           | array    | One entry per curated row swept; each element is also the per-row line shape.     |
| `complete`       | boolean  | Whether the sweep ran to the end of the curated set (partial-report signal).      |
| `rows_expected`  | integer  | The count of rows the sweep intended to attempt (the partial-report denominator). |

`summary.passing` = count of rows whose verdict is `verified_working` OR
`passed_not_verified` (the verified block — the strongest applicable attempt
passed, whether or not execution was observed). `summary.total` = `rows` length.

Per row (all eight required):

| Field                        | Type            | Notes                                                                         |
|------------------------------|-----------------|-------------------------------------------------------------------------------|
| `kcdx_id`                    | integer         | The entity id; the consumer resolves the entity name from its own DB by this. |
| `version`                    | string          | The resolved version the row was checked at.                                  |
| `verdict`                    | enum            | One of the seven snake_case tokens above.                                     |
| `method_rank`                | integer 1–5     | The proof-strength rank of the strongest method that ran.                     |
| `invoke_attempted`           | boolean         | Whether the sweep actively invoked/exercised the target this run.            |
| `invoke_skip_reason`         | string-or-null  | When `invoke_attempted` is false, why: `unsafe_to_call` / `uncontainable` / `not_a_callable_kind`; null otherwise. |
| `detail`                     | string          | Free-text cause line; required, the empty string permitted.                  |
| `matched_address_version_id` | integer-or-null | The `address_version` row the swept bytes matched. Non-null integer (≥ 0) on a verified-block row; null otherwise. Enforced by a row-level conditional. |

## The attribution invariant (the matched-id rule)

The schema encodes, on the row object, a draft-07 `if`/`then`/`else`:

- `if verdict in { verified_working, passed_not_verified }` → `then`
  `matched_address_version_id` is a **non-null integer** (`minimum: 0`). A
  verified-block row matched SOME candidate `address_version` row, so it names
  which (the importer extends that row's interval forward when the swept version
  sat in a gap).
- `else` (a `failed` / `not_applicable` / `cannot_check` / `skipped` / `error`
  row) → `matched_address_version_id` is **null**. A non-pass row matched no
  candidate row, so it names none.

A verified-block row always names its row; every other verdict carries null. A
verified-block row with a null/missing matched id, or a non-pass row with a
non-null one, is a malformed report and is rejected.

## The incremental-flush contract (durability)

The sweep does NOT accumulate all rows in memory and emit one JSON document at the
end. It **appends one row-result per line to a line-delimited sink (JSONL)** —
written and flushed the instant each row's attempt completes — and a finalize pass
then wraps the accumulated lines into the finalized JSON document. This is what
makes a crashed-mid-sweep report still ingestible: a sweep that dies at row N
leaves a durable, complete-up-to-N record (the live-exercise tier drives real game
code, so a mid-sweep fault/hang is a real risk).

The schema therefore validates **both shapes**:

- **A per-row line** is exactly one `rows[]` element object. Validate a single
  line against the `rows.items` subschema (one row-result per line).
- **The finalized document** wraps the rows into the top-level shape
  (`schema_version` + `game_version` + `summary` + `rows[]` + the partial signal).

**The partial-report signal** — `complete` (boolean) + `rows_expected` (integer)
— lets the consumer tell a whole-set sweep from one that stopped early:

- `complete: true` with `rows` length == `rows_expected` → swept the whole
  curated set.
- `complete: false` with `rows` length < `rows_expected` → the sweep stopped
  early; the report is partial.

A **partial report is VALID** — it ingests fine; only its `complete` flag and
short `rows` array tell the consumer it stopped early. It is not rejected.

## Recorded interpretations (where the design did not fully determine a field)

- **`name` is NOT in the report.** The per-row fields are `kcdx_id`, resolved
  version, verdict, the active-attempt triple/flags, and detail — `name` is not
  among them. The consumer renders the name by resolving it from its own loaded DB
  by `kcdx_id`; a `kcdx_id` not in the DB is the stale-report case the consumer
  surfaces. (Carrying `name` would also let a stale report's name disagree with
  the live DB.)
- **`detail` is required-with-empty-allowed**, not optional — every row's shape
  stays uniform for the three parsers, while a clean row can carry no extra cause.
- **`kcdx_id` is an integer** (the stable integer entity id plugins reference).
- **`schema_version` is an integer `const 3`** — the simplest pinned form three
  languages compare exactly.
- **`game_version` / `version` are free-form strings** in the project's
  resolved-tag form (e.g. `release_1_5_1164953_841` or `1.5.1164953`).
- **`complete` + `rows_expected` are required on the finalized document.** The
  schema validates the finalized document (and, against `rows.items`, a single
  per-row line); the partial signal exists precisely so a finalized-but-early
  document is distinguishable, so both fields are part of the finalized contract
  every report carries — a partial report sets `complete: false` and carries fewer
  rows than `rows_expected`, it does not omit the fields.
- **`invoke_skip_reason` is string-or-null with a fixed three-value enum.** It is
  null when `invoke_attempted` is true (an exercise ran, no skip reason); the
  schema permits null and the three reason strings without a cross-field
  constraint, leaving the producer to set the coherent pairing.

## Adding a verdict / field later

The contract is FROZEN at `schema_version` 3. A new verdict token, a new field, or
a changed required-set is a `schema_version` bump (a new pinned value), surfaced as
a contract change — not a silent edit, because both repos parse this file.
