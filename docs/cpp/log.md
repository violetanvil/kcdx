# kcdx logging (↔ kcdx.log)
> Part of the [kcdx C++ API](index.md).

Structured logging. **Built** — `kcdxInterface::Log`, the `kcdxLogLevel` enum,
and the header-only `kcdxLogger` wrapper, all in
[`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h).

## Call shape — the raw call

```cpp
void (*Log)(kcdxPluginHandle self, uint32_t level,
            const char* category, const char* msg);
```

| Arg | Type | Meaning |
|---|---|---|
| `self` | `kcdxPluginHandle` | Your handle; tags the line as the SOURCE field. |
| `level` | `uint32_t` (`kcdxLogLevel`) | `kcdxLog_Trace` (0) / `kcdxLog_Debug` (1) / `kcdxLog_Info` (2) / `kcdxLog_Warn` (3) / `kcdxLog_Error` (4). |
| `category` | `const char*` | A short stable tag for grouping (e.g. `"INIT"`, `"HOOK"`). Pass `null` and the engine substitutes `"PLUGIN"`. The CATEGORY field. |
| `msg` | `const char*` | UTF-8 null-terminated. Do **not** append your own newline — the engine adds it. |

**Returns:** nothing. **Errors:** none signalled; a default-handle/null call is
a safe no-op. Safe to call from any thread.

**Routing** (matches the Lua surface): `Info`/`Warn`/`Error` always reach the
engine log AND your plugin's own log. `Debug`/`Trace` reach your plugin's log
only when dev mode is on AND `category` passes the `dev_categories` filter.

## Call shape — the ergonomic wrapper (preferred)

`kcdxLogger` is the C++ idiom — the mirror of Lua's `kcdx.log.info`/`warn`/…,
with printf-style formatting built in (Lua makes you `string.format` yourself;
C++ varargs do it inline). Header-only, zero-allocation per call (formats into a
1 KiB stack buffer, truncates if exceeded).

```cpp
struct kcdxLogger {
    kcdxLogger() = default;
    kcdxLogger(const kcdxInterface* a, kcdxPluginHandle s);
    bool ready() const;
    void Trace(const char* category, const char* fmt, ...) const;
    void Debug(const char* category, const char* fmt, ...) const;
    void Info (const char* category, const char* fmt, ...) const;
    void Warn (const char* category, const char* fmt, ...) const;
    void Error(const char* category, const char* fmt, ...) const;
};
```

A default-constructed `kcdxLogger` (no api pointer) is a no-op — useful as a
file-scope `kcdxLogger gLog;` assigned during `Plugin_Load`.

## Minimal snippet

```cpp
static kcdxLogger gLog;

bool kcdxPlugin_Load(const kcdxInterface* api) {
    gLog = kcdxLogger(api, api->GetPluginHandle("my.plugin"));
    gLog.Info("INIT", "loaded, engine v0x%08X", api->kcdxVersion);
    return true;
}
```

This is the C++ mirror of [kcdx.log](../lua/log.md).
