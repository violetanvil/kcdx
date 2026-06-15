# Phase 3 — Wire the report's-DLL context + render the inline diff + the banner

Consume Phase 2's `fixDivergence` worker on the s04 `[Fix ▸]` arrival path: extend the carry channel
to bring the report's `game_version` + failing-row context to s04 (so 'actual' derives from the
report's divergent DLL), render the DLL-link prompt + no-DLL state, then render the inline per-field
recorded-vs-actual + the extended "What diverged" banner. The UI half of the feature — E4 (wiring),
E5, E6, E7, E8, E9, E10, E11.

## Step-grain ledger

| Step | Status | Commit |
|---|---|---|
| [3.1 — Carry the report's-DLL context + the no-DLL link prompt](step-1-carry-context-and-prompt.md) | DONE | FE:6e4786f |
| [3.2 — Render the inline per-field diff + the extended banner](step-2-render-inline-diff-and-banner.md) | DONE | FE:3c53e22 |

3.2 (render): new `FieldDivergenceMarker` renders the inline per-field recorded-vs-actual in
FieldRow's reserved gutter (law 1, no reflow; glyph+text — law 7); the "What diverged" banner now
names the diverged field(s) (E7); honest no-divergence-found / cannot-check banner states (E9/E10,
AP14 — never a silent empty or faked pass); function `signature` renders `cannot-derive` honestly.
Consumes `computeFixDivergence` (2.1) over the carried report's-DLL context (3.1) — reuse-only. The
deferred concrete `actual` scalar stays null (no new extraction; the marker lights a "build:" column
if a future worker populates it). Gate: `npm run build` exit 0 + `npx vitest run` 582/582 (7 new
falsifiable tests). **Build-DONE; the Phase-3 milestone UAT (below) is the remaining gate.**

3.1 (wiring): `DeepLinkTarget.fixDetail` extended `{detail}` → `{detail, gameVersion}` (the existing
law-10 channel, no new one); `[Fix ▸]` carries `report.game_version`; FieldEditor renders the no-DLL-
yet advisory prompt prefilled from the carried version (law 4 — never gates `[Review changes]`). The
inline per-field diff render + the extended banner + the milestone UAT are 3.2. NOTE: 3.1's added
App.test.tsx tests raised the KI-0020 s09/back-stack timing-flake frequency (still non-deterministic;
the suite passes 575/575 on clean runs; the failing tests pass in isolation) — same tracked defect,
not a 3.1 regression.

## Phase gate

`npm run build` exit 0 + `npx vitest run` green in the frontend repo for each step's component tests,
AND — per `.claude/rules/ux-first-class.md` — the **user-facing milestone acceptance**: the
maintainer opens s04 via a `[Fix ▸]` on a `failed` row and experiences the per-field
recorded-vs-actual inline + the "What diverged" banner + the no-DLL-yet prompt → diffed-after-link
flow. The milestone UAT fires on step 3.2 (substantive + under-specified UI per the loop §F.1
cadence). Phase 3 → DONE on that acceptance, not on build-green alone.
