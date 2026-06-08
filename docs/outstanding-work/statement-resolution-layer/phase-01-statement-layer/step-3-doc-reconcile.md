# Step 3 — reconcile the DEV-only doc drift (both lanes)

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 3.

## What

Reconcile the documentation that drifted from the Phase 9.3 design: the Phase 9.1
README's blanket "statements are DEV-only" claim (true for the bulk, wrong for the
curated subset this prerequisite ships), and the Phase 9.3 step docs' references to a
non-existent `statements.captures` column (captures live in the joined
`referenced_vars` table; the old `applicable_ops` column was dropped). Docs-only; lands
after steps 1-2 so the docs describe what was actually built.

## Scope

- **`../../restructure/phase-09.1-reference-db/README.md`** — the line "The
  `statements` / `referenced_vars` / `call_edges` tables are DEV-only (Phase 9.4's
  `kcdx.find` discovery; not consumed by production)" is corrected to the accurate
  split: the **bulk** `statements` / `referenced_vars` and all of `call_edges` are
  DEV-only (Phase 9.4); the **curated-function subset** of `statements` /
  `referenced_vars` ships to `reference.sqlite` for the Phase 9.3 runtime surface
  (this prerequisite). Point at this plan tree.
- **`../../restructure/phase-09.3-namespaces/step-1-locator-namespace.md`** +
  **`step-5-statement-namespace.md`** (and any other §9.3 step doc) — replace the
  `statements.captures` references with the accurate model: per-statement metadata is
  `statements.kind` / `callee` / `string_ref` / `byte_range_len`; captures are the
  joined `referenced_vars` rows (by `statement_idx`), NOT a `statements.captures`
  column. Note `applicable_ops` was dropped (op-fit is computed at apply-time from
  `byte_range_len`, not stored).
- Update the §9.3 step docs' "Phase 9.1 DONE provides the statements metadata" line to
  point at THIS prerequisite as the provider (Phase 9.1 shipped address resolution; the
  statement layer ships here).

## Test bar (runs AT this step)

Docs-only — verified by the deletion-hygiene / survivor-sweep discipline
(`.claude/rules/deletion-hygiene.md`): a grep confirms no surviving doc claims the
curated statement subset is wholly DEV-only, and no Phase 9.3 step doc references the
non-existent `statements.captures` column. No build, no launch.

## Dependencies

Steps 1 + 2 — the docs describe the shipped projection (step 1) and the refdb API
(step 2) as built. Ordered last so the reconciled text matches what landed.

## Design authority

[`../plan-spec.md`](../plan-spec.md) §"Why this exists" + §"The column contract" (the
accurate model the docs are corrected to). The drift sources are named in scope above.
Build to the plan-spec's model, not this summary.

## Disassembler-test / author-burden note

Author-invisible — documentation reconcile. No author-facing surface, no hex, no DB
rows.
