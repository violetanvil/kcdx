# 5.1 [FE] s08 worklist: import (File API) + ingest progress bar + pass/fail split + the 9 s08 states

## What

Build the s08 verification-worklist screen (a NEW screen) up to the worklist surface: the
`[Import verification report]` entry → pick `report.json` in-page via the File API (client-side,
no backend read seam — D31b) → the ingest progress bar (determinate, N/M rows — D31c) → the
populated worklist (summary header `M/N passing` + the pass/fail split control + the worklist
table, one row per reported row with its verdict badge + detail) + the `[Fix ▸]` jump on a
failing row. The 9 s08 states. This is the consumer of the Phase-4 report (validated against the
Phase-1 schema). The bulk-re-verify batch confirm is step 2.

## Scope

One commit in the frontend repo: the s08 screen — import entry, File-API report read + parse
(validating against the Phase-1 schema), the ingest progress bar, the summary header, the
pass/fail split control, the worklist table with per-row verdict badge + detail, the per-row
select (passing rows) + `[Select all passing]`, and the `[Fix ▸]` → s02/s04 navigation — plus the
9 states. The s06 batch confirm + the bulk-re-verify spine wiring is step 2 (the `[Re-verify N
rows]` button renders here, its handler lands in step 2). Built to the s08 screen spec.

## Test bar

Vitest unit/component tests in the frontend repo: importing a sample Phase-4 report parses +
validates (and a malformed one shows the error state); the ingest progress bar shows N/M; the
pass/fail split filters; each verdict badge renders (Unchanged/Changed/CannotCheck) glyph+text;
a row whose `kcdx_id` is unknown to the current DB renders CannotCheck + is excluded from select;
`[Fix ▸]` navigates to s02/s04 (user-action, law 3). Runnable at this step (the schema + a sample
report exist from Phase 1 / Phase 4) — `.claude/rules/test-discipline.md`,
`.claude/rules/incremental-delivery.md`.

## Dependencies

- **1.2** — the JSON report schema (the import validates against it).
- **4.1** — the in-game plugin (produces a real report to ingest; a sample report suffices for
  the unit tests, the real one for acceptance).

## Reference

[`../plan-spec.md`](../plan-spec.md) — Group E (import, worklist) + Group H (s08 worklist UX) +
cross-step invariant 1 (client-side File-API ingest, no backend seam — D31b).

## Design authority

`data/maintainer-tool/ui/screens/s08-verification-worklist.md` §"Contents" (the 12 Contents
elements — import entry, summary header, ingest progress, split control, worklist table, per-row
select, select-all-passing, re-verify-N, fix-row, back) + §"The verdict (per row)" + §"States &
variants" (the **9 states**: empty / loading-ingesting / populated / error-malformed /
error-unknown-id / disabled / edge-0-fail / edge-0-pass / edge-long). Plus
`data/maintainer-tool/ui/design.md` — the `ingest progress bar` + `verdict badge` silhouettes +
the screen-index/nav-map carrying s08. Report ingest is **D31b** (File API), progress is **D31c**.
Build to these sections, not to this doc's summary.

## UX

Carried from the s08 spec (`.claude/rules/ux-first-class.md` — not invented):
- **Empty (no report imported)** — the resting state: "Import a verification report
  (`report.json`) to review which rows still resolve." + the `[Import verification report]`
  affordance. Not a blank pane.
- **Loading (ingesting)** — the `ingest progress bar`: "Parsing report… `N` / `M` rows"
  (determinate; the count is known from the file — D31c). Occupies the worklist's region, resolves
  in place (law 1).
- **Populated** — the summary header (`M/N passing`) + the pass/fail split (default Failing first)
  + the worklist table (every reported row, passing grouped from failing).
- **Error (malformed / unreadable report)** — "Couldn't read that report — it isn't a valid
  verification report (`<reason>`)." (system-caused copy) + `[Pick another file]`; stays on empty,
  nothing ingested.
- **Error (a row's `kcdx_id` unknown to the current DB)** — the row renders CannotCheck + "row
  `<id>` is not in the current database (stale report?)" + excluded from bulk-select; never
  silently dropped.
- **Disabled** — `[Re-verify N rows]` disabled (not hidden) when zero passing rows are selected
  (conveyed by more than color, law 7); a failing row has no select checkbox — its `[Fix ▸]` is
  the action.
- **Edge** — 0 failing → split defaults Passing, "All `N` rows still resolve"; 0 passing → the
  batch action disabled, every row offers `[Fix ▸]`; a 157+-row report scrolls the table while the
  summary header + action bar pin (law 1); a long name/detail wraps within its cell (law 1).
  Keyboard-reachable controls; glyph+text verdicts.

## Disassembler-test / author-burden

None — s08 consumes the engine's whole-DB verdict report; the maintainer reviews verdicts the
engine produced rather than hand-checking rows. It reduces author burden (the disassembler-test
direction), adds no hand-hex input.
