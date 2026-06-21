# KI-0028 — the wedge frame is a WINDOW/DISPLAY-MODE loop, not entity-init (offset-vs-RVA correction)

**Date:** 2026-06-21
**Method:** static disassembly (`disasm_36eb39_outer_loop.py`), no launch.
**Trust:** primary evidence (binary read + three independent cross-checks). NOT hypothesis.

## The correction (load-bearing — overturns the KI's "entity-init" identification)

The PROBE L wedge stack frames are labeled **`WHGame!ffxFsr2ResourceIsNull+0x36eb39`**
etc. (`cdb_pl_probeL_wedge.txt` lines 274–287). `ffxFsr2ResourceIsNull` is a
**nearest-export label** — handoff §2.6 says discount it (the real function is MB past
the export). The KI body + `disasm_pj3_compute_frame.py` stripped the prefix and
disassembled the bare offset `0x36eb39` **as a raw RVA**, landing in an unrelated
entity-name-registration stub (which coincidentally holds `"dummy_no_ai"`/`"player"`/
GUID strings) — and concluded "the wedge is entity/AI init." That identification is an
**offset-vs-RVA conflation artifact.**

Real RVA = `ffxFsr2ResourceIsNull` export (`0x4fb100`) + offset:

| cdb frame | raw offset read as RVA (WRONG) | REAL RVA (= 0x4fb100 + off) |
|-----------|-------------------------------|------------------------------|
| `…+0x36eb39` (the "entity-init" frame) | 0x36eb39 (entity stub) | **0x869c39** |
| `…+0x36ff17` (frame above poll) | 0x36ff17 | **0x86b017** |
| `…+0x36af90` (the bounded focus poll) | 0x36af90 | **0x866090** |
| `…+0x16c7a0` ("update dispatcher") | 0x16c7a0 | 0x6678a0 |

**Cross-check that proves the offset base (3 independent):**
1. `0x4fb100 + 0x36af90 = 0x866090` = Main's confirmed focus-poll RIP (KI line 466 /
   FINDINGS: `0x866090 = inc edi` after `call Sleep`; poll body RVA `0x865fb4`). Exact match.
2. Disassembling raw `0x36eb39` finds a 0x159-byte tail-dispatch stub with **0 back-edges**
   — cannot be the looping wedge. Disassembling real `0x869c39` finds a real function with
   **6 back-edges enclosing the wedge call site.**
3. The real function carries the cvar string **`"r_Fullscreen"`** and polls the
   window-manager singleton at `0x492b890` (KI line 436–437 already ID'd `0x492b890` as
   "a window/system-manager singleton, gEnv-family… non-NULL at runtime").

## What the real function (RVA 0x869c39) actually is

A **window / display-mode / fullscreen management** routine. Evidence in its body:
- `lea rdx, "r_Fullscreen"` at `0x869b7f` → `call [rax+0xb8]` (a cvar/console get on a manager).
- Repeated vtable calls on the `0x492b890`-cluster singletons (`[+0x80]`, `[+0x740]`,
  `[+0x2a0]`, `[+0xb8]`, `[+0x10]`, `[+0x40]`) — `.data` manager-singleton pointers
  (`0x492b880/8a8/8c0/890/908`), the window/display manager family.
- The wedge call site is `0x869c36 call qword [rax+0x40]` (into the `0x549b4a0` object),
  return-into `0x869c39` — exactly the live stack's frame.

## The outer loop (the previously-unread load-bearing fact)

The call site sits inside two enclosing back-edges, both gated by **retry counters in `.data`**:
- `0x869c6f jne -> 0x869bb9` and `0x869c90 jmp -> 0x869bb9`, gated by
  `0x869c68 cmp dword [0x56628dc], -1`.
- `0x869ca8 jne -> 0x869b2d` and `0x869cc0 jmp -> 0x869b2d`, gated by
  `0x869ca1 cmp dword [0x56628d8], -1`.
- The loop re-enters at `0x869bb9`/`0x869b2d`, re-runs cvar/window-manager vtable calls
  (`call [rax+0x2a0]`, `call [rax+0x10]`, `call [rax+0x40]`), and compares results against
  `.data` flags `0x556d080` (byte) / `0x556d084` (dword) and the counters `0x56628d8/dc`.

So the infinite repetition is a **display-mode/window-state apply-and-poll loop** that
re-runs until a window/display-manager state (read through the `0x492b890`-family
singletons + the `0x556d080/084` flags) reaches the value it waits for — which, under the
FS-takeover swap, never does. This is consistent with the EARLIER static recon (KI line
430–453) that read the inner helper `0x866090` as a `GetActiveWindow`-vs-expected-handle
poll — the inner poll is bounded; THIS outer loop (0x869c39) is the unbounded one.

## What this means for the root-cause hunt

- The subsystem is **window/display-mode/fullscreen bring-up**, NOT entity/AI init. Every
  "entity-init" / "CreateInstance entity construction" statement in the KI trail downstream
  of the `0x36eb39`-as-RVA read is the same artifact and should be downgraded.
- This RECONCILES the otherwise-awkward facts: the menu video decodes + loops (RenderThread
  fine), audio plays, the tick advances — yet no frame presents and input is dead. A
  display-mode/fullscreen apply loop that never completes would leave the swapchain
  un-presentable (PROBE K: present frozen + swapchain `ERROR_BUSY`) while everything else runs.
- USER EVIDENCE that fits (KI line 450): a launch WITH window focus acquired still hung —
  the gate is not bare `GetActiveWindow`-match (the inner bounded poll), it is this OUTER
  display-mode loop's completion condition.

## Still UNKNOWN (do not overclaim — the mechanism is not yet pinned)

- **WHICH state the loop waits on, and HOW the FS swap perturbs it.** The loop reads
  window-manager singletons + `.data` flags; it is NOT yet shown which one the swap leaves
  wrong, nor by what path (the freeze window is FS-silent, so it is a state set EARLIER, or
  a non-FS side effect of the swap). The next probe must OBSERVE the loop's exit condition
  live (the values of `0x556d080`/`0x556d084`/`0x56628d8`/`0x56628dc` and the
  `0x492b890`-family vtable returns) swap-on vs swap-off — theory-independent.
- The two retry-counter globals at `0x56628d8`/`0x56628dc` are compared against `-1`
  (sentinel "not set"?) — their semantics are unread.
- `0x549b4a0` (the object the wedge `call [rax+0x40]` dispatches into) is unidentified.

## DEEPER READ (2026-06-21) — the exit condition is a critical-section-guarded completion token

`disasm_869c39_exit_cond.py` + the two helper disasms pin the loop's exit semantics:

- The loop body at `0x869c39` re-runs via `0x869c6f jne -> 0x869bb9` / `0x869ca8 jne ->
  0x869b2d`, each gated by `cmp dword [<counter>], -1; jne`. **It re-loops WHILE the
  counter is `!= -1`; it falls through to the `ret` (clean exit) when the counter is `-1`.**
  Two counters: `0x56628d8` and `0x56628dc` (both `.data`).
- Before each re-check the loop calls **`fn 0x1c1e988`** (passed the counter addr in rcx):
  `EnterCriticalSection` (`call [rip+...]`) → `cmp [counter],0; jne skip` →
  **`mov [counter], 0xffffffff`** (sets the `-1` "done" sentinel) ONLY if the counter was 0.
- **`fn 0x1c1e91c`** is the registration/increment side: `EnterCriticalSection` →
  `inc` a global id (`0x...`) → store it into `*counter` AND into a TLS slot
  (`gs:[0x58]` + index*8) → `LeaveCriticalSection`. A deferred-task / once-registration.
- The loop also reads result flags `0x556d080` (byte) / `0x556d084` (dword) set from the
  return values of vtable calls on the window-manager singletons (`[0x492b8c0]+0x2a0`,
  `[rsi]+0x10` where `rsi` = the `r_Fullscreen` cvar object), and the wedge call site
  `0x869c36 call [[0x549b4a0]+0x40]`.

**Mechanism shape (pinned to a class, NOT yet to the exact actor):** this is a
**critical-section-guarded producer/consumer completion handshake**. A display-mode
operation registers a task/id (`0x1c1e91c` sets the counter nonzero), then the loop spins
re-checking until the token flips to `-1` (= done, via `0x1c1e988` when the work reports 0).
Under the FS-takeover swap the completion never arrives → counter never reaches `-1` → the
loop never exits → Main re-enters the bounded focus-poll `Sleep` every iteration (which is
why every `-pv`/invasive sample catches it at `0x866090`).

This is **cross-thread** (a critical section + a TLS-indexed id => a producer on another
thread is expected to complete the task). That reconciles P-B (vanilla boots — the producer
runs) and P-F (swap is the differentiator — the swap stalls the producer or the state it
needs). The freeze being FS-SILENT fits: the perturbed state was set EARLIER (or is a
non-FS side effect of the swap), not an in-progress file op at wedge time.

## STATIC IS NOW EXHAUSTED (results-driven §4 boundary)

What static CANNOT settle, and the live probe owed:
- The live VALUES of `0x56628d8` / `0x56628dc` at the wedge (is it stuck nonzero = task
  registered, never completed? or `-1` already = the loop is NOT the wedge and the re-entry
  is from above each frame?).
- WHO registered the id (`0x1c1e91c` caller) and WHICH thread is the expected producer that
  flips it — and whether that producer is alive / stalled / never spawned under the swap.
- What `0x549b4a0`'s `[+0x40]` and the `r_Fullscreen`-object's `[+0x10]` return live.

**Owed live probe (theory-independent, swap-on vs swap-off):** read these exact globals +
the critical-section owner from a zero-perturbation watcher (or an invasive cdb `dd`
on `0x56628d8`/`0x56628dc` + `!cs` on the section `0x1c1e988`/`0x1c1e91c` enter). Outcome
map: counter stuck nonzero swap-on but reaches `-1` swap-off => the swap stalls the producer
of THAT task => identify the producer; counter already `-1` at wedge => this loop is not the
wedge, the per-frame re-entry from above is, widen up the stack. Fix stays in kcdx full-init
ownership (no thunk-back) on every branch.

## Reuse pointers

- Script: `disasm_36eb39_outer_loop.py` (this dir) — targets the REAL RVAs; the offset base
  is `EXPORT_FFX = 0x4fb100`.
- Wedge stack: `cdb_pl_probeL_wedge.txt` lines 270–287.
- The offset-vs-RVA trap applies to EVERY `ffxFsr2ResourceIsNull+0x…` / `NVSDK_NGX_…+0x…`
  frame in the trail — add `0x4fb100` (or the NGX export RVA) before disassembling.
