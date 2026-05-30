# Phase 9.2 — unified named-target surface (kcdx.declare + smart resolver)

**Status: IN PROGRESS.** Detail: [`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.2".

The unified named-target surface: `kcdx.declare` (author entries) + a
smart-resolver sub-verb shape `kcdx.<verb>.<name>.<mode>` on hook/bytes/code over
one table (curated refdb rows + author declarations), routed through the
owner-aware `address_library::ResolveByName`, with a C++ mirror. Phase 9.7
(curated-target sub-verb resolver) merged into this phase — the declare store and
the smart resolver are two halves of one surface.

Most of the surface is live; the lone residual is the `kcdx_scan` **console
command** (in-game iterative AOB discovery — the discover-then-declare loop is
gated behind it). The `kcdx.scan{...}` Lua diagnostic equivalent already ships;
the console verb is the explicit author-facing in-game discovery form.

## Step ledger

| Step | Status | Commit |
|---|---|---|
| declare store + smart resolver + sub-verb surface + C++ mirror | DONE | 2dac79b |
| engine-direct AP4 carve-out (unblocks cap-59-fires + cap-64/65) | DONE | 1c01c9d |
| kcdx.scan{...} Lua diagnostic | DONE | — |
| kcdx.declared(name) value accessor | DONE | — |
| [kcdx_scan console command](step-1-kcdx-scan-console.md) | NOT STARTED | — |

Phase 9.2's row in [`../README.md`](../README.md) flips to `DONE` when
`step-1-kcdx-scan-console.md` lands.

`src/survival_pass.{cpp,h}` is built but it is the **curated-track** safety
mechanism only (per §11.8.3); Track-2 declared entries do not feed survival_pass
— they go through the badge / recovery-rollback path
([`../track2-recovery-rollback.md`](../../track2-recovery-rollback.md), not yet
written).
