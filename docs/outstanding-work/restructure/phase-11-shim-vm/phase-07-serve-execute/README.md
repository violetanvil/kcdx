# Phase 6 — served-.lua execute confirmation (KI-0006)

The last open asset-system capability. With FIX A's single runtime (Phase 5) and the
early kcdx-owned slot (Phase 4), confirm a served `.lua` EXECUTES end-to-end — via the
instrumentable kcdx slot, NOT the engine's crashing mod-init loader. KI-0006's
execute-leg is the deliverable; a residual crash is root-caused only if it reproduces.

## Step ledger (step-grain)

Status: `NOT STARTED` · `BLOCKED` · `DONE` · `NEEDS REWORK`. Commit = short hash when
`DONE`, `—` otherwise.

| Step | Status | Commit |
|---|---|---|
| [1 — serve-AND-execute via the kcdx slot; KI-0006 execute-leg](step-1-serve-execute-confirm.md) | NOT STARTED | — |

## Phase verification gate

- **Build green + the served-`.lua` executes:** a `.lua` the kcdx early slot serves
  runs end-to-end, self-reporting a file-scope marker the agent reads from
  `kcd.log`/`kcdx-dev.log` — proving SERVE-AND-EXECUTE (KI-0006's open criterion).
- PROBE Q stays silent (the serve-execute path introduces no sentinel).
- **If a crash reproduces post-FIX-A:** it is root-caused in mechanism terms (AP17,
  `.claude/rules/anti-patterns.md`) with the cross-CRT variable eliminated — the
  surviving evidence (the `WHGame+0xB2DBA0` victim site, the cap-78-`overlay_entry`
  correlation) is the starting point. **If no crash reproduces:** KI-0006 closes on
  the confirmed execute (the hazard class structurally collapsed by FIX A).
- KI-0006 moves to `closed/` + reindexed in the same change that lands the
  confirmation (`.claude/rules/doc-organization.md`); the asset-system Phase 3 step-10
  row returns to DONE.
- A permanent regression row self-reports the serve-execute.
