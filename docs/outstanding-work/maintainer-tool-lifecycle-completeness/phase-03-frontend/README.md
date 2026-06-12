# Phase 3 — Frontend: the s09 view + the s08 D41 fixes (UI)

**Intent:** the user-facing layer — the new s09 Needs-action view (built to its screen spec) consuming
the Phase-2 endpoint, and the s08 worklist's D41 fixes (the already-acted reconciliation display, the
close→needs-action flag, the Fix-flow completeness). All in the SEPARATE gitignored frontend repo
(`data/maintainer-tool/frontend/`), gated by `npm run build` + `vitest run` (NOT `build.ps1`), committed
in the nested repo with an `FE:<hash>` ref in the ledger.

## Step-grain ledger

| Step | Status | Commit |
|---|---|---|
| [3.1 — The s09 view shell + detection display + the s01 affordance + all states](step-1-s09-view-shell.md) | NOT STARTED | — |
| [3.2 — The s09 per-row resolution actions (navigate to s02/s04/s05 + return)](step-2-s09-resolution-actions.md) | NOT STARTED | — |
| [3.3 — The s08 reconciliation display + close→needs-action flag](step-3-s08-reconciliation-display.md) | NOT STARTED | — |
| [3.4 — The s08 Fix-flow completeness (detail to s04 + return path + applied value)](step-4-s08-fix-flow.md) | NOT STARTED | — |

## Phase verification gate

A UI-touching phase — the gate INCLUDES user-facing acceptance (`.claude/rules/ux-first-class.md`), not
only build/test green. Each step lands its Vitest unit/component tests (`npm run build` exit 0 +
`vitest run` green) in the frontend repo. The phase is done when: the s09 Needs-action view renders the
by-kind sections + all states and its resolution actions route correctly (3.1, 3.2); the s08 worklist
moves already-acted rows to no-further-action + flags an orphaning close (3.3); the Fix-flow carries the
detail + returns + shows the applied value (3.4) — AND the **milestone user-acceptance checkpoint** fires
for the substantive + under-specified UI (s09 is a NEW screen; the s08 changes are substantive): the
maintainer experiences the s09 view (open from the navigator badge → the by-kind sections → resolve a
row → it drops off → the all-clear state) and the s08 reconciliation/Fix-flow, then accepts. Build to the
s09 + s08 screen specs (`.claude/rules/spec-conformance.md`), not the step-doc summaries.
