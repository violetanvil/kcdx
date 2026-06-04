---
id: KI-0004
opened: 2026-06-03
status: Closed
closed: 2026-06-03
closed_by_commit: 54ebf4e
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

## Fix (as landed)

In `cap-72.cpp`, the unbounded `wsprintfA(buf, ...)` into `char buf[256]` was replaced
with the bounded **`snprintf(buf, sizeof(buf), ...)`** (the standard-C size-bounded
formatter that every other C++ test plugin already uses — cap-04/07/08/09/12/13;
cap-72 was the lone deviation) at all 8 reason-formatting sites, plus `#include
<cstdio>`. `snprintf` caps the write at `sizeof(buf)` and null-terminates, so the
overrun is structurally impossible. Bounded truncation of an over-256-byte reason is
acceptable — the row's int PASS/FAIL passed to `ReportTestResult` is the signal; the
prose is human-readable detail. No engine/interface change — a single-file
test-plugin fix.

## Resolution

**Root cause:** `cap-72.cpp` formatted every `ReportTestResult` reason string into a
fixed `char buf[256]` using the **unbounded `wsprintfA(buf, "<format>", ...)`** (no
size parameter). The CAP-72-callable reason string, after `%s`→"sys_pakPriority" +
`%d`→"2" expansion, is **369 bytes** — a 113-byte overrun of the 256-byte stack
buffer (the literal format text alone is 353 bytes; measured at Gate B). `wsprintfA`
wrote past `buf[256]`, overwriting the `/GS` stack canary; the stack-cookie check
fired `__fastfail` (`int 29h`, `c0000409` / `FAST_FAIL_STACK_COOKIE_CHECK_FAILURE`,
`FAIL_FAST_STACK_BUFFER_OVERRUN`) on the function epilogue in
`cap_72!kcdxPlugin_Load`. The order: the four reason strings were formatted (overrun)
and the rows logged PASS *first*, then the cookie check fired on return — which is why
all four CAP-72 rows appear in the log and the dispatch then dies. cap-71 (Lua) never
ran because cap-72's crash aborted the InputLoaded dispatch. The original path made
the overflow INEVITABLE: an unbounded formatter writing a statically-too-long string
into a fixed buffer overflows on every successful read. The engine `cvar::` core, the
v3 `kcdxConsoleInterface`, and the GetCVar→ICVar→vtable[2/4] dispatch were NEVER the
cause — they produced correct live values (GetCVarInt sys_pakPriority=2, the miss
sentinel survived) before the epilogue check fired.

**Fix:** commit `54ebf4e` — `wsprintfA(buf, ...)` → `snprintf(buf, sizeof(buf), ...)`
at all 8 sites + `#include <cstdio>` (see §Fix). Bounds the write so the overrun
cannot occur; conforms cap-72 to the universal repo idiom.

**Verification:** Gate B (root-cause-verifier) independently re-read the minidump
(confirmed `c0000409` / cookie-check-failure / faulting module `cap_72`, stack-resident
reason-text fragments) and measured the 369-byte overrun → `land-fix`. The fix is
user-confirmed live: the launch at `kcdx-dev_2026-06-03_23-45-49.log` booted to menu
with **NO new crash bundle**, and all 9 cvar rows reported **PASS** — CAP-71 (Lua)
badarg/callable/float/bool/miss + CAP-72 (C++) callable/float/bool/miss. cap-71, which
crash-aborted before, now runs to completion. cap-72 IS the regression test (the
crashing test now completes). The 22 unrelated FAILs in that run are pre-existing
author-target/Address-Library-resolve failures (CAP-20/28/33/34/COMP-12), out of
scope for this KI.

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
