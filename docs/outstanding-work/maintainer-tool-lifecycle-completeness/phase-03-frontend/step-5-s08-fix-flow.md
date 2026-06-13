# 3.5 [FE] The s08 Fix-flow completeness — detail to s04 + return path + applied value (E5, E6, E7)

## What

Complete the s08 worklist's `[Fix ▸]` flow per D41/D42: (E5) `[Fix ▸]` carries the failing row's
divergence `detail` (the engine's reason, e.g. *"on-disk body hash mismatch: build diverged from the
recorded version"*) to the s04 field editor so the maintainer sees WHAT diverged without re-checking the
worklist; (E6) `[Fix ▸]` **PUSHES s02/s04 onto the 3.2 back-stack as a state-carrying frame** (law 10) —
the s08 frame stores the ingested report (parsed worklist + block split + scroll), so `‹ back` restores
it EXACTLY with no re-import (the report is client-side only — D31 — so the carried frame, not a
re-fetch, is what preserves it); a `[Fix ▸]` that left s04 dirty surfaces the unsaved-changes guard
(Save/Discard/Cancel, D44) before navigating back; (E7) an applied row (after a confirmed close/verify)
shows its RESULTING value (the new `valid_through`), not only an "applied" marker. In the SEPARATE
frontend repo.

## Scope

One commit in the frontend repo (`data/maintainer-tool/frontend/src/worklist/`):
- `VerificationWorklist.tsx` — `[Fix ▸]` PUSHES s02/s04 onto the 3.2 back-stack, building the s08 frame
  with the ingested-report state carried, and passes the row's `detail` to the s04 editor (the editor
  surfaces it as divergence context); an applied row renders its resulting `valid_through` value.
- The s08 frame's carried state (the parsed report + block split + scroll) so 3.2's `‹ back` restores it
  with no re-import; the dirty-editor guard (3.2's) fires on a dirty-s04 `‹ back`.

Does NOT build the back-stack primitive (3.2 owns push/reset/`‹ back`/state-carry/the guard — this
CONSUMES it); does NOT change the reconciliation display (3.4) or the resolver (1.1).

## Test bar

Vitest component tests: clicking `[Fix ▸]` on a failing row PUSHES s04 CARRYING the row's `detail`
(assert the divergence reason reaches the s04 surface); `‹ back` from s04 restores the s08 worklist with
the imported report intact — no re-import (assert the carried report state, not a fresh empty re-mount);
an applied row shows its resulting `valid_through` value (not just an "applied" glyph). **FALSIFIABLE:**
a `[Fix ▸]` that drops the `detail` (s04 shows no divergence context) fails; a `‹ back` that re-mounts
s08 empty (loses the report, forcing a re-import) fails; an applied row showing no resulting value fails.
Gate: `npm run build` exit 0 + `vitest run` green. Runnable AT this step (the 3.2 back-stack + the s08
worklist + s04 editor exist).

## Dependencies

- **3.2** — the back-stack primitive (push, state-carrying frames, `‹ back` restore, the dirty-editor
  guard); `[Fix ▸]` PUSHES onto it and the report-intact return IS the state-restore it provides. Hard
  prerequisite.
- The existing s08 worklist + the s04 field editor (6.1/6.3).
- Independent of 3.4 (both are s08-surface changes touching distinct concerns — order either; landing
  3.4 first keeps the s08 reconciliation coherent before the Fix-flow polish).

## Reference

[`../plan-spec.md`](../plan-spec.md) — E5, E6, E7.

## Design authority

`data/maintainer-tool/ui/screens/s08-verification-worklist.md` §"The Fix flow carries context and
returns" + §"Returned from a `[Fix ▸]`" + §"Unsaved-changes guard" + the Contents `[Fix ▸]` row + the
`‹ back` row + law 10. Build to THIS screen spec, not this doc's summary.

## UX

Carried from the s08 screen spec (`.claude/rules/ux-first-class.md`):
- **Fix carries context** — `[Fix ▸]` PUSHES s04 WITH the divergence `detail` shown (what to fix is
  visible at the editor, not lost on navigation).
- **Fix returns** — `‹ back` (3.2's back affordance, labeled "‹ back to the report") restores the
  worklist with the imported report intact, no re-import (no one-way dead-end; the maintainer continues
  the worklist). A dirty-s04 `‹ back` surfaces the unsaved-changes guard first (D44).
- **Applied value visible** — after a confirmed close/verify, the row shows its resulting value (the new
  `valid_through`), so the maintainer sees what the action produced, not just that it "applied".

## Disassembler-test / author-burden

None — a maintainer-tool FE screen; no author-facing plugin input, no game-function target.
