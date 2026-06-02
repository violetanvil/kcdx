---
paths:
  - "**/*"
---

# Input validation policy — all data crossing a trust boundary is untrusted

Every value crossing a trust boundary is validated before any processing or allocation. A trust boundary is any place data enters from outside the code's control: external process / FFI output (third-party code loaded into the process — untrusted even though it shares the address space), on-disk manifests / files an external party authored, network payloads, an IPC surface between layers, and a repo-authored detector/config the engine reads. The repo names its concrete boundaries + caps in the relevant append; this rule is the language-agnostic floor.

## Validate length/size BEFORE allocating

For every variable-length input, reject an over-cap count before allocating for it — never allocate first and check after (a source reporting a billion entries has already won by then). Caps are named constants the repo provides (the append / its spec); the discipline — cap-check precedes allocation — is this rule.

## Validate the content, not just the structure

Structural deserialization (a serde derive, a JSON parse) checks shape, not semantics. After deserializing, validate the values:

- **Strings crossing an FFI/wire boundary** — well-formed encoding (a checked decode, never an unchecked cast), no interior NULs, length under the per-field cap, and a charset constraint where the value becomes a path/identifier (no path separators, shell metacharacters, or zero-width characters).
- **Paths** — reject `..` traversal segments; reject paths resolving outside the permitted root; reject a symlink pointing outside the root after canonicalization. A malformed path from an external source is a contract violation — log (`.claude/rules/logging.md`) + skip, don't trust.
- **A value that becomes a filesystem name** (a profile/identifier the user supplies) — validate the charset before it touches the filesystem; validation prevents path injection.

## No shell/command execution from untrusted data

Data received from a network, an external process, or an external-authored manifest is NEVER passed to a process-spawn argument, a shell-execute API, or any API that interprets a string as a command or an executable path. Absolute rule, no exceptions — if you believe you must execute something derived from received data, stop and ask the user.

## Defend at the boundary even when the source "should" behave

A misbehaving or compromised external source can flood a callback / event surface. The receiving side enforces its own caps (rate limiting, coalescing, debounce, truncation) at the boundary — backpressure being the source's responsibility does not remove the receiver's duty to defend.

## What this is NOT

- NOT the repo's concrete cap values, boundary list, or sanitizing pipeline — those are the append's (or its spec's).
- NOT a substitute for the trust-boundary handling inside a specific subsystem's design — this rule is the floor every boundary clears; a subsystem may add stricter rules above it.
