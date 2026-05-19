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
| `LUA`     | CFUNCTION_ADDR, PUSH_POINTER, CLOSURE_DUMP, DYNAMIC_HOOK_REQ |
| `MEMORY`  | SCAN, ALLOC, FREE, READ |

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
