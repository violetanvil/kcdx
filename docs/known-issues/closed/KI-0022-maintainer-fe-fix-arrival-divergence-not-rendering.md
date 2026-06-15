# maintainer-tool FE: D45 divergence diff does not render on a real `[Fix ▸]` arrival (build green, 582/582 passing)

**Status:** Closed
**Closed:** 2026-06-14
**Closed by commit:** FE:7d37417

**Component:** the maintainer-tool frontend repo (`data/maintainer-tool/frontend/`, the separate
gitignored nested repo). The `[Fix ▸]` → s04 deep-link carry path (`VerificationWorklist.tsx` →
`App.tsx` `pushEntityFromWorklist` → `EntityDetail.tsx` deep-link applier → `FieldEditor`). NOT the
kcdx engine.

## Symptom (user-reproduced, screenshot evidence)

User uploaded a verification report, clicked `[Fix ▸]` on a failed item, and landed on s04 for
`IsInCombat_callsite_26b` (kcdx_id 7, kind=callsite, version 1.5.1164953, verified 2026-05-28,
evidence live_test_plugin). NONE of the D45 render appeared:

- NO "What diverged" banner (the E5 prose banner, `COPY.divergenceTitle`).
- NO "Link the running-game DLL to see what diverged" prompt prefilled with the report's
  `game_version` (step 3.1's no-DLL state).
- Instead the GENERIC "Not verified against a game DLL." advisory + the generic "Verify against a DLL
  → Link the game Bin folder" section; the "Targeted game version" dropdown reads "Pick a game
  version" / "no folder linked" (NOT prefilled).
- The "Verify against a DLL" section IS expanded — so `openSection: "verify"` from the deep-link DID
  apply, but the `fixDetail` half (banner + no-DLL prompt) did not.

Conclusion from the symptom: `divergenceDetail` (=`fixDivergence`) AND `reportGameVersion`
(=`fixGameVersion`) are both falsy at `FieldEditor` on arrival — the `deepLink.fixDetail` object never
reached / never applied, even though `openSection` from the SAME deep-link did apply. Build is green
and `npx vitest run` is 582/582 — this is a RUNTIME wiring defect the passing test suite does not
catch (build-green is necessary, not sufficient).

## Static trace (done — reads correct end-to-end; this is why runtime probing is owed)

- `src/worklist/VerificationWorklist.tsx`: `[Fix ▸]` onClick (~914) → `onFixRow?.(row.kcdx_id, detail)`
  → `fixRowForBlock` (~464) injects `report.game_version` → `onFixRow(kcdxId, detail, report.game_version)`.
  `fixRowForBlock` correctly closes over `report.game_version` and is wired to both action blocks
  (~588, ~615).
- `src/App.tsx`: `pushEntityFromWorklist` (~270) builds
  `pushFrame("entity-detail", { kcdxId, deepLink: { openSection: "verify", fixDetail: { detail, gameVersion } } })`;
  wired `onFixRow={pushEntityFromWorklist}` (~467).
- `src/detail/EntityDetail.tsx`: the deep-link applier `useEffect` (guarded by `appliedDeepLinkRef`,
  ~489) applies `openSection` (491: `setVerifyOpen(true)`) AND `fixDetail` (498-502:
  `setFixDivergence(deepLink.fixDetail.detail)` + `setFixGameVersion(deepLink.fixDetail.gameVersion)`);
  threads `divergenceDetail={fixDivergence}` (1104) + `reportGameVersion={fixGameVersion}` (1110) to
  `<FieldEditor>`. A reset effect (~430-431) `setFixDivergence(null)` on entity change.

The static path is complete, yet `openSection` applies and `fixDetail` does not — a runtime
discrepancy that only a live observation resolves.

## Hypotheses (UNVERIFIED — probe, do not act on inference)

- **H-a** — the report row's `detail` is empty, or the pushed `fixDetail` object is malformed, so the
  push carries no usable context.
- **H-b** — the one-shot `appliedDeepLinkRef` applier consumes the deep-link before `fixDetail` is
  populated, or an ordering/guard issue applies `openSection` but skips the `fixDetail` branch.
- **H-c** — the entity-change reset effect (~430-431 `setFixDivergence(null)`) runs AFTER the applier
  sets it, clearing the state the applier just wrote.
- **H-d** — the screenshot's `[Fix ▸]` came via a path whose `onFixRow` is NOT `fixRowForBlock` (a
  block/row whose handler is the narrow 2-arg form), so `gameVersion` is undefined → `fixDetail` push
  is partial/absent. (Note: the row shows verified+evidence yet sat in a block the user could `[Fix ▸]`
  — worth confirming WHICH block fired.)

## Investigation trail (probes — one variable each, outcome written only after it runs)

| # | Probe (one variable) | Status | Outcome |
|---|---|---|---|
| P1 | Log the deep-link at the EntityDetail applier + the `[Fix ▸]` push + the reset effect (read-only, one reload, all three points): is `fixDetail` present/populated, and what is the applier-set vs reset order? | **RAN** | **H-c CONFIRMED.** Console (in order): (1) `[Fix] push` — `detail` + `gameVersion` BOTH populated (`detail="on-disk body hash mismatch: build diverged…"`, `gameVersion="release_1_5_1164953_841"`) → push side SOUND (H-a/H-d eliminated). (2) `RESET effect ran` (kcdxId=7). (3) `applier ran` + `applier SET fixDivergence/fixGameVersion` with the correct values. (4) **`RESET effect ran` AGAIN (kcdxId=7) — AFTER the applier SET.** The reset effect fires TWICE and its 2nd run lands AFTER the applier, nulling `fixDivergence`/`fixGameVersion` the applier just set. `verifyOpen` (set by the same applier, NO reset) survives → section expanded, banner+prompt gone (the exact symptom). |
| P2 | (was gated on P1 push-side) — NOT NEEDED: P1 showed the push carries `detail` + `gameVersion` correctly. Push side eliminated. | SKIPPED | Push-side confirmed sound by P1. |
| P3 | (was gated on P1 apply-side) — SUBSUMED by P1: the applier-set-vs-reset order was captured in the same P1 reload. The reset's 2nd fire after the applier is the loss. | SUBSUMED | See P1. Remaining sub-question: WHY does the kcdxId-keyed reset effect fire twice (StrictMode double-invoke in dev, or a 2nd commit where kcdxId/reloadKey settles) — being pinned before the fix design (AP17 mechanism). |

P1 is the discriminating first observation: it splits push-side (P2: H-a/H-d) from apply-side
(P3: H-b/H-c) in one read. Outcome→meaning:
- `fixDetail` present + both fields populated at the applier → push is fine; the loss is apply-side or
  downstream → P3.
- `fixDetail` absent / `gameVersion` undefined at the applier → the push dropped it → P2.

## Root cause (pinned — mechanism, AP17-grade)

`fixDivergence` + `fixGameVersion` are WRITTEN by the deep-link applier effect (deps include
`deepLink`, EntityDetail.tsx ~486) but RESET to `null` by a SEPARATE entity-load effect (deps
`[kcdxId, reloadKey]`, ~470, the reset at lines 431/434). The two effects are independent and
unordered. On a `[Fix ▸]` arrival both run for the same commit; `<StrictMode>` (main.tsx:22) is ON, so
in dev React double-invokes effects (mount → cleanup → re-mount) — and the reset effect's SECOND
invocation lands AFTER the applier's set (P1 console: push → RESET → applier-SET → **RESET again**),
nulling the values the applier just wrote. `verifyOpen` (set by the same applier, with NO reset
effect) survives → the "Verify" section is expanded but the banner + no-DLL prompt are gone (the exact
symptom).

The defect is the SPLIT state lifecycle: the divergence state's correctness depends on effect ORDERING
between two independent effects, which React does not guarantee and StrictMode actively exercises.
StrictMode did not cause the bug — it surfaced a latent ordering-dependency that would also fire on any
production re-commit of the entity-load effect after the applier. The two divergence fields are
uniquely exposed because the reset nulls EXACTLY them and nothing resets `verifyOpen`.

Falsifiable: remove the `setFixDivergence(null)`/`setFixGameVersion(null)` from the kcdxId-keyed reset
effect (or make the divergence state not depend on inter-effect order) → the applier's set survives →
the banner + prompt render. (The fix design is Gate-A'd — it touches load-bearing wiring across
EntityDetail; the reset's legitimate purpose, clearing a PRIOR entity's stale divergence on a genuine
entity change, must be preserved without clobbering a fresh same-arrival set.)

## Resolution

**Root cause:** `fixDivergence` + `fixGameVersion` (the s08 `[Fix ▸]` divergence context) were written by
the `[deepLink]`-keyed applier effect in `EntityDetail.tsx` but nulled by a SEPARATE `[kcdxId, reloadKey]`-keyed
entity-load effect (the two `setFix*(null)` resets). The two effects are independent and unordered. On a
`[Fix ▸]` arrival both run for the same commit; `<StrictMode>` (main.tsx) double-invokes effects in dev,
and the load effect's second invocation landed AFTER the applier's set — nulling the just-written values
(P1 console, verbatim order: push → RESET → applier-SET → RESET-again). `verifyOpen` (set by the same
applier, with NO reset writer) survived → the "Verify" section expanded but the banner + no-DLL prompt
vanished. The defect was the SPLIT, order-dependent state lifecycle: correctness depended on effect
ordering between two independent effects, which React does not guarantee and StrictMode actively exercises
(a production re-commit of the load effect after the applier would trip it identically — StrictMode
surfaced a latent bug, it did not cause one).

**Fix (Option 3, single-owner applier — user-approved, architect-reviewed Gate A):** made one dedicated
effect the SOLE writer of `fixDivergence` + `fixGameVersion`. It SETS them from `deepLink.fixDetail` on a
`[Fix ▸]` arrival (one-shot per deepLink object via `fixDivergenceRef`) and CLEARS them on a genuine entity
change (a `kcdxId` differing from `fixDivergenceEntityRef`, with no fresh `fixDetail`). The two reset lines
were removed from the entity-load effect — no second effect writes these fields, so there is no inter-effect
ordering to race. The `‹`-back-same-entity persist is preserved (re-entry with `deepLink===null` AND the
same `kcdxId` hits neither branch → the banner stays); the stale-clear-on-entity-switch invariant is
preserved (the clear branch fires on a real `kcdxId` change). The fix is entirely in
`data/maintainer-tool/frontend/src/detail/EntityDetail.tsx` — no worker / marker / push-side change (the
probe proved those were correct).

**Verification:** TWO regression tests, both falsifiable against the pre-fix code:
- `src/App.test.tsx` "[Fix ▸] arrival renders the divergence banner + no-DLL prompt UNDER StrictMode" —
  drives the REAL `[Fix ▸]` arrival through `App → EntityDetail` under `<StrictMode>` (a new
  `renderWithThemeStrict` helper) and asserts the banner + prompt render. **Confirmed falsifiable:** with
  the fix reverted (the split-effect code restored) this test FAILS (the exact bug); with the fix it PASSES.
  This is the test the original non-Strict arrival test could never be — the bug only surfaces under the
  double-invoke the pre-fix suite never exercised.
- `src/detail/EntityDetail.test.tsx` "a genuine entity switch (new kcdxId, no fixDetail) CLEARS the prior
  entity's divergence banner" — rerenders with a different `kcdxId` + `deepLink=null` and asserts the banner
  clears (the stale-clear invariant the single-owner fix must preserve).

Gate: `npm run build` exit 0; `npx vitest run` 584/584 on a clean run (the one intermittent `App.test.tsx`
s09 failure is the separate tracked KI-0020 timing flake, confirmed by its non-recurrence on re-run).

**Verified by the user:** confirmed 2026-06-14 — the user re-ran the original repro (upload report → `[Fix ▸]`
the failed row) and reported the divergence banner + the game-version-prefilled no-DLL prompt now appear on
arrival, and the inline per-field diff renders after linking the DLL ("working"). This was also the D45
Phase-3 milestone UAT the bug interrupted. Gate B root-cause-verifier returned `land-fix` (mechanism real,
single-owner fix not a mask, stale-clear invariant preserved, both regression tests falsifiable).
