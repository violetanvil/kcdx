# kcdx dev mode

A diagnostic logging mode for plugin authors. When enabled, kcdx emits
a structured trace of every meaningful internal action to a separate
log file. Off by default; near-zero cost when off.

## Enabling

Any kcdx.toml in any plugin folder can opt the entire engine into dev
mode by adding:

```toml
[kcdx]
dev_mode = true

# Optional: cap PER log file before rotation, in MB. Default 50.
dev_log_cap_mb = 50

# Optional: max historical log files to retain. Default 20.
# Once exceeded, the oldest .log.N gets deleted on next rotation.
# Set to 0 for unbounded retention (warning: may fill the disk on
# a long debug session — the author owns that risk).
dev_log_max_files = 20
```

If multiple plugins set `dev_mode`, dev mode is on (any-true wins).
The highest declared `dev_log_cap_mb` and `dev_log_max_files` win
(if plugin A wants 50MB / 20 files and plugin B wants 100MB / 5
files, kcdx uses 100MB and 20 — most-permissive on both axes).

When ANY plugin requests dev mode, all categories log. There are no
per-category opt-ins in v0.1 — if you turn it on you get everything.

## Log file

Lives next to `kcdx.log`:

```
<game>/Bin/Win64MasterMasterSteamPGO/plugins/kcdx-dev.log
```

When `kcdx-dev.log` reaches `dev_log_cap_mb`, it rotates:
- `kcdx-dev.log` → `kcdx-dev.log.1`
- existing `kcdx-dev.log.1` → `kcdx-dev.log.2`
- … up to `kcdx-dev.log.N` where N = `dev_log_max_files`
- the file beyond N is deleted on the next rotation

Default: 50MB per file × 20 files = ~1GB of trace before the oldest
gets pruned. Plugin authors who want more raise either value; the
disk-fill risk is theirs.

If `dev_log_max_files = 0`, kcdx never deletes any rotated file —
the directory accumulates `kcdx-dev.log.N` forever. Useful for
authors archiving a debugging session externally.

## Line format

Each line is independently parseable:

```
[HH:MM:SS.mmm T:tid] CATEGORY.ACTION key1=val1 key2=val2 ...
```

- Timestamp includes milliseconds (needed for correlating fast-firing events)
- `T:tid` is the OS thread ID (Windows GetCurrentThreadId)
- `CATEGORY.ACTION` is a dotted name; e.g. `PATCH.RESOLVE`, `HOOK.APPLY`,
  `SCRIPTING.DISPATCH/pre`
- Key/value pairs after; values are `0x...` for addresses, raw decimal
  for counts, double-quoted for strings with spaces
- Long values (asm dumps, byte buffers) get continuation lines prefixed
  `[T:tid]  | <data>` so each conceptual entry stays grep-correlatable
  by primary line

## Categories

Roughly one per kcdx subsystem:

| Category | Actions logged |
|---|---|
| `CONFIG`  | LOAD, PARSE_OK, PARSE_ERR per kcdx.toml |
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
| `LUA`     | CFUNCTION_ADDR/enter, CFUNCTION_ADDR/pushed, DYNAMIC_HOOK/request, DYNAMIC_HOOK/jit-*, DYNAMIC_HOOK/install-*, NUMBER_PROBE/sizes, NUMBER_PROBE/push_pull, NUMBER_PROBE/num_push_pull, NUMBER_PROBE/lightud |
| `MEMORY`  | SCAN, ALLOC, FREE, READ |

The `LUA.NUMBER_PROBE/*` family is produced on-demand by calling
`kcdx.lua._probe_numbers()` from pak Lua. It characterizes
`sizeof(lua_Number)` and the precision behavior of integer round-trips
across the float-precision boundary. Re-run it whenever a new KCD2
patch lands to confirm hard rule #17 still applies. See
[`lua-number-precision.md`](lua-number-precision.md).

A plugin author wanting "tell me about every hook" greps `^.*HOOK\.`.
"Tell me about my plugin specifically" greps for the plugin handle or
name. "Tell me about dispatch firing" greps `SCRIPTING.DISPATCH`.

## Off-path cost

The macro:

```c++
#define KCDX_DEV(category, action, ...)                                  \
    do {                                                                 \
        if (kcdx::dev::IsEnabled())                                      \
            kcdx::dev::Emit(category, action, __VA_ARGS__);              \
    } while (0)
```

`IsEnabled()` returns `g_enabled.load(std::memory_order_relaxed)` — a
single non-atomic-fenced load. When off, the branch predictor picks the
not-taken path; cost is one load per call site (well under 1 ns).

When on, `Emit` formats the line and queues it onto an mpsc lock-free
buffer drained by a dedicated logger thread, so call-site cost stays
bounded even under firehose conditions like every-tick dispatch fires.

## Implementation files

| File | Role |
|---|---|
| `src/dev.h` | Public macro + IsEnabled accessor |
| `src/dev.cpp` | Backend: log file open, rotation, format helpers, drain thread |
| `src/config.cpp` | Parse `[kcdx] dev_mode` + `dev_log_cap_mb`, call `dev::SetEnabled()` |
| 40+ existing TUs | Sprinkle `KCDX_DEV(...)` at each interesting handoff |

The sprinkles are the bulk of the work. Each is one or two lines; total
~150 LOC of call-site additions across the engine.

## Boundary

Dev mode is a diagnostic. It does NOT change behavior:
- The 5c.7a hard rule (no sol2 on live state) still applies
- The first-wins conflict-engine model is unchanged
- No new APIs exposed to plugins (other than the `[kcdx] dev_mode`
  config field)

Plugin authors writing pak Lua get the same observability as DLL
plugin authors. The trace shows registrations + dispatches for both.

## How this helps the open investigation

The current Phase 5g investigation (why `cfunction_address(kcdx.hello.greet)`
returns a different value than `lua_tocfunction` returns kcdx-side)
falls out as a side effect of `LUA.CFUNCTION_ADDR` + `SCRIPTING.REGISTER`
both being logged with full context. The diff between the two log
lines names the exact handoff that mutates the value.

---

# Dev-mode-gated test suite

kcdx ships a permanent regression test suite as a set of plugins under
[`test-plugins/`](../test-plugins/). The suite runs on every game
boot, each plugin self-reports pass/fail, and kcdx aggregates results
into kcdx.log. **The entire suite is gated on dev mode** — when dev
mode is off (i.e. the user is just playing the game with mods), the
test-suite plugins skip all work and produce zero log noise.

## Gating model

Test-suite plugins flag themselves with one TOML key:

```toml
[kcdx]
test_suite_only = true
```

When kcdx parses a config file with `test_suite_only = true`:
- If dev mode is **off** for this session: kcdx silently skips
  every `[[patch]]`, `[[hook]]`, `[[mid_hook]]`, `[[trampoline]]`,
  etc. in that file. No log line. The plugin's DLL (if any) still
  loads and still receives `kcdxPlugin_Load`, but the C++ side
  checks `kcdx::dev::IsEnabled()` at entry and early-returns —
  also silently. Production users never see the suite in their
  logs.
- If dev mode is **on**: kcdx parses and applies the entries
  normally. The plugin's `kcdxPlugin_Load` runs its check, calls
  the test-result reporting API, and the aggregator picks it up.

For pak-Lua test scripts (Test Suite plugins shipped as pak mods
rather than DLLs), there's no TOML to gate. The pak script
checks at script-load time:

```lua
if not kcdx or not kcdx.dev or not kcdx.dev.is_enabled() then
    return  -- dev mode off, skip the whole test
end
```

Reason for engine-side gating instead of "always run but early-out
in plugin code": `[[patch]]` and `[[hook]]` entries get applied at
config-load time, BEFORE any plugin C++ runs. Without the
`test_suite_only` skip, a CAP-01-patch test plugin would actually
apply its patch in production, defeating the purpose.

Production users don't ship `dev-mode-enable/kcdx.toml` (the
trivial opt-in plugin), so dev mode is off, so the suite is
silent. Developers always have `dev-mode-enable` installed, so
the suite always runs on boot.

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

The aggregator emits a roll-up line to kcdx.log on each kcdx
engine lifecycle message firing:

```
[12:34:56][INFO] Test suite: 12/14 passing as of kPostLoad
[12:34:56][INFO]   FAIL CAP-05-paklua-runtime: dispatch did not fire
[12:34:56][INFO]   FAIL COMP-04-runtime-vs-patch: collision not detected
```

The summary always reports both passing-count and total-registered;
failures are listed by name + reason. Tests that didn't report yet
(e.g. they wait for `kPostLoadGame` to check state) appear in
neither the pass nor fail bucket and are deducted from both
numerator and denominator — the summary tells you which message
the count is "as of" so you can correlate.

When dev mode is off, the aggregator emits nothing — there are no
registrations to count.

## When to update the suite

- **Adding a new feature to kcdx?** Add a test plugin under
  `test-plugins/<row-id>-<short-name>/` that exercises it.
- **Fixing a bug?** Add a regression test that fails before the
  fix and passes after.
- **Changing existing behavior?** Re-run the suite locally
  (launch game with dev mode on, look at the summary). If a
  previously-passing test now fails, you either broke something
  or the test needs updating — figure out which before committing.
- **Going to commit anything in `src/` or `include/`?** Re-run the
  suite. The matrix README's roll-up table should show the suite
  state at the commit SHA you're about to land.

## Caveat: caps not currently max'd

Reading config.cpp:405-414 closely: when multiple plugins declare
`dev_log_cap_mb` / `dev_log_max_files`, the values are **overwritten
in load order** (last writer wins) rather than max'd. This document
says "most-permissive wins" because that's the intended behavior,
but the engine implementation needs a small fix to actually do
max(). Tracked as a Phase 5 cleanup task. Workaround for now: set
the caps you want in the single `dev-mode-enable/kcdx.toml`, since
nothing else in your install is likely to set them.
