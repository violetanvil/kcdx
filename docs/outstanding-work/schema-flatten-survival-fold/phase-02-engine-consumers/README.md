# Phase 2 — engine + consumers read the folded columns

**Intent.** Migrate every consumer to read the folded columns off `address_versions` (not the
`survival` sibling): the engine SELECT + `DecodeVersionRow` + the `ResolveResult` fields (the
§11.3 comprehensiveness contract wired), and the read seam (`read_api`) + the maintainer-tool
backend passthrough. End state: every consumer reads the av columns — so deleting the sibling
(Phase 3) breaks nothing.

Shared spec: [`../plan-spec.md`](../plan-spec.md). Design:
[`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md) §11.3 (the
ResolveResult contract). Engine authority: [`src/refdb.h`](../../../../src/refdb.h)
(`ResolveResult`).

## Step ledger

| Step | Status | Commit |
|---|---|---|
| 4 engine SELECT + decode + ResolveResult — src/refdb.cpp DecodeVersionRow reads the folded columns; ResolveResult (refdb.h) gains the fields (append-only); the survival pass reads av columns, not the sibling | DONE | 926e18b |
| 5 read seam + backend — read_api.read_version_rows surfaces the folded columns (per the curated display set); the maintainer-tool backend passes them through | NOT STARTED | — |

## Step docs

4. [step-4-engine-select-decode-resolveresult.md](step-4-engine-select-decode-resolveresult.md)
5. [step-5-read-seam-backend.md](step-5-read-seam-backend.md)

## Verification gate (phase end)

- The engine build is green (`pwsh ./build.ps1` exit 0 + the three artifacts) with the engine
  reading the folded columns off `address_versions`; the survival pass consumes the av columns,
  not the sibling table.
- The `ResolveResult` comprehensiveness contract (§11.3) holds for the folded columns: every
  folded column the engine reads has a `ResolveResult` field; no field lacks a backing column.
- The read seam (`test_read_api`) surfaces the folded columns in the version-row read contract;
  the backend read endpoint passes them through (backend suite green).
- This phase still does NOT delete the survival table — both the sibling and the av columns
  exist; consumers now read the av columns. Deletion is Phase 3.
- Build-green is necessary, not sufficient: the engine reading the folded columns correctly is
  confirmed by the data-core decode assertion + (at the whole-feature checkpoint) a game launch
  if any engine resolve path's behavior changed. (The fold is data-shape-preserving for the
  resolve path — the av columns carry the same facts the survival table did — so no
  matrix-row behavior should change; a launch confirms no regression.)
