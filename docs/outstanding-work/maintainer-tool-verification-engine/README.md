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

## Current state + what's next (2026-06-15)

**Where we are: Phases 0–6 are COMPLETE AND ACCEPTED — this tree is DONE.** Phase 6 (the s08
frontend report-ingestion worklist) accepted 2026-06-15 against the real 2026-06-09 production v3
report: the on-import reconcile (KI-0023 fix), name resolution, the three-block split, the
No-action collapsible, the proof-rank chips, and the 0-failing edge header all confirmed; the
verify-all / close-intervals writes were accepted at the prior 2026-06-09 sweep. All five phases
of the verification engine are now live-accepted.

**Where we are: Phases 0–5 are COMPLETE AND ACCEPTED.** Phase 2 (the browser static checker + the
per-author UI) + Phase 3 (the C++ engine survival extension, D27) + Phase 4 (the D36 engine
rank-ladder + per-kind §11.6 matrix) landed + accepted. **Phase 5 (the `kcdx_verify_all`
console-after-save-load sweep PRODUCER — per-row stream + D37 incremental JSONL flush → v3 report)
is DONE + ACCEPTED** (the 2026-06-09 live launch, `kcdx-dev_2026-06-09_22-33-38.log`): 5.1 the v3
schema (7-state enum + method_rank/invoke fields + the D37 incremental contract, f07d20c), 5.2 the
`kcdx_verify_all` command + the save-load precondition gate (187ad3d), 5.3 the producer end-to-end —
per-row stream + incremental flush + v3 finalize, the new `src/survival_report.{cpp,h}` unit
(9b0ee59). The full-run signal: `ACCEPT-RESULT: PASS kcdx_verify_all — v3 report finalized (157/157
rows, 141 passing)`; `RESULT name=cap-95-verify-all-command verdict=PASS`; the report
`kcdx-verify_2026-06-09_22-33-38.json` VALIDATES (schema_version 3, complete, 157/157 rows). Phase-4
follow-up still open: TD-0009 (the engine↔browser agreement scoped to the 4 algorithm-identical
kinds; 3 superset kinds deferred). A Phase-6 worklist item surfaced by the sweep: kcdx_id 12
(`string_exec_autoexec_cfg`) read `failed` / `resolved_va_not_in_live_text` — a real DB-vs-binary
divergence the engine correctly surfaced (a maintainable row, not a producer defect). **Next: Phase
6** (the frontend report ingestion — the two-block worklist that imports the v3 report Phase 5 now
produces).

**Phases 4–5 RE-PLANNED 2026-06-09 against D36 (active-attempt verification).** The original single
Phase 4 (a static boot-automatic sweep) was superseded by the D36 design revision (the 7-state
verdict enum + the 5-rank proof ladder + the live-exercise tier + the console-after-save-load
trigger). It is now TWO phases: **Phase 4** extends the engine checker into the rank-ladder + the
per-kind matrix (§11.6) — engine-only, synthetic boot self-tests; **Phase 5** is the
`kcdx_verify_all` console-after-save-load sweep that drives the ladder, streams per-row to the
console, and emits the v3 report. The frontend report-ingestion phase renumbered Phase 5 → **Phase
6** (its content unchanged; it consumes the v3 report Phase 5 produces). Design authority:
`data/maintainer-tool/design.md` D36 + §11.6 + D33(rev) (`3ede052`).

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
| [Phase 4 — Engine rank-ladder + per-kind matrix (D36)](phase-04-engine-rank-ladder/README.md) | DONE | 3921d62 — 4.1 75ddd8c (7-state enum + ceiling + precise not_applicable) / 4.2 36d61a5 (rank-1 observed: HOOKED + CALLED-by-kcdx) / 4.3 cdabde8 (rank-2 cvar safe-read + rank-3 vtable_base walk) / 4.4 3921d62 (per-kind §11.6 dispatcher + the vtable_base reachability re-route). cap-84 sub-checks 10-16. **ACCEPTED at the 2026-06-09 live launch** (kcdx-dev_2026-06-09_21-07-33.log): cap-84-survival-dispatch PASS + cap-85 PASS (no regression). The 2 suite FAILs (CAP-20-addrname / CAP-28-typo-fails-fast) are pre-existing other-lane rows, not Phase-4. |
| [Phase 5 — Console-triggered batch sweep + v3 report (D36)](phase-05-plugin-sweep/README.md) | DONE | 9b0ee59 — 5.1 f07d20c (v3 schema) / 5.2 187ad3d (kcdx_verify_all command + save-load precondition) / 5.3 9b0ee59 (producer: per-row stream + D37 incremental flush + v3 finalize; new src/survival_report.{cpp,h}). **ACCEPTED at the 2026-06-09 live launch** (kcdx-dev_2026-06-09_22-33-38.log): `ACCEPT-RESULT: PASS kcdx_verify_all` (157/157 rows, 141 passing) + cap-95 PASS; the v3 report validates. 1 sweep `failed` row (kcdx_id 12, real DB-vs-binary divergence — a Phase-6 worklist item, not a defect). |
| [Phase 6 — Frontend report ingestion](phase-06-report-ingestion/README.md) | DONE | All steps landed: 6.1 FE:6e7f3b1 (s08 worklist) / 6.2 42ebd79+FE:17bfa12 (/confirm/batch) / 6.2a-fix 69f54d2 (valid_through authored column, D40) / 6.2b 201e646 (reverify_resolver + /save/reverify-batch preview, D39) / 6.3 FE:d8771ff (the two batch actions). **Milestone UI acceptance ACCEPTED 2026-06-15** on the real 2026-06-09 production v3 report: name resolution (D34), the three-block split + No-action collapsible + verdict-badge/proof-rank chips, the 0-failing edge header, and the KI-0023 on-import reconcile (a re-imported fully-acted report shows all 142 acted rows under "no further action" with no batch opened — ground-truth confirmed via the resolver's `already_acted` classification). The verify-all / close-intervals WRITES were accepted at the prior 2026-06-09 sweep run (their suite/ACCEPT logs landed); the worklist's reconcile + render + edge states are accepted this session. |
