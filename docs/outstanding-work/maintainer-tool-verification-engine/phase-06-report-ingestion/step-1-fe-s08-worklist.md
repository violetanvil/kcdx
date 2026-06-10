# 6.1 [FE] s08 worklist: import (File API, v3) + ingest progress + the THREE-block worklist + the s08 states

## What

Build the s08 verification-worklist screen (a NEW screen) up to the worklist surface: the
`[Import verification report]` entry → pick the report JSON in-page via the File API (client-side,
no backend read seam — D31b) → **validate it against the v3 schema** → the ingest progress bar
(determinate, N/M rows — D31c) → the populated worklist as **THREE blocks** (D36 verdict→block
mapping): a **verified block** (`verified_working` + `passed_not_verified`), a **failing block**
(`failed`), and a **no-action / informational block** (`not_applicable` / `cannot_check` /
`skipped` / `error` — shown, no action, a collapsed-by-default `collapsible section`). Each row
renders its `kcdx_id` · name (resolved from the loaded DB by id) · resolved version · matched
`address_version` id (D34) · the **`live verdict badge`** (the 7-state verdict) · the **`proof-rank
chip`** (`rank N · <method>` from `method_rank`) · the detail line (which conditionally surfaces the
invoke posture — `invoke_skip_reason` only when informative: `unsafe_to_call` / `uncontainable`;
`not_a_callable_kind`/null suppressed). Plus the summary header (`M/N passing` + a `partial`
marker), the block split control, the per-block selects + select-all, the `[Fix ▸]` jump on a
failing row, the **partial-report warning banner** (`complete: false` → the N-of-M gap), and the
s08 states. This is the consumer of the Phase-5 **v3** report. The two bulk batch actions
(verify-all, close-intervals) land in step 6.2.

## Scope

One commit in the frontend repo: the s08 screen — import entry, File-API report read + parse
(validating against the **v3** schema; a non-v3 / malformed report → the error state), the ingest
progress bar, the summary header (+ `partial` marker), the block split control (`Failing` /
`Verified` / `No action` / `All`), the **three-block worklist** table with per-row `live verdict
badge` + `proof-rank chip` + detail + the matched-`address_version`-id column, the per-row select in
EACH action block (a verify checkbox on verified-block rows, a close-intervals checkbox on
failing-block rows; NO checkbox on no-action-block rows) + `[Select all verified]` /
`[Select all failing]`, the `[Fix ▸]` → s02/s04 navigation, the partial-report warning banner, and
all the states. The s06 batch confirms + the two bulk-action spine wiring is step 6.2 (the
`[Verify N rows]` and `[Close intervals · N rows]` buttons RENDER here; their handlers land in 6.2).
Built to the reconciled s08 screen spec.

## Test bar

Vitest unit/component tests in the frontend repo: importing a sample Phase-5 **v3** report parses +
validates (and a malformed / v2-shaped / `schema_version != 3` one shows the error state); the
ingest progress bar shows N/M; the three-block split groups `verified_working`+`passed_not_verified`
into the verified block, `failed` into the failing block, and
`not_applicable`/`cannot_check`/`skipped`/`error` into the no-action block; each `live verdict
badge` renders glyph+text from the 7-state token; each `proof-rank chip` renders `rank N · <method>`
from `method_rank` (1–5); the matched `address_version` id renders on a verified-block row and is
absent (null) on a failing/no-action row; a **partial report** (`complete: false`, rows <
`rows_expected`) shows the warning banner + the N-of-M gap and the present rows stay actionable; a
row whose `kcdx_id` is unknown to the current DB renders in the no-action block with `cannot_check`
+ is excluded from select; the invoke-posture detail shows `unsafe_to_call`/`uncontainable` and
suppresses `not_a_callable_kind`/null; `[Fix ▸]` navigates to s02/s04 (user-action, law 3). Runnable
at this step (the v3 schema + a sample v3 report exist from Phase 5 / the schema's samples) —
`.claude/rules/test-discipline.md`, `.claude/rules/incremental-delivery.md`.

## Dependencies

- **Phase 5, step 5.1** — the **v3** JSON report schema (the import validates against it; carries
  the 7-state verdict + `method_rank` + `invoke_attempted`/`invoke_skip_reason` + the matched id +
  `complete`/`rows_expected`).
- **Phase 5, step 5.3** — the in-game sweep producer (emits a real v3 report to ingest; the schema's
  sample reports + a fixture suffice for the Vitest tests, the real one for acceptance).
- The reconciled s08 screen spec + the Layer-1 `live verdict badge` + `proof-rank chip` silhouettes
  (the design authority below).

## Reference

[`../plan-spec.md`](../plan-spec.md) — Group E (import, three-block worklist) + Group H (s08
worklist UX) + cross-step invariant 1 (client-side File-API ingest, no backend seam — D31b).

## Design authority

`data/maintainer-tool/ui/screens/s08-verification-worklist.md` (the reconciled v3/D36 spec)
§"Contents" (import entry, summary header + `partial` marker, partial-report banner, ingest
progress, block split control, the three-block worklist table with the `live verdict badge` +
`proof-rank chip` + matched-`address_version`-id column, the per-block selects, select-all per
action block, the two bulk actions, the no-action rows, fix-row, back) + §"The verdict (per row) —
the 7-state enum + the proof rank" (the 7 verdict tokens → the `live verdict badge`; the
`method_rank` → the `proof-rank chip`; the conditional invoke-posture detail; the matched id shown)
+ §"Three blocks — two action, one no-action" + §"States & variants" (empty / loading-ingesting /
populated-three-block / partial-report / error-malformed-non-v3 / error-unknown-id /
per-block-disabled / edge-0-failing / edge-0-verified / edge-all-no-action / edge-long). Plus
`data/maintainer-tool/ui/design.md` — the `live verdict badge` + `proof-rank chip` + `ingest
progress bar` + `warning banner` + `collapsible section` silhouettes + the screen-index/nav-map
carrying s08. The v3 contract is
`data/maintainer-tool/report-schema/verification-report.schema.json`. Report ingest is **D31b**
(File API), progress is **D31c**, the three-block mapping is **D36**, the matched id is **D34**, the
partial-report signal is **D37**. Build to these sections (the RECONCILED s08 spec + the v3 schema),
not to this doc's summary.

## UX

Carried from the reconciled s08 spec (`.claude/rules/ux-first-class.md` — not invented):
- **Empty (no report imported)** — the resting state: "Import a verification report to review which
  rows still resolve." + the `[Import verification report]` affordance. Not a blank pane.
- **Loading (ingesting)** — the `ingest progress bar`: "Parsing report… `N` / `M` rows"
  (determinate; the count is known from the file — D31c). Occupies the worklist's region, resolves
  in place (law 1).
- **Populated** — the summary header (`M/N passing`) + the block split (default Failing first) +
  the three blocks (verified / failing / the collapsed no-action block) + the worklist table (each
  row: `live verdict badge` + `proof-rank chip` + matched id + detail).
- **Partial report (`complete: false`)** — a `warning banner` pins above the worklist: "Partial
  report — the sweep stopped at `N` of `M` rows (it may have stopped mid-run). The rows below are
  complete; the remaining `M−N` were not checked — re-run for a full report." (system-caused copy,
  law 4 advisory). The N present rows render normally and stay fully actionable; the summary header
  carries a `partial` marker.
- **Error (malformed / non-v3 report)** — "Couldn't read that report — it isn't a valid
  verification report (`<reason>`)." (a v2-shaped / `schema_version != 3` report is a `<reason>`
  here) + `[Pick another file]`; stays on empty, nothing ingested.
- **Error (a row's `kcdx_id` unknown to the current DB)** — the row renders in the no-action block
  with a `cannot_check` `live verdict badge` + "row `<id>` is not in the current database (stale
  report?)" + excluded from any bulk-select; never silently dropped.
- **Disabled (per action block)** — `[Verify N rows]` disabled when no verified row is selected;
  `[Close intervals · N rows]` disabled when no failing row is selected (conveyed by more than
  color, law 7). Verified-block rows carry the verify checkbox; failing-block rows carry the
  close-intervals checkbox AND a `[Fix ▸]`; no-action-block rows carry NEITHER (shown, no action —
  D36). (The bulk-action buttons render here; their handlers land in 6.2.)
- **Edge** — 0 failing → split defaults Verified, "All `N` checked rows still resolve", only
  verify-all active; 0 verified → the verify action disabled, close-intervals active, every failing
  row also offers `[Fix ▸]`; all-no-action (e.g. a from-menu report where the live-exercise rows all
  `skipped`) → the two action blocks empty, the no-action block expanded by default with a note to
  load a save and re-run; a 157+-row report scrolls the table while the summary header + the partial
  banner + the action bar pin (law 1); a long name/detail wraps within its cell (law 1).
  Keyboard-reachable controls; glyph+text verdicts + proof-rank chips.

## Disassembler-test / author-burden

None — s08 consumes the engine's whole-DB verdict report; the maintainer reviews verdicts the
engine produced rather than hand-checking rows. It reduces author burden (the disassembler-test
direction), adds no hand-hex input.
