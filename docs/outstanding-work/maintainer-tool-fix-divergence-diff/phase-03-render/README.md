# Phase 3 — Wire the report's-DLL context + render the inline diff + the banner

Consume Phase 2's `fixDivergence` worker on the s04 `[Fix ▸]` arrival path: extend the carry channel
to bring the report's `game_version` + failing-row context to s04 (so 'actual' derives from the
report's divergent DLL), render the DLL-link prompt + no-DLL state, then render the inline per-field
recorded-vs-actual + the extended "What diverged" banner. The UI half of the feature — E4 (wiring),
E5, E6, E7, E8, E9, E10, E11.

## Step-grain ledger

| Step | Status | Commit |
|---|---|---|
| [3.1 — Carry the report's-DLL context + the no-DLL link prompt](step-1-carry-context-and-prompt.md) | NOT STARTED | — |
| [3.2 — Render the inline per-field diff + the extended banner](step-2-render-inline-diff-and-banner.md) | NOT STARTED | — |

## Phase gate

`npm run build` exit 0 + `npx vitest run` green in the frontend repo for each step's component tests,
AND — per `.claude/rules/ux-first-class.md` — the **user-facing milestone acceptance**: the
maintainer opens s04 via a `[Fix ▸]` on a `failed` row and experiences the per-field
recorded-vs-actual inline + the "What diverged" banner + the no-DLL-yet prompt → diffed-after-link
flow. The milestone UAT fires on step 3.2 (substantive + under-specified UI per the loop §F.1
cadence). Phase 3 → DONE on that acceptance, not on build-green alone.
