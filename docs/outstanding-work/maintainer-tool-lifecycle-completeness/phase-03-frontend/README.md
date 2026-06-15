# Phase 3 — Frontend: the s09 view + the s08 D41 fixes (UI)

**Intent:** the user-facing layer — the new s09 Needs-action view (built to its screen spec) consuming
the Phase-2 endpoint, and the s08 worklist's D41 fixes (the already-acted reconciliation display, the
close→needs-action flag, the Fix-flow completeness). All in the SEPARATE gitignored frontend repo
(`data/maintainer-tool/frontend/`), gated by `npm run build` + `vitest run` (NOT `build.ps1`), committed
in the nested repo with an `FE:<hash>` ref in the ledger.

## Step-grain ledger

| Step | Status | Commit |
|---|---|---|
| [3.1 — The s09 view shell + detection display + the s01 affordance + all states](step-1-s09-view-shell.md) | DONE | FE:151c331 — s09 view shell + 3 by-kind sections + s01 [Needs action ▸ N] badge + all states (populated/all-clear/loading/error/no-DB/edge); wire types match the /needs-action backend shape. npm build exit 0 + vitest 496/0. step-review PROCEED (the all-clear success tint uses the proven raw-`var(--kcdx-success)` form — `success` is NOT a registered Mantine color in theme.tsx, only `accent` is; the bare-role form works for error/warning/info but is unverified for success, so the raw-var form is the correct path, not a follow-up). |
| [3.2 — The in-app content back-stack + state-carrying frames + ‹ back + unsaved-changes guard (D42/D43/D44)](step-2-nav-backstack.md) | DONE | FE:6aaaeaa — headless navStack (push/reset/pop + state-restore-by-identity) + useNavStack React wrapper with the D44 guard interception + BackAffordance (law-10 ‹back, both breakpoints, destination-labeled, reserved space, phone-root→navigator-home) + UnsavedChangesGuard (Save/Discard/Cancel) + D43 two-source version dropdown ("From your linked Bin · new"). App.tsx flat-toggle model replaced; dirty seam via FieldEditor onDirtyChange. npm build exit 0 + vitest 527/527 (40 files, incl. the phone root-back path). step-review PROCEED (re-task fixed a phone root-back dead-end the first pass missed — the cold review caught it; one L note: AppShell.onBack is a documented harmless defensive fallback, optional future cleanup). User-approved deferrals to consumer steps: s08 report-into-frame→3.5, s02 lifecycle dirty source→3.3, the guard's Save+Discard runners→the first consumer pushing out of a dirty s04 (3.3/3.5). |
| [3.3 — The s09 per-row resolution actions (PUSH the resolve flow + ‹ back return)](step-3-s09-resolution-actions.md) | DONE | FE:0b64b6d — per-kind action buttons (uncovered→Author successor/Deprecate/Supersede, never-verified→Verify, broken-ref→Fix reference), each PUSHES the canonical resolve flow + drop-off-on-return (law 3/6/10). The DEEP-LINK mechanism (user-settled this step, decision captured): the pushed frame carries `{kcdxId, deepLink?: {openSection?: lifecycle|verify, createPrefill?}}`; EntityDetail auto-opens the target sub-surface ONCE on entry (one-shot — a ‹back restores the maintainer's own toggles; law 3/10); the Verify/Lifecycle sections became controlled. Plus the s02 lifecycle dirty source + Discard runner for the D44 guard (the 3.2 deferral this step owned). npm build exit 0 + vitest 541/541. step-review PROCEED (two stale comments fixed; the load-bearing one-shot test asserts auto-open→collapse→‹back→re-enter stays collapsed). The deep-link mechanism is reused by 3.5's [Fix ▸]→s04. |
| [3.4 — The s08 reconciliation display + close→needs-action flag](step-4-s08-reconciliation-display.md) | DONE | FE:b3779bc — the report-vs-DB reconciliation display (E3): a re-imported row whose recommended action already landed renders in a "No further action" group (out of the actionable verified/failing blocks, no checkbox, not in any batch, the backend's action-keyed marker — "interval already closed" / "already current"); the FE READS the backend `already_acted` classification, never re-derives it (D41 fact 2). Plus the close→needs-action flag (E4): a landed close-intervals batch fires onCloseLanded → bumps the navigatorRefreshKey so the s09 standing view + s01 badge re-detect any orphaned entity from DB state; the close stays one atomic transaction. ReverifyBatchRow → discriminated union on `status` (actionable | already_acted) matching the as-built backend; buildConfirmBatchRequest skips already_acted rows (no no-op confirm write). npm build exit 0 + vitest 552/552 (+11 falsifiable tests). step-review PROCEED (cold gate; 8 dimensions incl. an independent design.md D41 re-read confirming the preview-point reconciliation seam is settled, not autonomous). Seam (captured): reconciliation comes from the EXISTING /save/reverify-batch preview per design.md §10 D41 (re-classify the computed diff, not new plumbing). |
| [3.5 — The s08 Fix-flow completeness (PUSH s04 + ‹ back report-intact + applied value)](step-5-s08-fix-flow.md) | DONE | FE:2dc6b42 — the s08 [Fix ▸] flow completeness (D41/D42 E5/E6/E7). E5: [Fix ▸] carries the failing row's divergence detail to s04 (DeepLinkTarget gains fixDetail?, pushed on the SINGLE law-10 deep-link channel with openSection:"verify", reusing 3.3's one-shot applier; EntityDetail surfaces it as an advisory "What diverged" banner). E6: [Fix ▸] PUSHES a state-carrying frame; ‹ back restores the report intact, no re-import (kept the keep-mounted model per the settled decision — no controlled-frame refactor); the deferred 3.2 seam lands (the s04 field-editor DISCARD registered with the nav store so the guard's [Discard] drops the edits; Save keeps the s06-confirm no-runner fallback, D44). E7: an applied row shows its resulting valid_through (read VERBATIM from the resolver's field_delta, D39; bare "✓ applied" when no value — AP14, no fabrication). npm build exit 0 + vitest 565/565 (+13 falsifiable tests incl. the no-value AP14 case + the ‹back report-survival assertion). step-review PROCEED (cold gate, 7 dimensions; both settled decisions verified honored, the E7 value traced real not fabricated). Settled (captured in step-5 doc): E6 keep-mounted + falsifiable test; E5 fixDetail on the single channel. |

## Phase verification gate

A UI-touching phase — the gate INCLUDES user-facing acceptance (`.claude/rules/ux-first-class.md`), not
only build/test green. Each step lands its Vitest unit/component tests (`npm run build` exit 0 +
`vitest run` green) in the frontend repo. The phase is done when: the s09 Needs-action view renders the
by-kind sections + all states (3.1); the in-app content back-stack drives navigation among the content
screens — resolve-actions push state-carrying frames, top-level destinations reset, `‹ back` restores
full state, a dirty-editor navigate-away surfaces the unsaved-changes guard, and the version-new selector
offers a linked-Bin-resolved-new version (3.2, TRD D42/D43/D44 + law 10); the s09 resolution actions PUSH
the canonical resolve flow and `‹ back` returns to s09 intact (3.3); the s08 worklist moves already-acted
rows to no-further-action + flags an orphaning close (3.4); the Fix-flow PUSHES s04 carrying the detail,
`‹ back` restores the report with no re-import, and an applied row shows its resulting value (3.5) — AND
the **milestone user-acceptance checkpoint** fires for the substantive + under-specified UI (s09 is a NEW
screen; the back-stack is a new navigation model; the s08 changes are substantive): the maintainer
experiences the s09 view (open from the navigator badge → the by-kind sections → resolve a row → `‹ back`
→ it drops off → the all-clear state), the s08 reconciliation/Fix-flow (`[Fix ▸]` → s04 → `‹ back` to the
report intact), and the unsaved-changes guard, then accepts. Build to the s09 + s08 + s02 screen specs +
`ui/design.md` law 10 (`.claude/rules/spec-conformance.md`), not the step-doc summaries.

**Gate met — milestone acceptance ACCEPTED 2026-06-15.** On the live tool (backend `127.0.0.1:8000`
+ vite `127.0.0.1:5173`, live `/needs-action` = 7 never-verified entities at version 1.5.1164953), the
maintainer walked the surface and confirmed: s09 opened from the navigator badge with the by-kind
sections + counts as specced (Uncovered/Broken-refs `(0)` muted-none, Never-verified `(7)` expanded);
`[Verify ▸]` PUSHED s04; the KI-0024-fixed trio-edit path held (blanking a verified trio cell blocked,
no raw-validator error attributed to the populated fields); `‹ back` restored s09 with state intact; the
s08 Fix-flow carried detail to s04 and `‹ back` returned to the report intact. Build green + vitest
565/565 across the 5 steps. Phase 3 DONE.
