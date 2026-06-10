# Restructure ledger reconciliation — 2026-06-10

A one-session doc-only reconciliation pass after a full open-items sweep found
the top ledger and several sub-trees stale. This ledger tracks the fix; delete
it once all rows are DONE and committed.

## Findings (verified against git + files, 2026-06-10)

- Top README Phase 11 row already corrected NOT STARTED → IN PROGRESS (commit
  `75d3f95`) — but the "live-confirmed" wording overclaims (P2 `3f6e09e`/`54d98c8`
  + P3-s1 `18c0ac5` are `[unverified — pending launch]`; only P3-s2 keystone
  `3b99fea` is live-confirmed).
- `phase-09.3-namespaces/README.md` header says `NOT STARTED` — STALE; the phase
  is DONE (closed `1c71269`, all 7 steps live-verified) and the top ledger agrees.
- `phase-11-shim-vm/README.md` header says `READY` — should read IN PROGRESS.
- Phase-11 P4 is re-scoped to FOUNDATION-ONLY (event gate + RegisterRuntimeOverlay
  CAS + cap-82), but its README + step-1/step-2 docs still describe the OLD
  "early slot + boot swap" full scope (the slot-runner moved to P5 steps 5/7).
- Top README named only 3 of 8 Active TDs; TD-0006 + TD-0007 are phase
  prerequisites (TD-0006 ↔ the 9.4 find-corpus split; TD-0007 gates P11 P6).
- asset-system Phase 3 is NEEDS REWORK (served-`.lua` execute unconfirmed =
  KI-0006, bundled into P11 P7).

## Reconciliation ledger

| # | Chunk | Status | Commit |
|---|---|---|---|
| 1 | Fix stale headers (9.3 → DONE, P11 → IN PROGRESS) + soften top-README "live-confirmed" overclaim | DONE | (this commit) |
| 2 | Surface the full open-items map in the top README (5 unnamed TDs, TD-0006/0007 as prerequisites, asset-system Ph3 NEEDS REWORK, P11 internal owed items) | DONE | (this commit) |
| 3 | Re-author Phase-11 P4 README + step-1/step-2 docs to the foundation-only scope (redirect slot/serve content to P5) | DONE | (this commit) |

Chunks 1+2 landed in `1348c55` (top-restructure-doc edits). Chunk 3 is the
P4-sub-tree re-author (this commit). None touched the in-flight P5 step-7/8/9
edits (unrelated, left for their owner).

**RECONCILIATION COMPLETE.** All three chunks DONE. This tracker can be deleted —
the reconciled state lives in the README + sub-tree docs, and the audit trail is in
the two commits. (Left in-tree until the next pass touches the restructure root, so
the just-completed sweep is discoverable; not a permanent lifecycle artifact.)
