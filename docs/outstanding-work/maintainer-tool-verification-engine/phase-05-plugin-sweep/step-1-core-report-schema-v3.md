# 5.1 [CORE] Report schema v3 — the 7-state enum + method_rank/invoke fields + redefined summary

## What

Bump the verification-report JSON schema from v2 to **v3** to carry D36's active-attempt model: the
per-row verdict enum widens from the 4-token static set to the **7-state** set; three new per-row
fields land (`method_rank` 1–5, `invoke_attempted` bool, `invoke_skip_reason` null |
`unsafe_to_call` | `uncontainable` | `not_a_callable_kind`); and the `summary` roll-up + the
`matched_address_version_id` verdict-conditional are redefined to the D36 worklist mapping. This is
the cross-repo contract the producer (5.3) emits and the FE consumer (Phase 6) reads — it lands
FIRST in the phase because the producer cannot emit to a schema that does not exist.

## Scope

One commit in `data/refdata-extractor/` (the schema + its data-core test):
`data/maintainer-tool/report-schema/verification-report.schema.json` —
- `schema_version` const `2` → `3`.
- `verdict` enum: `["resolves_works","wrong_target","dead","cannot_check"]` →
  `["verified_working","passed_not_verified","failed","not_applicable","cannot_check","skipped","error"]`.
- new required per-row fields: `method_rank` (integer 1–5), `invoke_attempted` (boolean),
  `invoke_skip_reason` (null or one of the 3 reason strings).
- `summary.passing` redefined: count of rows whose verdict is `verified_working` OR
  `passed_not_verified` (the D36 worklist "verified block"), not just `resolves_works`.
- the row `if/then/else`: `matched_address_version_id` is a non-null integer on the pass verdicts
  (`verified_working` / `passed_not_verified`), null otherwise (a `failed`/`cannot_check`/etc. row
  matched no candidate row).
- **the incremental-flush contract (D37) — the schema defines BOTH shapes:** (a) the **per-row JSONL
  line** = exactly one `rows[]` element object (the same per-row shape, one per line — the durable
  append the sweep writes as each row resolves); (b) the **finalized v3 document** (`schema_version`
  + `game_version` + `summary` + `rows[]`) the finalize pass produces at sweep end; plus (c) a
  **partial-report signal** — a top-level `complete` (boolean) + `rows_expected` (integer) so the FE
  consumer distinguishes a report that swept the whole curated set from one that stopped early (a
  mid-sweep death leaves `complete: false` / fewer rows than `rows_expected`). The FE ingest (Phase
  6) reads the finalized document; the JSONL line shape + the partial signal are what make a
  crashed-mid-sweep report still ingestible.
- the schema `description` strings updated to the D36/D37 meanings (drop the "v1/v2" + "4 tokens"
  prose; state the incremental-flush durability contract).

## Test bar

The data-core schema test (`data/refdata-extractor/tests/`, pytest — the existing schema-fixture
round-trip pattern): assert a v3 sample report VALIDATES (a `verified_working` row carries
`method_rank` 1 + `invoke_attempted` + a non-null `matched_address_version_id`; a `failed` row
carries null `matched_address_version_id`; a `passed_not_verified` foreign-function row carries
`invoke_attempted: false` + `invoke_skip_reason: "unsafe_to_call"`), AND a report with the OLD
4-token verdict or missing the new fields is REJECTED. **The incremental-flush contract (D37):**
assert a single per-row JSONL line VALIDATES as one `rows[]` element (the durable per-row shape);
assert a finalized document carries `complete` + `rows_expected`; assert a PARTIAL report
(`complete: false`, `rows[]` length < `rows_expected`) still VALIDATES (a crashed-mid-sweep report
is ingestible, not malformed). FALSIFIABLE: a v3 report missing `method_rank` validating, an
old-enum verdict validating, or a partial report (fewer rows than `rows_expected`, `complete: false`)
being REJECTED, fails the test. Runnable AT this step (pure schema + fixture, no engine, no game).
Per `.claude/rules/test-discipline.md`.

## Scope note — schema location + the public/private boundary

The schema lives at `data/maintainer-tool/report-schema/` (a public-dir path). The schema is a
data contract (no AI-dev vocabulary, no private path) — keep its `description` strings
self-contained and free of any private citation (`.claude/rules/public-private-boundary.md`).

## Dependencies

- **Phase 1 step 1.3** — the v2 schema this supersedes (the existing frozen contract +
  `matched_address_version_id`).
- (Independent of Phase 4 — the schema is the wire contract; the engine 4.x and the producer 5.3
  both build TO it.)

## Reference

[`../plan-spec.md`](../plan-spec.md) — the v2→v3 schema bump + the 7-state enum.

## Design authority

`data/maintainer-tool/design.md` **D36** (the 7-state enum + the `method_rank` /
`invoke_attempted` / `invoke_skip_reason` fields + "the report schema bumps v2 → v3") + **D37** (the
incremental-flush contract — the per-row JSONL line shape + the finalized v3 document + the
`complete` / `rows_expected` partial-report signal) + the existing schema
`data/maintainer-tool/report-schema/verification-report.schema.json` (the v2 shape this edits). Build
the v3 contract to D36's field set + enum AND D37's dual-shape + partial-signal, not this doc's
summary.

## UX

Not a maintainer-tool UI step (a data contract). No user gesture (a data-core pytest gate).

## Disassembler-test / author-burden

None — a JSON schema; no author-facing input.
