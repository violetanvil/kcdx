# Maintainer-tool verification engine

**Intent:** link a game DLL on your machine and the tool verifies what you author against
the real binary (not record-only) — static per-author in the browser + live functional
in-game in bulk; the un-deferred R5 + restored R12 link table. Settled design:
[`plan-spec.md`](plan-spec.md) (TRD `data/maintainer-tool/design.md` §6 US-11 + D24–D32; the
s02/s04/s08 screen specs + the Layer-1 `ui/design.md`; per-kind checks in
`data/maintainer-tool/fingerprint-per-kind.md`).

Authored by `/plan` (structure only — no code built). A later `/execute` / `/feature` cycle
reads a step doc as its `Source work-item` and flips its ledger row. Two-repo split: **[FE]**
steps land in the SEPARATE gitignored frontend repo (gate `npm run build` + Vitest);
**[ENG]** in kcdx `src/` (gate `pwsh ./build.ps1` + live launch); **[TEST]** in kcdx
`test-plugins/`; **[CORE]** in `data/refdata-extractor/python/seeds_shared/` (gate pytest).

## Phase-grain status ledger

| Step | Status | Commit |
|---|---|---|
| [Phase 0 — Probes](phase-00-probes/README.md) | NOT STARTED | — |
| [Phase 1 — Shared contracts](phase-01-contracts/README.md) | NOT STARTED | — |
| [Phase 2 — Frontend static checker + per-author UI](phase-02-frontend-checker/README.md) | NOT STARTED | — |
| [Phase 3 — C++ engine survival extension](phase-03-engine-survival/README.md) | NOT STARTED | — |
| [Phase 4 — In-game plugin + report](phase-04-ingame-plugin/README.md) | NOT STARTED | — |
| [Phase 5 — Frontend report ingestion](phase-05-report-ingestion/README.md) | NOT STARTED | — |
