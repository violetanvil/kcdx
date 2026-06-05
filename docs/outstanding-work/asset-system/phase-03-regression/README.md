# Phase 3 — regression coverage

The permanent regression net for the asset system (`test-suite.md`): a suite-gated
`test-plugins/` plugin (or plugins) that exercises every asset-system capability
and self-reports, so a future change can never silently regress it.

## Step ledger

Status: `NOT STARTED` · `BLOCKED` · `DONE` · `NEEDS REWORK`. Commit = short hash
when `DONE`, `—` otherwise.

| Step | Status | Commit |
|---|---|---|
| [10 — permanent asset-system test plugin(s) + matrix rows](step-10-test-plugins.md) | BLOCKED → Phase 11 — the matrix + cap-77 + comp-17 landed (`2087368`); CAP-77-keyed + COMP-17 PASS; the one unconfirmed acceptance criterion (a served `.lua` EXECUTING, design §3) is **KI-0006**, BUNDLED into Phase 11 (FIX A collapses the dual-runtime that underlies the confirmed cross-CRT-free hazard + reworks the serve-execute area; user-approved deferral 2026-06-05). Step 10 returns to DONE when KI-0006's Phase-11 re-attempt lands a confirmed serve-AND-EXECUTE row. | 2087368 |

## Phase verification gate

- **Build green** + the suite self-reports via the canonical acceptance signal
  (`acceptance-signal.md`, `test-suite.md` — `ACCEPT-RESULT` / `ACCEPT-SUITE` /
  `suite: X/Y passing` read by the agent from `kcdx-dev.log`).
- The rows prove, each with a falsifiable claim (AP15): add-new applies (loose lane,
  HOOK 2), replace-vanilla applies (pak/mount lane via HOOK 1's redirect — US-1), a
  cross-plugin reference resolves (US-3), the chain/conflict winner serves + loser
  is reported (US-4), and a stock Nexus/Workshop pak resolves unchanged through the
  HOOK-1 MISS fall-through (US-7).
- Phase done when every row is recorded in `test-plugins/README.md` and the suite
  is green at the landing commit.
