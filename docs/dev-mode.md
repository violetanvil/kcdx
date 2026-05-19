# kcdx logging model

kcdx writes to three distinct log streams. Each one targets a different
audience and has its own enable/verbosity controls. Mixing them up is
the #1 way to drown in noise, so the model is worth reading once.

## The three streams at a glance

| Stream | File | Audience | Default | Configured by |
|---|---|---|---|---|
| Engine status | `<plugins>/kcdx.log` | End user + everyone | always on, minimal | nothing — kcdx writes what it writes |
| Engine internals trace | `<plugins>/kcdx-dev.log` | kcdx contributor (debugging the engine) | OFF | `<plugins>/kcdx-engine.toml` |
| Per-plugin log | `<plugins>/<plugin>/<plugin>.log` | plugin author (debugging their plugin) | always on, `info` level | each plugin's own `kcdx.toml` `[plugin] log_level` |

The streams are independent. Turning on engine dev mode doesn't make
plugin logs more verbose. Cranking a plugin's log level doesn't
contaminate the engine trace. Each audience reads one file.

---

## Stream 1: `kcdx.log` — engine status

Always written, always minimal. Contains:

- `kcdx.asi loaded` + module-directory startup line
- Per-plugin discovery summaries (`Discovered plugin '...' v0x...`)
- Per-plugin load result (`kcdxPlugin_Load OK` / `... returned false`)
- Conflict-engine pre-flight summary
- Patch / hook apply summaries
- Engine lifecycle messages (`Firing kcdxMessage_PostLoad...`)
- Test-suite roll-up lines (`Test suite: X/Y passing as of ...`) — see test-suite section below

A player who's just running the game and sees a problem reads
`kcdx.log` first. It's intentionally quiet: one or two lines per
plugin, no internal trace, no JIT disasm, no per-call dispatch
events. If `kcdx.log` is clean and the game still misbehaves, the
problem is in a plugin — read that plugin's log next.

---

## Stream 2: `kcdx-dev.log` — engine internals trace

OFF by default. When on, kcdx emits a structured trace of every
meaningful internal action (hook installs, dispatch fires, conflict
matrix decisions, JIT disasm, MinHook return codes, dev-probe data).

**This stream is for kcdx contributors debugging the engine, not for
plugin authors debugging plugins.** A plugin author should rarely
need it; if their plugin is misbehaving, they look at their own
plugin's log first. They only crack open kcdx-dev.log if they
suspect the engine is doing something they didn't ask it to.

### Enabling

Create `<plugins>/kcdx-engine.toml` (engine config file, sits next
to `kcdx.asi`):

```toml
[kcdx]
dev_mode          = true     # turns the engine-internals trace on
dev_log_cap_mb    = 50       # optional, default 50
dev_log_max_files = 20       # optional, default 20

# Optional category filter. If absent or empty, log every category.
# If present, only the listed categories emit. Useful for "I'm
# debugging dispatch chain, give me only SCRIPTING + LUA" runs.
dev_categories    = ["LUA", "SCRIPTING"]

# Engine-wide: report patches/hooks but don't actually apply them.
# Useful for verifying a load-order or conflict-detection question
# without risking a crash.
dry_run           = false
```

`kcdx-engine.toml` is the ONLY engine-level config. **Plugin
`kcdx.toml` files cannot turn dev mode on or off** — that would be
cross-plugin contamination (Plugin A flipping global state that
changes how Plugin B is parsed). The setting lives where it
belongs: next to the engine binary.

Production users don't ship `kcdx-engine.toml`; dev mode is off and
the file doesn't exist. Developers ship one and the engine reads
it at startup.

### Rotation

When `kcdx-dev.log` reaches `dev_log_cap_mb`, it rotates:
- `kcdx-dev.log` → `kcdx-dev.log.1`
- existing `kcdx-dev.log.1` → `kcdx-dev.log.2`
- … up to `kcdx-dev.log.N` where N = `dev_log_max_files`
- the file beyond N is deleted on the next rotation

Default: 50 MB × 20 files ≈ 1 GB before pruning. Set `dev_log_max_files
= 0` to disable pruning entirely (the contributor accepts the disk
fill risk).

### Line format

```
[HH:MM:SS.mmm T:tid] CATEGORY.ACTION key1=val1 key2=val2 ...
```

- Timestamp in milliseconds (necessary for correlating dispatch fires)
- `T:tid` is the OS thread ID (`GetCurrentThreadId`)
- `CATEGORY.ACTION` is a dotted name (e.g. `PATCH.RESOLVE`, `SCRIPTING.DISPATCH/pre`)
- Key/value pairs trailing: `0x...` for addresses, raw decimal for counts, double-quoted for strings with spaces
- Long values (asm dumps, byte buffers) get continuation lines

### Categories

| Category | When it emits |
|---|---|
| `CONFIG`  | TOML parse events |
| `PATCH`   | RESOLVE (pattern/context/anchor counts), APPLY (bytes before/after) |
| `HOOK`    | RESOLVE, INSTALL, COLLISION (first-wins) |
| `MID_HOOK`| RESOLVE, INSTALL, COLLISION |
| `TRAMP`   | RESOLVE, INSTALL, SYMBOL_EXPORT |
| `CONFLICT`| ENTRY (pairwise check), RECORD (category + verdict) |
| `POOL`    | RESERVE (new region + base), ALLOC (size, owner, slot) |
| `MINHOOK` | CREATE, ENABLE, REMOVE, DISABLE (status code) |
| `PLUGIN`  | DISCOVERED, LOADED, PRELOADED, MSG_DISPATCH, TASK_RUN |
| `MSG`     | FIRE (type + listener count) |
| `SCRIPTING` | REGISTER, DISPATCH/pre, DISPATCH/post, DISPATCH/mid, REENTRY_GUARD |
| `JIT`     | EMIT (size, addr, source), DISASM (one inst per continuation line) |
| `LUA`     | CFUNCTION_ADDR/*, DYNAMIC_HOOK/*, NUMBER_PROBE/* |
| `MEMORY`  | SCAN, ALLOC, FREE, READ |
| `TEST`    | REGISTER (test plugin registered), REPORT (pass/fail recorded), SUMMARY (roll-up emitted) |

The `LUA.NUMBER_PROBE/*` family is on-demand via
`kcdx.lua._probe_numbers()` from pak Lua; characterizes
`sizeof(lua_Number)` etc. Re-run on each KCD2 patch to confirm
hard rule #17 still applies. See [`lua-number-precision.md`](lua-number-precision.md).

### Off-path cost

```c++
#define KCDX_DEV(category, action, ...)                                  \
    do {                                                                 \
        if (kcdx::dev::IsCategoryEnabled(category))                      \
            kcdx::dev::Emit(category, action, __VA_ARGS__);              \
    } while (0)
```

`IsCategoryEnabled` does a single non-atomic-fenced flag load (when
master flag is off) or one additional bitset check (when on). When
dev mode is off, branch predictor picks the not-taken path; cost
is well under 1 ns per call site.

When on, `Emit` formats the line and queues it onto an mpsc lock-
free buffer drained by a dedicated logger thread, so call-site cost
stays bounded under firehose conditions like every-tick dispatch
fires.

---

## Stream 3: `<plugin>/<plugin>.log` — per-plugin log

Always written. Each plugin's `api->Log(self, level, msg)` calls
land in its own log file. The level threshold is set per-plugin in
the plugin's own `kcdx.toml`:

```toml
[plugin]
name      = "author.my-mod"
author    = "..."
version   = "1.0.0"
log_level = "info"   # "debug" | "info" | "warn" | "error" | "off"
```

- `debug` — everything, including verbose tracing the author put in
- `info` (default) — normal operational lines
- `warn` — issues the author wants someone to notice
- `error` — something failed
- `off` — write nothing (file still opens; for plugins that want to
  selectively log only in their own debug builds)

When the plugin calls `api->Log(self, kcdxLog_Info, "...")` and
`log_level = "warn"`, the call is dropped before formatting. The
plugin author tunes their own verbosity without rebuilding the DLL.

This stream is for the plugin author debugging their own plugin.
Other plugins do not appear here. Engine internals do not appear
here. Just `[plugin-name] info: message text`.

---

## Boundary between streams

Quick reference: where should a given log line live?

- **"My .asi loaded but plugins didn't"** → kcdx.log
- **"Plugin X is loaded but my dispatch chain misbehaves"** → kcdx-dev.log (only if you're a contributor); plugin X's log otherwise
- **"My plugin X has a bug in its dispatch callback logic"** → X.log (your own log)
- **"kcdx's conflict matrix made a decision I don't understand"** → kcdx-dev.log
- **"My plugin can't find its config file"** → X.log (use `api->Log`)
- **"Test suite results"** → kcdx.log (the roll-up) + per-plugin logs (the details)

---

# Test suite (dev-mode-gated)

kcdx ships a permanent regression test suite as a set of plugins
under [`test-plugins/`](../test-plugins/). The suite runs on every
game boot, each plugin self-reports pass/fail, and kcdx aggregates
results into kcdx.log.

**The entire suite is gated on engine dev mode** — when dev mode is
off (i.e. the user is just playing the game with mods), the test-
suite plugins skip all work and produce zero log noise.

## Gating model

Test-suite plugins flag themselves with one TOML key:

```toml
[kcdx]
test_suite_only = true
```

When kcdx parses a plugin `kcdx.toml` with `test_suite_only = true`:
- If dev mode is **off** (no `kcdx-engine.toml` or `dev_mode = false`):
  kcdx silently skips every entry in that file (no [[patch]],
  [[hook]], [plugin], etc. gets registered). C++ DLLs in the
  plugin folder also check `kcdx::dev::IsEnabled()` in their
  `Plugin_Load` and early-return silently. **Production users see
  zero suite output.**
- If dev mode is **on**: kcdx parses + applies normally. The
  plugin's `Plugin_Load` runs its check, calls the test-result
  reporting API, and the aggregator picks it up.

For pak-Lua test scripts (test plugins shipped as pak mods rather
than DLLs), there's no TOML to gate on. The pak script does an
early-out at script-load time:

```lua
if not kcdx or not kcdx.dev or not kcdx.dev.is_enabled() then
    return  -- dev mode off, skip this entire test
end
```

Reason for engine-side gating instead of "always run but early-out
in plugin code": `[[patch]]` / `[[hook]]` entries are applied at
config-load time, BEFORE any plugin C++ runs. Without the
`test_suite_only` skip, a CAP-01-patch test plugin would actually
apply its patch in production, defeating the purpose.

## Test result reporting

Each test plugin calls one API to report its result. C++:

```c++
api->ReportTestResult(handle,
    /*testName=*/"CAP-01-patch",
    /*pass=*/    true,
    /*reason=*/  "applied at 0x7FFCF9051759");
```

Pak Lua:

```lua
kcdx.test.report("CAP-05-paklua-runtime", true,
                 "dispatch fired 5/5 times")
```

`testName` should be the matrix row ID (e.g. `CAP-05`,
`COMP-03`) so the kcdx.log summary lines up with
[`test-plugins/README.md`](../test-plugins/README.md). `reason` is
freeform; aim for one short sentence that explains the verdict.

A test may report multiple times during a session (e.g. once at
`kcdxPlugin_Load`, again at `kPostLoadGame`). Last report wins —
the aggregator keeps a `<testName> -> {pass, reason, sourceHandle,
timestamp}` map and overwrites on each call.

## Aggregator output

The aggregator emits a roll-up line to `kcdx.log` on each kcdx
engine lifecycle message firing:

```
[12:34:56][INFO] Test suite: 12/14 passing as of kPostLoad
[12:34:56][INFO]   FAIL CAP-05-paklua-runtime: dispatch did not fire
[12:34:56][INFO]   FAIL COMP-04-runtime-vs-patch: collision not detected
```

The summary always reports both passing-count and total-registered;
failures are listed by name + reason. Tests that haven't reported
yet (e.g. they wait for `kPostLoadGame` to check state) appear in
neither bucket and are deducted from both numerator and denominator
— the summary tells you which message the count is "as of" so you
can correlate.

When dev mode is off, the aggregator emits nothing (no test plugins
are registered).

## When to update the suite

- **Adding a new feature to kcdx?** Add a test plugin under
  `test-plugins/<row-id>-<short-name>/` that exercises it.
- **Fixing a bug?** Add a regression test that fails before the
  fix and passes after.
- **Changing existing behavior?** Re-run the suite locally
  (launch game with `kcdx-engine.toml` + dev mode on, read the
  summary). If a previously-passing test now fails, you either
  broke something or the test needs updating — figure out which
  before committing.
- **Committing anything in `src/` or `include/`?** Re-run the
  suite. The matrix README's roll-up table should show the suite
  state at the commit SHA you're about to land.

---

## Implementation files

| File | Role |
|---|---|
| `src/dev.h` / `dev.cpp` | Engine-internals trace: macro, IsEnabled accessor, log file open + rotation + format helpers + drain thread |
| `src/config.cpp` | Parses `kcdx-engine.toml` at startup. Calls `dev::SetEnabled`, `dev::SetCategories`, `patch::g_dryRun`. Walks plugin `kcdx.toml` files (without dev_mode/dry_run keys). |
| `src/log.cpp` | The three log writers: `kcdx.log`, `kcdx-dev.log`, per-plugin `<plugin>.log`. Each one gates on its own setting (always-on, dev mode, plugin log_level). |
| `src/plugin_loader.cpp` | Reads `[plugin] log_level` into LoadedPlugin; `Thunk_Log` consults it before formatting. |
| 40+ existing TUs | `KCDX_DEV(...)` sprinkles at each interesting handoff. |
