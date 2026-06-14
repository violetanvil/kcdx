# Plan spec — maintainer-tool per-field divergence diff (D45)

## Goal

Build the s04 `[Fix ▸]` **per-field divergence diff** — when the maintainer opens the s04 field
editor from a `failed` s08 verification-report row, the editor surfaces WHICH recorded
`address_versions` field diverged from the running build, **recorded-vs-actual, inline at the field
to edit**, plus a top "What diverged" summary banner. Re-derived IN-BROWSER against the report's
divergent DLL; frontend-only — no engine change, no report-schema bump.

## Settled design (the authority — build to it, not to this summary)

- **`data/maintainer-tool/design.md` D45** (committed `6ac2501`) — the functional decision (the five
  settled facts + the rejected alternatives + the two `unverified, probe before building` clauses).
- **`data/maintainer-tool/ui/screens/s04-field-editor.md`** §"Arriving from a failing report row —
  the per-field divergence diff (TRD D45)" + the three new States & variants entries tagged "(TRD
  D45)" — the screen-spec authority for the inline render, the banner, the no-DLL state, the states.
- **`data/maintainer-tool/ui/screens/s08-verification-worklist.md`** §"The Fix flow carries context
  and returns (TRD D41/D42/D45)" — the s08 side (carries `detail` + trigger; the per-field diff
  renders on s04, not s08).

Every step that builds a surface dereferences the screen spec above as its load-bearing authority
(`.claude/rules/spec-conformance.md`); this spec is a pointer, never a replacement.

## The settled facts (verbatim from D45 — the cross-step invariants)

1. **DIFF SOURCE — the in-browser per-kind check, reused.** The diff re-runs the EXISTING s04
   per-kind static check (`frontend/src/editor/verdictCheck.ts` `runVerdictCheck` →
   `extractSurvivalCheck` → `checker`), per the kind-relevant field set (s04 §"Field relevance by
   kind"). NO new check is written.
2. **'ACTUAL' DERIVES FROM THE REPORT'S DLL — the build that diverged.** Not a version-matching DLL.
   The report carries `game_version`; the maintainer links/picks that running build. **Whether each
   kind's existing check behaves correctly against a divergent DLL is `unverified, probe before
   building`; the per-field ATTRIBUTION of `runVerdictCheck`'s one row-level verdict to the specific
   diverged field is `unverified, probe before building` too** — both settled by the Phase-1 probe.
3. **NO-DLL-YET STATE — recorded values + a non-blocking prompt.** On `[Fix ▸]` arrival before a
   suitable DLL is linked (the common case): every RECORDED field value + the engine's prose `detail`
   + a non-blocking "Link the running-game DLL to see what diverged" prompt (prefilled from the
   report's `game_version`). NEVER block the editor (law 4 — the existing s04 "no DLL → advisory,
   authoring proceeds" contract).
4. **PROMINENCE — inline per-field + the summary banner.** Each diverged kind-relevant field shows
   recorded-vs-actual INLINE (recorded value, derived actual, diverged marker glyph+text not
   color-alone — law 7; reusing s04's reserved gutters — law 1); PLUS the existing E5 "What diverged"
   banner (`COPY.divergenceTitle`) EXTENDED to name the diverged field(s) at a glance.
5. **STATES.** no-DLL-linked · DLL-linked-and-diffed · no-divergence-found (the check PASSES against
   the new build — surfaced honestly, never a silent empty) · check-cannot-run (a per-kind
   CannotCheck — `vtable_index`, a function row with no recorded `content_hash` — honest, advisory).

## Cross-step invariants

- **Probe-first ordering is mandatory** (`.claude/rules/incremental-delivery.md` +
  `.claude/rules/results-driven.md`). Phase 1 settles both `unverified` mechanisms (divergent-DLL
  behavior + per-field attribution) BEFORE any attribution layer or UI is built. No step rests on the
  provisional clause unprobed.
- **Frontend-only.** Every change is in the SEPARATE gitignored frontend repo
  (`data/maintainer-tool/frontend/`). The gate is `npm run build` (= `tsc -b && vite build`) +
  `npx vitest run` — NOT `pwsh ./build.ps1`. The kcdx ledger references the FE commit as
  `FE:<hash>`.
- **Reuse, never re-implement.** The diff sits on the EXISTING `runVerdictCheck` /
  `extractSurvivalCheck` / `checker` machinery + the existing `DeepLinkTarget.fixDetail` /
  `FieldEditor.divergenceDetail` carry channel + the existing `matchedBuffer` / PE-parse path. New
  code is the per-field attribution layer + the report's-DLL derivation + the inline render — never a
  second check.
- **Law 4 (advisory, never blocks)** holds throughout — the diff is advisory context; it never gates
  `[Review changes]`. Law 1 (reserved space, no reflow) + law 7 (glyph+text, never color-alone) on
  every new render.
- **No author-facing plugin surface, no game-function target** — this is a maintainer-tool FE screen;
  the disassembler test does not bear (the maintainer picks the DLL, the tool does the byte work).

## Coverage map — every design element → its step

| Design element | Covered by | Notes |
|---|---|---|
| E1 — probe: existing per-kind check behaves correctly against a DIVERGENT (non-version-matching) DLL | Phase 1 / step 1.1 | the `unverified` clause (D45 fact 2); resolved before any consumer |
| E2 — probe: `runVerdictCheck`'s row-level verdict can be ATTRIBUTED to the specific diverged kind-relevant field (signature vs rva for `function`) | Phase 1 / step 1.1 | the attribution `unverified` clause (D45 fact 2 + the §C.4 gate finding) |
| E3 — the per-field attribution layer (one row-level verdict → which kind-relevant field diverged) | Phase 2 / step 2.1 | the `fixDivergence` pure worker; built to the Phase-1 answer |
| E4 — 'actual' derives from the REPORT's DLL (the divergent build) | Phase 2 / step 2.1 (logic) + Phase 3 / step 3.1 (the report-DLL context wiring) | D45 fact 2 |
| E5 — the DLL-link prompt on `[Fix ▸]` arrival, prefilled from `game_version`, non-blocking | Phase 3 / step 3.1 | D45 fact 3; s04 §"The no-DLL-yet state" |
| E6 — inline per-field recorded-vs-actual render (marker glyph+text law 7, reserved gutter law 1) | Phase 3 / step 3.2 | D45 fact 4; s04 §"Per-field recorded-vs-actual" |
| E7 — the "What diverged" banner extended to name the diverged field(s) | Phase 3 / step 3.2 | D45 fact 4; s04 §"The What diverged banner (E5, extended)" |
| E8 — state: no-DLL-linked (recorded + prompt) | Phase 3 / step 3.1 | D45 fact 5; s04 States |
| E9 — state: DLL-linked-and-diffed (inline + banner) | Phase 3 / step 3.2 | D45 fact 5; s04 States |
| E10 — state: no-divergence-found / cannot-check (honest, never silent empty) | Phase 3 / step 3.2 | D45 fact 5; s04 States; AP14 |
| E11 — the `[Fix ▸]`→s04 carry channel extended from prose-only to the per-field-diff context (report `game_version` + failing-row context) | Phase 3 / step 3.1 | s08 Fix-flow paragraph; `DeepLinkTarget.fixDetail` / `FieldEditor.divergenceDetail` |
| E12 — s08 row carries `detail` + trigger; the per-field diff renders on s04 | COVERED-BY-EXISTING | the `fixDetail` channel already carries `detail` (step 3.5 / `e0b2de1`); the sharpened s08 spec only relocates WHERE the diff renders (s04) — no new s08 work. Recorded here so no element is unaccounted. |

Every enumerated design element resolves to a step or to COVERED-BY-EXISTING; no element is silently
dropped and no deferral was made (the two provisional-mechanism clauses are RESOLVED by Phase 1, not
deferred).
