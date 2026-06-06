# P4 step 2 — boot-asset swap serves (KI-0005) + AP14 warn decision

> **BLOCKED on the new Phase 5 — read [`../RESUME-STATE.md`](../RESUME-STATE.md) first.**
> This step consumes the worker early-slot RUNNER, whose shape was deferred to
> the new Phase 5 (`bring-forward-early-capability`). It rides the Phase-5-settled
> slot shape; it cannot build until that design + the runner land. P4 step 1 now
> ships only the foundation (gate + CAS); the slot this step needs comes from
> Phase 5.

## What

Deliver the user-required capability: a `kcdx.assets.replace` registered in the early
Lua slot (step 1) WINS a boot-asset open — the resolver serves the overlay with
`rt=HIT` for an asset the engine opens once at `CSystem::Init`. This is KI-0005's
deferred Lua-runtime boot serve. Then decide the AP14 teaching warn's fate against
the now-observable early-slot behavior.

## Scope

- Confirm the boot-open path observes the early-slot's runtime-store registration
  because it is GATED on the slot's readiness event (step 1's gate), not because the
  register happened to run first. The store + resolver consult already exist; KI-0005's
  gap was the author's Lua running after the boot open — and the FIX is step 1's
  mandatory event gate (the boot open blocks until the slot signals), NOT a timing
  adjustment ("run the slot earlier"). A boot serve that works only because of
  wall-clock ordering is the cross-thread race the gate exists to kill (design §5).
- Re-instrument the resolver per
  `_research/probe-archive/ki0005-resolver-dds-observer.md` to confirm the boot asset
  opens with `rt=HIT` from the early-registered overlay (the user sees the
  replacement render).
- **AP14 warn decision (build-time, lean narrow):** with the early slot serving boot
  assets, narrow the warn to LATE-slot boot targets (an early-slot boot replace is
  valid → no warn; a late `plugin.lua` boot replace still can't win → keep the warn
  pointing at the early slot or the sidecar). Remove only if the early slot fully
  subsumes the late path AND a late-slot boot target is structurally rejected (never
  a silent no-op). Decide against the observed subsumption.
- Close the KI-0005 boot-serve deliverable (`before-game-hooks.md` §6b item 4).

## Test bar

A `test-plugins/cap-NN-boot-asset-swap/` regression: an early-slot `kcdx.assets.replace`
of a boot asset serves with `rt=HIT`, and the boot open observed the slot's readiness
event SIGNALED before resolving (the gate held — the same falsifiable proof as step
1's order-inversion row, design §5; `rt=HIT` via the GATE, not via wall-clock luck).
Self-reports via the canonical signal. **A KI-0005-regression row** asserts a
LATE-slot boot target still warns (FAILS on a silent no-op — the exact regression
KI-0005's warn prevents). **User-facing acceptance** (`.claude/rules/ux-first-class.md`):
the user confirms the visible boot asset (e.g. the menu logo) renders replaced via the
runtime path; the agent confirms `rt=HIT`. Confirmed by the user's launch + the
agent's dev-log read.

## Dependencies

P4 step 1 (the early slot must exist + run before the boot open for the register to
win it). The asset seam + runtime store already exist and are gated (KI-0005 facts).

## Design authority

[`../lua-vm-design.md`](../lua-vm-design.md) §7.1 (the boot-swap delivery + the AP14
warn decision) + [`../../before-game-hooks.md`](../../../before-game-hooks.md) §6b
(the named deliverable) + the asset design `asset-replacement.md` §5.1/§5.4
(take-effect = the Lua-VM-lifecycle boundary). Build the asset-replace surface to the
asset design, not this doc's summary.

## UX

The capability is "declare a boot-asset replacement in early Lua, it renders." The
narrowed AP14 warn TEACHES (use the early slot or the sidecar) on the still-impossible
late path — never a silent non-serve (the KI-0005 failure mode). The user perceives
the swap; that perception is the acceptance.

## RE / author-burden note

No author hex — the author declares a vpath + a replacement asset. No new DB rows.

## Reference

[`../plan-spec.md`](../plan-spec.md) coverage rows E12, E13; design §7.1.
