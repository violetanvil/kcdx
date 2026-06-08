# P5 step 3 — promote the phase model: lifecycle event per phase + reconcile existing messages

## What

Make the startup timeline REACTABLE: fire a lifecycle event at each author-reachable
phase (§4 of the design) through the existing message bus, so an author subscribes
via the existing lifecycle verb (`kcdx.on(event, fn)` Lua / `RegisterListener` C++,
full parity). Reconcile the existing lifecycle messages
(`PostLoad`/`PostPostLoad`/`InputLoaded`/`LuaReady`) as NAMED POINTS on this one
timeline — not a separate set. A subscription to an already-past phase fires
immediately (the `kcdx.dev.on_ready` discipline, generalized to every phase).

## Scope

- For each author-reachable phase (the ctx-B milestones — WHGame-mapped,
  Address-Library ready, kcdx-subsystems-ready, before_game early-slot — and the ctx-C
  live points), fire its lifecycle message at its `AdvanceTo` point. New phase
  messages append to `kcdxMessageType` (`include/kcdx/Interfaces.h`) at the END
  (AP11-safe, append-only).
- **Reconcile the existing messages as timeline points** — a build-time READ of each
  existing message's firing site (`PostLoad`/`PostPostLoad`/`InputLoaded`/`LuaReady`)
  to map it to its phase on the timeline; they keep firing as today, now NAMED on the
  one timeline (no duplicate event, no behavior change to existing subscribers).
- **Late-subscribe fires immediately:** a `kcdx.on`/`RegisterListener` for a phase
  that has already passed fires the callback now (read `g_phase`; if `>= ` the
  subscribed phase, fire-now; else queue). Generalizes the on-ready already-ready
  path to every phase.
- The phase event tokens are author-friendly stable names (e.g.
  `kcdx.on("kcdx.subsystems_ready", fn)`) reconciled from `init::Name()`.
- Lua + C++ parity (`.claude/rules/lua-api-surface.md`): the same events fire on both
  buses; a Lua plugin AND a C++ plugin (or one driving both) exercise it.
- Docs (`.claude/rules/docs-discipline.md`): the new events get their `docs/lua/` +
  `docs/cpp/` entries + glossary terms (the per-event entries; the whole-timeline doc
  is step 9).

## Test bar

A `test-plugins/cap-NN-phase-events/` (Lua) + a C++ mirror (parity): a listener
subscribed to `kcdx.subsystems_ready` FIRES at that phase, on the right thread (a row
reads the phase + tid at fire time — FAILS if it fired at the wrong phase or wrong
thread); a listener subscribed to an ALREADY-PAST phase fires IMMEDIATELY (a row that
subscribes late and asserts the fire-now path — FAILS if it never fires); an existing
message (e.g. `LuaReady`) still fires for an existing subscriber (a regression row —
FAILS if the reconciliation broke an existing subscriber). PROBE Q silent. Confirmed
by the user's launch + the agent's dev-log read.

## Dependencies

P5 step 2 (the kcdx-subsystems-ready phase must exist before an event can fire at it;
the worker must reach the phases the events fire at).

## Design authority

[`bring-forward-design.md`](bring-forward-design.md) §5.1 (react = a lifecycle event
per phase via the existing bus; late-subscribe fires immediately) + §4 (the
author-reachable phase set the events cover) + §8 claim 5 (the phase-token
reconciliation is a build-time read of the firing sites, provisional). Build to §5.1,
not this summary.

## RE / author-burden note

No author hex. The phase events fire from the engine's `AdvanceTo` points (no
game-binary target). The reconciliation reads existing message firing sites in kcdx
source. No new DB rows.

## Reference

[`../plan-spec.md`](../plan-spec.md) §"Phase 5" rows "Lifecycle event per
author-reachable phase", "Existing messages reconciled as timeline points",
"Late-subscribe-fires-immediately", "New phase kcdxMessageType values"; design §5.1,
§4, §8.5.
