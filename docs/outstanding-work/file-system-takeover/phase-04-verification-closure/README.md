# Phase 4 — verification + closure

Ship the permanent regression coverage + the subsystem reference doc, and close
KI-0019/KI-0006 with a repro-clean launch and a root-cause mechanism paragraph.

Depends on Phase 3 (the full takeover is live).

Shared spec: [`../plan-spec.md`](../plan-spec.md).

## Step-grain ledger

| Step | Status | Commit |
|---|---|---|
| [4.1 — regression plugin(s) + matrix rows + subsystem doc](step-1-regression-docs.md) | NOT STARTED | — |
| [4.2 — close KI-0019/KI-0006](step-2-close-kis.md) | NOT STARTED | — |

## Verification gate (phase done when)

- 4.1: permanent `test-plugins/` rows exercise (a) a vanilla-pak asset read by
  kcdx, (b) a loose override winning, (c) a stock pak mod loading unchanged — each
  a falsifiable matrix row; the file-system subsystem reference doc lands
  (`.claude/rules/structure-by-responsibility.md`); the author-facing contract is
  verified unchanged (design §7). A launch confirms the matrix `suite: X/Y
  passing`, agent-read.
- 4.2: KI-0019 + KI-0006 each carry a Resolution section with the root-cause
  MECHANISM paragraph (AP17 — the cross-CRT straddle named, not "no longer
  crashes"), gated through `root-cause-verifier`; the repro launch is clean; the
  close ceremony lands (move to `closed/` + reindex, `.claude/rules/doc-organization.md`).
