---
id: KI-0013
opened: 2026-06-08
status: closed
closed: 2026-06-08
closed_by_commit: f6a0d2c
commit_at_filing: 2d0ea6ee
---

# Dev-mode crash log is not self-sufficient — no full stack, no per-frame symbolication, no registers (a dev can't debug a crash from the log alone)

**Status:** closed

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

**Root cause:** `src/crash_guard.cpp` `LogFault()` gated its entire stack walk
behind `if (ctx && IsKernelOrNtdll(moduleName))` (`:294`) — so the walk ran ONLY
for `RaiseException`-class faults whose rip lands in kernel/ntdll, and even then
emitted only the FIRST non-kernel culprit frame via `WalkToCulprit`. For the
common case — a direct ACCESS_VIOLATION in `WHGame.dll` — the rip is not kernel,
so the condition was false, the walk never ran, and the log carried exactly one
raw frame (`rip`/`module`/`module_rva`) with no call chain. Separately, the
faulting `CONTEXT` was available on the unhandled-filter path (`:249`,
`EXCEPTION_POINTERS->ContextRecord`) but no GPRs were ever logged, so the garbage
operand a deref fault leaves in a register was invisible. The dev's only
substantive signal was the `FAULTED_FIRE` hook-fire-history ring, which reads like
a "spiral" and actively mis-framed KI-0012.

**Fix (`f6a0d2c`):** two additive, `ctx`-gated emissions in `LogFault`, both
reusing the existing SEH-safe x64 walker machinery (`RtlLookupFunctionEntry` +
`RtlVirtualUnwind`):
- `FAULTED_REGS` — all GPRs (rax..r15, rip/rsp/rbp) as hex KV values.
- `LogFullStack` — walks + logs the FULL stack frame-by-frame (`FAULTED_FRAME`
  per frame: index + module leaf + module_rva + raw pc), UNCONDITIONALLY when
  `ctx` is available (not gated on `IsKernelOrNtdll`), plus a `FAULTED_FRAMES_END`
  terminator naming how the walk ended (`complete`/`stack_unreadable`/
  `no_progress`) so a truncated stack is loud, never a silent blank.
The fix copies the `CONTEXT` before unwinding (the live faulting context the
minidump needs stays intact), is bounded at 64 frames, guards every stack read
with `ReadProcessMemory`, bails on no-progress, and adds no allocation/lock/throw
inside the SEH filter — the advance logic is duplicated (not shared) so the proven
`WalkToCulprit` culprit-summary path stays byte-identical. The existing `FAULTED`
/ `FAULTED_CULPRIT` / `FAULTED_INVENTORY` / `FAULTED_FIRE` lines are untouched.

**Verification (user-confirmed):** the fixed engine was deployed and the user
relaunched into the same crash (session `2026-06-08_22-15-16`). The new log
carries everything that previously required a manual `cdb` minidump dive:
- `FAULTED_REGS` — `rdx=0x670000017884B334` (the garbage source pointer, `rbx`
  identical), `rip=0x7FF975DAE220`, all 17 GPRs.
- A full 20-frame `FAULTED_FRAME` chain — the complete `WHGame.DLL` graphics-init
  stack (frames 0-14: the FSR2/NGX/`CreateInstance` chain) → `KingdomCome.exe`
  (15-17) → `KERNEL32`/`ntdll` (18-19) — the identical stack previously extracted
  by hand from the dump.
- `FAULTED_FRAMES_END frames=20 reason=complete` — the terminator fired correctly.

A mod author can now debug a crash from the dev log alone — the core promise.
This SEH-filter code runs only on a real OS-triggered fault (not constructible
from a `cap-NN` test plugin without a fault-injection seam, which would be the
DI-for-tests anti-pattern); verified by the green build + the SEH-contract
step-review + the real crash's log above.
