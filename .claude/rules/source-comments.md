---
paths:
  - "src/**"
  - "lib/**"
  - "crates/**"
  - "packages/**"
  - "app/**"
  - "internal/**"
  - "pkg/**"
---

# Source comments — written for the next agent, signal-dense, never restating the code

A production-source comment carries what the code cannot, for the next reader. The source counterpart to `.claude/rules/imperative-only.md` (which scopes to `.claude/` bodies); the repo names its tag vocabulary + doc-comment convention in the relevant append.

## Rules

- **Comment the why, not the what.** Carry intent / INVARIANT / SAFETY precondition / SOURCE of a non-obvious external fact (`.claude/rules/dependencies.md` — doc URL or vendored path, never recall) / known BUG. Never restate what the next line says — it dilutes and rots. Code needing a comment to say what it does usually needs clearer code.
- **Signal-dense.** Re-read on every open; write the tightest phrasing that carries the signal. No preamble, no narration.
- **Tag load-bearing comments** with the repo's scannable vocabulary (default `WHY`/`INVARIANT`/`SAFETY`/`SOURCE`/`BUG`) so a reader greps the file's invariants and hazards.
- **No lift-history, no commented-out code, no doc-generator ceremony in internal source.** Version control holds history and dead code; a published-API surface follows the repo's API-doc convention.

## What this is NOT

- NOT `.claude/rules/imperative-only.md` — that scopes to `.claude/` bodies; this to production source. Same economy, different artifact.
- NOT the required correctness annotations elsewhere — `.claude/rules/concurrency.md` (lock-order, relaxed-ordering) and `.claude/rules/logging.md` (swallowed-error reason) own those; this governs how all comments read, not whether those exist.
- NOT the repo's tag vocabulary / doc-comment tooling / API-doc convention — the append's.
- NOT a mandate to comment more — default is no comment; one appears only when it carries what the code cannot.
