# maintainer-tool per-field divergence diff (D45)

The s04 `[Fix ▸]` per-field divergence diff: when the maintainer opens the field editor from a
`failed` s08 verification-report row, the editor surfaces WHICH recorded `address_versions` field
diverged from the running build — **recorded-vs-actual, inline at the field to edit** — plus a top
"What diverged" summary banner. Re-derived in-browser against the report's divergent DLL;
frontend-only (no engine change, no report-schema bump).

**Settled design:** `data/maintainer-tool/design.md` D45 (`6ac2501`) +
`data/maintainer-tool/ui/screens/s04-field-editor.md` §"Arriving from a failing report row" +
`data/maintainer-tool/ui/screens/s08-verification-worklist.md` §"The Fix flow carries context and
returns". Shared spec + the design→step coverage map: [`plan-spec.md`](plan-spec.md).

**Gate:** every phase is in the SEPARATE gitignored frontend repo (`data/maintainer-tool/frontend/`);
the gate is `npm run build` + `npx vitest run` (NOT `pwsh ./build.ps1`). The kcdx ledger row
references the FE commit as `FE:<hash>`.

## Status ledger — phase-grain (the canonical completion surface)

| Step | Status | Commit |
|---|---|---|
| Phase 1 — Probe the two unverified mechanisms (divergent-DLL behavior + per-field attribution) | DONE | dea42f7 |
| Phase 2 — The `fixDivergence` per-field attribution + diff-model worker (pure logic) | DONE | FE:3d89897 |
| Phase 3 — Wire the report's-DLL context + render the inline diff + the banner (UI; milestone UAT) | BLOCKED | FE:3c53e22 |

Both Phase-3 steps BUILT (3.1 wiring `FE:6e4786f`, 3.2 render `FE:3c53e22`); gate green (582/582).
`BLOCKED` on the **milestone UAT** only — the Phase-3 gate requires the user-facing acceptance (s04
spec + `.claude/rules/ux-first-class.md`: Phase 3 → DONE on that acceptance, not build-green alone).
Flips to DONE when the user walks the `[Fix ▸]`-on-a-failed-row → no-DLL-prompt → link-DLL → inline
per-field recorded-vs-actual + "What diverged" banner flow and accepts it.

Per-phase step-grain ledgers: [phase-01-probe/](phase-01-probe/README.md) ·
[phase-02-diff-model/](phase-02-diff-model/README.md) ·
[phase-03-render/](phase-03-render/README.md).

`/plan` authored this tree; it builds nothing. Run `/execute` (or `/feature`) per step with the step
doc as the `Source work-item`; landing a step flips its ledger row (the orchestrator writes the row,
not a human).
