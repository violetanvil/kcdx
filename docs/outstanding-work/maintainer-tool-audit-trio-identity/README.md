# maintainer-tool audit-trio identity + verified_date model

**Intent:** implement the settled audit-trio identity + verified_date model (TRD D17a/D17b,
landed `c99438c`) in the maintainer-tool frontend — `verified_by` becomes the signer identity
sent as the git commit author (closing the D17 FE gap), and `verified_date` becomes a read-only
system fact shown only when the row is verified.

Shared spec + the full design→step coverage map: [`plan-spec.md`](plan-spec.md).

All steps land in the SEPARATE gitignored frontend repo (`data/maintainer-tool/frontend/`) —
gated by `npm run typecheck` + `npx vitest run` + `npm run build`, NOT kcdx's `build.ps1`. The
kcdx ledger references the FE commits with `FE:<hash>`.

## Phase-grain ledger

| Phase | Status | Commit |
|---|---|---|
| [Phase 01 — audit-trio identity + verified_date](phase-01-audit-trio-identity/README.md) | NOT STARTED | — |
