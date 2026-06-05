# Phase 5 — Frontend report ingestion

**Intent:** build the s08 verification-worklist screen — import the in-game plugin's JSON report
(File API, client-side, D31b), show the ingest progress + the pass/fail split + the worklist, and
route bulk re-verify through the existing save spine as ONE batched, all-or-nothing transaction
(D32). This is the consumer side of the cross-repo report contract (the Phase-4 plugin is the
producer; the Phase-1 schema the seam). In the SEPARATE gitignored frontend repo (D23) — gated by
`npm run build` + Vitest. Ordered last: it consumes the report the Phase-4 plugin produces.

## Step-grain ledger

| Step | Status | Commit |
|---|---|---|
| [5.1 [FE] s08 worklist: import + ingest progress + pass/fail split + the 9 s08 states](step-1-fe-s08-worklist.md) | NOT STARTED | — |
| [5.2 [FE] Bulk re-verify → the s06 batch field-delta confirm (D32) + confirm-spine routing](step-2-fe-bulk-reverify-batch-confirm.md) | NOT STARTED | — |

## Phase verification gate

A UI-touching phase — the gate INCLUDES user-facing acceptance (`.claude/rules/ux-first-class.md`),
not only build/test green. Phase 5 is done when: s08 (steps 1–2) builds to the s08 screen spec
and passes `npm run build` + Vitest (incl. ingesting a real Phase-4 report, the pass/fail split,
and a batched bulk re-verify through the save spine). The **milestone user-acceptance checkpoint**
fires for the substantive + under-specified UI (s08 is a NEW screen): the maintainer experiences
importing a `report.json`, the ingest progress bar, the pass/fail worklist, selecting passing
rows, the ONE batched field-delta confirm, and the all-or-nothing commit — plus a `[Fix ▸]` jump
to a failing row. Advisory throughout (every verdict through the confirm spine; nothing lands
silently — law 4 / D28).
