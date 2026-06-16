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
| 1 — verification-engine Phase 6 milestone UI acceptance (s08 import → 3-block worklist → 2 batch actions → s06 confirm + `[Fix ▸]`) → flips that tree's Phase 6 to DONE | DONE — accepted 2026-06-15 on the real 2026-06-09 production v3 report (name resolution, 3-block split, No-action collapsible, proof-rank chips, 0-failing edge header, KI-0023 on-import reconcile all confirmed; verify-all/close-intervals writes accepted at the prior sweep). Phase 6 flipped to DONE | — (acceptance, no commit hash) |
| 1a — KI-0023 fix: close-intervals batch re-showed the acted row broken on re-import (design gap — reconciliation only fired on a batch round-trip, never on import). Fixed via `/debug` by on-import reconciliation (FE `46d3c09`, capture D46 `592ea9d`); Gate A + Gate B `land-fix`; user-confirmed. UNBLOCKS step 1 | DONE | FE:46d3c09 |
| 1b — red-box divergence-emphasis enhancement (make WHAT broke more visible — a red box around the diverged element); its own FE `/design`-fork → `/feature`/`/execute` cycle with its own UAT | DONE — design settled (D47, `490e791`) + built (`FE:f26322b`): the s04 `diverged-field box` (error-token border+tint on a field row keyed to divergence.status==="diverged", persists through edit, composes with the dirty + glyph+text markers; laws 1/7/9). Build green + vitest 594/594; both `/design` gates + step-review PROCEED. UI is fully-directed (D47 fixed every visible decision) → no mid-build milestone UAT. **Accepted 2026-06-15 on the vitest evidence** (6 falsifiable rows: renders for diverged & only diverged, persists through edit, composes with the dirty + glyph+text markers, keys on `status` not the boolean — the AP14 guard) + build-green + the gates; the live pixel render is unverifiable against the current DB (the one `failed` row is already-closed → no `[Fix ▸]` target, and no divergent DLL is linked), so the border+tint pixels are unconfirmed by eyeball — the wiring + laws 1/7/9 are asserted. | FE:f26322b |
| 2pre — KI-0024 fix: audit-trio orphan on a direct trio edit (resequenced ahead of Step 2 — it sits in Step 2's s09 `[Verify ▸]` → s04 trio-edit acceptance path). User-hit during Step 2 acceptance ("on random fields all the time"). The s04 trio coupling fired one direction only; every trio-cell-initiated path (direct write, D29 auto-suggest, EMPTYING a verified cell) could orphan the all-or-null trio + the error surface showed the raw validator string on the wrong fields. Fixed both directions + the error surface via `/debug` (FE `8557c11` version-empty; FE `158dd3f` version-set + teaching error/deficient-attribution); Gate A (1b + 2a) + Gate B `land-fix`; vitest 604/604; user-confirmed. Closed `8697121`. UNBLOCKS Step 2's trio-edit acceptance | DONE | FE:8557c11 + FE:158dd3f |
| 2 — lifecycle-completeness Phase 3 milestone UI acceptance (s09 needs-action view + back-stack + unsaved-guard + s08 reconciliation/Fix-flow) → flips that tree's Phase 3 to DONE | DONE — accepted 2026-06-15 on the live tool: s09 opened from the navigator badge (7 never-verified at version 1.5.1164953, by-kind sections + counts as specced), `[Verify ▸]` PUSHED s04, the KI-0024-fixed trio-edit path held (verified_by won't blank, no raw-validator error on the populated fields), `‹ back` restored s09 intact, s08 Fix-flow carried detail to s04 + report-intact ‹back. (A dead shared dev-backend surfaced mid-walk as a browser CORS/NetworkError — relaunched the uvicorn process, re-verified the `Access-Control-Allow-Origin` header for the vite origin; code/config were correct, no defect.) Phase 3 flipped to DONE | — (acceptance, no commit hash) |
| 3 — KI-0021 fix: new-entity verified first row unsavable (port the `verified_date` today-auto-fill from CreateVersionForm to CreateEntityForm + regression test) via `/debug`. **Same audit-trio family as KI-0024.** Fold in the s05 `CreateVersionForm` blank-trio gap Gate B surfaced during KI-0024 | DONE — `/debug KI-0021` found the fix had ALREADY landed (FE:9ff0fb5, audit-trio coupling wired into CreateEntityForm.setField+revertField) during the audit-trio pass, with a falsifiable regression test (vitest 7/7, verified live this session: FE build exit 0 + revert→test-goes-red). The KI doc sat stale-open; Phase B skipped (cause statically verified), Gate B `root-cause-verifier` → `land-fix`. The folded s05 `CreateVersionForm` blank-trio concern verified NON-EXISTENT as a live trap (it starts from a complete prefilled trio — source row + verified_date-today default — so cannot orphan). Closed (KI-0021 → closed/ + reindexed). | FE:9ff0fb5 |
| 4b — general validator-message → teaching-copy translation layer ("option 2c", deferred by the user at KI-0024's Fork-2): translate EVERY raw validator message the editor surfaces (not just the trio-incomplete verdict) to teaching copy + per-shape deficient-field attribution | DONE (build landed; awaiting acceptance) — `/execute` built `validatorTeaching.ts`: a per-shape table (stable-substring matcher + user-approved teaching copy + deficient-field resolver) covering all ~20 data-core validator shapes + a strip-file:line-prefix fallback; `translateValidatorError` routes every error through it in FieldEditor + both create forms (and their rowErrors channels). Presentation-only — validity stays the API's `res.valid` (law 6); the raw `…seed.csv:NN:` string never reaches the maintainer. Per-shape falsifiable tests (overlap pairs resolve distinctly; fallback asserts no raw-prefix leak). Structure + copy + fallback all user-approved up front. Build exit 0 + vitest 632/632 (manager-run, AP8); step-review PROCEED. FE:a3c5eb3 | FE:a3c5eb3 |
| 4 — KI-0020 fix: harden the flaky `App.test.tsx` s09 timing tests | INVESTIGATED — kept OPEN with a watch (does NOT block close-out). `/debug KI-0020` re-observed before designing any harden: NON-REPRODUCIBLE across 13 full-suite runs incl. forced contention (24 over-subscribed workers + 12 CPU spinners), all green 632/632, zero `act()` warnings. Probably already resolved by `7d37417` (KI-0022's single-owner StrictMode-stable s09 effect collapsed the same double-fire race) but UNVERIFIED — a flake that won't reproduce can't be proven fixed (`results-driven`/AP17). Per user decision, no unverifiable harden lands; KI stays open with a revival trigger (reopen when an s09 `App.test` next reds, capturing the signature a verifiable harden needs). A test-infra flake, not a product defect (s09 behaviours pass in isolation), so it does not gate the sequence close-out. | — (no fix — kept open) |
| 5 — db-direct Phase 5: Docker packaging (image serving the built frontend + mounted-checkout + env-injected push-credential seam) via `/feature` | NOT STARTED | — |

**Reshape (2026-06-15):** the Phase 6 acceptance walk-through surfaced a correctness bug
(KI-0023) + a UX enhancement request (red-box). 1a (KI-0023) closed via `/debug` (on-import
reconciliation, FE `46d3c09`, capture D46). **Step 1 accepted 2026-06-15** on the real production
v3 report — the DB was already in its post-acceptance state from the prior session, so the worklist
correctly showed all acted rows under "no further action," which directly demonstrated the KI-0023
on-import reconcile; the verify-all/close-intervals writes were accepted at the prior sweep. Phase 6
is DONE. Step 1b (the red-box enhancement) is DONE — design settled (D47, `490e791`) + built
(`FE:f26322b`), gates green.

**Step 2 acceptance then surfaced KI-0024 (2026-06-15):** during the s09 `[Verify ▸]` → s04 trio-edit
walk-through the user hit the audit-trio orphan ("on random fields all the time"). Resequenced ahead
of Step 2 as row **2pre** and closed via `/debug` (FE `8557c11` + `158dd3f`, close `8697121`) — both
coupling directions + the raw-validator-string/wrong-field error surface fixed; user-confirmed. Two
follow-ups it surfaced are now tracked rows: the s05 same-shape blank-trio gap folded into Step 3
(KI-0021 family); the general teaching-error layer (option 2c) as row **4b**.

**Step 2 ACCEPTED (2026-06-15):** the lifecycle-completeness Phase 3 milestone UI acceptance passed on
the live tool (s09 needs-action view + `[Verify ▸]`→s04 + back-stack restore + s08 Fix-flow), so that
tree's Phase 3 flipped to DONE. A dead shared dev-backend surfaced mid-walk as a browser CORS error —
relaunched the uvicorn process and re-verified the CORS header for the vite origin; no code/config
defect.

**Step 3 CLOSED (2026-06-15):** `/debug KI-0021` found the fix had already landed (FE:9ff0fb5) during
the audit-trio coupling pass, with a falsifiable regression test (vitest 7/7) — the KI doc was just
stale-open. Gate B `root-cause-verifier` → `land-fix`; KI moved to closed/. The folded s05
`CreateVersionForm` blank-trio concern was verified non-existent as a live trap (complete prefilled
trio).

**Step 4b built (2026-06-15):** the general validator-message → teaching-copy translation layer
(`validatorTeaching.ts`, FE:a3c5eb3) — all ~20 validator shapes → user-approved teaching copy +
deficient-field attribution + a strip-prefix fallback; build exit 0 + vitest 632/632 + step-review
PROCEED. The build is fully-directed (copy/structure/fallback approved up front) so it carries no
mid-build milestone UAT — its single user-acceptance walk-through stays OPEN (offered; not yet
confirmed).

**Step 4 INVESTIGATED, kept OPEN (2026-06-15):** `/debug KI-0020` re-observed before hardening — the
s09 `App.test.tsx` flake is NON-REPRODUCIBLE across 13 full-suite runs incl. forced contention (all
green 632/632, zero `act()` warnings), probably already resolved by `7d37417` (KI-0022) but UNVERIFIED.
Per user decision, no unverifiable harden lands; KI-0020 stays open with a watch trigger and does NOT
block close-out (a test-infra flake, not a product defect). Immediate work resumes: **Step 5 —
db-direct Phase 5 Docker** (the last sequence item), via `/feature`.

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

When the remaining rows are DONE, the maintainer tool's tracked open work is closed (the tool
functionally complete + packaged, both FE features accepted, KI-0021 closed). **Step 4 (KI-0020) is the
one row that does NOT close to DONE** — it is a non-reproducible test-infra flake intentionally kept
OPEN with a watch trigger (not a product defect; does not gate close-out). So the close condition is:
Steps 1 / 1a / 1b / 2pre / 2 / 3 / 4b / 5 DONE + Step 4b's acceptance confirmed, with KI-0020 carried
as a tracked open watch-item. This driver doc moves to `closed/` per
`.claude/rules/doc-organization.md` at that point.
