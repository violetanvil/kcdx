# 2.2 [BACKEND] The reconciliation classification in the /save/reverify-batch preview (E2 surface)

## What

Surface the report-vs-DB reconciliation (the 1.1 resolver logic) through the `/save/reverify-batch`
preview so the FE knows which report rows are ALREADY-ACTED-ON vs still actionable. The resolver (1.1)
already produces no edit-spec for an already-done row; this step makes the preview response carry the
per-row classification (actionable vs already-acted / no-action) so the s08 worklist can move an
already-acted row to its "no further action" state (3.3) without re-deriving it client-side. The preview
stays read-only / preview-only (D16 — Save-previews / Confirm-transacts); no write, no transaction.

## Scope

One commit in the kcdx tree:
- `data/maintainer-tool/backend/app/routes_save.py` — the `/save/reverify-batch` preview: extend its
  per-row response so a row whose recommended action is already reflected (the resolver returned no
  edit-spec for it) is marked `already_acted` / no-action, alongside the actionable rows' field-deltas.
  The FE reads this classification (it never computes it — D41).
- The response model addition (the per-row classification field).

Does NOT change `/confirm/batch` (transacts unchanged), the resolver's logic (1.1 owns it), or the FE.

## Test bar

- **backend test** (`data/maintainer-tool/backend/tests/test_reverify_batch_endpoint.py`): POST a
  close-intervals batch where one row is ALREADY closed (already-acted) and one is OPEN (actionable);
  the preview response marks the already-closed row `already_acted` / no-action (no field-delta) and the
  open row actionable (with its `valid_through` delta); assert the DB byte-identical (preview writes
  nothing). **FALSIFIABLE:** an already-acted row classified actionable (or carrying a no-op delta) fails;
  a preview that mutated the DB fails the byte-identical row. Emits the canonical `ACCEPT-RESULT` /
  `ACCEPT-SUITE`. Runnable AT this step (1.1 resolver skip + the preview endpoint exist).

## Dependencies

- **1.1** — the resolver's already-done skip (the classification rests on the resolver returning no
  edit-spec for an already-done row).
- The existing `/save/reverify-batch` preview endpoint (6.2b, `201e646`).

## Reference

[`../plan-spec.md`](../plan-spec.md) — E2 + the cross-step invariant "no report-schema change".

## Design authority

`data/maintainer-tool/design.md` **D41** fact (2) (the reconciliation classification surfaced to the FE) +
`data/maintainer-tool/ui/screens/s08-verification-worklist.md` §"report-vs-DB reconciliation" (the
already-acted → no-further-action display the classification drives). Build to D41's settled reconciliation.

## Disassembler-test / author-burden

None — a preview-only HTTP endpoint over the data-core; no author-facing input, no game-function target.
