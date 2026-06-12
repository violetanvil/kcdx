# Phase 6 — Frontend report ingestion (the s08 verification worklist)

**Intent:** build the s08 verification-worklist screen — import the in-game plugin's **v3** JSON
report (File API, client-side, D31b), show the ingest progress + the **three-block worklist**
(verified / failing / no-action — D36's verdict→block mapping), surface each row's 7-state verdict
+ proof-rank + the partial-report signal, and route the **two batch actions** (verify-all with the
proof-rank-keyed `evidence_kind` + the matched-row `valid_through` extend on a gap-pass — D29/D34;
close-intervals retracting `valid_through` to `last_verified_at_version` — D35) through the existing
save spine as batched, all-or-nothing transactions (D32). **Per D39 the re-verify edit-specs are
RESOLVED + computed by the data-core** (a new `reverify_resolver` + a `/save/reverify-batch` preview
endpoint — step 6.2b); the FE sends the report rows + displays the returned field-deltas + relays the
confirm to `/confirm/batch`, it never computes the edits. This is the consumer side of the cross-repo
report contract (Phase 5's `kcdx_verify_all` sweep is the producer; the v3 schema the seam). The s08 FE
is in the SEPARATE gitignored frontend repo (D23) — gated by `npm run build` + Vitest; the data-core
resolver + the preview endpoint (6.2b) are kcdx-tree, gated by the data-core pytest + backend test.
Ordered last: it consumes the v3 report Phase 5 produces.

**Design authority:** the **reconciled s08 screen spec**
`data/maintainer-tool/ui/screens/s08-verification-worklist.md` (v3/D36) + the Layer-1
`data/maintainer-tool/ui/design.md` (`live verdict badge` + `proof-rank chip` silhouettes) + the
**v3 report schema** `data/maintainer-tool/report-schema/verification-report.schema.json` + TRD
`data/maintainer-tool/design.md` D28 / D29 (rev) / D31b / D32 / D34 / D35 / D36 / **D39** (the data-core
resolve seam — the re-verify edit-specs are computed by `reverify_resolver` via `/save/reverify-batch`,
not the FE). Build to those, not this README's summary.

## Step-grain ledger

| Step | Status | Commit |
|---|---|---|
| [6.1 [FE] s08 worklist: import (v3) + ingest progress + the THREE-block worklist + the s08 states](step-1-fe-s08-worklist.md) | DONE | FE:6e7f3b1 |
| [6.2 [BE/CORE] The D32 batch-confirm transaction — `/confirm/batch` endpoint + `confirmBatch` client](step-2-be-batch-confirm-endpoint.md) | DONE | 42ebd79 (BE/data-core) + FE:17bfa12 (client) — update_version_rows_batch + POST /confirm/batch + confirmBatch client. The under-scoping (no `valid_through` in EDITABLE_VERSION_COLUMNS, so the write path rejected the D34/D35 edit) was corrected by step 6.2a-fix (69f54d2); returned to DONE on that landing (loop §C.3 trigger-2). |
| [6.2a-fix [BE/CORE] `valid_through` becomes an authored + auto-filled column — the write affordance + the seam move (D40)](step-2a-fix-valid-through-write-path.md) | DONE | 69f54d2 — valid_through moved to the authored seed (`valid_through_version` FK); the interval-edit write branch (`_apply_one_db` PRESENT path) emits it while US-5 still preserves a closed interval (`_UPDATE_PRESERVE_COLUMNS` unchanged); the authored-closed-only interval validator; TD-0011 (bulk-overlay re-export deferral); curated-seed re-baseline. Full data-core suite 181 passed; step-review commit-step. |
| [6.2b [BE/CORE] The re-verify resolver + the `/save/reverify-batch` preview seam (D39)](step-2b-be-reverify-resolver.md) | DONE | 201e646 — `reverify_resolver.py` (read-only; verify-all trio + proof-rank `evidence_kind` D29-rev + D34 gap-extension; close-intervals deterministic interval-containing target + D35 retract; verdict routing) + `/save/reverify-batch` preview-only endpoint (D16, writes nothing) producing the `{kcdx_id, valid_from_version, edits}` shape `/confirm/batch` transacts. Full data-core suite 182 passed; backend reverify test 5 passed; step-review commit-step (mutation-verified falsifiable tests). |
| [6.3 [FE] The two batch actions — send rows → `/save/reverify-batch` → s06 batch confirm → `/confirm/batch`](step-3-fe-bulk-reverify-batch-confirm.md) | NOT STARTED | — |

## Phase verification gate

A UI-touching phase — the gate INCLUDES user-facing acceptance (`.claude/rules/ux-first-class.md`),
not only build/test green. Phase 6 is done when: the s08 FE (steps 6.1, 6.3) builds to the
reconciled s08 screen spec and passes `npm run build` + Vitest, AND the D32 batch endpoint (step
6.2) + **the D39 re-verify resolver + `/save/reverify-batch` preview seam (step 6.2b)** pass the
data-core pytest + the backend test (incl. ingesting a real Phase-5 **v3** report; the three-block
verified/failing/no-action split keyed on the 7-state verdicts; the per-row `live verdict badge` +
`proof-rank chip`; the partial-report banner driven by `complete`/`rows_expected`; the **verify-all**
batch — incl. the data-core-computed proof-rank-keyed `evidence_kind` and a gap-pass `valid_through`
extension on the matched row — and the **close-intervals** batch — the data-core-resolved
deterministic close-target + the `valid_through` retract — the FE displaying the preview's returned
deltas and relaying the confirm to the `/confirm/batch` all-or-nothing transaction). The
**milestone user-acceptance checkpoint** fires for the substantive + under-specified UI (s08 is a
NEW screen): the maintainer experiences importing a v3 report, the ingest progress bar, the
three-block worklist (verdict badge + proof-rank per row), the partial-report banner, selecting rows
in each action block, the batched field-delta confirms (the trio + the proof-rank-keyed
`evidence_kind` + `valid_through` extend for verify-all; the `valid_through` retract for
close-intervals), the all-or-nothing commits — plus a `[Fix ▸]` jump to a failing row. Advisory
throughout (every verdict through the confirm spine; nothing lands silently — law 4 / D28).
