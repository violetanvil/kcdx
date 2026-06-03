# Phase 3 — regression coverage

The permanent regression net for the asset system (`test-suite.md`): a suite-gated
`test-plugins/` plugin (or plugins) that exercises every asset-system capability
and self-reports, so a future change can never silently regress it.

## Step ledger

Status: `NOT STARTED` · `BLOCKED` · `DONE` · `NEEDS REWORK`. Commit = short hash
when `DONE`, `—` otherwise.

| Step | Status | Commit |
|---|---|---|
| [9 — permanent asset-system test plugin(s) + matrix rows](step-9-test-plugins.md) | NOT STARTED | — |

## Phase verification gate

- **Build green** + the suite self-reports via the canonical acceptance signal
  (`acceptance-signal.md`, `test-suite.md` — `ACCEPT-RESULT` / `ACCEPT-SUITE` /
  `suite: X/Y passing` read by the agent from `kcdx-dev.log`).
- The rows prove, each with a falsifiable claim (AP15): override applies (US-1),
  a cross-plugin reference resolves (US-3), the chain/conflict winner serves +
  loser is reported (US-4), and a stock Nexus/Workshop pak resolves unchanged
  through the overlay-miss fall-through (US-7).
- Phase done when every row is recorded in `test-plugins/README.md` and the suite
  is green at the landing commit.
