---
id: KI-0013
opened: 2026-06-08
status: open
commit_at_filing: 2d0ea6ee
---

# Dev-mode crash log is not self-sufficient — no full stack, no per-frame symbolication, no registers (a dev can't debug a crash from the log alone)

**Status:** open

## Summary

When the game crashes in dev mode, the kcdx crash log gives a mod author almost
nothing actionable. For a direct ACCESS_VIOLATION in `WHGame.dll` (the common
case — a bad-pointer deref in game code, not a `RaiseException`-class fatal), the
`[GUARD] FAULTED` line logs ONLY the single faulting frame's raw `rip` + `module`
+ `module_rva` — no call stack, no per-frame symbolication, and no register
state. The one substantive thing the log DOES dump is the `FAULTED_FIRE`
hook-fire-history ring, which is misleading: it reads like a "spiral" on whatever
hook fired most recently, when the actual fault is unrelated and elsewhere.

This violates the core promise that **devs debug their mods from the dev log**
(`.claude/rules/agent-builds-and-deploys.md` — the agent/user reads the log, not
a debugger). Surfaced concretely during KI-0012: every probe in that
investigation required a manual `cdb` minidump dive (`.ecxr; k; !analyze -v`) to
get the faulting stack, the symbolicated frame (`ffxFsr2ResourceIsNull+0x633120`),
and the garbage operand register (`rdx`) — none of which the log carried. A mod
author has no such tooling and would be stuck at "it crashed in WHGame.dll."

## The defect (root-caused from the source)

`src/crash_guard.cpp` `LogFault()` (the unhandled-exception filter path, which
HAS the faulting `CONTEXT`):

1. **The full stack is never logged.** A working, SEH-safe, allocation-free x64
   native stack walker EXISTS — `WalkToCulprit()` (`RtlLookupFunctionEntry` +
   `RtlVirtualUnwind`, the same machinery Windows' own SEH dispatch uses, no
   `SymInitialize` needed). But it is gated behind `if (ctx && IsKernelOrNtdll(moduleName))`
   (`crash_guard.cpp:294`) — it ONLY runs when the faulting rip is in
   kernel/ntdll (a `RaiseException`-class fault, e.g. the 0xC8 CryFatalError
   path). For a direct AV in `WHGame.dll`, the rip is NOT kernel, so the walk
   never runs and NO stack is logged. And even when it runs, it emits only the
   FIRST non-kernel culprit frame (`FAULTED_CULPRIT`), never the full chain.

2. **No register state.** The faulting `CONTEXT` is available on this path
   (`EXCEPTION_POINTERS->ContextRecord`) but no GPRs are logged. The actual
   smoking gun in KI-0012 — the garbage source pointer in `rdx` — is invisible
   from the log.

3. **No per-frame symbolication aid.** The one frame logged is a raw
   `module_rva` integer; the dev gets `module_rva=11723296`, not
   `WHGame+0x0B2E220` in a form they can paste into a symbol tool. (kcdx has no
   PDB/symbol resolution at runtime, which is fine — but `module + hex rva` per
   frame is the minimum the dev needs, and it is missing for all but one frame.)

## Reproduction

- Any dev-mode launch that crashes with a direct AV in WHGame.dll (KI-0012's
  FSR2/NGX crash is a live repro: launch the `369a99c`-era `kcdx.dll`,
  `dev_mode = true`).
- Read the newest `<game-bin>/kcdx-engine/logs/kcdx-dev_<ts>.log` `[GUARD]`
  lines: a single `FAULTED` frame + the `FAULTED_FIRE` ring; NO stack, NO regs.

## Evidence

- `crash_guard.cpp:294` — `if (ctx && IsKernelOrNtdll(moduleName))` gates the
  entire stack walk to kernel-origin faults only.
- `crash_guard.cpp:298-305` — `WalkToCulprit` emits a SINGLE `FAULTED_CULPRIT`
  frame (the first non-kernel), never the full stack.
- `crash_guard.cpp:265-282` — the `FAULTED` line logs `rip`/`module`/`module_rva`
  + thread; no registers.
- `WalkToCulprit` (`crash_guard.cpp:147-235`) — the walker is already SEH-safe,
  no-throw, allocation-free, bounded (64 frames), guards every stack read with
  `ReadProcessMemory`. Generalizing it to log every frame is low-risk: the
  per-frame `ModuleForAddress` + rva computation it already does is exactly what
  a full-stack dump needs.
- KI-0012's faulting stack + the `rdx` garbage operand — obtained by hand via
  `cdb -z <dump> -c ".ecxr; kn; r"`, NOT from the log.

## Fix direction (the fix lands via /execute; design surfaced to the user first)

Make the dev-mode `[GUARD] FAULTED` block self-sufficient — all reusing the
existing SEH-safe machinery, no new allocation/lock/throw inside the handler:

1. **Log the FULL stack on EVERY fault** (not just kernel-origin) — generalize
   the `WalkToCulprit` walk into a "walk + log every frame" pass emitting one
   `FAULTED_FRAME` line per frame (`frame# + module + module_rva`, the dev's
   symbolication input), bounded + guarded exactly as the existing walk is. Keep
   the `FAULTED_CULPRIT` summary for the RaiseException case.
2. **Log the register state** — one `FAULTED_REGS` line with the GPRs
   (rax..r15, rip, rsp, rbp) from the faulting `CONTEXT` (fixed format, zero
   allocation).
3. Keep the inventory + fire-ring lines (additive signal), but de-emphasize the
   fire ring so it does not read as the cause (it is a breadcrumb, not the
   fault).

## Resolution

(pending — `/execute` the crash-log fix above. Once landed + deployed, KI-0012
resumes with a log that shows the full symbolicated stack + the garbage register
directly, likely resolving it from the log alone.)
