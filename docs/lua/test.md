# kcdx.test
> Part of the [kcdx Lua API](index.md).

| Call | Args | Returns |
|---|---|---|
| `kcdx.test.report(name, pass, reason)` | string name, boolean pass, optional string reason | nothing |

Records a test-suite result, rolled into the engine's `suite: X/Y passing`
summary. The mirror of the C++ `ReportTestResult`. Used by `test_suite_only`
plugins under dev mode.

```lua
kcdx.test.report("CAP-XX", got == expected, "round-trip ok")
```
