# 3.2 [FE] The s09 per-row resolution actions — navigate to the canonical resolve flow (E11)

## What

Wire the s09 view's per-row resolution actions onto the rows 3.1 rendered: each kind's
kind-appropriate buttons that NAVIGATE to the EXISTING canonical resolve flow (s09 reimplements no
editing surface — law 6). Uncovered orphan row → `[Author successor ▸]` (→ s05 create-new-version,
prefilled from the closed row) / `[Deprecate ▸]` / `[Supersede ▸]` (→ s02 lifecycle editor);
Never-verified row → `[Verify ▸]` (→ s04 field editor's verify surface); Broken-reference row →
`[Fix reference ▸]` (→ s02 lifecycle editor). Each out-link carries a RETURN PATH back to s09, and a
resolved entity DROPS OFF the s09 list in place on return (the view never auto-navigates on a
resolution — law 3; the list re-queries `/needs-action` and the now-complete entity no longer
qualifies). In the SEPARATE frontend repo.

## Scope

One commit in the frontend repo (`data/maintainer-tool/frontend/src/`):
- The s09 view component (3.1's `NeedsActionView.tsx`) — add the per-row action buttons (kind-keyed),
  each firing the navigation intent to the existing s02/s04/s05 flow (reuse the existing navigation
  seam `App.tsx` uses for s08's `[Fix ▸]` → `select_entity` and the s05 create-new-version entry).
- The return-to-s09 path + the drop-off-in-place (re-query `/needs-action` on return; the resolved
  entity falls off).

Does NOT reimplement any editing surface (s02/s04/s05 own resolution); does NOT change s08.

## Test bar

Vitest component tests: each kind's action button fires the correct navigation intent — `[Author
successor]` → the s05 create-new-version entry (prefilled with the orphaned row); `[Deprecate]` /
`[Supersede]` / `[Fix reference]` → the s02 lifecycle editor for that entity; `[Verify]` → the s04
verify surface. On return from a resolve flow, the view re-queries and a resolved entity drops off the
list (mock the `/needs-action` response shrinking). **FALSIFIABLE:** an action that navigates to the
WRONG flow (e.g. `[Verify]` → s02) fails; the view must NOT auto-navigate on a resolution (law 3 — it
updates in place). Gate: `npm run build` exit 0 + `vitest run` green. Runnable AT this step (3.1's view
+ the existing s02/s04/s05 navigation seam exist).

## Dependencies

- **3.1** — the s09 view shell + rows the actions attach to.
- The existing s02/s04/s05 navigation seam + the s05 create-new-version entry (the canonical flows the
  actions route to; they already exist — verified by the s09 soundness gate).

## Reference

[`../plan-spec.md`](../plan-spec.md) — E11.

## Design authority

`data/maintainer-tool/ui/screens/s09-needs-action.md` §"Contents" (rows 35–46 — each action's target
flow) + §"Links in / out" (the return path) + law 2/3/6 (the actions route to the canonical editor,
return to s09, drop off in place). Build to THIS screen spec, not this doc's summary.

## UX

Carried from the s09 screen spec (`.claude/rules/ux-first-class.md`):
- **Populated** — each row shows its kind-appropriate action button(s); the maintainer clicks one and
  is taken to the canonical resolve flow.
- **The resolution flow + return** — the action navigates to s02/s04/s05; a `‹ back` returns to s09
  (the report/list state preserved — law 2/3); the resolved entity drops off the list in place; the
  view never auto-navigates (law 3).
- **Disabled** — a per-row action is disabled (more than color — law 7) when its resolve flow is
  unavailable in a degraded state (e.g. the DB read seam down).
- **Feedback** — the drop-off-on-return IS the feedback that the resolution landed (the entity is no
  longer incomplete); the all-clear state appears when the last gap is resolved.

## Disassembler-test / author-burden

None — a maintainer-tool FE screen; no author-facing plugin input, no game-function target.
