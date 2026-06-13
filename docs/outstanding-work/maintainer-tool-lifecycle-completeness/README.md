# Maintainer-tool lifecycle completeness (TRD D41)

**Intent:** implement TRD D41 — entity-lifecycle completeness + report-vs-DB reconciliation. The
maintainer tool never leaves an entity silently incomplete at the current game version (a standing
needs-action view over the orphan / never-verified / broken-reference set), and the report worklist
reconciles against current DB state (already-acted rows show no-further-action; a close that orphans
an entity flags needs-action) — plus the three s08 Fix-flow completeness gaps. Data-core → backend →
frontend, each step independently testable at its layer.

**Settled design:** `data/maintainer-tool/design.md` **D41** (`88bb582`) + the **s09 Needs-action view**
screen spec `data/maintainer-tool/ui/screens/s09-needs-action.md` (`3dcf3e0`) + the **s08** D41 sections
`data/maintainer-tool/ui/screens/s08-verification-worklist.md` (`88bb582`). Build to those, not this
README's summary. Shared spec + coverage map: [plan-spec.md](plan-spec.md).

## Phase-grain status ledger

| Step | Status | Commit |
|---|---|---|
| [Phase 1 — Data-core: reconciliation skip + lifecycle detection](phase-01-data-core/README.md) | DONE | e7f7e88 — 1.1 07fcf70 (close-intervals already-done skip, D41 fact 2) + 1.2 e7f7e88 (lifecycle-completeness detection query, D41 fact 1). Both data-core steps green; full data-core 183 passed. |
| [Phase 2 — Backend: expose detection + classification](phase-02-backend/README.md) | DONE | c816184 — 2.1 d319f14 (read-only GET /needs-action exposes the detection query) + 2.2 c816184 (the /save/reverify-batch preview classifies actionable vs already_acted). Both backend steps green; full backend suite 79 passed. |
| [Phase 3 — Frontend: the s09 view + the s08 D41 fixes](phase-03-frontend/README.md) | IN PROGRESS | 3.1 FE:151c331 (s09 view shell + all states) + 3.2 FE:6aaaeaa (the in-app content back-stack + ‹back + unsaved-changes guard + D43 version-new selector, D42/D43/D44) + 3.3 FE:0b64b6d (s09 per-row resolution actions + the frame-state deep-link mechanism + the s02 lifecycle guard seam) + 3.4 FE:b3779bc (the s08 report-vs-DB reconciliation display + the close→needs-action flag, D41 E3/E4 — already-acted rows move to "No further action", read from the backend classification, never re-derived) + 3.5 FE:2dc6b42 (the s08 [Fix ▸] flow completeness, D41/D42 E5/E6/E7 — detail-carry to s04 on the single deep-link channel, ‹ back report-intact via the kept-mounted model + the s04 guard discard, applied valid_through read verbatim from field_delta). All 5 build steps landed + gated. Phase awaiting ONLY the milestone user-acceptance checkpoint (the whole s09 + s08 + back-stack + guard surface in the browser) — Phase 3 flips to DONE when the user accepts. |
