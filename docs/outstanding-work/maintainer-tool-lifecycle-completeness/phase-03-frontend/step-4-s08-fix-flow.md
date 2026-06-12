# 3.4 [FE] The s08 Fix-flow completeness — detail to s04 + return path + applied value (E5, E6, E7)

## What

Complete the s08 worklist's `[Fix ▸]` flow per D41: (E5) `[Fix ▸]` carries the failing row's divergence
`detail` (the engine's reason, e.g. *"on-disk body hash mismatch: build diverged from the recorded
version"*) to the s04 field editor so the maintainer sees WHAT diverged without re-checking the
worklist; (E6) `[Fix ▸]` gains a RETURN PATH back to the worklist with the imported report intact (the
report's client-side state survives the Fix excursion — no one-way dead-end, since s08 and s02/s04 are
peer content screens); (E7) an applied row (after a confirmed close/verify) shows its RESULTING value
(the new `valid_through`), not only an "applied" marker. In the SEPARATE frontend repo.

## Scope

One commit in the frontend repo (`data/maintainer-tool/frontend/src/worklist/` + the navigation seam):
- `VerificationWorklist.tsx` + `App.tsx` (the s08↔s04 navigation) — `[Fix ▸]` passes the row's `detail`
  to the s04 editor (the editor surfaces it as the divergence context); a return path from s04 back to
  s08 preserves the imported report (the report state is not dropped on navigate-away); an applied row
  renders its resulting `valid_through` value.

Does NOT change the reconciliation display (3.3) or the resolver (1.1).

## Test bar

Vitest component tests: clicking `[Fix ▸]` on a failing row navigates to s04 CARRYING the row's `detail`
(assert the divergence reason reaches the s04 surface); returning from s04 lands back on the s08 worklist
with the imported report intact (not lost); an applied row shows its resulting `valid_through` value (not
just an "applied" glyph). **FALSIFIABLE:** a `[Fix ▸]` that drops the `detail` (s04 shows no divergence
context) fails; a return that loses the report fails; an applied row showing no resulting value fails.
Gate: `npm run build` exit 0 + `vitest run` green. Runnable AT this step (the s08 worklist + s04 editor +
the navigation seam exist).

## Dependencies

- The existing s08 worklist + the s04 field editor + the s08↔s04 navigation seam (6.1/6.3).
- Independent of 3.3 (both are s08-surface changes, but they touch distinct concerns — order either,
  landing 3.3 first keeps the s08 reconciliation coherent before the Fix-flow polish).

## Reference

[`../plan-spec.md`](../plan-spec.md) — E5, E6, E7.

## Design authority

`data/maintainer-tool/ui/screens/s08-verification-worklist.md` §"The Fix flow carries context and
returns" + the Contents `[Fix ▸]` row. Build to THIS screen spec, not this doc's summary.

## UX

Carried from the s08 screen spec (`.claude/rules/ux-first-class.md`):
- **Fix carries context** — `[Fix ▸]` takes the maintainer to s04 WITH the divergence `detail` shown
  (what to fix is visible at the editor, not lost on navigation).
- **Fix returns** — a `‹ back` / return path lands back on the worklist with the imported report intact
  (no one-way dead-end; the maintainer continues the worklist).
- **Applied value visible** — after a confirmed close/verify, the row shows its resulting value (the new
  `valid_through`), so the maintainer sees what the action produced, not just that it "applied".

## Disassembler-test / author-burden

None — a maintainer-tool FE screen; no author-facing plugin input, no game-function target.
