# Phase 5 — backend reference doc

**Intent.** The backend layer is a new responsibility unit; it gets its reference
/ subsystem doc (`structure-by-responsibility.md` §6 — a new unit's doc lands with
the unit). Last phase: the layer is built (Phases 1–4), now it is documented for
the next maintainer — the interface, the two backends, the routing, the
foreign-hook path, and how to add a third backend.

Shared spec: [`../context.md`](../context.md).

## Status ledger (step-grain)

| Step | Status | Commit |
|---|---|---|
| Step 9 — backend-layer subsystem/reference doc | NOT STARTED | — |

## Verification gate

- Step 9: the reference doc exists at the repo's reference-docs home, covers the
  `IDetourBackend` contract + both backends + the routing predicate + the
  foreign-hook path, and a maintainer can answer "how do I add a third backend?"
  from it. Docs-only — no build/launch (the doc is the deliverable).
