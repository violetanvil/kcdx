# kcdxInterface::ReportTestResult (↔ kcdx.test)
> Part of the [kcdx C++ API](index.md).

Report a regression-test result. **Built** — `kcdxInterface::ReportTestResult`
on the root interface in
[`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h) (no
`QueryInterface` needed). This is the C++ original that the Lua
`kcdx.test.report` mirrors.

## Call shape

```cpp
void (*ReportTestResult)(kcdxPluginHandle self,
                         const char*      testName,
                         int              pass,    // 0 = fail, nonzero = pass
                         const char*      reason); // nullable
```

| Arg | Type | Meaning |
|---|---|---|
| `self` | `kcdxPluginHandle` | Your handle. |
| `testName` | `const char*` | The matrix row ID (e.g. `"CAP-01"`). |
| `pass` | `int` | `0` = fail, nonzero = pass. |
| `reason` | `const char*` | Short freeform pass/fail explanation; nullable. |

**Returns:** nothing. **Behaviour:** the last call for a given `testName` wins
(a plugin may re-report on later lifecycle messages). **No-op when dev mode is
off** — production users never see test-suite output. The aggregator rolls
results into a `Test suite: X/Y passing` line on each engine lifecycle message.

## Minimal snippet

```cpp
bool ok = (got == expected);
api->ReportTestResult(self, "CAP-XX", ok ? 1 : 0, ok ? "round-trip ok" : "mismatch");
```

This is the C++ mirror of [kcdx.test](../lua/test.md).
