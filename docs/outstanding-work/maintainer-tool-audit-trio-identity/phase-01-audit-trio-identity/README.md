# Phase 01 — audit-trio identity + verified_date

**Intent:** the whole audit-trio model (TRD D17a/D17b) in two independent commit-grain steps —
the `verified_by` → confirm-author wiring (Step 1) and the `verified_date` read-only/conditional
render (Step 2). Both FE-repo, both independently verifiable at their position via Vitest.

## Step-grain ledger

| Step | Status | Commit |
|---|---|---|
| [1 — verified_by → the confirm author identity (D17a)](step-1-verified-by-confirm-author.md) | DONE | FE:6bfa833 |
| [2 — verified_date read-only + shown-only-when-verified (D17b)](step-2-verified-date-readonly-conditional.md) | NOT STARTED | — |

## Phase verification gate

A UI-touching phase — the gate INCLUDES user-facing acceptance (`.claude/rules/ux-first-class.md`),
not only build/test green. Phase 01 is done when: both steps pass `npm run typecheck` + `npx vitest
run` + `npm run build` in the FE repo; AND the **milestone user-acceptance checkpoint** confirms,
against a live linked session, that (a) on Confirm the git commit is authored by the row's
`verified_by` (the signer), with `verified_by` prefilled-but-overrideable; and (b) `verified_date`
is read-only and appears ONLY on a verified row (an unverified row shows no `verified_date` cell),
system-set to today on verify. The audit-trio render is substantive + the verified_date show/hide
+ read-only treatment is under-specified at the pixel level → the milestone UAT fires.
