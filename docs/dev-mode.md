# kcdx dev mode

Dev mode is the single user-facing toggle that turns on verbose
logging. It opens `kcdx-dev.log`, lifts plugin `log_level` floors, and
gates the regression test suite.

The full logging model — destinations, routing rules, severity
guidance — lives in [`logging.md`](logging.md). This doc covers only
the dev-mode-specific surface.

## Enabling

Create `<kcdx-engine>/engine.toml` (engine config file, sits in the
engine-owned data folder next to `kcdx.log`):

```toml
[kcdx]
dev_mode       = true                  # opens kcdx-dev.log, lifts log_level floors
dev_categories = ["MESSAGING", "GUARD"] # optional; narrows kcdx-dev.log only
dry_run        = false                 # report patches/hooks but don't apply
```

`engine.toml` is the ONLY engine-level config. **Plugin `kcdx.toml`
files cannot turn dev mode on or off** — that would be cross-plugin
contamination (Plugin A flipping global state that changes how
Plugin B is parsed). The setting lives where it belongs: in the
engine-owned data folder, separate from any plugin.

Production users don't ship `engine.toml`; dev mode is off and the
file doesn't exist. Developers ship one and the engine reads it at
startup.

## What dev mode changes

1. **Opens `kcdx-dev.log`** at `<kcdx-engine>/logs/kcdx-dev_<ts>.log`.
   This file receives every severity (TRACE/DEBUG/INFO/WARN/ERROR)
   from every source (engine + every plugin) that passes the
   `dev_categories` filter.
2. **Lifts plugin `log_level` floors.** Each plugin's `kcdx.toml`
   can set `log_level = "warn"` (for example) to suppress its own
   TRACE/DEBUG/INFO output from the plugin's own file. With dev mode
   on, those floors are bypassed — devs see everything.
3. **Gates the regression test suite.** Test-suite plugins
   self-skip when dev mode is off; see the test-suite section below.

What dev mode does NOT change:

- The engine log (`kcdx.log`) is unaffected. It's always-on at the
  INFO floor.
- The category filter (`dev_categories`) only narrows `kcdx-dev.log`.
  Engine log and per-plugin logs still see every category.
- Plugin author intent. If a plugin author wrote no log calls,
  enabling dev mode does NOT magically produce plugin-side trace
  output. It does mean the engine-attributed lines (GUARD faults,
  validation issues) become more visible.

## Categories

Every log line is tagged with a short category string. The
`dev_categories` filter is a string-equality allow-list — empty list
means "every category passes." Set this to focus the dev log on a
specific subsystem.

Current categories emitted by the engine (non-exhaustive; new
categories are a string-change at the call site, no enum):

| Category | Subsystem |
|---|---|
| `CONFIG`     | TOML parse events |
| `DISCOVERY`  | Plugin folder walking, kcdx.toml detection |
| `MANIFEST`   | Plugin manifest validation, name conflicts |
| `PATCH`      | Byte-rewrite resolve + apply |
| `HOOK`       | Function hook resolve + install |
| `MID_HOOK`   | Mid-function hook resolve + install |
| `TRAMP`      | Trampoline resolve + symbol export |
| `CONFLICT`   | Pairwise overlap detection |
| `POOL`       | Trampoline pool region + slot allocation |
| `MINHOOK`    | MinHook return codes |
| `PLUGIN`     | Plugin DLL discovery + load |
| `MESSAGING`  | Engine + plugin message broadcasts |
| `SCRIPTING`  | Lua callback dispatch + registration |
| `GUARD`      | Crash-guard breadcrumbs + fault attribution |
| `JIT`        | asmjit emit + disasm |
| `LUA`        | C-Lua API thunks, number-precision probe |
| `MEMORY`     | Plugin-facing memory inspect / read / scan |
| `TEST`       | Test-suite register, report, summary |

The `LUA.NUMBER_PROBE/*` family is on-demand via
`kcdx.lua._probe_numbers()` from pak Lua; characterizes
`sizeof(lua_Number)` etc. Re-run on each KCD2 patch to confirm hard
rule #17 still applies. See [`lua-number-precision.md`](lua-number-precision.md).

## Boundary between log destinations

Quick reference: where should a given log line live? See
[`logging.md`](logging.md) for the full table; the quick version:

- **"My .asi loaded but plugins didn't"** → `kcdx.log`
- **"Plugin X faulted during InputLoaded"** → both `kcdx.log` and `X.log` (engine attributes GUARD lines to the plugin's file automatically)
- **"Plugin X has a bug in its dispatch callback logic"** → X.log (plugin uses `KCDX_LOG_*` for their own trace)
- **"kcdx's conflict matrix made a decision I don't understand"** → `kcdx-dev.log` (requires dev mode)
- **"My plugin can't find its config file"** → X.log (use `api->Log`)
- **"Test suite results"** → `kcdx.log` (the roll-up) + per-plugin logs (the details)

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
- If dev mode is **off** (no `engine.toml` or `dev_mode = false`):
  kcdx silently skips every entry in that file (no [[patch]],
  [[hook]], [plugin], etc. gets registered). C++ DLLs in the
  plugin folder also check `kcdx::log::IsDevModeEnabled()` in their
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
[12:34:56.789][INFO][engine][TEST] Test suite: 12/14 passing as of kPostLoad
[12:34:56.789][INFO][engine][TEST]   FAIL CAP-05-paklua-runtime: dispatch did not fire
[12:34:56.789][INFO][engine][TEST]   FAIL COMP-04-runtime-vs-patch: collision not detected
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
  (launch game with `engine.toml` + dev mode on, read the
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
| `src/log.{h,cpp}` | Unified logging router (engine + dev + per-plugin destinations). All log routing decisions live here. |
| `src/dev.{h,cpp}` | Compatibility shim over `kcdx::log`. `KCDX_DEV` macro expands to `LOG_DEBUG_KV`. `dev::KV` aliases `log::KV`. Header-only; `dev.cpp` is intentionally empty. |
| `src/config.cpp` | Parses `engine.toml` at startup. Calls `log::SetDevMode`, `log::SetCategoryFilter`, `patch::g_dryRun`. Walks plugin `kcdx.toml` files (which can't set engine-level keys). |
| `src/interfaces.cpp` | `Thunk_Log` — the public `api->Log` shim. Honors per-plugin `log_level` floor (except for WARN/ERROR which always pass). |
| `src/plugin_loader.cpp` | Reads `[plugin] log_level` into LoadedPlugin. Eagerly opens each plugin's log stream after load. |
| `include/kcdx/Interfaces.h` | `KCDX_LOG_*` macros for plugin authors. The 5-line short form over `api->Log`. |
