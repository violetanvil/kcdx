# P5 step 6 — boot-asset serve via the early slot (KI-0005) + the AP14 warn decision

## What

Deliver KI-0005: a `kcdx.assets.replace` registered in the `lua_before` early slot
(step 5) WINS a boot-asset open — the resolver serves the overlay with `rt=HIT` for an
asset the engine opens once at `CSystem::Init` and caches. The early slot runs before
the boot open (step 5) + the boot-open path is GATED on the slot's readiness event
(Phase-4 foundation), so the runtime store is populated when the engine opens the boot
asset. Then decide the AP14 teaching warn's fate against the now-observable early-slot
serve.

## Scope

- Confirm the boot-open path (`asset_overlay.cpp` HOOK 1/HOOK 2, game-main) observes
  the early-slot's runtime-store registration BECAUSE it is GATED on the slot's
  readiness event (the Phase-4 event gate), not because the register happened to run
  first — a serve that works only by wall-clock ordering is the forbidden race
  (`.claude/rules/concurrency.md`).
- Re-instrument the resolver per `_research/probe-archive/ki0005-resolver-dds-observer.md`
  to confirm the boot asset opens with `rt=HIT` from the early-registered overlay.
- **AP14 warn decision (build-time, lean narrow):** with the early slot serving boot
  assets, narrow the warn to LATE-slot boot targets (an early-slot boot replace is
  valid → no warn; a late `plugin.lua` boot replace still can't win → keep the warn
  pointing at the early slot or the sidecar). Remove only if the early slot fully
  subsumes the late path AND a late-slot boot target is structurally rejected (never a
  silent no-op — the KI-0005 regression).
- Close the KI-0005 boot-serve deliverable.

## Test bar

A `test-plugins/cap-NN-boot-asset-swap/` (suite-gated): an early-slot
`kcdx.assets.replace` of a boot asset serves with `rt=HIT`, and the boot open observed
the slot's readiness event SIGNALED before resolving (the gate held — the same
falsifiable order-inversion proof as the Phase-4 gate; `rt=HIT` via the GATE, not
wall-clock luck). A KI-0005-regression row asserts a LATE-slot boot target still warns
(FAILS on a silent no-op). PROBE Q silent.

## UX (user-facing — `.claude/rules/ux-first-class.md`)

The capability is "declare a boot-asset replacement in early Lua, it renders." The
**user-facing acceptance:** the user sees a boot asset (e.g. the menu logo) render
REPLACED via the runtime path; the agent confirms `rt=HIT` from the dev log. The
narrowed AP14 warn TEACHES (use the early slot or the sidecar) on the still-impossible
late path — never a silent non-serve (the KI-0005 failure mode). The user perceives
the swap; that perception is the acceptance.

## Dependencies

P5 step 5 (the `lua_before` early slot must run + register the asset before the boot
open) + the Phase-4 foundation (the event gate orders the slot vs the boot open; the
CAS makes the worker write safe). The asset seam + runtime store already exist
(KI-0005 facts).

## Design authority

[`bring-forward-design.md`](bring-forward-design.md) §6 US-5 (the boot-swap delivery)
+ §7.4 (the gate orders the slot vs the boot open) + the asset design
[`../../../../design/asset-replacement.md`](../../../../design/asset-replacement.md)
§5.1/§5.4 (take-effect = the Lua-VM-lifecycle boundary). Build the asset-replace
surface to the asset design, not this summary.

## RE / author-burden note

No author hex — the author declares a vpath + a replacement asset. No new DB rows.

## Reference

[`../plan-spec.md`](../plan-spec.md) §"Phase 5" row "Boot-asset serve via the early
slot (KI-0005) + AP14 warn"; design §6 US-5, §7.4.
