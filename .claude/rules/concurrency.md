---
paths:
  - "**/*"
---

# Concurrency discipline — memory safety kills data races, not logical ones

A memory-safe language's type system eliminates **data races** by construction. It does NOT eliminate **logical races** — TOCTOU, lost updates, lock-order inversion, a lock held across an await/yield, async-cancellation drops, missing memory ordering. This rule is the language-agnostic discipline for those; the repo names its concrete primitives (its channel types, atomic/ordering API, cancellation token, race-test tool) in the relevant append.

## Default: messages between units, not shared mutable state

Units (subsystems / modules / tasks) communicate by passing messages over the repo's channel primitives — NOT via a shared lock-guarded value reached across a boundary. Messages make ownership explicit (the sender no longer holds the value), eliminate lock-order inversion at boundaries, and eliminate lock-held-across-await deadlocks at boundaries.

**A shared lock-guarded value reached ACROSS a unit boundary requires explicit per-use user approval** — surface it (`.claude/rules/design-authority.md`), don't reach for it silently.

## Within a unit: atomics first, locks last

When state is shared internally within one unit:

1. **Counter / flag state** → an atomic with explicit ordering (below).
2. **Bounded message queue** → a bounded channel.
3. **Coordinated multi-field state** → a level-triggered watch primitive (current value on subscribe, notified on change).
4. **A lock as last resort** — its comment names every other lock that may be held when this one is acquired (lock-order documentation). A lock held across an await/yield is forbidden unless the primitive is explicitly the across-await flavor AND the comment justifies why it is correct here; never work around a "lock held across await" lint.

## Memory ordering — every relaxed ordering is justified

A relaxed ordering is correct ONLY for a counter nobody synchronizes against. For ANY atomic establishing a happens-before edge (a writer publishes data, a reader consumes it), use release on the writer + acquire on the reader (or the sequentially-consistent ordering when uncertain). **Every relaxed ordering carries a comment explaining why no happens-before is needed** — its absence is the tell that the ordering is wrong.

## Async cancellation — every cancellable arm is cancellation-safe

A future in a cancellable select arm can be dropped mid-execution; its cancellation must not leave shared state half-updated. Cancellation-UNSAFE shapes: a multi-step write to a guarded value where the later step matters; a send-then-await-ack the send made observable; a multi-await file write without atomic-rename (use temp-file + rename). Do cancellation-unsafe work in a stored, explicitly-cancellable task with cleanup — never rely on drop for correctness. **A spawned task whose handle is never stored (fire-and-forget) is forbidden in non-test code** — it makes shutdown nondeterministic.

## Staleness guard on async-fetch-then-write

Every "fetch async, then write the result" checks that the generation/epoch did not change while the fetch was in flight (capture a generation before the fetch; only write if it still matches). The canonical bug it prevents: a fast user-driven switch whose slow background work overwrites the new state with stale results.

## Concurrent primitives get a race-permutation test where the repo provides one

When a type uses locks/atomics for cross-thread coordination and the repo provides an interleaving-permutation test tool (named in the append), that type gets such a test — the only mechanical way to prove a concurrent type race-free. Where no such tool exists, the type's concurrency invariants are documented + reviewed.

## What this is NOT

- NOT the polling rule (`.claude/rules/polling.md`) — that owns "don't sample state on a timer"; this owns "coordinate correctly when you do share state." A shared value read in a loop violates both.
- NOT the repo's concrete primitives (channel types, ordering API, cancellation token, race-test tool) — those are the append's. This rule is the discipline; the primitives are the repo's.
