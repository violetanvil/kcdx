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
| [3.4 — The s08 reconciliation display + close→needs-action flag](step-4-s08-reconciliation-display.md) | NOT STARTED | — |
| [3.5 — The s08 Fix-flow completeness (PUSH s04 + ‹ back report-intact + applied value)](step-5-s08-fix-flow.md) | NOT STARTED | — |

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
