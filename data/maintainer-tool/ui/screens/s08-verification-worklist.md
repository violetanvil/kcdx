# s08 — Verification report worklist (import the in-game report · pass/fail · batched bulk re-verify)

## Phase & fidelity
v1 / high. TRD authority: [`../../design.md`](../../design.md) §6 US-11 + §10 D28/D29/D31.

## Purpose / when shown
Shown when the maintainer imports the in-game verification plugin's JSON report (the live
per-row verdicts — every DB row checked in the running game, TRD D25/D28). The screen presents
every reported row + its verdict, splits passing from failing, and lets the maintainer
**bulk re-verify** the passing rows in one batched, confirmed transaction (law 5 at batch
scale) and jump to fix a failing row. The report is read **client-side via the File API** (the
maintainer picks `report.json` in-page; no backend read seam — TRD D31). The screen does NOT
verify anything itself — it consumes a verdict the engine already produced.

## Region & position
The main content area (right pane on wide; a drilled-in full view on phone), peer to s02/s03 —
NOT an overlay (a 157-row review session is a working surface, not a dim-and-dismiss overlay,
law 2). Reached from s01's `[Import verification report]` affordance. The app shell persists
around it (law 2); `‹ back` (phone) returns to s01.

## Contents
| Element | Component (Mantine) | Data bound | Intent emitted |
|---|---|---|---|
| `[Import verification report]` (entry) | `button` (default) — lives in s01 | — | `import_report()` → File API picker |
| Report summary header | `section header` | the imported report's source filename + `M/N passing` | — (status, read-only) |
| Ingest progress | `ingest progress bar` | parsed-row count `N / M` | — (transient; resolves to the worklist) |
| Pass/fail split control | `filter control` (`SegmentedControl`) | `Passing` / `Failing` / `All` | `filter_verdicts(group)` |
| Worklist table | `version table row` ×N (one per reported row) | `kcdx_id` · `name` (mono) · resolved version · `verdict badge` · detail | `select_row(kcdx_id)` (drills to fix) |
| Per-row select | `Checkbox` (passing rows only) | the row's inclusion in the batch | `toggle_select(kcdx_id)` |
| `[Select all passing]` | `button` (subtle) | toggles all passing rows | `select_all_passing()` |
| `[Re-verify N rows]` | `button` (primary) | the selected passing rows | `bulk_reverify(selected)` → s06 batch confirm |
| A failing row's `[Fix ▸]` | `button` (subtle) | the failing row | `select_entity(kcdx_id)` → s02 / s04 |
| `‹ back` (phone) | `drill-down back` | — | `back_to_list()` → s01 |

**The verdict (per row).** The in-game LIVE check reports one of (TRD D25/D28): **resolves+works**
(the address resolved via the real engine path + landed on real working code) → the `verdict
badge` reads **Unchanged**; **dead** (resolved to 0 / garbage) or **wrong-target** (resolved to
a body that no longer matches) → **Changed**; **cannot-check** (the row's kind can't be checked
this run — e.g. a deferred `vtable_index`) → **CannotCheck**. (The static `Ambiguous` verdict is
an s04 author-time outcome, not a live-report one — s08's report carries live verdicts only.)
The badge is glyph+text, never color-alone (law 7); the detail line names the cause.

**Bulk re-verify = ONE batched confirm (law 5 at batch scale, TRD D28).** Selecting passing
rows + `[Re-verify N rows]` opens an **s06 batch confirm** (the `batch field-delta list`): every
selected row's audit-trio change shown as `field: old → new` (per row: `last_verified_at_version
→ <report version>`, `evidence_kind → live_production` per D29, `verified_date → today`,
`verified_by → <injected identity>`), under ONE confirm action. The maintainer reviews the whole
batch delta, confirms once → ONE atomic transaction (validate → write → export → commit + push,
law 5). Re-verify is an UPDATE, so the new-row approval gate (law 8) does NOT apply. No silent
bulk write — the delta is always shown before anything lands.

## States & variants
- **Empty (no report imported)** — the screen's resting state before an import: a neutral
  prompt *"Import a verification report (`report.json`) to review which rows still resolve."* +
  the `[Import verification report]` affordance. Not a blank pane.
- **Loading (ingesting)** — while parsing the picked report: the `ingest progress bar` —
  *"Parsing report… `N` / `M` rows"* (determinate; the row count is known from the file, TRD
  D31). Occupies the worklist's region and resolves in place to the populated worklist (law 1).
- **Populated** — the summary header (`M/N passing`) + the pass/fail split + the worklist table
  (every reported row, passing grouped from failing) + the batch action bar. The default split
  view is **Failing first** (the rows needing attention), then Passing.
- **Error (malformed / unreadable report)** — the File API read failed or the JSON is not a
  valid report: *"Couldn't read that report — it isn't a valid verification report (`<reason>`)."*
  (system-caused copy naming the cause) + `[Pick another file]`. The screen stays on the empty
  state; nothing is ingested.
- **Error (a row's kcdx_id is unknown to the current DB)** — a report row references an id the
  loaded DB doesn't have (a stale report against a changed DB): the row renders with a
  **CannotCheck** badge + *"row `<id>` is not in the current database (stale report?)"* and is
  excluded from bulk-select. Never silently dropped (law: no silent-success).
- **Disabled** — `[Re-verify N rows]` is disabled (not hidden) when zero passing rows are
  selected; the disabled state is conveyed by more than color (law 7). A failing row's select
  checkbox is absent (only passing rows are batch-re-verifiable) — its `[Fix ▸]` is the action.
- **Edge content** — a report with **0 failing** (all pass) → the split defaults to Passing,
  the summary reads *"All `N` rows still resolve"*; **0 passing** (all fail) → the batch action
  is disabled, every row offers `[Fix ▸]`; a **very long report** (157+ rows) scrolls the
  worklist table, the summary header + the action bar stay fixed (law 1); a long `name` /
  detail wraps within its cell without pushing siblings (law 1).

## Links in / out
- **In:** s01 `[Import verification report]` (`import_report` → the File API picker → this
  screen). (There is no auto-navigation to s08 — the maintainer chooses to import, law 3.)
- **Out:** `bulk_reverify` → s06 (batch confirm overlay) → on confirm, one atomic txn → toast
  *"Re-verified `N` rows"* (or *"blocked — Retry"*), returns to the worklist with the re-verified
  rows updated IN PLACE (law 3 — the result updates status in place, never re-navigates); a
  failing row's `[Fix ▸]` → `select_entity` → s02 (then s04 to author the corrected `rva` /
  `signature`); `back_to_list` (phone) → s01.

## Applicable laws
- **Law 1** — the summary header + action bar never reflow as rows scroll or verdicts update;
  the ingest progress occupies the worklist's reserved region.
- **Law 2** — s08 is a content-area screen, NOT an overlay; the shell persists; only the s06
  batch confirm (an overlay) dims over it.
- **Law 3** — importing, selecting rows, confirming, and `[Fix ▸]` are all user actions; the
  re-verify result updates the rows' status IN PLACE and never auto-navigates, auto-selects, or
  auto-opens a row.
- **Law 4** — every imported verdict is advisory; a Changed/CannotCheck row is surfaced, never
  a block; bulk re-verify applies only the maintainer's explicitly selected rows, through the
  confirm (no silent bypass); no DLL/report content is uploaded (the report is read client-side).
- **Law 5** — bulk re-verify is ONE atomic, confirmed transaction with the batch field-delta
  shown; a failure rolls back fully; the git commit/push stays invisible plumbing ("Re-verified
  N rows", never the git mechanics).
- **Law 6** — the audit-trio writes the re-verify performs are validated by the shared
  validator (reached through the API); s08 renders the verdict, it never reimplements a rule.
- **Law 7** — the `verdict badge` + the pass/fail state are glyph+text, never color-alone; the
  disabled batch action is conveyed by more than color.
- **Law 9** — every color/space/size in s08 resolves to a Layer-1 token; no raw value.

## Responsive behavior
- **Wide (two-pane):** s08 renders in the right pane (the navigator stays in the left); the
  worklist table is full-width within the pane; the s06 batch confirm is a centered modal.
- **Phone (drill-down):** s08 is a full-screen drilled-in view reached from s01's import action,
  with `‹ back` to s01; the worklist table scrolls vertically, the summary header + action bar
  pin; the s06 batch confirm is a full-screen sheet. The per-row detail collapses under the
  badge rather than into a wide column.
