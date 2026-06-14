# 3.1 [FE] Carry the report's-DLL context + the no-DLL link prompt (E4-wiring, E5, E8, E11)

## What

Extend the `[Fix ▸]`→s04 carry channel so s04 can derive 'actual' from the REPORT's divergent DLL,
and render the no-DLL-yet state. Today the channel carries only the prose `detail`
(`DeepLinkTarget.fixDetail: {detail}` → `EntityDetail` one-shot applier → `FieldEditor.divergenceDetail`,
the E5 prose banner from step 3.5 / `e0b2de1`). This step:
- **(E11) extends the carry channel** to also bring the report's `game_version` + the failing-row
  context s04 needs to derive 'actual' from the report's DLL (the divergent build, D45 fact 2) — riding
  the SAME single law-10 deep-link frame channel, consistent with the existing `fixDetail` (no new
  channel — the same decision 3.5/E5 made).
- **(E5/E8) renders the no-DLL-yet state** — on `[Fix ▸]` arrival before a suitable DLL is linked
  (the common case, since the divergent build is new): every recorded field value + the prose `detail`
  + a NON-BLOCKING "Link the running-game DLL to see what diverged" prompt, prefilled from the report's
  `game_version`. NEVER block the editor (law 4 — the existing s04 "no DLL → advisory, authoring
  proceeds" contract). The per-field "actual" fills in once the DLL is picked (3.2 renders the diffed
  result; this step delivers the recorded + prompt baseline + the wired DLL context the diff will use).

This is the wiring half — the diff context reaches s04 and the no-DLL baseline renders; the inline
diff render + the extended banner are 3.2.

## Scope

One commit in the frontend repo, across the carry path:
- `frontend/src/detail/EntityDetail.tsx` — `DeepLinkTarget.fixDetail` extended to carry the report's
  `game_version` + the failing-row context (alongside the existing `detail`); the one-shot applier
  threads it to the FieldEditor (extending the existing `divergenceDetail` wiring, not a new channel).
- `frontend/src/worklist/VerificationWorklist.tsx` — `[Fix ▸]` includes the report's `game_version` +
  the row context in the pushed frame's `fixDetail` (the report is already in scope on s08 — it carries
  `game_version` per the v3 schema).
- `frontend/src/editor/FieldEditor.tsx` — render the no-DLL-yet state: the recorded values (already
  rendered) + the non-blocking DLL-link prompt prefilled from the carried `game_version` (reusing the
  existing DLL-link affordance / `matchedBuffer` path; the prompt is advisory, law 4 — it never gates
  `[Review changes]`).

Does NOT render the inline per-field diff or the extended banner (3.2); does NOT build the
`fixDivergence` worker (2.1 — consumed here for the context plumbing, rendered in 3.2); does NOT
change the back-stack primitive (3.2 of the lifecycle-completeness plan owns push/`‹ back`).

## Test bar

Vitest component tests, runnable AT this step (the carry channel + the FieldEditor + the worker
exist):
- `[Fix ▸]` on a `failed` row pushes s04 carrying the report's `game_version` + the row context
  (assert the carried frame state, not just the prose `detail`).
- On arrival with NO DLL linked: s04 renders every recorded field value + the prose `detail` + the
  "Link the running-game DLL" prompt prefilled from the carried `game_version`; `[Review changes]` is
  NOT gated by the missing DLL (law 4 — the editor is usable).

**FALSIFIABLE:** a `[Fix ▸]` that drops the `game_version` (s04 cannot derive which build is the
report's) fails; a no-DLL arrival that blocks the editor / disables `[Review changes]` on the missing
DLL fails (law 4 violation); a prompt NOT prefilled from the report's `game_version` (the maintainer
must hand-type the version) fails. Gate: `npm run build` exit 0 + `npx vitest run` green.

## Dependencies

- **2.1** — the `fixDivergence` worker (the context this step carries is what 3.2 feeds to the
  worker; this step plumbs the `game_version` + DLL context the worker needs). Ordered before the
  render (3.2) so the diff has its inputs.
- The existing `DeepLinkTarget.fixDetail` / `EntityDetail` one-shot applier / `FieldEditor.divergenceDetail`
  channel (step 3.5 / `e0b2de1`) — extended, not replaced.
- The existing DLL-link affordance + `matchedBuffer` / PE-parse path (the verification-engine Phase 2)
  — reused for the prompt + the bytes.

## Reference

[`../plan-spec.md`](../plan-spec.md) — E4 (wiring), E5, E8, E11; the settled facts (fact 2 — the
report's DLL; fact 3 — the non-blocking no-DLL state); the "reuse, never re-implement" invariant.

## Design authority

`data/maintainer-tool/ui/screens/s04-field-editor.md` §"'Actual' derives from the REPORT's DLL" +
§"The no-DLL-yet state (law 4)" + the States & variants "Arrived from a failing `[Fix ▸]` — no DLL
linked" entry, AND `data/maintainer-tool/ui/screens/s08-verification-worklist.md` §"The Fix flow
carries context and returns" (the s08 side carries `detail` + the report context). Build to THESE
screen specs, not this doc's summary.

## UX

Carried from the s04 screen spec (`.claude/rules/ux-first-class.md`):
- **No-DLL-yet (the common arrival)** — recorded values + the engine's prose reason + a non-blocking
  "Link the running-game DLL to see what diverged" prompt, prefilled from the report's `game_version`.
  The maintainer always sees WHAT they're fixing and the recorded baseline; the editor is never gated
  on the DLL (law 4). The prefill is the maintainer-side disassembler-test (the tool reads the version
  from the report, the maintainer never hand-types it).
- **Empty/loading/error/disabled/edge** — the prompt's absent-DLL state IS the empty-actual state
  (recorded shown, actual pending); a malformed/unreadable linked DLL surfaces the existing
  advisory; `[Review changes]` enablement is unchanged (law 4 — the DLL never disables it).

## Disassembler-test / author-burden

None — a maintainer-tool FE screen; no author-facing plugin input, no game-function target. (The
DLL-link prefill from the report's `game_version` is the maintainer-side spirit of the test — the
tool resolves the version from the artifact, the maintainer never hand-types it.)
