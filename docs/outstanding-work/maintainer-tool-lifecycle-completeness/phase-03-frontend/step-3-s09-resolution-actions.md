# 3.3 [FE] The s09 per-row resolution actions — navigate to the canonical resolve flow (E11)

## What

Wire the s09 view's per-row resolution actions onto the rows 3.1 rendered: each kind's
kind-appropriate buttons that **PUSH** the EXISTING canonical resolve flow onto the **3.2 content
back-stack** as a state-carrying frame (law 10; s09 reimplements no editing surface — law 6).
Uncovered orphan row → `[Author successor ▸]` (→ s05 create-new-version, prefilled from the closed
row) / `[Deprecate ▸]` / `[Supersede ▸]` (→ s02 lifecycle editor); Never-verified row → `[Verify ▸]`
(→ s04 field editor's verify surface); Broken-reference row → `[Fix reference ▸]` (→ s02 lifecycle
editor). Each resolve-action PUSHES (it does not reset — a resolve carries context, law 10), so
`‹ back` returns to THIS s09 view with its state restored (section toggles + scroll), and a resolved
entity DROPS OFF the s09 list in place on return (the view never auto-navigates on a resolution —
law 3; the list re-queries `/needs-action` and the now-complete entity no longer qualifies). In the
SEPARATE frontend repo.

## Scope

One commit in the frontend repo (`data/maintainer-tool/frontend/src/`):
- The s09 view component (3.1's `NeedsActionView.tsx`) — add the per-row action buttons (kind-keyed),
  each firing a PUSH of the resolve flow (s02/s04/s05) onto the 3.2 back-stack as a state-carrying
  frame (the s09 frame carries its section toggles + scroll so `‹ back` restores them).
- The drop-off-in-place on return (re-query `/needs-action` when the resolve frame pops back to s09;
  the resolved entity falls off).

Does NOT build the back-stack primitive (3.2 owns it — this CONSUMES it); does NOT reimplement any
editing surface (s02/s04/s05 own resolution); does NOT change s08.

## Test bar

Vitest component tests: each kind's action button fires the correct navigation intent — `[Author
successor]` → the s05 create-new-version entry (prefilled with the orphaned row); `[Deprecate]` /
`[Supersede]` / `[Fix reference]` → the s02 lifecycle editor for that entity; `[Verify]` → the s04
verify surface. On return from a resolve flow, the view re-queries and a resolved entity drops off the
list (mock the `/needs-action` response shrinking). **FALSIFIABLE:** an action that navigates to the
WRONG flow (e.g. `[Verify]` → s02) fails; a resolve-action that RESETS the stack instead of pushing
(losing the s09 frame so `‹ back` can't return to it) fails; the view must NOT auto-navigate on a
resolution (law 3 — it updates in place). Gate: `npm run build` exit 0 + `vitest run` green. Runnable
AT this step (3.1's view + the 3.2 back-stack primitive + the existing s02/s04/s05 screens exist).

## Dependencies

- **3.2** — the content back-stack primitive (push/reset/`‹ back`/state-carrying frames); the resolve
  actions PUSH onto it. Hard prerequisite — without it there is no stack to push onto.
- **3.1** — the s09 view shell + rows the actions attach to.
- The existing s02/s04/s05 resolve screens + the s05 create-new-version entry (the canonical flows the
  actions route to; they already exist — verified by the s09 soundness gate).

## Reference

[`../plan-spec.md`](../plan-spec.md) — E11.

## Design authority

`data/maintainer-tool/ui/screens/s09-needs-action.md` §"Contents" (rows 35–46 — each action's target
flow) + §"Links in / out" (the PUSH + the `‹ back` return path) + law 2/3/6/10 (the actions PUSH the
canonical editor onto the back-stack, `‹ back` restores s09, the resolved entity drops off in place).
Build to THIS screen spec, not this doc's summary.

## UX

Carried from the s09 screen spec (`.claude/rules/ux-first-class.md`):
- **Populated** — each row shows its kind-appropriate action button(s); the maintainer clicks one and
  is taken to the canonical resolve flow.
- **The resolution flow + return** — the action PUSHES s02/s04/s05 onto the back-stack; a `‹ back`
  returns to s09 with its state restored (section toggles + scroll — law 10); the resolved entity drops
  off the list in place; the view never auto-navigates (law 3).
- **Disabled** — a per-row action is disabled (more than color — law 7) when its resolve flow is
  unavailable in a degraded state (e.g. the DB read seam down).
- **Feedback** — the drop-off-on-return IS the feedback that the resolution landed (the entity is no
  longer incomplete); the all-clear state appears when the last gap is resolved.

## Disassembler-test / author-burden

None — a maintainer-tool FE screen; no author-facing plugin input, no game-function target.
