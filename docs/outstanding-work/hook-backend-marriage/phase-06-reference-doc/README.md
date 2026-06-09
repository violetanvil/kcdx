# Phase 6 — backend reference doc

**Intent.** The backend layer is a new responsibility unit; it gets its reference
/ subsystem doc (`structure-by-responsibility.md` §6 — a new unit's doc lands with
the unit). Last phase: the layer is built (Phases 1–5), now it is documented for
the next maintainer — the interface, the `InstallRuntime` seam, the two backends,
the routing, the foreign-hook path, why install is per-hook (no batch), and how to
add a third backend.

Shared spec: [`../context.md`](../context.md).

## Status ledger (step-grain)

| Step | Status | Commit |
|---|---|---|
| Step 11 — backend-layer subsystem/reference doc | DONE | `554288f` |

## Verification gate

- Step 11: the reference doc exists at the repo's reference-docs home
  (`docs/hook-backend.md`), covers the `IDetourBackend` contract + the
  `InstallRuntime` seam + both backends + the routing predicate + the foreign-hook
  path + why install is per-hook (no batch), and a maintainer can answer "how do I
  add a third backend?" from it. Docs-only — no build/launch (the doc is the
  deliverable). **DONE.**
