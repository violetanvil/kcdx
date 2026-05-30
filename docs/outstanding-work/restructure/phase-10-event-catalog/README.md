# Phase 10 — `[[event]]` → `kcdx.on(...)` gameplay event catalog

**Status: NOT STARTED** (lifecycle events DONE; gameplay catalog not started).
Detail: [`../00-original-plan.md`](../00-original-plan.md) §"Phase 10". Addresses
`docs/design-gaps.md` gap #15.

`kcdx.on(<lifecycle_event>, fn)` is a shipped Phase 2 surface — the lifecycle event
catalog is wired through `src/messaging.cpp` and tested (cap-24 PASSes; every
save/load/post-load/input-loaded/etc. event registered). What's NOT built: the
10–15 NEW gameplay events. Each is one kcdx-owned hook against the underlying
CryEngine call path; plugins subscribe via `kcdx.on(...)` without each plugin
re-hooking the same function.

## Step ledger

| Step | Status | Commit |
|---|---|---|
| lifecycle event catalog (`kcdx.on` lifecycle bridge) | DONE | — |
| [1 — RE + hook the first gameplay events (damage / item / dialogue / combat / quest)](step-1-gameplay-events.md) | NOT STARTED | — |
| [2 — `cap-XX` gameplay-event subscription test](step-2-test.md) | NOT STARTED | — |

## Hash-tracking note

Each gameplay event's underlying hook is a function-name reference in the reference
DB; when KCD2 updates and changes the backing function, subscribers get the same
`on_changed` posture as a direct `kcdx.hook.*` call. A broken event gives a
teaching error naming the event + the underlying function; other subscriptions in
the same plugin keep working. Same survival contract as direct hooks, applied
uniformly.
