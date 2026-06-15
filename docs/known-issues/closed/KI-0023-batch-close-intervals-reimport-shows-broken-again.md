---
id: KI-0023
opened: 2026-06-15
status: Closed
closed: 2026-06-15
closed_by_commit: 46d3c09
commit_at_filing: f15ae4f
---

# maintainer-tool FE: a confirmed close-intervals batch shows the row broken again on re-import (reconciliation gap)

**Status:** Closed 2026-06-15 — fixed by on-import reconciliation (FE `46d3c09`, capture D46 `592ea9d`); user-confirmed repro.

A close-intervals batch action reports "applied" and the WRITE genuinely lands, but
re-importing the SAME v3 report shows the acted row in an actionable/broken block again
instead of "No further action." The interval IS closed in the DB; the re-import does not
reflect it. Surfaced during the verification-engine Phase 6 milestone UI acceptance
(2026-06-15) — blocks that acceptance.

## Trail

| Date | Action | Result |
|------|--------|--------|
| 2026-06-15 | User ran a close-intervals batch on the 157-row report `kcdx-verify_2026-06-09_22-33-38.json`; UI said applied | Batch committed `f15ae4f` ("batch save 1 version-row UPDATE") |
| 2026-06-15 | User re-imported the SAME report | The acted row shows broken/actionable again, not "No further action" — the symptom |
| 2026-06-15 | Inspected `git show f15ae4f` — what the write actually changed | kcdx_id 12 (`string_anchor`, `string_exec_autoexec_cfg`): `valid_through_version` empty → `1.5.1164953`. The WRITE is correct — the interval is closed in the DB. |
| 2026-06-15 | Read the imported report's actionable rows | The `failed`/`cannot_check` rows ALL carry `matched_address_version_id: None` (incl. kcdx_id 12, verdict `failed`, method_rank 3). |
| 2026-06-15 | Static-traced the FE classification path (`worklistModel.ts` + `VerificationWorklist.tsx`) — ground truth, no probe needed | ROOT CAUSE FOUND. The already-acted reconciliation is FE-side and the `AlreadyActedMap` is populated ONLY by `onAlreadyActed` (`VerificationWorklist.tsx:355`), which fires from a live `/save/reverify-batch` round-trip during a batch action IN THE CURRENT SESSION. `openReport` RESETS `alreadyActed` to an empty Map on every import (`VerificationWorklist.tsx:221`). `reconciledBlocks` (`worklistModel.ts:144-145`) pulls a row to "no further action" ONLY if `alreadyActed.has(kcdx_id)` — empty after re-import → kcdx_id 12 (`failed`) classifies into `failing` from the report JSON verdict alone. |
| 2026-06-15 | Read the D41-fact-2 design intent (`step-4-s08-reconciliation-display.md:5-8,16-19` + phase-03 README:16) | The reconciliation was SPECIFIED to read `already_acted` "from the `/save/reverify-batch` preview response" — "the EXISTING reconciliation point, not new plumbing." There is NO on-import classification pass by design; the preview only runs when a batch action opens. A plain re-import (act → re-import → look, no new batch) never invokes the preview, so `alreadyActed` is never populated. The spec is internally consistent; KI-0023 is a design GAP it did not cover (reconcile-on-import). |
| 2026-06-15 | Checked the resolver attribution path (`data_core.py:65-72`) | Candidate mechanism #2 FALSIFIED: close-intervals resolves by "the interval-containing row of kcdx_id+version," NOT by `matched_address_version_id` — so the no-matched-id is irrelevant to the resolver. And the resolver is never even CALLED on a plain re-import, so it cannot be the failure point. |

## Facts

- The write side is NOT the bug: the close-intervals confirm committed a real, correct change (kcdx_id 12 `valid_through_version` → `1.5.1164953`, the interval closed). Ground-truth-confirmed via `git show f15ae4f`.
- The bug is on the RE-IMPORT / reconciliation side: a re-import of the same report does not move the already-acted row to "No further action."
- The acted row (kcdx_id 12) has `matched_address_version_id: None` in the report. So does every other actionable row in this report.
- The "already-acted" reconciliation (D41 fact 2) is supposed to classify a row whose recommended action already landed as `already_acted` (server-side, via `/save/reverify-batch`'s resolver skip), and the FE reads that classification (it never re-derives). Built in lifecycle-completeness Phase 3 step 3.4 (`FE:b3779bc`) + the resolver in verification-engine Phase 6 step 6.2b (`201e646`).

## Root cause (mechanism — confirmed by static trace, not theorized)

The already-acted reconciliation never runs on a plain re-import, so the FE classifies every row from the report JSON verdict alone. Mechanism, end to end:

1. `openReport` resets `alreadyActed` to an empty Map on every import (`VerificationWorklist.tsx:221`).
2. The new report's rows are classified by `buildWorklistRows` → `blockForVerdict` purely from the report's own verdict — kcdx_id 12 is `failed` → the `failing` (actionable) block (`worklistModel.ts:53-58, 82`).
3. `reconciledBlocks` pulls a row OUT to "no further action" only when `alreadyActed.has(kcdx_id)` (`worklistModel.ts:144-145`) — but `alreadyActed` is empty post-import.
4. → kcdx_id 12 renders in `failing` = "broken/actionable again." The symptom.

The `AlreadyActedMap` is populated ONLY by `onAlreadyActed` (`VerificationWorklist.tsx:355-360`), which fires from a live `/save/reverify-batch` round-trip **during a batch action in the current session**. By design (D41 fact 2, `step-4-s08-reconciliation-display.md:5-8,16-19`) reconciliation reads `already_acted` "from the `/save/reverify-batch` preview response" — "the EXISTING reconciliation point, not new plumbing." There is **no on-import classification pass**. A re-import-and-look (no new batch action) never invokes the preview → `alreadyActed` stays empty → the acted row re-shows.

This is a **design gap, not a code defect**: the spec wired reconciliation to the batch-action preview round-trip; it never covered reconcile-on-import, which is the KI's reproduction.

Candidate mechanism #2 (resolver attribution gap on `matched_address_version_id: None`) is **falsified**: close-intervals resolves by "the interval-containing row of kcdx_id+version," not by matched id (`data_core.py:65-72`), and the resolver is never called on a plain re-import anyway.

## Open question — the FIX is a design fork (the user's call, routed through Gate A architect-review)

The mechanism is known; the FIX requires a design decision (how on-import reconciliation should fire — and D41 explicitly chose "not new plumbing," which one option would contradict). The fork + recommendation are surfaced to the user after the Gate A architect-review verdict (`/debug` §B.6). Remaining design question feeding that fork:

- **Verdict semantics:** kcdx_id 12 is `failed` (the verification genuinely failed), not a passing row — closing its interval does not make the verification pass. Is "no further action" even the right post-close state for a `failed` row, or does the report correctly show it as still-failing? This bears on whether the fix is "reconcile on import" vs "the current behavior is correct and the UX expectation is the gap." Resolved as part of the surfaced fork.

## Resolution

**Root cause (mechanism):** the s08 already-acted reconciliation never ran on a plain re-import, so the worklist classified every row from the report JSON verdict alone. The `alreadyActed` map — which `reconciledBlocks` (`worklistModel.ts:144-145`) uses to pull a row out to "no further action" — was reset to an empty Map on every import (`VerificationWorklist.tsx:227`, the reset block at 220-228) and was populated ONLY by `onAlreadyActed` (`VerificationWorklist.tsx:411-422`), which fired from a live `/save/reverify-batch` round-trip during a batch action in the current session. A re-import-and-look (no new batch action) never invoked that preview → `alreadyActed` stayed empty → `reconciledBlocks` had nothing to move out → an already-closed `failed` row (kcdx_id 12) re-classified into the actionable `failing` block via `blockForVerdict` (`worklistModel.ts:53-58, 82`). This is a DESIGN GAP, not a code defect: D41 fact 2 deliberately scoped reconciliation's only invocation to the batch-action preview round-trip ("the EXISTING reconciliation point, not new plumbing"); it never covered reconcile-on-import, which is exactly the KI's reproduction. (Candidate mechanism #2 — a resolver attribution gap on `matched_address_version_id: None` — was falsified: close-intervals resolves by the interval-containing row of kcdx_id+version, not by matched id, and the resolver is never even called on a plain re-import.)

**Gate A architect-review (2026-06-15, cold, theory withheld):** HALT-escalate → forward-and-wait. Independently confirmed the mechanism + that the maintainer's expectation is correct (the s08 screen spec §"Already acted on" PROMISES "No further action" for an already-closed `failed` row; current behavior contradicts the spec). Q3 settled: "No further action" ≠ "passed". Three options weighed against the cornerstones; Option A recommended (wins on UX).

**Fix (commit `46d3c09` in the frontend repo `data/maintainer-tool/frontend/`; decision capture D46 in `592ea9d`):** the settled Option A — on-import reconciliation. In `VerificationWorklist.tsx`, `handleFile` now calls a new `reconcileOnImport(rows)` after building the rows and BEFORE `setState({kind:"populated"})`: it splits the actionable blocks (failing → `close-intervals`, verified → `verify-all`), fires the EXISTING `apiClient.previewReverifyBatch` per block (read-only), extracts the `status === "already_acted"` rows via the existing `alreadyActedRows` helper, and populates `setAlreadyActed` before first paint. The FE still READS the classification, never re-derives it (D41 fact 2 holds — the resolver/preview owns the already-done predicate); only the invocation point is new (also on import, not just on batch-open). No new endpoint, no new resolver logic. Degrades gracefully (AP14): a per-block preview failure is logged and that block is left un-reconciled (rows stay actionable, the pre-fix behavior), never blocking the import. The decision is captured as TRD D46 + the s08 screen spec §"Report-vs-DB reconciliation".

This addresses the MECHANISM (not the symptom): the already-acted rows were invisible to `reconciledBlocks` because their classification was never computed on the re-import path; the fix computes it at import time, so the rows the maintainer already acted on are pulled out of the actionable blocks on first paint — exactly the path the spec promised but the as-built D41 invocation could not reach.

**Verification:**
- **Cause-test (regression):** 3 falsifiable vitest cases in `VerificationWorklist.test.tsx` — (1) an already-closed failing row imports straight into "no further action" (NOT the actionable failing block); (2) a row the preview does NOT mark already_acted stays actionable on import (over-reconciliation guard); (3) a rejecting import-time preview still imports with rows actionable (graceful-degrade/AP14). Falsifiable: a re-imported already-closed row rendered actionable fails (1). Manager-re-run gate: `npm run build` exit 0 + `vitest run` 588/588.
- **User-confirmed repro (2026-06-15):** re-imported the same 157-row report with NO batch action taken this session (the interval was already closed in the DB from a prior session) — the worklist showed 15 rows under "No further action," confirming on-import reconciliation classifies already-acted rows without requiring a batch action. The exact KI reproduction, fix confirmed.

## Reproduction

1. Frontend `http://127.0.0.1:5173` → s08 verification worklist.
2. Import `kcdx-verify_2026-06-09_22-33-38.json` (157 rows).
3. Run the close-intervals batch on the failing row (kcdx_id 12); confirm → "applied".
4. Re-import the SAME report.
5. EXPECTED: the acted row shows "No further action". ACTUAL: it shows broken/actionable again.
