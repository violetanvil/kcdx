# s08 — Verification report worklist (import the in-game report · pass/fail · batched bulk re-verify)

## Phase & fidelity
v1 / high. TRD authority: [`../../design.md`](../../design.md) §6 US-11 + §10 D28/D29/D31 +
**D36** (the 7-state verdict + the 5-rank proof ladder — the active-attempt model) + D33/D34/D35.

## Purpose / when shown
Shown when the maintainer imports the in-game verification plugin's **v3** JSON report (the live
per-row verdicts — every curated DB row actively attempted in the running game, TRD D25/D28/D36).
The report is produced by the `kcdx_verify_all` console command the maintainer runs after loading
a save (D33/D36). The screen presents every reported row + its **7-state verdict** and the
**proof-rank** that produced it, splits the rows into three blocks (verified / failing /
no-action), and lets the maintainer **bulk re-verify** the verified rows and **close intervals**
on the failing rows — each in one batched, confirmed transaction (law 5 at batch scale) — and
jump to fix a failing row. The report is read **client-side via the File API** (the maintainer
picks the report JSON in-page; no backend read seam — TRD D31). The screen does NOT verify
anything itself — it consumes verdicts the engine already produced.

## Region & position
The main content area (right pane on wide; a drilled-in full view on phone), peer to s02/s03 —
NOT an overlay (a 157-row review session is a working surface, not a dim-and-dismiss overlay,
law 2). Reached from s01's `[Import verification report]` affordance — a **top-level destination
that RESETS the content back-stack** to a fresh root at s08 (law 10). The app shell persists
around it (law 2). A `[Fix ▸]` **PUSHES** s02/s04 onto the stack as a state-carrying frame (law
10), so `‹ back` from the fix returns to THIS s08 worklist with **the ingested report intact** —
the parsed pass/fail rows, the block split, and the scroll position all restored, **no re-import**
(the report is client-side only — TRD D31 — so the state-carrying frame is what preserves it).
At s08's own stack root there is no `‹ back` (the navigator is the way out on wide; on phone
`‹ back` returns to the navigator home).

## Contents
| Element | Component (Mantine) | Data bound | Intent emitted |
|---|---|---|---|
| `[Import verification report]` (entry) | `button` (default) — lives in s01 | — | `import_report()` → File API picker |
| Report summary header | `section header` | the imported report's source filename + `M/N passing` (+ a `partial` marker when `complete: false`) | — (status, read-only) |
| Partial-report banner | `warning banner` | `complete` / `rows_expected` vs rows-actual (shown only when `complete: false`) | — (advisory; the N present rows stay actionable) |
| Ingest progress | `ingest progress bar` | parsed-row count `N / M` | — (transient; resolves to the worklist) |
| Block split control | `filter control` (`SegmentedControl`) | `Failing` / `Verified` / `No action` / `All` | `filter_blocks(block)` |
| Worklist table | `version table row` ×N (one per reported row) | `kcdx_id` · `name` (mono) · resolved version · matched `address_version` id (TRD D34) · `live verdict badge` · `proof-rank chip` · detail | `select_row(kcdx_id)` (drills to fix) |
| Per-row select (verified block) | `Checkbox` (verified-block rows only) | the row's inclusion in the verify-all batch | `toggle_select(kcdx_id)` |
| Per-row select (failing block) | `Checkbox` (failing-block rows only) | the row's inclusion in the close-intervals batch | `toggle_select_failing(kcdx_id)` |
| No-action rows | `version table row` (no checkbox) | the 4 no-action verdicts — `not_applicable` / `cannot_check` / `skipped` / `error` | — (shown, no action — TRD D36) |
| `[Select all verified]` / `[Select all failing]` | `button` (subtle) | toggles all rows in the block | `select_all_verified()` / `select_all_failing()` |
| `[Verify N rows]` | `button` (primary, verified block) | the selected verified rows | `bulk_verify(selected)` → s06 batch confirm (TRD D32/D35) |
| `[Close intervals · N rows]` | `button` (primary, failing block) | the selected failing rows | `bulk_close_intervals(selected)` → s06 batch confirm (TRD D35) |
| A failing row's `[Fix ▸]` | `button` (subtle) | the failing row + its divergence `detail` (TRD D41) | `fix_row(kcdx_id)` → PUSHES s02 / s04 onto the back-stack (law 10), CARRYING the row's `detail`; `‹ back` returns to this worklist, report intact |
| `‹ back` (content-pane, depth > 1) | `back affordance` | the stack | `nav_back()` → the screen that pushed s08 (law 10); at root → navigator |

**The verdict (per row) — the 7-state enum + the proof rank (TRD D36).** The in-game sweep runs
the strongest APPLICABLE active attempt per row and reports a **7-state verdict** (the v3
report-schema tokens, snake_case on the wire) plus the **`method_rank`** (1–5) of the method that
produced it — a verdict is the CEILING of the strongest method that ran. The row renders the
**`live verdict badge`** (the 7 states) beside the **`proof-rank chip`** (`rank N · <method>`),
both glyph+text never color-alone (law 7), with the matched `address_version` id shown for an
attributed row (TRD D34). The states:

- **`verified_working`** — observed executing correctly this session (rank 1 — the only rank that
  earns it; an engine-hooked target that fired, or a kcdx-called target that ran + returned).
- **`passed_not_verified`** — the strongest applicable attempt PASSED but cannot prove execution
  (ranks 2–5 — a safe-read / reachability / on-disk-hash / resolution pass); real evidence, not
  proof-of-working, so it caps below the top rung.
- **`failed`** — an attempt the row should pass returned wrong (diverged bytes / dead resolve /
  wrong target / a safe-read that faulted).
- **`not_applicable`** — the version-applicability check RAN and found the running build's version
  isn't covered by this row (a version gap), distinct from `cannot_check`.
- **`cannot_check`** — the attempt ran but the row lacks the inputs the check needs (no
  fingerprint, or a deferred kind like `vtable_index`).
- **`skipped`** — a precondition wasn't met THIS run (the live-exercise tier needed a loaded save
  it didn't get — run from the menu; or the module DLL was absent). The only "didn't run", and it
  names exactly why.
- **`error`** — the verification harness itself faulted on this row (distinct from `failed`: the
  ROW may be fine, the TEST blew up).

The detail line names the cause; it ALSO surfaces the **invoke posture** (`invoke_skip_reason`)
**only when it adds information** — `unsafe_to_call` ("couldn't safely invoke — a foreign function
kcdx never synthetically calls") or `uncontainable` on a row that COULD have been a callable target;
the trivially-obvious `not_a_callable_kind` / null (an anchor / data slot / vtable is obviously not
called) is suppressed (no noise). The verdict is version-applicability + reachability + the
live-exercise tier — NOT a runtime "does the code still work" body hash (the corrected D25; the
hash is on-disk). The static `Ambiguous` verdict is an s04 author-time outcome, NOT a report token —
deliberately absent from the report enum; s08 carries the live-pass verdicts only.

**Three blocks — two action, one no-action (TRD D36 verdict→block mapping).** The worklist splits
into:
- a **verified block** — `verified_working` + `passed_not_verified` → the **verify-all** action;
- a **failing block** — `failed` → the **close-intervals** action;
- a **no-action / informational block** — `not_applicable` / `cannot_check` / `skipped` / `error`
  → shown, NO action (no checkbox; the engine reported these as "nothing for the maintainer to do
  here" — a version gap, a deferred kind, a precondition miss, a harness fault). Rendered as a
  **`collapsible section`** (collapsed by default — it is informational, not the work surface), the
  rows fully visible + auditable when expanded, never silently dropped (law 4 / no silent-success).

**Report-vs-DB reconciliation — an already-acted-on row shows no-further-action (TRD D41).** The
worklist is NOT stateless against the DB: the data-core reconciles each report row against the
CURRENT database state and derives whether the report's recommended action is ALREADY reflected
(it reads the row's live state for the report's `(kcdx_id, version)` — the `/save/reverify-batch`
preview already reads per-row DB state). A row whose recommended action already landed —
a `failed` row whose interval is already closed to `last_verified_at_version`
(`valid_through == last_verified_at_version`, non-NULL); a verified-block row already covered
(`last_verified_at_version >= report version`) — **moves out of its actionable block into a
"no further action" state** (rendered like the no-action block: surfaced, auditable, no checkbox,
not in any batch, with a marker — *"interval already closed at this version"* /
*"already current"*). The actionable verified/failing blocks therefore show ONLY rows that still
NEED the action — a re-imported partly-acted report shows true remaining work, never a wall of
already-done rows framed as new, and confirming never produces a no-op write (the data-core emits
no edit-spec for an already-done row — the close-intervals already-done skip mirrors verify-all's
existing skip, TRD D41/D39). Nothing is hidden (law 4 / no silent-success — the already-resolved
rows are shown, just not actionable).

**Two batched bulk actions, each ONE batched confirm (law 5 at batch scale, TRD D32/D35).** The
verified block and the failing block each have their own bulk action + s06 batch confirm. Neither
auto-writes — the maintainer reviews the whole batch delta and confirms once → ONE atomic
transaction (validate → write → export → commit + push, law 5); both are all-UPDATE so the new-row
gate (law 8) does NOT apply. The no-action block has no bulk action.

- **Verify-all** (verified block, `[Verify N rows]`) — every selected verified-block row's
  `field: old → new` delta: `last_verified_at_version → <report version>`, `verified_date →
  today`, `verified_by → <injected identity>`, `evidence_kind → <by the row's proof rank>` (D29 —
  a `verified_working` row, rank-1 observed live execution, writes `live_production`; a
  `passed_not_verified` row, ranks 2–5 static, writes **`live_test_plugin`** (the in-game test
  plugin checked it but didn't observe it execute) — NOT `live_production`, because it didn't
  execute), AND — when the report's version sat in a GAP or
  beyond the matched row's interval — `valid_through → <report version>` on the matched
  `address_version` row (TRD D34, the gap-pass extension). A row already covered
  (`last_verified_at_version >= report version`) is not in the verified block (nothing to add).
  (Both `verified_working` and `passed_not_verified` are real passes the maintainer can confirm in
  bulk; the `live verdict badge` + `proof-rank chip` make the tier visible in the batch confirm so
  the maintainer sees which rows were observed live vs statically passed before confirming.)
- **Close-intervals** (failing block, `[Close intervals · N rows]`) — every selected failing
  row's `valid_through → <its last_verified_at_version>` delta: the failed row's interval retracts
  to the last version it passed, because the sweep disproved validity beyond it (TRD D35). No
  "failed" field is written — not advancing `last_verified_at_version` already leaves the row
  UNVERIFIED at the new version by the existing derivation. The maintainer then fixes the function
  individually via `[Fix ▸]` (a corrected/variant row via the author flow, AP18 per-row — never in
  the batch).

**Close → needs-action (TRD D41).** A close-intervals action that ORPHANS an entity — retracts
`valid_through` below the current game version with no successor row covering it, and the entity is
neither deprecated nor superseded — leaves an incomplete lifecycle. The close stays ONE atomic
transaction (it is not coupled to a forced next step); the now-orphaned entity is FLAGGED as
**needs action** and appears in the standing needs-action view (below) for the maintainer to
complete separately (author a successor row for the current version, OR mark the entity deprecated,
OR supersede it). The orphan is never silent — the close surfaces it, the maintainer resolves it
when they choose.

**The Fix flow carries context and returns (TRD D41/D42).** `[Fix ▸]` is a round-trip,
context-carrying navigation, not a one-way drop: it carries the failing row's divergence `detail`
(the engine's reason, e.g. *"on-disk body hash mismatch: build diverged from the recorded
version"*) to the s04 field editor so the maintainer sees WHAT diverged without re-checking the
worklist; and it **PUSHES a state-carrying frame onto the content back-stack (law 10)** — the s08
frame stores the parsed report (the worklist + block split + scroll), so `‹ back` from the fix
**restores the report exactly, with no re-import** (the report is client-side only — TRD D31 — and
the state-carrying frame, not a re-fetch, is what preserves it). An applied row (after a confirmed
close or verify) shows its RESULTING value (the new `valid_through`), not only an "applied"
marker — the maintainer sees what the action produced. **If a `[Fix ▸]` excursion left the s04
editor dirty, navigating back (or away) first surfaces the unsaved-changes guard** (Save / Discard
/ Cancel, law 10) — the fix is never silently lost or silently committed on the way back to the
report.

**The standing needs-action view (TRD D41).** Lifecycle completeness is a standing, tool-wide
property, not a per-report afterthought: a **needs-action view/filter** lists every entity whose
lifecycle is incomplete at the current game version — the uncovered-at-V orphan (no interval covers
the current version, not deprecated, not superseded → no row is authoritative for the current
version; the entity is UNVERIFIED there, resolving only by best-match to a stale closed row), a never-verified row
(`last_verified_at_version IS NULL`), and the broader version-relative integrity gaps — reachable
any time and catching gaps from ANY flow (a close-intervals orphan, a hand-edit, a pre-existing
incomplete row). Each entry names its resolution path. (Surface placement is the tool-wide
needs-action view; this s08 screen feeds it — a close that orphans a row lands in it.)

No silent bulk write — the delta is always shown before anything lands.

## States & variants
- **Empty (no report imported)** — the screen's resting state before an import: a neutral
  prompt *"Import a verification report to review which rows still resolve."* + the
  `[Import verification report]` affordance. Not a blank pane.
- **Loading (ingesting)** — while parsing the picked report: the `ingest progress bar` —
  *"Parsing report… `N` / `M` rows"* (determinate; the row count is known from the file, TRD
  D31). Occupies the worklist's region and resolves in place to the populated worklist (law 1).
- **Populated** — the summary header (`M/N passing`) + the block split control + the three blocks
  (failing / verified / no-action) + the batch action bar. The default split view is **Failing
  first** (the rows needing attention), then Verified, then the collapsed No-action block.
- **Partial report (`complete: false`)** — the sweep finalized after stopping early (a mid-run
  death; the v3 report carries `complete: false` + `rows_expected` > rows-actual, the D37 partial
  signal). A `warning banner` pins above the worklist: *"Partial report — the sweep stopped at
  `<rows-actual>` of `<rows_expected>` rows (it may have stopped mid-run). The rows below are
  complete; the remaining `<M−N>` were not checked — re-run `kcdx_verify_all` for a full report."*
  (system-caused copy, law 4 advisory). The `<rows-actual>` present rows render normally and stay
  fully actionable (the verify-all / close-intervals batches act on the real rows); the banner
  makes the gap loud so a partial report never reads as a complete-with-fewer-rows one. The summary
  header also carries a `partial` marker.
- **Error (malformed / unreadable report)** — the File API read failed or the JSON is not a
  valid v3 report (fails v3-schema validation): *"Couldn't read that report — it isn't a valid
  verification report (`<reason>`)."* (system-caused copy naming the cause) + `[Pick another
  file]`. The screen stays on the empty state; nothing is ingested. (A v2-shaped or older report
  is a `<reason>` here — the importer validates against v3.)
- **Error (a row's kcdx_id is unknown to the current DB)** — a report row references an id the
  loaded DB doesn't have (a stale report against a changed DB): the row renders in the no-action
  block with a **`cannot_check`** `live verdict badge` + *"row `<id>` is not in the current
  database (stale report?)"* and is excluded from any bulk-select (it has no checkbox). Never
  silently dropped (law: no silent-success).
- **Disabled** — each action block's bulk action is disabled (not hidden) when zero rows in that
  block are selected: `[Verify N rows]` when no verified row is selected, `[Close intervals · N
  rows]` when no failing row is selected; the disabled state is conveyed by more than color
  (law 7). Verified-block rows carry the verify checkbox; failing-block rows carry the
  close-intervals checkbox AND a `[Fix ▸]` (the close retracts the interval; the fix authors the
  correction — distinct actions); no-action-block rows carry NEITHER (shown, no action — D36).
- **Already acted on (report-vs-DB reconciliation, TRD D41)** — a re-imported row whose recommended
  action already landed in the DB (a `failed` row whose interval is already closed to
  `last_verified_at_version`; a verified row already covered at the report version) renders in a
  **"no further action"** state — surfaced + auditable (a marker *"interval already closed"* /
  *"already current"*), NO checkbox, NOT in any batch. It is excluded from the actionable
  verified/failing blocks so those show only rows still needing the action; confirming a batch never
  includes it (the data-core emits no edit-spec for an already-done row — no no-op write, no
  empty-delta confirm). Shown, never hidden (law 4 / no silent-success); the maintainer sees a
  partly-acted re-import as true remaining work plus the already-resolved rows, never a wall of
  done rows framed as new.
- **Orphaned by a close (needs action, TRD D41)** — a close-intervals action that leaves an entity
  with no interval covering the current game version and no deprecation/supersession flags it as
  **needs action**: the close completes atomically, and the orphaned entity appears in the standing
  needs-action view for the maintainer to complete (author successor / deprecate / supersede). The
  orphan is surfaced, never silent; resolving it is a separate deliberate act, not forced inline.
- **Edge content** — a report with **0 failing** → the split defaults to Verified, the summary
  reads *"All `N` checked rows still resolve"*, only the verify-all action is active; **0 verified**
  (all fail) → the verify action is disabled, the failing block's close-intervals action is active,
  and every failing row also offers `[Fix ▸]`; **all rows no-action** (e.g. a from-menu report
  where the live-exercise rows all `skipped`, or every kind deferred) → the two action blocks are
  empty (their actions disabled), the no-action block holds every row and is expanded by default
  (it IS the content), with a note steering the maintainer to load a save and re-run for the live
  tier; a **very long report** (157+ rows) scrolls the worklist table, the summary header + the
  partial banner + the action bar stay fixed (law 1); a long `name` /
  detail wraps within its cell without pushing siblings (law 1).
- **Returned from a `[Fix ▸]` (law 10)** — after a `[Fix ▸]` pushed s02/s04 and the maintainer
  `‹ back`s, s08 renders the SAME ingested report it held before — the parsed worklist, the block
  split, the scroll position, the per-row select state — all restored, **no re-import dialog, no
  empty state** (the report's client-side state was carried in the frame, not re-fetched, TRD
  D31/D42). An actioned row (a confirmed fix) shows its updated verdict/value in place (law 3).
- **Back affordance (law 10)** — at stack depth > 1, a `‹ back to <destination>` control sits
  top-left of the content pane in reserved space (no reflow, law 1); at s08's root (after the
  reset-from-s01 import) there is no `‹ back` (the navigator is the way out).
- **Unsaved-changes guard (law 10)** — if a `[Fix ▸]` excursion left the s04 editor dirty and the
  maintainer navigates back to s08 (or elsewhere), the Save / Discard / Cancel confirm (the
  `overlay surface`) fires first; nothing is saved or lost without an explicit choice (TRD D44).

## Links in / out
- **In:** s01 `[Import verification report]` (`import_report` → the File API picker → this
  screen). (There is no auto-navigation to s08 — the maintainer chooses to import, law 3.)
- **Out:** `bulk_verify` / `bulk_close_intervals` → s06 (batch confirm overlay) → on confirm, one
  atomic txn → toast *"Verified `N` rows"* / *"Closed intervals on `N` rows"* (or *"blocked —
  Retry"*), returns to the worklist with the actioned rows updated IN PLACE (law 3 — the result
  updates status in place, never re-navigates); a failing row's `[Fix ▸]` → PUSHES s02
  (then s04 to author the corrected `rva` / `signature`, possibly a new/variant version row — AP18
  per-row) onto the back-stack (law 10), and `‹ back` → THIS worklist with the report intact; at
  s08's stack root, `‹ back` → the navigator.

## Applicable laws
- **Law 1** — the summary header + action bar never reflow as rows scroll or verdicts update;
  the ingest progress occupies the worklist's reserved region.
- **Law 2** — s08 is a content-area screen, NOT an overlay; the shell persists; only the s06
  batch confirm (an overlay) dims over it.
- **Law 3** — importing, selecting rows, confirming, and `[Fix ▸]` are all user actions; the
  re-verify result updates the rows' status IN PLACE and never auto-navigates, auto-selects, or
  auto-opens a row.
- **Law 10** — the content back-stack: `[Import verification report]` from s01 RESETS the stack to
  a fresh s08 root; `[Fix ▸]` PUSHES s02/s04 as a state-carrying frame; `‹ back` restores THIS s08
  frame's full state — the ingested report (worklist + block split + scroll) intact, no re-import
  (the report is client-side only, law 4 / TRD D31). A `[Fix ▸]` that left s04 dirty surfaces the
  unsaved-changes guard (Save/Discard/Cancel) before navigating back. The `‹ back` affordance sits
  top-left of the content pane at depth > 1, labeled with its destination; absent at s08's root.
- **Law 4** — every imported verdict is advisory; a `failed` row (failing block), a no-action row
  (`not_applicable`/`cannot_check`/`skipped`/`error`), and a partial report are all SURFACED, never
  a block; bulk verify/close applies only the maintainer's explicitly selected rows, through the
  confirm (no silent bypass); no DLL/report content is uploaded (the report is read client-side).
- **Law 5** — bulk re-verify is ONE atomic, confirmed transaction with the batch field-delta
  shown; a failure rolls back fully; the git commit/push stays invisible plumbing ("Re-verified
  N rows", never the git mechanics).
- **Law 6** — the audit-trio writes the re-verify performs are validated by the shared
  validator (reached through the API); s08 renders the verdict, it never reimplements a rule.
- **Law 7** — the `live verdict badge` + the `proof-rank chip` + the block state are glyph+text,
  never color-alone; the disabled batch action is conveyed by more than color.
- **Law 9** — every color/space/size in s08 resolves to a Layer-1 token; no raw value.

## Responsive behavior
- **Wide (two-pane):** s08 renders in the right pane (the navigator stays in the left); the
  worklist table is full-width within the pane; the s06 batch confirm is a centered modal.
- **Phone (drill-down):** s08 is a full-screen drilled-in view reached from s01's import action;
  the back-stack drives the drill-down `‹ back` (law 10) — at s08's root it returns to the
  navigator home, and after a `[Fix ▸]` the fix's `‹ back` returns to this worklist, report intact; the worklist table scrolls vertically, the summary header + action bar
  pin; the s06 batch confirm is a full-screen sheet. The per-row detail collapses under the
  badge rather than into a wide column.
