---
id: KI-0004
opened: 2026-06-03
status: open
commit_at_filing: 5f42a0a685a10c4df77491d6a329f1ec4d1571c8
---

# KI-0004 — boot crash after deploying the `kcdx.cvar.*` Lua plugin (cap-71); C++ path clean

## Symptom

The `/feature` acceptance launch of the `kcdx.cvar.*` read surface crashed mid-boot
(BugSplat; watchdog crash bundle `crash_2026-06-03_23-29-56.zip`, 108 MB minidump
`KingdomCome.exe.54832.dmp`, empty bugsplat marker). A clean launch the same session
BEFORE the two cvar plugins were deployed did NOT crash.

## Reproduction

1. Deploy the engine `kcdx.dll` (cvar core + v3 interface) + the two cvar test
   plugins (`cap-71-cvar-read` Lua, `cap-72-cpp-cvar-read` C++) to the live install.
2. Launch KCD2 (dev_mode on).
3. Crash mid-boot, during the `input_loaded` test dispatch.

## Evidence (ground truth — read from the failing-session logs + crash bundle)

- **C++ path is GREEN** — `cap-72-cpp-cvar-read` reported all 4 rows PASS in-game at
  InputLoaded: `GetCVarInt("sys_pakPriority")=2`, `GetCVarFloat`/`GetCVarBool`
  succeeded, the miss row's `0x6BADF00D` sentinel survived (no-garbage-write). So the
  engine `cvar::` core (`src/cvar.{h,cpp}`), the GetCVar→ICVar→vtable[2/4] dispatch,
  and the v3 `kcdxConsoleInterface` all work live — NOT the suspect.
- **The dev log ends ABRUPTLY** at `23:30:12.324`, right after the last `cap-72`
  report line (`CAP-72-miss RESULT=PASS`) — mid the InputLoaded dispatch.
- **The Lua plugin `cap-71-cvar-read` reported ZERO rows** — no `CAP-71-*`
  RESULT/REPORT lines anywhere; no `cap_71_cvar_read` per-plugin log file in the
  crash bundle. Its `kcdx.on("input_loaded")` callback never completed.
- **Regression, not pre-existing** — the prior run (`kcdx-dev_2026-06-03_22-35-01.log`,
  before the cvar plugins) reached `SUMMARY passing=143/174` past menu with no crash
  bundle. Deploying the two cvar plugins introduced the crash.
- The crash bundle is extracted at `%TEMP%\kcdx_crash_2329` (the 108 MB minidump is
  there; faulting stack not yet read).

## Suspect set (from the diff, not intuition)

The crash is in the **Lua cvar read path** — `src/lua_bind_cvar.cpp` (the thunks
`Lua_CvarGetInt`/`Bool`/`Float`, committed `ce5371d`) OR
`test-plugins/cap-71-cvar-read/plugin.lua`, triggered when cap-71's `input_loaded`
callback calls `kcdx.cvar.get_int`/`get_bool`/`get_float`. The C++ surface uses the
SAME engine core and is clean, so the fault is in the Lua BINDING path specifically —
where the dual-Lua sentinel hazard (`lua-bridge.md`) lives (kcdx's vendored Lua and
WHGame's Lua share one `lua_State`).

## Investigation trail

Probe plan (persisted before running — `.claude/rules/plan-persistence.md`); flip each
row's Result as it lands.

| # | Action | Result |
|---|--------|--------|
| P1 | PROBE A: read the minidump faulting stack (cdb `.ecxr; !analyze -v`) — what module/frame faulted, what exception code | **RESOLVED.** Fault is in `cap_72.dll` (NOT the Lua path) — `cap_72!kcdxPlugin_Load+0x725`, exception `c0000409` `FAIL_FAST_STACK_BUFFER_OVERRUN` (stack-cookie check). The stack holds ASCII fragments of the report reason-string ("core, so", "ved valu", "e (compa") — a `char buf[256]` overflowed. |

(Root cause confirmed at P1 — no further probe needed; see Root cause below.)

## Root cause (mechanism)

`cap-72.cpp` formats every `ReportTestResult` reason into a fixed `char buf[256]`
using **`wsprintfA(buf, "<format>", ...)`** — an UNBOUNDED formatter (no size
parameter, the Windows `sprintf`-family equivalent). The **CAP-72-callable** reason
string (cap-72.cpp:104-111) is, after `%s`→"sys_pakPriority" + `%d`→"2" expansion,
LONGER than 256 bytes (the literal text alone exceeds it). `wsprintfA` writes past
`buf[256]`, overwriting the stack canary; the `/GS` stack-cookie check fires
`__fastfail` (`int 29h`, `c0000409`) on the function epilogue — which is why all four
CAP-72 rows logged their reports FIRST (the overflow happened during the formatting,
the cookie check on return) and the log then died mid-dispatch. cap-71 (Lua) never
ran because cap-72's crash aborted the InputLoaded dispatch before cap-71's callback.

The C++ sibling cap-69 (console-print) was clean because it passes string LITERALS to
`ReportTestResult` directly — no `wsprintfA`, no fixed buffer, no overflow. The bug is
entirely in the cap-72 TEST PLUGIN's reporting code; the engine `cvar::` core, the v3
interface, and the GetCVar→ICVar dispatch are all confirmed CORRECT (the 4 rows
reported PASS with real values before the epilogue check fired).

## Fix (settled)

In `cap-72.cpp`, replace the unbounded `wsprintfA(buf, ...)` into `char buf[256]` with
a bounded formatter — `wnsprintfA(buf, ARRAYSIZE(buf), ...)` (the size-bounded
shlwapi sibling) OR a larger buffer sized to the longest format — at every one of the
8 call sites. Bounded truncation is acceptable for a test reason string (the row's
PASS/FAIL is the signal; the prose is human-readable detail). No engine/interface
change — a single-file test-plugin fix.

## Facts

- The C++ cvar surface reads `sys_pakPriority`=2 + float + bool + the miss sentinel
  survives, all in-game (CAP-72 4/4 PASS). (ground truth, run 23-29-56)
- cap-71 (Lua) produced no report rows and no per-plugin log; the dev log truncates
  at the cap-72 reports in the same InputLoaded dispatch. (ground truth, run 23-29-56)
- The pre-cvar run (22-35-01) reached SUMMARY 143/174 past menu, no crash. (ground truth)

## Open questions

- (causal, UNVERIFIED) Does the crash occur INSIDE a `kcdx.cvar.*` thunk
  (`lua_bind_cvar.cpp`), or in cap-71's `plugin.lua` around the call? — P1 (the
  faulting stack) discriminates.
- (causal, UNVERIFIED) Is it the dual-Lua sentinel hazard (`lua-bridge.md`) — a value
  push or a `lua_State` touch in the thunk tripping WHGame's GC — or something
  simpler (a bad arg shape, an ordering issue in plugin.lua)?
