# Phase 1 — audit + column design

**Intent.** Prove the scope before changing anything. Enumerate EVERY value the
importer writes to every DB column, classify each by its source (authored seed
column / dump table / game-version string / format validator / **prose or
inference**), and produce the per-kind explicit-column plan that Phase 2 builds.
The whole point of the user's "no more missed items" directive lives here: the
fix can only be complete if the audit is exhaustive.

Shared spec: [`../context.md`](../context.md).

## Step ledger

| Step | Status | Commit |
|---|---|---|
| 1 exhaustive value-provenance audit + per-kind column plan | DONE | (landed) |

## Step docs

1. [step-1-value-provenance-audit.md](step-1-value-provenance-audit.md)

## Verification gate (phase end)

- The audit table in `context.md` (or a dedicated audit doc it links) lists every
  `address_versions` / `address_names` / `survival` column the importer writes,
  with each value's source classified. Every value traces to an authored column
  or a named legitimate non-prose source — OR is flagged as a prose/inference
  finding to fix.
- The findings set is a superset of the four pre-plan findings (F1–F4 in
  `context.md`) — i.e. the audit confirms those AND surfaces any others.
- The per-kind column plan names the exact new column(s) Phase 2 adds and the
  exact columns the sprawl collapses into.
- No code or seed change in this phase — it is a proof + design-capture step.
