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
- **3.3** (`FE:0b64b6d`) — the frame-state DEEP-LINK mechanism (`deepLink: {openSection?: "lifecycle"|
  "verify", createPrefill?}` carried in the pushed frame; EntityDetail auto-opens the target sub-surface
  ONCE on entry, one-shot). `[Fix ▸]`→s04 REUSES this: the push carries `openSection: "verify"` (land on
  the s04 verify surface) PLUS the failing row's divergence `detail`. Build to the existing mechanism —
  do NOT re-invent it. Extend the deep-link payload only if carrying `detail` needs a field it lacks
  (surface that, don't invent).
- The existing s08 worklist + the s04 field editor (6.1/6.3).
- Independent of 3.4 (both are s08-surface changes touching distinct concerns — order either; landing
  3.4 first keeps the s08 reconciliation coherent before the Fix-flow polish).

## Decisions settled this step (captured — user-approved before build)

Two forks the design left open were surfaced and settled before building (decision-capture):

- **E6 report-survival — KEEP the keep-mounted-and-hide model + add the falsifiable test (NOT the
  literal frame-lift refactor).** App already preserves the s08 report across a `[Fix ▸]` push→`‹ back`
  because `VerificationWorklist` stays MOUNTED (toggled by `hidden`), so law 10 / D31 (report intact,
  no re-import) holds today. 3.5 does NOT refactor the worklist into a controlled frame-state component
  (the doc's "cleaner model" wording) — that is churn on a green, gated surface for zero UX/Capability
  gain. 3.5 ADDS the FALSIFIABLE test asserting `‹ back` from a `[Fix ▸]` restores the SAME report
  (block split + scroll + applied state), no re-import. Rejected: the literal controlled-component
  frame-lift (effort/churn-risk on working code, no cornerstone win — UX is identical either way).
- **E5 detail-carry — EXTEND `DeepLinkTarget` with an optional `fixDetail?: {detail: string}` field.**
  `[Fix ▸]` pushes the s02 frame carrying BOTH `openSection: "verify"` (land on the s04 verify surface,
  reusing 3.3's deep-link applier) AND `fixDetail: {detail}` (the failing row's divergence reason);
  EntityDetail's one-shot applier surfaces the detail at the s04 editor so the maintainer sees WHAT
  diverged without re-checking the worklist (the E5 goal). The routed context rides the SINGLE law-10
  frame channel (the deep-link), consistent with 3.3. Rejected: a separate detail channel on
  EntityDetail (re-introduces the intent-drifts-from-the-frame risk 3.3 designed out by carrying
  everything in one frame-state channel). The step-doc already sanctioned extending the payload "if
  carrying detail needs a field it lacks" — it does.

## Seams consumed from 3.2 (user-approved deferrals this step owns)

3.2 (`FE:6aaaeaa`) built the back-stack primitive but deferred two s08/s04-specific seams to HERE,
because this step wires the `[Fix ▸]` push (s08→s04) that first makes them load-bearing:
- **s08's parsed report lifted INTO the frame's carried state.** 3.2 preserved report survival across
  a push→`‹ back` excursion via the kept-mounted-and-toggle model (the report survives because
  `VerificationWorklist` stays mounted) — the behavior law 10/D31 requires works today. This step lifts
  the report state into the nav-store frame (making the worklist a controlled component) so the frame
  literally carries it, the cleaner law-10 model, as `[Fix ▸]` builds the s08→s04 push chain that needs
  it. FALSIFIABLE test: `‹ back` from the fix restores the SAME report (asserted via carried frame
  state, not just kept-mounted survival).
- **The guard's Save + Discard runners for a dirty s04 pushed from `[Fix ▸]`.** 3.2's guard intercepts
  a dirty navigate-away but registers no Save/Discard runner (Save is a safe no-op-stay until wired —
  never a silent navigate, D44/AP14). This step registers the s04 save spine (the s06 confirm) +
  a discard (clear the dirty s04 edits) with the nav store so the guard's Save/Discard are truthful
  on the `[Fix ▸]`→dirty-s04→`‹ back` path. (3.3 owns the same for a dirty s02 lifecycle.)

## Note for the linked-Bin-install-source step (not this step)

3.2's D43 version-new selector is built + tested (the dropdown OFFERS a linked-Bin-resolved-new version,
marked "from your linked Bin · new", selectable). The s02 spec's "auto-SELECT the new version on
resolve" (s02 line ~176) is deliberately NOT implemented yet — `linkedBinVersion` is always null today
(no step threads the linked-Bin install source into s02), and auto-select-on-resolve is in tension with
law 3. The auto-select lands with the (separate) step that wires the s02 linked-Bin install source.

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
