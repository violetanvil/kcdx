# 3.1 [FE] The s09 view shell + detection display + the s01 affordance + all states (E10, E12, E13, E14)

## What

Build the new s09 Needs-action view: the screen shell (peer content screen in the right pane / phone
drill-in), the header (`Needs action · N entities · at version <V>`), the three by-kind collapsible
sections (Uncovered at current version / Never verified / Broken references — each rendering its
entities + the specific gap), consuming the Phase-2 `/needs-action` endpoint (2.1). Plus the s01
navigator's `[Needs action ▸ N]` affordance + count badge (un-badged at 0), and ALL the view's states.
The per-row RESOLUTION ACTIONS are step 3.2 (this step renders the rows + the gap; the action buttons
wire next). In the SEPARATE frontend repo.

## Scope

One commit in the frontend repo (`data/maintainer-tool/frontend/src/`):
- A new screen component (e.g. `src/needs-action/NeedsActionView.tsx` + `.module.css`) — the shell,
  header, the three collapsible sections (reuse the existing collapsible-section primitive the s08
  worklist's no-action block uses), the entity rows (id · name · gap), consuming a new client method
  for `/needs-action`.
- `src/api/client.ts` + `src/api/types.ts` — the `/needs-action` client method + response types
  (matching the 2.1 backend wire shape).
- `src/App.tsx` — wire s09 as a peer content screen (the shell-routing seam, like s08); the s01
  navigator (`EntityNavigator.tsx`) gains the `[Needs action ▸ N]` affordance + `Badge` count.

Does NOT wire the per-row resolution actions (3.2) or change s08 (3.3/3.4).

## Test bar

Vitest unit/component tests (frontend repo): the view renders the three by-kind sections from a mocked
`/needs-action` response (each kind's entities + gap); the **states** — populated (sections, N>0 expanded
/ N=0 collapsed-with-"none"), **empty=all-clear** (the success-framed copy when all kinds are 0),
**loading** (the in-flight indicator), **error** (the named-cause copy + retry); the s01 `[Needs action ▸ N]`
affordance renders the count + is un-badged at 0. **FALSIFIABLE:** the all-clear state must show the
success-framed copy (not a neutral empty) when N=0; the badge must be absent at 0. Gate: `npm run build`
exit 0 + `vitest run` green. Runnable AT this step (the 2.1 endpoint exists to mock against).

## Dependencies

- **2.1** — the `/needs-action` endpoint this view binds to (built before so the FE drives a real
  endpoint; tests mock its response).
- The existing collapsible-section primitive + the s08/s01 shell-routing pattern.

## Reference

[`../plan-spec.md`](../plan-spec.md) — E10, E12, E13, E14.

## Design authority

`data/maintainer-tool/ui/screens/s09-needs-action.md` §"Region & position" + §"Contents" (the shell,
header, by-kind sections, the s01 affordance) + §"States & variants" (empty=all-clear / loading / error /
disabled / edge) + §"Applicable laws". Build the pixels/interaction to THIS screen spec, not this doc's
summary. The Layer-1 tokens/laws: `data/maintainer-tool/ui/design.md` (the `success`/`warning` tokens, the
collapsible-section primitive, laws 1/2/3/4/7/9).

## UX

Carried from the s09 screen spec (`.claude/rules/ux-first-class.md` — not invented):
- **Populated** — the header + the three collapsible kind sections; a section with N>0 expanded (the
  work surface), N=0 collapsed with a `0` count + muted "none" (surfaced, never hidden — law 4).
- **Empty = all-clear (the GOAL)** — when every kind is 0: *"Every entity's lifecycle is complete at
  version `<V>` — nothing needs action."* in the `success` token (an accomplishment, not a blank pane);
  the s01 badge un-badged.
- **Loading** — the fetch-in-flight indicator, resolving in place (law 1).
- **Error** — *"Couldn't compute the needs-action set: `<reason>`."* (system-caused copy) + retry.
- **Edge** — a long name / gap wraps in its cell (law 1); a long list scrolls while the header pins;
  phone = a full-screen drill-in.
- **Consistency** — reuses the design-system collapsible-section + the peer-content-screen shell pattern
  the s08 worklist established.

## Disassembler-test / author-burden

None — a maintainer-tool FE screen; no author-facing plugin input, no game-function target.
