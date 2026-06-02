---
paths:
  - "**/*"
---

# Polling policy — event-driven is the default; polling needs explicit user approval

Event-driven is the default. **Polling is forbidden except on explicit, direct user approval, per instance.** Before writing any loop that samples state on an interval, stop, identify it as polling, and surface it for the user to approve — the agent NEVER self-approves polling. A `// approved:` marker is the RECORD of a user sign-off already given, never a substitute for asking. Polling introduces latency jitter, wastes CPU, and masks the real event structure of the system. The repo names its concrete event primitives in the relevant append; this rule is the language-agnostic floor.

## Patterns that ARE polling — recognize before writing

Misclassification is the failure mode — these get rationalized as "periodic work" or "just a sleep." Each is polling; each needs the approval flow below before it is written.

- **Loop with sleep** — `loop { sleep(d); do_thing(); }`, any duration, sync or async, anywhere.
- **Periodic ticker** — an interval timer driving anything, even wrapped in a select.
- **Retry with sleep** — `for _ in 0..N { if try()? break; sleep(d); }`.
- **Atomic-flag spin** — `while !flag.load() { ... }`, even with a CPU-yield hint. (No sleep, still polling — it just doesn't yield.)
- **Async try-receive loop** — `loop { match rx.try_recv() {...} sleep().await; }` — use a blocking `recv().await` instead.
- **Watch-file-for-changes** — stat-the-file-on-a-timer — use an OS filesystem-event API.
- **Wait-for-state-propagation in tests** — `for _ in 0..200 { if ready break; sleep(); }` — a symptom of a missing event source in production code, NOT a test-only convenience.
- **"Is it done yet" check** — `loop { if worker.done() break; sleep(d); }` — use a completion signal from the worker.

**Test code is not exempt.** A test that polls for state usually signals the production code is missing an event source — surface that.

## The default: every state change has a natural source event

Reach for the event first: a filesystem-event API for on-disk changes; a direct callback/invocation for a fired event; awaiting an async handler for completion; a level-triggered channel (current value on subscribe + notified on change) for state; a one-shot signal for completion; a blocking receive for channel data; a multi-future select for "whichever happens first." Cross-thread state changes travel over a channel the receiver waits on — never a timer. **Shared mutable state read in a loop IS polling** — if you are checking a lock in a loop, the problem is modeled wrong; restructure so the change is delivered as a message.

## What requires explicit, direct user approval — per instance

Any polling loop. The agent does NOT decide polling is acceptable on its own. When you believe polling is genuinely necessary:

1. **State the resource or condition** you need to observe.
2. **Explain why there is no event source** — name the OS API / library / protocol you checked that lacks one.
3. **Propose the interval and the task structure.**
4. **STOP and wait for the user's explicit approval before writing the loop.** This is a design decision the user owns (`.claude/rules/design-authority.md`) — surface it, never self-grant.

Only AFTER the user's direct sign-off, annotate the loop/sleep line with `// approved: <reason>` (or the repo's marker). The marker records an approval that already happened; it is never the agent approving itself. The correct shape for approved periodic work uses a cancellation-aware select (an interval arm + a shutdown/cancellation arm) so the task responds to cancellation immediately — never a bare `sleep` in a loop (which blocks cancellation and makes shutdown nondeterministic).

## No busy-wait / spin loops

Spin loops are forbidden outside a lock-free structure explicitly designed for one (a bounded-spin-count primitive). Every wait has a proper blocking or async primitive — use a channel, condvar, or notification, not a spin on a flag.

## What this is NOT

- NOT the concurrency rule (`.claude/rules/concurrency.md`) — that owns the channels-vs-shared-state + memory-ordering + cancellation discipline; this rule owns "don't sample on a timer." They cite each other at the boundary (a loop checking shared state is both a concurrency smell and polling).
- NOT a ban on the repo's sanctioned periodic primitive when the user has approved it — the marker + the cancellation-aware shape make an approved interval legitimate.
