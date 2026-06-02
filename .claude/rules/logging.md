---
paths:
  - "src/log.*"
  - "src/**/*.cpp"
  - "include/kcdx/Interfaces.h"
---

# Logging discipline — every failure state is logged; logging is event-driven

Two floors, both non-negotiable: **every failure state is logged** (no silent swallow, anywhere), and **logging is event-driven, not loop-driven** (a log fires on a state change / failure / lifecycle event, never on a schedule or per loop iteration). The kcdx METHODOLOGY — the logger, the KV format, the severities, the destinations — is below; the OBLIGATION is this rule.

## The kcdx logging API — the methodology

Unified `kcdx::log` API. Five severities (Trace < Debug < Info < Warn < Error), category tags, three destinations (engine log, dev log, per-plugin log).

- **Use structured logging**: `LOG_DEBUG_KV("CATEGORY", "action", KV("key", value), ...)`. The KV format greps reliably; freeform printf doesn't. (The KV helper is qualified `log::KV(...)` inside the `LOG_*_KV` macros — a bare unqualified `KV(...)` fails to compile (C3861); put the qualifier in any brief that adds logging.)
- **Category tag** stable across a feature (`MID_HOOK`, `SCRIPTING`, `SAVE_LOAD`, etc.).
- **Plugin authors** use the `kcdxLogger` struct in `include/kcdx/Interfaces.h`. Do not bypass.
- **Don't roll your own log format** for probes / diagnostics. Use `LOG_DEBUG_KV` so the dev log can be filtered.

## Every failure state is logged — explicit logging everywhere a path can fail

A failure branch that returns, skips, retries, degrades, or swallows an error MUST emit a log line saying what failed and enough context to act on it. There is no silent failure path. An error caught and discarded without a log is a defect.

- Every error/anomaly branch logs before it returns or continues. Catching an error to translate or recover it still logs at the point of translation.
- The log line names the situation, the context (the identifiers needed to locate it), and the underlying error. "Failed" with no context is not a log.
- A swallowed error (caught and intentionally ignored) is logged with the explicit, sanctioned reason it is safe to ignore — never dropped wordlessly.
- This is the runtime counterpart to the skeptical-expert posture (`.claude/rules/skeptical-expert.md`): a failure the system hides from its own logs is a failure no one can verify. (Dropping/neutralizing author input instead of failing loud is AP14 — `anti-patterns.md`.)

## Logging is event-driven — never per-iteration on a hot path

A log statement fires when something changes — a state transition, a failure, a lifecycle event. It never fires on a schedule or once per loop iteration. Most logging primitives allocate and take a lock internally; one per hot-path iteration is measurable jitter plus noise that buries real events.

**Forbidden inside any loop classified as a hot path:** a log call at any level. If a condition worth logging occurs on a hot path, count it (an atomic counter) and log the count at the next state transition or session-level event — not per iteration. (Hot-path discipline pairs with `.claude/rules/memory.md`.)

## Levels carry meaning — the names are kcdx's, this rule names the contract

kcdx's level names are above (Trace/Debug/Info/Warn/Error); the contract each level carries is the floor:

- **An unrecoverable failure for a session/subsystem** (needs user attention) logs at Error.
- **A recoverable anomaly** that degraded quality or forced a retry/skip logs at Warn — and is one of the failure states the "every failure logged" floor above requires.
- **Lifecycle events** (process start/exit, a subsystem loaded/unloaded, a mode/profile switch, an operation started/completed with its counts + duration) log at Info — exactly one line per event, always-on in production.
- **Diagnostic detail** logs at Debug (off in production hot paths) — `LOG_DEBUG_KV` is the probe/diagnostic level.
- **Deep tracing** (Trace) is explicit-flag-only and never committed enabled.

## Crash bundles

On game crash, `kcdx-watchdog.exe` zips logs + (dev-mode-gated) minidump into `kcdx-engine/logs/crash/crash_<ts>.zip`.

## What this is NOT

- NOT a license to log on hot paths — the every-failure floor never overrides the no-per-iteration-hot-path floor; count and report at the transition.
- NOT the memory rule (`.claude/rules/memory.md`) or the polling rule (`.claude/rules/polling.md`) — this rule cites them at the hot-path boundary; it owns the logging obligation, not the allocation or sampling discipline.

## Full doc

`docs/logging.md`.
