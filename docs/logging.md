# Logging in kcdx

This doc covers the kcdx logging system end-to-end: what gets logged
where, who configures it, and how to call it from engine code or a
plugin. It's the canonical reference. If `kcdx/CLAUDE.md` or
`docs/dev-mode.md` disagree with this file, this file wins and they
should be updated.

## The promise

Three guarantees the logging system makes:

1. **A player who never enabled anything can attach two files to a bug
   report (the engine log + the affected plugin's log) and the mod
   author will see what went wrong.**
2. **A mod author who set `log_level = "off"` in their `kcdx.toml` to
   keep their own debug noise out of their plugin's log file still
   gets every error and warning surfaced.**
3. **An engine dev who flips `dev_mode = true` gets the full firehose
   in `kcdx-dev.log`, and can narrow it via `dev_categories = [...]`
   without affecting the always-on bug-report channels.**

If any future change to the logging system breaks one of these, it's a
regression even if all the tests still pass.

## The four dimensions of a log call

Every log call carries four properties:

| Dimension | Values | Picked by |
|---|---|---|
| **Severity** | TRACE < DEBUG < INFO < WARN < ERROR | Call site |
| **Source** | `"engine"` or a plugin's stable name | Implicit (engine API vs `api->Log`) |
| **Category** | Free-form short string (e.g. `"DISCOVERY"`, `"GUARD"`) | Call site |
| **Body** | printf'd message OR `action key=val key=val` | Call site |

## The three destinations

| File | Path | When open | Format |
|---|---|---|---|
| Engine log | `<kcdx-engine>/logs/kcdx_<ts>.log` | Always (Init) | `[HH:MM:SS.mmm][LEVEL][SOURCE][CATEGORY] body` |
| Dev log | `<kcdx-engine>/logs/kcdx-dev_<ts>.log` | When `dev_mode = true` | `[HH:MM:SS.mmm][LEVEL][SOURCE][CATEGORY] body  [tid=N]` |
| Per-plugin log | `<plugins>/<X>/logs/<X>_<ts>.log` | Eager, per DLL-bearing plugin | `[HH:MM:SS.mmm][LEVEL][SOURCE][CATEGORY] body` |

`<ts>` is `YYYY-MM-DD_HH-MM-SS` of the session start. One file per
session; retention is the newest 20 files per stream (engine, dev,
each plugin counted independently). Old files are pruned on `Init()`.

The dev log adds `tid=<thread-id>` on lines emitted from any thread
other than the main thread. The engine and per-plugin files don't
(too noisy for consumers).

## The three configuration knobs

### `engine.toml` — kcdx-engine/engine.toml

```toml
[kcdx]
dev_mode       = false                  # default false
dev_categories = ["DISCOVERY", "GUARD"] # default [] (empty = all categories)
```

- `dev_mode` opens the dev log and lifts plugin `log_level` floors.
  Defaults to off — players never need this.
- `dev_categories` narrows the **dev log only**. Empty list = every
  category passes. Non-empty = only listed categories reach the dev
  log. Never affects the engine log or per-plugin logs.

### Plugin's `kcdx.toml` — plugins/<X>/kcdx.toml

```toml
[plugin]
name      = "violetanvil.hello-plugin"
log_level = "info"   # trace | debug | info | warn | error | off
```

`log_level` is a **floor for the plugin's own file**. With dev mode
off, only severities at or above this floor appear in
`plugins/<X>/logs/<X>_<ts>.log`. With dev mode on, the floor is
fully bypassed — devs see everything.

**`log_level` never suppresses ERROR or WARN.** Setting `log_level = "off"` does NOT mean "hide problems"; it means "don't write my own
chatter to my file." Errors and warnings always land.

`log_level` has **no effect** on the engine log or dev log. Those
have their own gates.

## The routing rules

For every log call, three independent yes/no decisions:

### Does it go to `kcdx.log` (engine log)?

```
Yes iff severity >= INFO
```

Filter not applied. Dev mode not required. This is the always-on
bug-report channel.

Translation: TRACE and DEBUG never appear in `kcdx.log`. INFO/WARN/
ERROR always do (engine-side AND plugin-side).

### Does it go to `kcdx-dev.log` (dev log)?

```
Yes iff dev_mode AND (dev_categories is empty OR category in dev_categories)
```

All severities pass when the dev mode + category gates open. No
INFO floor — that's the point of the dev log.

### Does it go to `plugins/<X>/logs/<X>_<ts>.log` (per-plugin log)?

A call is plugin-attributed when:
- The plugin called `api->Log(...)`, OR
- The engine attributes an event to that plugin (GUARD FAULTED line
  naming plugin X, manifest reject for plugin X, etc.)

Routing per severity:

```
ERROR:   always — regardless of log_level, regardless of dev_mode
WARN:    always — regardless of log_level, regardless of dev_mode
INFO:    if log_level <= info  OR dev_mode
DEBUG:   if log_level <= debug OR dev_mode
TRACE:   if log_level <= trace OR dev_mode
```

So:
- Errors/warnings always reach the file. Always.
- The floor only suppresses *lower-severity* lines.
- Dev mode bypasses the floor entirely.

## Engine-injected lines in per-plugin files

When kcdx itself observes something about plugin X (a guarded callback
faulted, the manifest failed validation, a dependency wasn't found),
the engine logs the line both to `kcdx.log` AND to plugin X's own
file. The author doesn't need to instrument anything; the file is
already populated with the engine's view.

This is the magic that makes guarantee #1 work: a player attaches the
engine log + the plugin's log, and the plugin's log already contains
the relevant engine-side observations attributed to that plugin.

In code, this looks like: the engine code calls `EmitPlugin(level,
handle, category, msg)` even though the caller is engine code, when
the event is *about* a specific plugin. The plugin file gets its copy.

## Severity rules of thumb

Every call site has to pick a severity. The bar:

| Severity | Use for |
|---|---|
| **TRACE** | Per-iteration loop bodies, per-frame events, ultra-verbose internals. Fire 100s of times per session. |
| **DEBUG** | Detailed flow that's useful while debugging the engine itself but not when reading a bug report. JIT-emitted bytes, AOB hex per match, "skipped non-matching listener". |
| **INFO** | State transitions a consumer would need to see in a bug report. Lifecycle milestones, resolution outcomes, plugin-author-facing decisions, broadcasts. |
| **WARN** | Something is wrong but the system kept going. A plugin's manifest was rejected, an optional dependency missing, an AOB resolved to 0 matches (so the patch silently won't apply). |
| **ERROR** | A feature won't work. A required dependency missing, a hook installation failed, a guarded callback faulted, a TOML parse failure. |

The single test for `INFO`:

> If a player crashes and sends this log, would the line help the mod
> author understand what was happening?

- Yes → INFO
- "Maybe, if I'm debugging the engine itself" → DEBUG
- "Only if I'm tracing a specific subsystem hard" → TRACE

Per-category audit periodically: grep `kcdx.log` for `[INFO][engine][<X>]`
for each category X and ask whether all those lines pass the test.
Drift to TRACE/DEBUG if not.

### Currently-known INFO calls that should be DEBUG

(Audit captured 2026-05-20. Migrate opportunistically.)

- `[trampoline 'X'] allocated N bytes at 0x...` — fires N times per plugin per session; consumer doesn't care.
- `[trampoline 'X'] exported symbol 'X.Y' -> 0x...` — diagnostic-only; symbol table is grep-able from elsewhere.
- `[hook 'X'] post-install bytes at target: E9 ...` — diagnostic-only.
- Per-listener messaging dispatch bookend pairs when listener count is high — downgrade to DEBUG unless the broadcast failed.

## Engine-side API

```cpp
#include "log.h"

// printf-style:
LOG_INFO ("DISCOVERY", "Discovered plugin '%s' from %s", name, path);
LOG_WARN ("MANIFEST",  "Plugin '%s' rejected: %s", name, reason);
LOG_ERROR("GUARD",     "FAULTED site=%s code=%s", site, codeName);
LOG_DEBUG("MESSAGING", "broadcast type=%u listeners=%zu", type, n);
LOG_TRACE("DISCOVERY", "directory_iterator entry %s", path);

// Structured (action + key=value KVs) — for events where field
// discipline matters (crash guards, discovery walker breadcrumbs):
LOG_INFO_KV("DISCOVERY", "accept",
    KV("path", folder),
    KV("toml", tomlPath));
LOG_ERROR_KV("GUARD", "FAULTED",
    KV("site", site),
    KV("code", "ACCESS_VIOLATION"),
    KV("rip",  reinterpret_cast<void*>(rip)));

// Plugin-attributed lines (engine code attributing an event to a
// specific plugin — gets mirrored to the plugin's file):
LOG_PLUGIN_INFO (handle, "LIFECYCLE", "kcdxPlugin_Load OK");
LOG_PLUGIN_WARN (handle, "VALIDATION", "missing optional dependency '%s'", dep);
LOG_PLUGIN_ERROR(handle, "GUARD",     "FAULTED site=%s rip=0x%llX", site, rip);
LOG_PLUGIN_INFO_KV(handle, "LIFECYCLE", "loaded", KV("ver", v));
```

Hot-path cost when dev mode is off:
- LOG_INFO/WARN/ERROR: always do the work (they always-on land in
  kcdx.log).
- LOG_DEBUG/TRACE: one atomic load + branch-predicted skip. The
  format work and KV materialization is gated; off-state pays ~nothing.

## Plugin-side API

Plugins call the public log surface via `kcdxInterface->Log`:

```cpp
api->Log(self, level, category, msg);
```

where `level` is one of:

```cpp
kcdxLog_Trace = 0
kcdxLog_Debug = 1
kcdxLog_Info  = 2
kcdxLog_Warn  = 3
kcdxLog_Error = 4
```

The verbose form is fine but most plugins want a short form. Use
the `kcdxLogger` wrapper declared in `kcdx/Interfaces.h`:

```cpp
// In a plugin's translation unit (could also be a member of your
// plugin's main class, or a local in a function — wherever fits):
static kcdxLogger g_log;

bool kcdxPlugin_Load(const kcdxInterface* api) {
    g_log = kcdxLogger(api, api->GetPluginHandle("violetanvil.hello-plugin"));

    g_log.Info ("INIT",      "loaded; engine v0x%08X", api->kcdxVersion);
    g_log.Warn ("MESSAGING", "Messaging interface unavailable");
    g_log.Error("INIT",      "RegisterFunction failed: %s", err);
    return true;
}
```

`kcdxLogger` is a small header-only struct that holds the
`kcdxInterface*` + `kcdxPluginHandle` and exposes:

```cpp
struct kcdxLogger {
    kcdxLogger(const kcdxInterface* api, kcdxPluginHandle self);

    void Trace(const char* category, const char* fmt, ...);
    void Debug(const char* category, const char* fmt, ...);
    void Info (const char* category, const char* fmt, ...);
    void Warn (const char* category, const char* fmt, ...);
    void Error(const char* category, const char* fmt, ...);
};
```

Each method formats with `vsnprintf` (bounded at 1 KiB) and forwards
to `api->Log(self, level, category, formatted_msg)`. No global names
required; no macros in user code. Mockable in tests by constructing
with a mock interface.

### Category naming for plugin authors

Pick short, stable strings the modder will grep for. Examples from
the hello-plugin example:

- `"INIT"` — Plugin_Load wiring, sub-interface query results
- `"MESSAGING"` — engine message handlers
- `"TASK"` — task callbacks
- `"TRAMPOLINE"` — trampoline pool allocation
- `"SCRIPTING"` — RegisterFunction outcomes

There's no enum. Pick whatever helps you grep your own log later.

## Worked scenarios

### Scenario A — consumer crash, no configuration

Player runs the game with defaults. Plugin `my-plugin` has the default
`log_level = "info"`. Mid-`InputLoaded` broadcast, my-plugin's
callback faults.

- `kcdx.log` gets `[ERROR][engine][GUARD] FAULTED site=messaging.broadcast plugin=my-plugin code=ACCESS_VIOLATION rip=0x...`
- `kcdx-dev.log` does not exist (dev mode off).
- `my-plugin/logs/my-plugin_<ts>.log` also gets the same ERROR line — engine attributed it to that plugin.

Player attaches both files. Mod author opens the plugin file and immediately sees the fault.

### Scenario B — author developing locally, full firehose

Author sets `dev_mode = true` in `engine.toml`. Their `kcdx.toml`
has `log_level = "info"`.

- `kcdx.log` gets INFO+ as usual.
- `kcdx-dev.log` gets every severity, every category, every source.
- `my-plugin/logs/my-plugin_<ts>.log` gets every severity (dev mode lifts the `log_level = "info"` floor) — their TRACE/DEBUG `api->Log` calls now visible, plus engine attributions.

### Scenario C — author quieted their plugin file

Author sets `log_level = "warn"`. Dev mode off.

- `kcdx.log` unchanged.
- `kcdx-dev.log` not open.
- `my-plugin/logs/my-plugin_<ts>.log`:
  - Author's own TRACE/DEBUG/INFO `api->Log` calls → dropped (floor).
  - Author's own WARN/ERROR `api->Log` calls → kept.
  - Engine-attributed WARN/ERROR → always kept.

So the consumer-submitted plugin file still surfaces every problem;
just doesn't have the author's verbose chatter.

### Scenario D — engine dev focusing on one subsystem

Author sets `dev_mode = true`, `dev_categories = ["MESSAGING", "GUARD"]`.

- `kcdx.log` unchanged. Filter doesn't apply.
- `kcdx-dev.log` shows only MESSAGING + GUARD lines, all severities.
- Plugin files unchanged. Filter doesn't apply.

## Summary table

| Destination | Severity gate | Filter applies? | Dev mode required? | log_level floor applies? |
|---|---|---|---|---|
| `kcdx.log` | ≥ INFO | no | no | no |
| `kcdx-dev.log` | none | yes | yes | no |
| plugin file (ERROR/WARN) | always | no | no | **no — always passes** |
| plugin file (INFO/DEBUG/TRACE) | severity-floor | no | dev mode bypasses floor | yes |

## Internal architecture notes

- `log.cpp` contains the single router (`Dispatch()`). One function decides which of the three destinations a given (level, source, category, body) tuple lands in.
- `dev.{h,cpp}` is a header-only compatibility shim over the unified API. `KCDX_DEV(c, a, ...)` expands to `LOG_DEBUG_KV(c, a, ...)`. The `dev::KV` type is an alias for `log::KV`.
- All three destinations use FILE* (`_wfopen`/`fwrite`/`fflush`), not `std::ofstream`. Bisected 2026-05-20: `std::ofstream` silently dropped writes on some plugin streams even with `is_open()` returning true.
- Per-plugin streams open eagerly via `OpenPluginStream(handle)` called from the plugin loader, so every DLL-bearing plugin's `logs/` folder exists from session start. TOML-only plugins are skipped.
- Plugin log files are named after the plugin's `[plugin] name` manifest field (e.g. `violetanvil.hello-plugin_<ts>.log`), with a fallback to the install folder name if the manifest name is empty.
- The dev log is opened lazily on first `SetDevMode(true)` call. If `engine.toml` doesn't set `dev_mode = true`, no `kcdx-dev_*.log` file ever appears in the player's `logs/` folder.

## Crash bundles (kcdx-watchdog.exe)

kcdx ships an external sidecar binary, `kcdx-watchdog.exe`, that
bundles up everything the consumer would need to file a useful bug
report when the game crashes.

### How it works

1. On startup (after `LoadAllConfigs` so the dev-mode flag is
   settled), `kcdx.asi`'s DllMain worker thread spawns
   `kcdx-watchdog.exe` via `CreateProcessW` with `DETACHED_PROCESS |
   CREATE_NO_WINDOW | CREATE_BREAKAWAY_FROM_JOB`. The spawn passes
   our PID + paths + session stamp + dev-mode flag.
2. The watchdog calls `OpenProcess(SYNCHRONIZE, ..., pid)` and
   `WaitForSingleObject(hProc, INFINITE)`. Zero CPU; it blocks on
   a kernel handle until the game dies.
3. On wake-up it reads `GetExitCodeProcess`. Exit code `0` → clean
   shutdown, exit silently. Non-zero → the game crashed.
4. After a 5-second grace period (lets BugSplat/WerFault finish
   writing their dumps), the watchdog zips the bundle into
   `<kcdx-engine>/logs/crash/crash_<sessionstamp>.zip`.

### What's in the zip

```
crash_<ts>.zip
├── kcdx/
│   ├── kcdx_<ts>.log                    (engine log)
│   └── kcdx-dev_<ts>.log                (dev log, if dev mode was on)
├── plugins/
│   ├── violetanvil.hello-plugin_<ts>.log
│   ├── kcdx.cap-01-patch_<ts>.log
│   └── ... (one flat file per plugin)
├── game/
│   └── kcd.log                          (game's own narration log)
└── crash/
    ├── KingdomCome.exe.<pid>.dmp        (only when dev_mode = true)
    └── bugsplat_<id>.log                (BugSplat diagnostics if any)
```

Layout is intentionally flat: four top-level directories, one file
per leaf. Plugin filenames already encode `<manifest.name>_<ts>` so
there are no collisions when 18 plugins share one directory.

### Dev-mode gates the minidump

The minidump (~108MB uncompressed) is the only bulky artifact. It's
included only when `dev_mode = true` in `engine.toml`.

| Scenario | engine.toml | Bundle contains | Approx size |
|---|---|---|---|
| Consumer playing with mods | `dev_mode = false` (or no file) | logs only | ~500KB |
| Engine dev / plugin author investigating | `dev_mode = true` | logs + dmp | ~40MB |

Rationale: for the common case of "plugin X crashed in its own
callback," the GUARD line in `kcdx.log` already names the plugin +
module + offset. The mod author needs nothing else. The dmp is
irreplaceable for crashes our logs can't see (fast-fails, kernel
kills, game-side faults), and that's exactly when an engine dev
would have dev mode on.

### When the watchdog can't help

`SetUnhandledExceptionFilter` catches most crash classes; the
watchdog is the safety net for the rest. There are still a few
crash classes neither can name:

- **Process killed externally** (TerminateProcess, kernel OOM,
  user task-manager kill). The watchdog will see exit code != 0,
  bundle the logs, but there's no fault to attribute.
- **Stack overflow with no guard page left.** Both the in-process
  filter AND the watchdog (which still ran fine) will produce
  output; the dmp will name the crashing function via the kernel.
- **Watchdog itself didn't launch** (security software blocked
  `CreateProcessW`, `kcdx-watchdog.exe` missing from `plugins/`).
  kcdx logs `[WARN][engine][WATCHDOG]` at startup; the in-process
  filter still runs.

### Watchdog's own log

The watchdog writes its own diagnostic log at
`<kcdx-engine>/logs/kcdx-watchdog_<ts>.log`. It records what it
collected, why files were skipped, and the game's exit code. This
is the file to read when something looks wrong with the bundle.

### Where the dmp comes from

Three possible sources, in priority order:

1. **`<kcdx-engine>/logs/kcdx_<ts>.dmp`** — written by
   `crash_guard::UnhandledFilter` when SEH catches the crash.
   Filtered minidump (~2-5MB) containing stacks, modules, and
   indirect-referenced memory. Most crashes land here.
2. **`%LOCALAPPDATA%/CrashDumps/KingdomCome.exe.<pid>.dmp`** —
   written by Windows Error Reporting when the crash bypasses SEH
   (heap-corruption fast-fails, kernel-level kills). Full-memory
   dump (~100MB).
3. **`%LOCALAPPDATA%/Temp/`** — best-effort BugSplat fallback. See
   [`known-issues/BugSplat dmp files don't reach disk for AV crashes.md`](known-issues/BugSplat%20dmp%20files%20don't%20reach%20disk%20for%20AV%20crashes.md)
   for why this is unreliable.

All three paths are scanned. If multiple are present they're all
bundled.

## Adding a new log call

Decision tree:

1. **Engine-side or plugin-side call site?**
   - Engine → `LOG_*` macros.
   - Plugin → `kcdxLogger` (member of struct or static instance).
2. **Severity** — apply the rule-of-thumb test (would a consumer's bug-report reader benefit?).
3. **Category** — short, stable, grep-able. Reuse an existing category if one fits. Adding a new category is a string change at the call site; no enum, no registration.
4. **printf or structured?** — Free-form prose → `LOG_INFO(...)`. Structured event data (named fields the log reader will programmatically query) → `LOG_INFO_KV(...)`.
5. **Plugin-attributed?** — If the engine is logging something about a specific plugin (a GUARD fault, a validation reject), use `LOG_PLUGIN_*` so the line mirrors to that plugin's file.

## Things deliberately NOT in this system

- **Async logging / ring buffers.** Synchronous writes with per-stream mutexes. Has measurable cost on hot paths but no plugin author has complained yet; revisit if a profile says we need it.
- **Log rotation by size.** Per-session files only. Size cap removed during the unification (was 20 MB; not needed once sessions are bounded).
- **Severity-per-category configuration.** Every category honors the same global INFO floor for `kcdx.log`. Adding per-category floors would let us dial down a chatty subsystem without losing visibility elsewhere, but it adds a config surface we don't need yet.
- **Network logging.** Not a thing.
