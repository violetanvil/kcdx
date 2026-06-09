# Maintainer-tool verification engine

**Intent:** link a game DLL on your machine and the tool verifies what you author against
the real binary (not record-only) — static per-author in the browser + version-applicability +
reachability in-game in bulk (at startup); the un-deferred R5 + restored R12 link table. Settled design:
[`plan-spec.md`](plan-spec.md) (TRD `data/maintainer-tool/design.md` §6 US-11 + D24–D35 — the
batch-verify loop D33–D35 added 2026-06-05; the s02/s04/s08 screen specs + the Layer-1
`ui/design.md`; per-kind checks in `data/maintainer-tool/fingerprint-per-kind.md`).

Authored by `/plan` (structure only — no code built). A later `/execute` / `/feature` cycle
reads a step doc as its `Source work-item` and flips its ledger row. Two-repo split: **[FE]**
steps land in the SEPARATE gitignored frontend repo (gate `npm run build` + Vitest);
**[ENG]** in kcdx `src/` (gate `pwsh ./build.ps1` + live launch); **[TEST]** in kcdx
`test-plugins/`; **[CORE]** in `data/refdata-extractor/python/seeds_shared/` (gate pytest).

## Current state + what's next (2026-06-08)

**Where we are: Phase 2 is COMPLETE** — the browser static checker (2.1–2.4), the s02 install-set
link surface (2.5), the s04 per-author verdict badge (2.6a–2.6e), and the s02 link-to-create
on-ramp (2.7, FE:7d2d6fa) are all landed; the 2.6 + (pending) 2.7 milestone UATs cover the
substantive UI. The statement-resolution-layer data update that gated 2.7 landed + was live-accepted;
2.7 built against the settled model. **Next: Phases 3–5** (the C++ engine survival extension, the
in-game batch plugin, the frontend report ingestion) are NOT STARTED — Phase 3 [ENG] is the next
build (the in-game bulk reachability check at startup).

## Phase-grain status ledger

| Step | Status | Commit |
|---|---|---|
| [Phase 0 — Probes](phase-00-probes/README.md) | DONE | (landed) |
| [Phase 1 — Shared contracts](phase-01-contracts/README.md) | DONE | ccd37e1 (1.1 e8a06cc, 1.2 35445b7, 1.3 ccd37e1) |
| [Phase 2 — Frontend static checker + per-author UI](phase-02-frontend-checker/README.md) | DONE | (landed) — 2.1–2.7 all DONE (FE:1459367/66f4716/d611c21/e83a57c/0ed135d+bfdff6f/00b2e78…27aa470 + 2.6a 9d84fcf + 2.7 FE:7d2d6fa); the browser checker + s02 install-set link + s04 verdict badge + the link-to-create on-ramp. 2.6 milestone UAT accepted; 2.7 milestone UAT pending |
| [Phase 3 — C++ engine survival extension](phase-03-engine-survival/README.md) | NOT STARTED | — |
| [Phase 4 — In-game plugin + report](phase-04-ingame-plugin/README.md) | NOT STARTED | — |
| [Phase 5 — Frontend report ingestion](phase-05-report-ingestion/README.md) | NOT STARTED | — |
