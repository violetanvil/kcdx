---
paths:
  - "src/log.*"
  - "src/**/*.cpp"
  - "include/kcdx/Interfaces.h"
---

# Logging

Unified `kcdx::log` API. Five severities (Trace < Debug < Info < Warn < Error), category tags, three destinations (engine log, dev log, per-plugin log).

## Rules

- **Use structured logging**: `LOG_DEBUG_KV("CATEGORY", "action", KV("key", value), ...)`. The KV format greps reliably; freeform printf doesn't.
- **Category tag** stable across a feature (`MID_HOOK`, `SCRIPTING`, `SAVE_LOAD`, etc.).
- **Plugin authors** use the `kcdxLogger` struct in `include/kcdx/Interfaces.h`. Do not bypass.
- **Don't roll your own log format** for probes / diagnostics. Use `LOG_DEBUG_KV` so the dev log can be filtered.

## Crash bundles

On game crash, `kcdx-watchdog.exe` zips logs + (dev-mode-gated) minidump into `kcdx-engine/logs/crash/crash_<ts>.zip`.

## Full doc

`docs/logging.md`.
