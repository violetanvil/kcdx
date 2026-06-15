# Maintainer-tool completion sequence

**Intent:** drive the maintainer tool's remaining open work to closed, in one agreed order
(user-settled 2026-06-15: acceptances → bugs → Docker). NOT a new feature — a cross-tree
EXECUTION ORDER over already-tracked items, each of which keeps its own canonical ledger in its
own tree. This doc is the sequencing source-of-truth; each row points at the tree that owns the
work. When a row lands, its OWN tree's ledger flips too (this is a driver, not a replacement).

This ledger exists because the work spans five independent trees with no shared home; per
`.claude/rules/plan-persistence.md` the agreed order is persisted before acting, not held in
conversation. A row flips to DONE when the owning tree's gate (the milestone UI acceptance, the
KI close, or the phase build+gate) is met.

## Status ledger (the execution order)

| Step | Status | Commit |
|---|---|---|
| 1 — verification-engine Phase 6 milestone UI acceptance (s08 import → 3-block worklist → 2 batch actions → s06 confirm + `[Fix ▸]`) → flips that tree's Phase 6 to DONE | BLOCKED — on KI-0023 (re-import reconciliation bug found during acceptance 2026-06-15) | — |
| 1a — KI-0023 fix: confirmed close-intervals batch shows the acted row broken again on re-import (write works; re-import/reconciliation defect; acted row has `matched_address_version_id: None`) via `/debug` — UNBLOCKS step 1 | NOT STARTED | — |
| 1b — red-box divergence-emphasis enhancement (make WHAT broke more visible — a red box around the diverged element); its own FE `/design`-fork → `/feature`/`/execute` cycle with its own UAT | NOT STARTED | — |
| 2 — lifecycle-completeness Phase 3 milestone UI acceptance (s09 needs-action view + back-stack + unsaved-guard + s08 reconciliation/Fix-flow) → flips that tree's Phase 3 to DONE | NOT STARTED | — |
| 3 — KI-0021 fix: new-entity verified first row unsavable (port the `verified_date` today-auto-fill from CreateVersionForm to CreateEntityForm + regression test) via `/debug` | NOT STARTED | — |
| 4 — KI-0020 fix: harden the flaky `App.test.tsx` s09 timing tests (fake timers / `waitFor` / `act`) via `/debug` or `/execute` | NOT STARTED | — |
| 5 — db-direct Phase 5: Docker packaging (image serving the built frontend + mounted-checkout + env-injected push-credential seam) via `/feature` | NOT STARTED | — |

**Reshape (2026-06-15):** the Phase 6 acceptance walk-through surfaced a correctness bug
(KI-0023) + a UX enhancement request (red-box). Step 1 is BLOCKED until 1a fixes KI-0023 and
the acceptance is re-run + accepted; 1b (the enhancement) is sequenced after the bug per the
user's "after the bug, its own cycle" decision. Immediate work: **1a (`/debug KI-0023`).**

The owning trees + their canonical ledgers:
- Steps 1: [maintainer-tool-verification-engine/](../maintainer-tool-verification-engine/README.md) Phase 6.
- Step 2: [maintainer-tool-lifecycle-completeness/](../maintainer-tool-lifecycle-completeness/README.md) Phase 3.
- Steps 3–4: [`docs/known-issues/`](../../known-issues/README.md) KI-0021, KI-0020.
- Step 5: [maintainer-tool-db-direct/](../maintainer-tool-db-direct/README.md) Phase 5.

## Out of scope (tracked elsewhere, not in this sequence)

- The function-kind **bulk-baseline dependency** for DB entity additions (surfaced by the
  `/add-db-entity` skill upgrade): the file-system-takeover plan's P1 (resolve the CCryPak swap
  seat by name → a function entity) depends on the version's bulk baseline covering that RVA. That
  is a takeover-plan edit, not maintainer-tool — flagged to record in
  [file-system-takeover/](../file-system-takeover/README.md) when P1's finding lands, not driven here.

## Close-out

When all five rows are DONE, the maintainer tool's tracked open work is closed (the tool
functionally complete + packaged, both FE features accepted, both KIs closed). This driver doc
moves to `closed/` per `.claude/rules/doc-organization.md`.
