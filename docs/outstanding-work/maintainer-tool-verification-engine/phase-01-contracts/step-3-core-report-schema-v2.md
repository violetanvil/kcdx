# 1.3 [CORE] Report schema v2 — add `matched_address_version_id`, bump `schema_version` 1→2

## What

Extend the frozen JSON verification report schema (1.2's v1) to **v2**: add a per-row nullable
**`matched_address_version_id`** field (the `address_version` row whose fingerprint the swept bytes
matched — the attribution D34 needs so the importer can extend the right row's interval), and bump
the top-level `schema_version` from `1` to `2`. The attribution model (D34) is the reason: the
in-game sweep doesn't just say pass/fail per `kcdx_id` — it resolves WHICH `address_version` row
the swept bytes belong to (by matching against each candidate row's fingerprint), and the importer
uses that id to attribute an uncovered version to the right row (and extend its `valid_through`).
The field is **null** on a non-match (`wrong_target`) or an uncheckable row (`cannot_check`/`dead`).

## Scope

One commit in the maintainer-tool tree (`data/maintainer-tool/report-schema/` + its validator test
under `data/refdata-extractor/tests/`): the schema file gains the nullable per-row
`matched_address_version_id` (integer, `minimum: 0`, nullable), `schema_version` becomes `const 2`,
the README's frozen-shape table + verdict notes are updated (a `resolves_works` row carries a
non-null matched id; `wrong_target`/`dead`/`cannot_check` carry null), and the valid + malformed
sample fixtures + the pytest validator are updated to v2 (a v2 report validates with the matched id
present on passing rows; a report with `schema_version: 1`, or a passing row missing the matched id,
is rejected). No producer plugin (Phase 4), no consumer UI (Phase 5) — the schema + its validator
only.

## Test bar

The existing pytest `test_report_schema.py` (extended to v2): a v2 valid report validates (the
matched id present + integer on `resolves_works` rows, null on failing/cannot_check rows;
`schema_version: 2` pinned); a v1-shaped report (`schema_version: 1`) is rejected; a passing row
**missing** `matched_address_version_id`, or carrying a non-integer/negative one, is rejected; the
old snake_case verdict-token freeze assertions stay green. The hand-rolled validator (no new dep,
per 1.2) is extended to check the new field. Runnable at this step (the schema + fixtures + the
validator exist from 1.2) — `.claude/rules/test-discipline.md`, `.claude/rules/incremental-delivery.md`.

## Dependencies

- **1.2** — the frozen v1 schema + its validator + fixtures (this step bumps them to v2).

## Reference

[`../plan-spec.md`](../plan-spec.md) — Group D (the report schema + the v2 attribution field);
cross-step invariant 4 (the JSON report is the cross-repo seam — a versioned, frozen contract).

## Design authority

`data/maintainer-tool/design.md` **D34** — "the report carries the matched row id per result
(`matched_address_version_id`) … the byte-match attributes [an uncovered version] to whichever row
it matched" + **D28** (revised) — the per-row report shape now includes `matched_address_version_id`
(null on a non-match/uncheckable). The schema FILE this step edits is
`data/maintainer-tool/report-schema/verification-report.schema.json` (the 1.2-frozen v1 contract;
its README records the frozen shape + the snake_case verdict tokens). Build to D34's named field +
the schema's existing strict shape (`additionalProperties: false`, the const-pinned version, the
closed enum), not to this doc's summary.

## UX

Not a UI step (a data contract). No user-facing surface; the schema is the producer/consumer wire
format.

## Disassembler-test / author-burden

None — a data contract (a schema field), not an author-facing input surface.
