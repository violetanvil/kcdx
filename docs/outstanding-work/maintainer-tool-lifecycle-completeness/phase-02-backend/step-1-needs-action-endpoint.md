# 2.1 [BACKEND] The needs-action read endpoint (E9)

## What

A read-only backend GET endpoint exposing the Phase-1 lifecycle-completeness detection query (1.2) —
the needs-action set the s09 Needs-action view binds to. It returns the three incomplete-lifecycle
kinds (Uncovered at current version / Never verified / Broken references) at the current game version,
each entity with its specific gap + the resolution path the view offers. Mirrors the existing
`routes_read.py` GET pattern (`/entities`, `/entities/{id}` — read-only, named-cause error copy, no
write). The total count feeds the s01 navigator's `[Needs action ▸ N]` badge.

## Scope

One commit in the kcdx tree:
- `data/maintainer-tool/backend/app/routes_read.py` (or a sibling read-route module) — a GET endpoint
  (e.g. `/needs-action`) calling the 1.2 detection module, returning the grouped-by-kind set + the
  total count. Read-only (no write, no transaction). Named-cause error on a detection failure (the
  s09 error state's copy source).
- The response Pydantic model (the shape the s09 view + the s01 badge consume).

Does NOT change the write path or any FE.

## Test bar

- **backend test** (`data/maintainer-tool/backend/tests/test_read_endpoints.py`): GET the needs-action
  endpoint returns the three kinds over a fixture DB (an orphan in Uncovered, a never-verified row in
  Never-verified, a dangling ref in Broken-references), with the total count; assert the DB is
  byte-identical after the read (a read endpoint touches nothing). **FALSIFIABLE:** a healthy fixture
  returns all-empty sets (count 0); a read that mutated the DB fails the byte-identical row. Emits the
  canonical `ACCEPT-RESULT` / `ACCEPT-SUITE`. Runnable AT this step (1.2 detection + the FastAPI read
  layer exist).

## Dependencies

- **1.2** — the detection query this endpoint serves (built before so the endpoint calls a real module).
- The existing `routes_read.py` read-only pattern + the FastAPI TestClient harness.

## Reference

[`../plan-spec.md`](../plan-spec.md) — E9 + the cross-step invariant "the detection is read-only".

## Design authority

`data/maintainer-tool/design.md` **D41** fact (1) (the standing needs-action view needs a read surface) +
`data/maintainer-tool/ui/screens/s09-needs-action.md` §Contents (the `[Needs action ▸ N]` binding + the
Loading/Error states the endpoint feeds). Build to the response shape the s09 view binds to, per the spec.

## Disassembler-test / author-burden

None — a read-only HTTP endpoint over the data-core; no author-facing input, no game-function target.
