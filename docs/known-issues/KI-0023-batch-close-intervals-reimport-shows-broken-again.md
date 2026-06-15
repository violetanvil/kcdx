---
id: KI-0023
opened: 2026-06-15
status: open
commit_at_filing: f15ae4f
---

# maintainer-tool FE: a confirmed close-intervals batch shows the row broken again on re-import (reconciliation gap)

**Status:** open

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

## Facts

- The write side is NOT the bug: the close-intervals confirm committed a real, correct change (kcdx_id 12 `valid_through_version` → `1.5.1164953`, the interval closed). Ground-truth-confirmed via `git show f15ae4f`.
- The bug is on the RE-IMPORT / reconciliation side: a re-import of the same report does not move the already-acted row to "No further action."
- The acted row (kcdx_id 12) has `matched_address_version_id: None` in the report. So does every other actionable row in this report.
- The "already-acted" reconciliation (D41 fact 2) is supposed to classify a row whose recommended action already landed as `already_acted` (server-side, via `/save/reverify-batch`'s resolver skip), and the FE reads that classification (it never re-derives). Built in lifecycle-completeness Phase 3 step 3.4 (`FE:b3779bc`) + the resolver in verification-engine Phase 6 step 6.2b (`201e646`).

## Open questions (probe these — do NOT theorize; mechanism is unknown)

- **Which path fails?** Two candidate mechanisms, mutually distinguishing, must be probed (not assumed):
  1. The FE re-derives the row's block purely from the report JSON verdict, ignoring the now-closed DB state — so a re-import always re-shows the report's original verdict.
  2. The server-side `reverify_resolver` already-closed skip (close-intervals: `valid_through == last_verified`) doesn't fire for this row — possibly because `matched_address_version_id: None` means the resolver can't attribute the report row to the now-closed DB row, so it never reaches the already-closed comparison.
- **Is `matched_address_version_id: None` the discriminator?** A `string_anchor` failed row carries no matched av id by the report's attribution invariant (a failed row has no matched block). If close-intervals' resolver keys its already-closed skip on the matched id, a no-matched-id row can never be classified already-acted — which would make this a resolver attribution gap, not an FE bug. Probe the resolver path for kcdx_id 12 against the post-`f15ae4f` DB.
- **Cross-check the verdict semantics:** kcdx_id 12 is `failed`, not a closeable open-interval row in the usual sense — confirm close-intervals is even the right action for a `failed` `string_anchor` row, or whether the UI offered an action that the resolver then can't reconcile (a UI/resolver-action mismatch).

## Reproduction

1. Frontend `http://127.0.0.1:5173` → s08 verification worklist.
2. Import `kcdx-verify_2026-06-09_22-33-38.json` (157 rows).
3. Run the close-intervals batch on the failing row (kcdx_id 12); confirm → "applied".
4. Re-import the SAME report.
5. EXPECTED: the acted row shows "No further action". ACTUAL: it shows broken/actionable again.
