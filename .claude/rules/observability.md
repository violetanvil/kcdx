---
paths:
  - "**/*"
---

# Observability — metrics are pushed event-driven to one collector; the same stream feeds every consumer

Applies ONLY to a repo with multiple long-running subsystems or a multi-stage pipeline whose runtime health/throughput is worth observing. Such a repo's subsystems emit metrics by PUSHING them, event-driven, to a single central collector — never by being polled from outside, and never each defining its own metrics type. Every consumer (a UI panel, the log, an alert) reads the SAME collected stream. The repo names its concrete metrics type, collector, channel primitive, and the per-operation summary fields in the relevant append; this rule is the floor. A repo with no subsystems/pipeline does not invoke this rule.

## One metrics type, one collector — subsystems do not define their own

There is a single shared metrics snapshot type, owned in a common location, and a single collector that merges every subsystem's contribution. A subsystem updates its slice and sends the update to the collector; it does NOT define a private metrics struct, and it does NOT expose a "come read my numbers" surface for an outside reader to scrape. One source of truth, merged centrally, broadcast once.

## Metrics are PUSHED on change, never POLLED from outside

A subsystem reports a metric by sending it over the repo's channel primitive when the value changes — a state transition, a discrete event (a stage swapped, an error occurred), a completed unit of work. No outside component samples a subsystem's state on a timer; no consumer refreshes on an interval. A timer that scrapes subsystem state is polling (`.claude/rules/polling.md`) — the collector receives pushes and the consumer subscribes to the merged stream, both event-driven.

## Hot-path counters aggregate locally, flush at a bounded rate

A per-frame / per-event / per-iteration metric is NOT sent on the hot path — sending an update per iteration is polling with extra steps and allocates/locks on the hot path (`.claude/rules/memory.md`, `.claude/rules/logging.md`). Aggregate it locally in an atomic counter or a rolling average and flush to the collector at a bounded cadence (a state transition, or at most a low fixed rate). Discrete one-off events (an error, a mode switch) are sent immediately as they happen.

## Every operation/session emits a summary on teardown

On the clean OR error teardown of an observed unit (a session, a pipeline run, a long operation), the collector emits one summary line of the final metrics at the repo's info level (`.claude/rules/logging.md`) — counts, durations, the salient rates. This gives a permanent per-run record without per-iteration logging; it is the same lifecycle-event-gets-one-info-line floor logging already requires, applied to the unit's metrics rollup.

## What this is NOT

- NOT the logging rule (`.claude/rules/logging.md`) — that owns every-failure-logged + event-driven log lines + the lifecycle info line; this owns the METRICS stream (pushed to a central collector, one type, consumed by all). They meet at the teardown summary, which is a logged lifecycle event carrying the metrics rollup.
- NOT the polling rule (`.claude/rules/polling.md`) — cited because scraping a subsystem's metrics on a timer is polling; this rule owns the push-to-collector shape that makes metrics event-driven.
- NOT the memory rule (`.claude/rules/memory.md`) — cited at the hot-path boundary (a metric is not sent per iteration); this owns where metrics flow, not the allocation discipline.
- NOT a mandate that every repo collect metrics — only a repo with subsystems/a pipeline whose health is worth observing invokes it. A single-shot CLI or a library does not.
- NOT the repo's concrete metrics type / collector / channel primitive / summary fields / flush cadence — those are the repo's (the relevant append). This rule is the language-agnostic floor.
