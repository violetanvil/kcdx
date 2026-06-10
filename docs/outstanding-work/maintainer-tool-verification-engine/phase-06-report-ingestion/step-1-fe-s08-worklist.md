# 5.1 [FE] s08 worklist: import (File API) + ingest progress + the TWO-BLOCK worklist (verified / failing) + the s08 states

## What

Build the s08 verification-worklist screen (a NEW screen) up to the worklist surface: the
`[Import verification report]` entry → pick `report.json` in-page via the File API (client-side,
no backend read seam — D31b) → the ingest progress bar (determinate, N/M rows — D31c) → the
populated worklist as **TWO reviewed blocks** (D35): a **verified block** (every `resolves_works`
row) and a **failing block** (every `wrong_target`/`dead` row), with the summary header `M/N
passing`, the pass/fail split control, and the worklist table — one row per reported row with its
verdict badge (the **snake_case** tokens `resolves_works`/`wrong_target`/`dead`/`cannot_check` →
the Unchanged/Changed/CannotCheck badges), detail, **and the matched `address_version` id** (D34) —
plus the `[Fix ▸]` jump on a failing row. The s08 states. This is the consumer of the Phase-4 **v2**
report (validated against the Phase-1 v2 schema). The two bulk batch actions (verify-all,
close-intervals) land in step 2.

## Scope

One commit in the frontend repo: the s08 screen — import entry, File-API report read + parse
(validating against the Phase-1 **v2** schema), the ingest progress bar, the summary header, the
pass/fail split control, the **two-block worklist** (verified block + failing block) table with
per-row verdict badge + detail + the matched-`address_version`-id column, the per-row select in
EACH block (a verify checkbox on passing rows, a close-intervals checkbox on failing rows) +
`[Select all passing]` / `[Select all failing]`, and the `[Fix ▸]` → s02/s04 navigation — plus the
states. The s06 batch confirms + the two bulk-action spine wiring is step 2 (the `[Verify N rows]`
and `[Close intervals · N rows]` buttons render here, their handlers land in step 2). Built to the
revised s08 screen spec.

## Test bar

Vitest unit/component tests in the frontend repo: importing a sample Phase-4 **v2** report parses +
validates (and a malformed / v1-shaped one shows the error state); the ingest progress bar shows
N/M; the two-block split groups `resolves_works` into the verified block and `wrong_target`/`dead`
into the failing block; each verdict badge renders (Unchanged/Changed/CannotCheck) glyph+text from
the snake_case tokens; the matched `address_version` id renders on an attributed row; a row whose
`kcdx_id` is unknown to the current DB renders CannotCheck + is excluded from select; `[Fix ▸]`
navigates to s02/s04 (user-action, law 3). Runnable at this step (the v2 schema + a sample report
exist from Phase 1 / Phase 4) — `.claude/rules/test-discipline.md`,
`.claude/rules/incremental-delivery.md`.

## Dependencies

- **1.3** — the **v2** JSON report schema (the import validates against it; carries the matched id).
- **4.1** — the in-game plugin (produces a real v2 report to ingest; a sample report suffices for
  the unit tests, the real one for acceptance).

## Reference

[`../plan-spec.md`](../plan-spec.md) — Group E (import, worklist) + Group H (s08 worklist UX) +
cross-step invariant 1 (client-side File-API ingest, no backend seam — D31b).

## Design authority

`data/maintainer-tool/ui/screens/s08-verification-worklist.md` §"Contents" (the revised Contents
elements — import entry, summary header, ingest progress, split control, **two-block** worklist
table with the matched-`address_version`-id column, the per-block per-row selects, select-all per
block, the two bulk actions, fix-row, back) + §"The verdict (per row)" (the **snake_case** tokens →
the Unchanged/Changed/CannotCheck badges; the matched id shown) + §"States & variants" (empty /
loading-ingesting / populated / error-malformed / error-unknown-id / disabled-per-block /
edge-0-fail / edge-0-pass / edge-long). Plus `data/maintainer-tool/ui/design.md` — the `ingest
progress bar` + `verdict badge` silhouettes + the screen-index/nav-map carrying s08. Report ingest
is **D31b** (File API), progress is **D31c**, the two-block worklist is **D35**, the matched id is
**D34**. Build to these sections (the REVISED s08 spec), not to this doc's summary.

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
- **Disabled (per block)** — `[Verify N rows]` disabled when no passing row is selected;
  `[Close intervals · N rows]` disabled when no failing row is selected (conveyed by more than
  color, law 7). Passing rows carry the verify checkbox; failing rows carry the close-intervals
  checkbox AND a `[Fix ▸]` (the close retracts the interval, the fix authors the correction —
  distinct actions; the buttons render here, their handlers land in step 2).
- **Edge** — 0 failing → split defaults Passing, "All `N` rows still resolve", only verify-all
  active; 0 passing → the verify action disabled, the close-intervals action active, every row also
  offers `[Fix ▸]`; a 157+-row report scrolls the table while the summary header + action bar pin
  (law 1); a long name/detail wraps within its cell (law 1). Keyboard-reachable controls; glyph+text
  verdicts.

## Disassembler-test / author-burden

None — s08 consumes the engine's whole-DB verdict report; the maintainer reviews verdicts the
engine produced rather than hand-checking rows. It reduces author burden (the disassembler-test
direction), adds no hand-hex input.
