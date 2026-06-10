# Phase 6 — Frontend report ingestion (the s08 verification worklist)

**Intent:** build the s08 verification-worklist screen — import the in-game plugin's **v3** JSON
report (File API, client-side, D31b), show the ingest progress + the **three-block worklist**
(verified / failing / no-action — D36's verdict→block mapping), surface each row's 7-state verdict
+ proof-rank + the partial-report signal, and route the **two batch actions** (verify-all with the
proof-rank-keyed `evidence_kind` + the matched-row `valid_through` extend on a gap-pass — D29/D34;
close-intervals retracting `valid_through` to `last_verified_at_version` — D35) through the existing
save spine as batched, all-or-nothing transactions (D32). This is the consumer side of the
cross-repo report contract (Phase 5's `kcdx_verify_all` sweep is the producer; the v3 schema the
seam). In the SEPARATE gitignored frontend repo (D23) — gated by `npm run build` + Vitest. Ordered
last: it consumes the v3 report Phase 5 produces.

**Design authority:** the **reconciled s08 screen spec**
`data/maintainer-tool/ui/screens/s08-verification-worklist.md` (v3/D36) + the Layer-1
`data/maintainer-tool/ui/design.md` (`live verdict badge` + `proof-rank chip` silhouettes) + the
**v3 report schema** `data/maintainer-tool/report-schema/verification-report.schema.json` + TRD
`data/maintainer-tool/design.md` D28 / D29 (rev) / D31b / D32 / D34 / D35 / D36. Build to those, not
this README's summary.

## Step-grain ledger

| Step | Status | Commit |
|---|---|---|
| [6.1 [FE] s08 worklist: import (v3) + ingest progress + the THREE-block worklist + the s08 states](step-1-fe-s08-worklist.md) | NOT STARTED | — |
| [6.2 [FE] The two batch actions — verify-all (proof-rank `evidence_kind` + `valid_through` extend) + close-intervals → s06 batch confirm](step-2-fe-bulk-reverify-batch-confirm.md) | NOT STARTED | — |

## Phase verification gate

A UI-touching phase — the gate INCLUDES user-facing acceptance (`.claude/rules/ux-first-class.md`),
not only build/test green. Phase 6 is done when: s08 (steps 1–2) builds to the reconciled s08 screen
spec and passes `npm run build` + Vitest (incl. ingesting a real Phase-5 **v3** report; the
three-block verified/failing/no-action split keyed on the 7-state verdicts; the per-row `live verdict
badge` + `proof-rank chip`; the partial-report banner driven by `complete`/`rows_expected`; the
**verify-all** batch — incl. the proof-rank-keyed `evidence_kind` and a gap-pass `valid_through`
extension on the matched row — and the **close-intervals** batch through the save spine). The
**milestone user-acceptance checkpoint** fires for the substantive + under-specified UI (s08 is a
NEW screen): the maintainer experiences importing a v3 report, the ingest progress bar, the
three-block worklist (verdict badge + proof-rank per row), the partial-report banner, selecting rows
in each action block, the batched field-delta confirms (the trio + the proof-rank-keyed
`evidence_kind` + `valid_through` extend for verify-all; the `valid_through` retract for
close-intervals), the all-or-nothing commits — plus a `[Fix ▸]` jump to a failing row. Advisory
throughout (every verdict through the confirm spine; nothing lands silently — law 4 / D28).
