# Maintainer-tool verification engine

> **Orientation — "the maintainer tool" is THREE separate efforts (don't conflate them):**
>
> | Plan tree | What it is | State |
> |---|---|---|
> | [`maintainer-tool-db-direct/`](../maintainer-tool-db-direct/README.md) | The DB-authoring **web app** — browse/search/author the reference DB (the six jobs), CSV auto-export, server-side commit. The tool itself. | Phases 1–4 DONE + running; **only Phase 5 (Docker packaging) left.** Functionally complete. |
> | [`maintainer-tool-audit-trio-identity/`](../maintainer-tool-audit-trio-identity/README.md) | A frontend refinement of that app — `verified_by` as the signer/commit-author identity + read-only `verified_date`. | **COMPLETE** (FE:6bfa833 + FE:0cc6d2d, UAT accepted). |
> | **THIS tree** (`maintainer-tool-verification-engine/`) | The **"link a DLL, verify what you author against the real binary"** feature — a separate capability bolted onto the tool. | **Phases 0–3 DONE + accepted; Phases 4–5 NOT STARTED.** |
>
> So **the maintainer tool as a whole is NOT 100% complete.** The authoring app is functionally done (Docker pending); THIS verification-engine feature is **3 of 5 phases**. The end-to-end verification loop is not live yet — the engine *can* verify (Phase 3), but nothing drives it in bulk in-game (Phase 4) and nothing surfaces the bulk result to the author (Phase 5).
>
> **The "front end" next step is Phase 5 of THIS tree — and it has a hard upstream dependency on Phase 4.** Phase 5 ingests the JSON report that Phase 4's in-game plugin *produces*; there is no report to ingest until Phase 4 lands. Phase 4 (engine `src/` + a `test-plugins/` driver, gated by `build.ps1` + a live launch) must come first, THEN Phase 5 (the `[FE]` report-ingestion UI in the separate frontend repo).

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

## Current state + what's next (2026-06-09)

**Where we are: Phases 0–3 are COMPLETE AND ACCEPTED.** Phase 2 (the browser static checker + the
per-author UI) landed; Phase 3 (the C++ engine survival extension — the AUTHORITY the browser
mirrors, D27) is DONE: 3.1 the per-kind dispatch + Ambiguous (8008e3d), 3.2 the 5 static
non-function checks + the anchor-dependency DAG (3c5e065), 3.3 the startup reachability + on-disk
version-applicability + D34 attribution pass (69c7cc2), 3.4 the JS↔C++ cross-impl agreement pin
(ffc51ae). One follow-up filed: TD-0009 (the engine↔browser agreement is HARD-pinned for the 4
algorithm-identical kinds; the 3 superset kinds, where the engine computes more than the
browser/Python subset by design, are deferred for reconciliation). **Next: Phases 4–5** (the in-game
batch plugin that drives the 3.3 startup verification pass + writes the JSON report; the frontend
report ingestion / two-block worklist).

**Phase-3 in-game acceptance: ACCEPTED at the 2026-06-09 live launch.** Once KI-0012 (the
parallel-lane boot crash, NOT survival code) landed and the then-current DLL was re-deployed +
hash-verified (`AB48333923E820DB4664A9873BE5402BC998EFE75E2763BC3415731A8D4243FC`), the clean launch
(`kcdx-dev_2026-06-09_12-46-54.log`) reported both survival rows PASS:
`RESULT name=cap-84-survival-dispatch verdict=PASS` + `RESULT name=cap-85-survival-agreement
verdict=PASS`. The 3 unrelated suite FAILs in the run (CAP-20-addrname / CAP-28-typo-fails-fast /
cap-90-pdb-internal-address) are other lanes, not Phase-3 rows. The phase gate is MET.

## Phase-grain status ledger

| Step | Status | Commit |
|---|---|---|
| [Phase 0 — Probes](phase-00-probes/README.md) | DONE | (landed) |
| [Phase 1 — Shared contracts](phase-01-contracts/README.md) | DONE | ccd37e1 (1.1 e8a06cc, 1.2 35445b7, 1.3 ccd37e1) |
| [Phase 2 — Frontend static checker + per-author UI](phase-02-frontend-checker/README.md) | DONE | (landed) — 2.1–2.7 all DONE (FE:1459367/66f4716/d611c21/e83a57c/0ed135d+bfdff6f/00b2e78…27aa470 + 2.6a 9d84fcf + 2.7 FE:7d2d6fa); the browser checker + s02 install-set link + s04 verdict badge + the link-to-create on-ramp. 2.6 + 2.7 milestone UATs both accepted |
| [Phase 3 — C++ engine survival extension](phase-03-engine-survival/README.md) | DONE | ffc51ae — 3.1 per-kind dispatch + Ambiguous (8008e3d), 3.2 the 5 static non-function checks + anchor DAG (3c5e065), 3.3 the startup reachability + on-disk version-applicability + D34 attribution pass (69c7cc2), 3.4 the JS↔C++ cross-impl agreement pin (ffc51ae). The full per-kind survival authority the browser mirrors (D27); cap-84 + cap-85 self-tests. Follow-up: TD-0009 (engine↔browser agreement scoped to the 4 identical kinds; 3 superset kinds deferred). |
| [Phase 4 — In-game plugin + report](phase-04-ingame-plugin/README.md) | NOT STARTED | — |
| [Phase 5 — Frontend report ingestion](phase-05-report-ingestion/README.md) | NOT STARTED | — |
