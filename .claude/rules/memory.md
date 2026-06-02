---
paths:
  - "**/*"
---

# Memory and allocation policy — hot paths are allocation-free after init

Heap allocation inside a real-time or tight loop causes unpredictable latency spikes from the allocator. Any hot path (a per-frame pipeline, a per-event batch, a tight scan/enumeration loop) is allocation-free after initialization. This rule applies wherever a hot path exists; a repo with no hot path today still inherits the discipline for when one lands. The repo names its concrete hot paths + buffer-pool mechanism in the relevant append; this rule is the language-agnostic floor.

## The rule: allocate before the loop, never inside it

All buffers and heap-allocated structures a hot-path stage uses are allocated during initialization — before the loop is entered. Inside the loop body, only pre-allocated memory is reused (clear-and-refill, not allocate-fresh). A function the loop calls that returns a freshly-owned collection/string is a hidden allocation — check its signature; refactor it to write into a caller-provided buffer.

## Where allocation is unrestricted

Outside hot paths, allocation is free: one-shot user-driven handlers, initial/startup loads, cache population, error paths, and UI render (subject to the UI layer's own performance discipline). The rule scopes to the hot path, not the whole program.

## Variable-size payloads on a hot path: a pre-allocated pool, not a fresh allocation

If a hot path streams variable-size events, use a pre-allocated ring of fixed-size buffers: the source checks one out and writes into it; the dispatcher returns it after delivery. A payload exceeding the pre-allocated size is logged (`.claude/rules/logging.md`) and dropped — never a fall-back heap allocation on the hot path.

## Reviewing a hot-path stage for allocations

Before marking a hot-path stage complete, search its loop body for the language's allocation shapes — fresh-collection constructors, owning conversions (to-owned / to-string / clone on a heap type), boxing/reference-counting constructors, and iterator-collect — plus calls returning an owned collection/string. Any found → refactor to a `&mut` buffer argument or a pre-allocated pool, and present the allocation-free evidence in the stage's verification.

## What this is NOT

- NOT a mandate to micro-optimize cold paths — outside the hot path, allocation is unrestricted; over-pooling cold code is wasted complexity.
- NOT the logging rule (`.claude/rules/logging.md`) or the polling rule — cited at the hot-path boundary (a hot path neither logs nor samples per iteration); this rule owns allocation.
