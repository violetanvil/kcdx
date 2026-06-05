# 1.2 The JSON verification report schema (the cross-repo contract)

## What

Define + freeze the **JSON verification report schema** — the single contract that crosses the
two-repo split (D23): produced by the in-game TEST plugin (Phase 4, kcdx tree) and consumed by
the FE s08 worklist (Phase 5, frontend repo). A versioned, frozen schema: per row `kcdx_id`,
resolved version, verdict (`resolves+works` / `dead` / `wrong-target` / `cannot-check`),
detail; plus a top-level version stamp + summary (D28). Ordered before BOTH the producer
(Phase 4) and the consumer (Phase 5) so each builds to the settled schema, not to each other.

## Scope

One commit landing the schema as a frozen, versioned artifact reachable by BOTH repos (the
schema definition + a sample valid report + a sample malformed report), with a validation
helper/test. The schema carries an explicit `schema_version`. Because the producer is kcdx and
the consumer is the gitignored frontend repo, the canonical schema + its fixtures live in the
kcdx tree (the producer's side); the frontend imports/vendors a copy or validates against it.
No producer plugin (Phase 4), no s08 UI (Phase 5) — the schema + its validator only.

## Test bar

A test (the layer of the repo the schema lands in — pytest under `seeds_shared/` if it lands
CORE-side, or a Vitest if it lands FE-side; it lands with the canonical kcdx-side copy →
pytest): `test_report_schema.py` — asserts the sample valid report validates against the
schema, a malformed report (missing a required field / an out-of-enum verdict) is rejected, and
`schema_version` is present + pinned. Runnable at this step (no producer/consumer needed) —
`.claude/rules/test-discipline.md`, `.claude/rules/incremental-delivery.md`.

## Dependencies

None within this plan (a fresh contract). It is itself a dependency of Phase 4 step 1
(producer) and Phase 5 step 1 (consumer).

## Reference

[`../plan-spec.md`](../plan-spec.md) — Group D (the report schema); cross-step invariant 4 (the
cross-repo JSON-report-schema contract is the one seam crossing the two-repo split).

## Design authority

`data/maintainer-tool/design.md` **D28** — the report's exact shape (per row: `kcdx_id`,
resolved version, verdict `resolves+works` / `dead` / `wrong-target` / `cannot-check`, detail;
written alongside `kcdx-dev.log`). The schema is built to D28's named fields + verdict enum,
never invented; the client-side File-API ingest path is **D31b**.

## Disassembler-test / author-burden

None — a data contract (a schema), not an author-facing input surface.
