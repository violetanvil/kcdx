# Phase 5 — Frontend report ingestion

**Intent:** build the s08 verification-worklist screen — import the in-game plugin's **v2** JSON
report (File API, client-side, D31b), show the ingest progress + the **two-block worklist**
(verified / failing — D35), and route the **two batch actions** (verify-all with the matched-row
`valid_through` extend on a gap-pass — D34; close-intervals retracting `valid_through` to
`last_verified_at_version` — D35) through the existing save spine as batched, all-or-nothing
transactions (D32). This is the consumer side of the cross-repo report contract (the Phase-4 plugin
is the producer; the Phase-1 v2 schema the seam). In the SEPARATE gitignored frontend repo (D23) —
gated by `npm run build` + Vitest. Ordered last: it consumes the report the Phase-4 plugin produces.

## Step-grain ledger

| Step | Status | Commit |
|---|---|---|
| [5.1 [FE] s08 worklist: import + ingest progress + the two-block worklist (verified/failing) + s08 states](step-1-fe-s08-worklist.md) | NOT STARTED | — |
| [5.2 [FE] The two batch actions — verify-all (+ `valid_through` extend) + close-intervals → s06 batch confirm](step-2-fe-bulk-reverify-batch-confirm.md) | NOT STARTED | — |

## Phase verification gate

A UI-touching phase — the gate INCLUDES user-facing acceptance (`.claude/rules/ux-first-class.md`),
not only build/test green. Phase 5 is done when: s08 (steps 1–2) builds to the revised s08 screen
spec and passes `npm run build` + Vitest (incl. ingesting a real Phase-4 **v2** report, the
two-block verified/failing split, the **verify-all** batch — incl. a gap-pass `valid_through`
extension on the matched row — and the **close-intervals** batch through the save spine). The
**milestone user-acceptance checkpoint** fires for the substantive + under-specified UI (s08 is a
NEW screen): the maintainer experiences importing a `report.json`, the ingest progress bar, the
two-block worklist, selecting rows in each block, the batched field-delta confirms (the trio +
`valid_through` extend for verify-all; the `valid_through` retract for close-intervals), the
all-or-nothing commits — plus a `[Fix ▸]` jump to a failing row. Advisory throughout (every verdict
through the confirm spine; nothing lands silently — law 4 / D28).
