# 5.3 [TEST] The sweep over the curated set + per-row streaming + v3 report emission + matrix row + 3-tree deploy

## What

Fill the `kcdx_verify_all` sweep body: iterate the **curated USER set** (the `address_versions`
rows carrying a `kcdx_id` — D33), drive the Phase-4 rank-ladder per row (every row an active attempt
+ a structured response — D36), attribute each result to the matched `address_version` row (D34),
auto-fill `evidence_kind` from the method tier on a pass (D29 — live-exercise → `live_production`),
**stream a per-row line to the console** as each attempt completes (D36 — never reads as a hang),
and emit the **v3 JSON report** (the per-row triple + flag + matched id) alongside `kcdx-dev.log`.
This is the producer end-to-end; it self-reports its suite verdict + records the matrix row +
deploys to all 3 plugin trees.

## Scope

One commit in kcdx `src/` + `test-plugins/`:
- the sweep loop over the curated USER set (the `refdb::ForEachCached` curated iterator Phase 3
  already uses), driving the Phase-4 `survival_verify` ladder per row + D34 attribution.
- D29 `evidence_kind`-from-tier on a pass (the method that produced `verified_working` /
  `passed_not_verified` sets the evidence tier).
- per-row console streaming via the verified overlay print channel
  (`kcdx.console.print` / `IConsole_PrintLine`): one line per row as its attempt completes —
  `[N/total] <name> v<ver> → <verdict> (rank <r>)`.
- **incremental report flush (D37) — NOT a bulk write at the end:** each row's result object is
  appended + flushed to a line-delimited (JSONL) sink the INSTANT its attempt completes (the same
  per-row tick as the console line above — the row is printed AND durably written together), so a
  sweep that hangs/crashes mid-run leaves a complete-up-to-row-N JSONL on disk. When the sweep
  finishes, a **finalize pass** wraps the accumulated lines into the v3 JSON document (the 5.1
  schema — `schema_version` + `game_version` + `summary` + `rows[]` + `complete: true` +
  `rows_expected`) alongside `kcdx-dev.log`. The known sink is the JSONL during the sweep, the
  finalized v3 JSON after. (A bulk write-at-end is forbidden — D37; the live-exercise tier makes a
  mid-sweep death a real risk, and a bulk write loses the whole report.)
- the `ACCEPT-SUITE` / `ACCEPT-RESULT` self-report (`.claude/rules/acceptance-signal.md`).
- the `test-plugins/README.md` matrix row + deploy to all 3 plugin trees
  (`kcdx-engine/builtin/` + `kcdx-plugins/` + `kcdx-plugins/test-suite/` — the deploy-all-trees
  discipline).

## Test bar

The matrix row IS the test bar (`.claude/rules/test-suite.md`): at a live launch (dev_mode on) the
maintainer loads a save + runs `kcdx_verify_all` → the sweep streams a per-row line per curated row,
**flushes each row's result to the JSONL sink as it completes (D37 — the JSONL grows during the
sweep, not all at once at the end)**, then finalizes a v3 JSON report that VALIDATES against the 5.1
schema (the per-row 7-state verdict + `method_rank` + `invoke_attempted` + `invoke_skip_reason` +
matched id + `complete: true` + `rows_expected`; the rank-1 rows `verified_working`, the
foreign-function rows `passed_not_verified` + `invoke_attempted: false`, the `vtable_index` rows
`cannot_check`), and self-reports `ACCEPT-SUITE` to `kcdx-dev.log`. The agent builds, deploys to all
3 trees, hash-verifies, enables dev mode; the user launches + loads + runs the command; the agent
reads the `ACCEPT-SUITE` PASS + confirms the report validates against the v3 schema. FALSIFIABLE: a
report that fails v3 validation, a row with no verdict (a passive non-result), a JSONL that appears
only as one bulk write at sweep end (the D37 violation — assert the sink carries ≥1 row's line
before the finalize, e.g. the row count grows across the sweep), or `ACCEPT-SUITE` absent — each
fails the row. Runnable AT this step (the Phase-4 ladder + the 5.1 schema + the 5.2 command all
exist). Per `.claude/rules/test-discipline.md`, `.claude/rules/incremental-delivery.md`.

## Dependencies

- **5.1** — the v3 schema the report is emitted to + validated against.
- **5.2** — the `kcdx_verify_all` command + save-load precondition the sweep runs under.
- **Phase 4** (4.1–4.4) — the rank-ladder + per-kind matrix driven per row.
- **Phase 3 (D34)** — the matched-`address_version_id` attribution (unchanged) the sweep records.

## Reference

[`../plan-spec.md`](../plan-spec.md) — the producer + the cross-repo report seam (cross-step
invariant: the JSON report is the FE-consumer contract).

## Design authority

`data/maintainer-tool/design.md` **D28** (the report producer + write-alongside-`kcdx-dev.log`) +
**D33** (the curated-USER-set scope + console-after-save-load + per-row streaming) + **D34** (the
matched `address_version_id` per row) + **D29** (`evidence_kind` from the passing check's tier) +
**D36** (the per-row response shape) + **D37** (the incremental per-row JSONL flush → finalize, never
a bulk write at end) + the **v3 schema** (step 5.1, the emit target). Build to those named contracts,
not this doc's summary.

## UX

The user gesture (per `.claude/rules/agent-builds-and-deploys.md`, `.claude/rules/acceptance-signal.md`):
launch → load a save → open console (`~`) → type `kcdx_verify_all` → tell me it ran. What the user
SEES: the console streams a per-row line as each attempt completes (clearly alive, never a frozen
console), then a final `ACCEPT-SUITE` summary line. The agent reads the report + the suite verdict
from the install; the user never reads a log line. The streaming IS the in-game loading/progress
state (the console-overlay analogue of a progress bar); a row that can't run reports its verdict
(`skipped` / `cannot_check`), never a silent gap.

## Disassembler-test / author-burden

None — a test-suite plugin + the engine sweep; no author-facing input. The sweep is the engine
verifying the whole curated DB so the maintainer never hand-checks rows one by one (the cornerstone:
the engine does the heavy lifting). No new game-function target (no AP18 addition).
