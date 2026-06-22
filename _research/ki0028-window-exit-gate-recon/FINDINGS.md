# KI-0028 - the window/display loop's exit gate + its writer (static, AP19)

**Date:** 2026-06-22
**Method:** static disassembly of WHGame.dll (release_1_5_1164953_841), image base 0x180000000. No launch.
**Trust:** primary evidence - every "loop tests F" / "fn W writes F" claim is backed by a body read with the cited `site 0x...`. Edges not read are marked **UNVERIFIED**.
**Reuse:** built on `_research/ki0028-fsr2-poll-loop-recon/` (RVA correction + PROBE M) and the byte-scan instrument from `_research/ki0028-metadata-consumer-recon/correlate_pcrypak_slots.py`.

## TL;DR

1. **The 0x869c39 loop's exit-gating fields are the two .data once-guard counters 0x56628d8 and 0x56628dc.** Each back-edge is gated by `cmp dword [<counter>], -1; jne <back-edge>`: loop re-loops WHILE != -1, falls through to ret when -1 (test site 0x869c68 reads [0x56628dc]; test site 0x869ca1 reads [0x56628d8]).
2. **But this is a std::call_once magic-static guard, NOT a producer/consumer completion token - and PROBE M already proved it is not the differentiator.** Writers are the MSVC once-init pair fn 0x1c1e91c (publish: SRW-lock + inc global id + store counter + WakeAllConditionVariable) and fn 0x1c1e988 (acquire: SRW-lock + claim by setting -1, or SleepConditionVariableSRW wait). The counter reaching 0x80002Bxx and never -1 is NORMAL - PROBE M saw identical evolution swap-on/off. Corrects the prior "cross-thread completion handshake" reading: no awaited external producer; the same thread drives the once to -1.
3. **The real perpetual spin sample (0x866090) is a BOUNDED focus poll (fn 0x865fb4, <=5x5ms), called once-per-tick from the tick dispatcher fn 0x667b24.** Its true exit gate is GetActiveWindow() == <engine-expected HWND> (test site 0x866029) plus a winmgr boolean [0x492b890]->[+0x80]->[+0x2b8] (test site 0x866081). Bounded => Main is NOT trapped in it; Main re-runs the whole per-frame tick and the poll re-runs each frame (the per-frame-frame trap, Reframe 6 / PROBE M).
4. **The wedge object 0x549b4a0 is a display/render-context object built per-call by factory fn 0xda65e4 from parent engine object 0x549b498.** 0x549b498 is written by **fn 0x1865a88** = **CSystem::Init / CryENGINE bring-up** (strings "Failed to create the GameFramework Interface!", "Failed to initialize CryENGINE!"). The wedge 0x869c36 call [[0x549b4a0]+0x40] dispatches into a method of this CSystem-built display context - the single statically-unresolvable link (vtable built at runtime by 0xda65e4).

## Q1 - which exact location the loop's EXIT branch tests

0x869c39 function (entry 0x8699f8, _full_loop.txt) is a call_once-guarded routine, NOT a self-contained infinite loop:
- Entry guards (0x869a15/2c/3a/4f): null-checks on [0x492b8c0]/[0x492b8a8]/[0x492b908]/[0x549b4a0]; any null -> je 0x869c3b -> ret 0.
- Two once-protected sub-tasks keyed on counters 0x56628d8 (A) / 0x56628dc (B). The exit branches:
  - site 0x869c68: cmp dword [0x56628dc], -1 -> 0x869c6f jne 0x869bb9 (BACK-EDGE while != -1).
  - site 0x869ca1: cmp dword [0x56628d8], -1 -> 0x869ca8 jne 0x869b2d (BACK-EDGE while != -1).
- Clean exit = wedge then ret: both guards satisfied -> 0x869c36 call [[0x549b4a0]+0x40] -> 0x869c39 jmp -> epilogue -> 0x869c5b ret. Wedge is the last action before return.

So this loop's exit-gating fields are **0x56628d8 and 0x56628dc** (.data dword, tested == -1). Flags 0x556d080 (byte)/0x556d084 (dword) are RESULT writes (site 0x869b72/0x869c1d/0x869c85/0x869cb5), not exit gates.

**Counter meaning - std::call_once (_gate_fns.txt):** fn 0x1c1e988: AcquireSRWLockExclusive(&0x50c5fb0); if counter==0 -> mov [counter],0xffffffff (claim, site 0x1c1e9a3); if == -1 -> SleepConditionVariableSRW(&0x50c5fa8,&0x50c5fb0,INFINITE,0) (site 0x1c1e9c0); else TLS-id + release. fn 0x1c1e91c: SRW-lock; inc global id 0x48fcf60; store into *counter+TLS; release; WakeAllConditionVariable(&0x50c5fa8) (site 0x1c1e97e). MSVC magic-static guard idiom. PROBE M read both live: identical swap-on/off, both freeze at 0x80002Bxx, never -1 even in the menu-reaching run -> this loop's own fields are NOT the differentiator (this CONFIRMS PROBE M).

## Q2 - the real spin gate (0x865fb4) and its writers

0x866090 is inside the BOUNDED window-focus poll fn 0x865fb4 (_focus_poll.txt), loop 0x866023..0x86609c, edi < 5, 5ms Sleep. Exit predicates (each jmp 0x86609e exits):

| gate | site | field | writer / subsystem |
|---|---|---|---|
| G6 window focus | 0x866029 cmp rax,rsi (rax=GetActiveWindow()@0x866023; rsi=expected HWND from [this+0x2d0]->[+0x740]@0x866017) | engine-expected window handle | window/swapchain bring-up sets [+0x740] HWND - **UNVERIFIED** (runtime vtable getter) |
| G7 winmgr bool | 0x866081 test al,al (al=[0x492b890]->[+0x80]->[+0x2b8]()) | winmgr singleton 0x492b890 | tracked by PROBE M (null->populated identically swap-on/off) |
| G3 session byte | 0x865fe7 cmp [0x492ba39],0 | .data byte | fn 0xb95bcc (cl_initClientActor) + fn 0xdaaf28 (sv_gamerules) - session/level load |
| G4 cvar dword | 0x865ff4 cmp [0x4927260],0 | .data dword | no direct-store writer (cvar storage, indirect) |

Poll is BOUNDED (<=5 iters) -> cannot be the hang itself. Sole caller: **fn 0x667b24 at site 0x667ddd** (strings BreakListenerThread/g_BreakListener = engine system-tick dispatcher; _dispatcher.txt). Main at 0x866090 perpetually = Main runs full per-frame ticks; the poll re-runs each frame.

## Wedge object 0x549b4a0 - identified (prior recon: UNKNOWN)

Installer fn 0xda6564 (_wedgeobj_ctor.txt): 0xda6579 call [old+0x28] (release); 0xda657f mov [0x549b4a0],0; 0xda659a call fn 0xda65e4 (factory from parent 0x549b498); 0xda65a6 mov [0x549b4a0],rax (install); 0xda65bf call [new+8] -> validity bool. => 0x549b4a0 = display/render-context rebuilt on demand. **0x549b498 written by fn 0x1865a88** - strings "Failed to create the GameFramework Interface!"/"Failed to initialize CryENGINE!" = **CSystem::Init/CryENGINE bring-up**. Sibling 0xda6118 carries wh_sys_DoCrash/wh_sys_DoFatalError => the 0xda6xxx cluster is CSystem/system-services. Slot [+0x40] target is **UNVERIFIED** - vtable built at runtime by 0xda65e4, no static .rdata lea.

## Strongest candidate for "the swap perturbs this writer"

1. **G6's engine-expected-HWND writer ([this+0x740])** - the only window/present-relevant gate whose producer runs during render-device/swapchain bring-up, the phase a perturbed early asset/config load (the slot-diff indirect residual) would derail. If swap makes an early load take a wrong arm, the swapchain/window object may never publish the expected HWND -> GetActiveWindow()==expected never holds. **UNVERIFIED** (runtime vtable setter).
2. **The 0x549b4a0 display-context factory 0xda65e4 (CSystem::Init lineage)** - wedge invokes [obj+0x40]; perturbing what 0xda65e4/parent 0x549b498 builds could make [+0x40] spin/fail. **UNVERIFIED** (runtime vtable).
3. The 0x869c39 once-counters + 0x492b890 winmgr bool are **exonerated** by PROBE M (identical swap-on/off) - explicitly NOT candidates.

**Boundary (results-driven section 4):** static identified the gate fields, classified the loop as a bounded per-frame poll (not the trap), named the wedge object's subsystem (CSystem::Init display context), and confirmed PROBE M's null. What static CANNOT settle: which runtime vtable getter (G6's HWND setter, or 0x549b4a0's [+0x40]) differs swap-on/off - both runtime-built, not .data. Next: the live multi-hop probe already owed - bracket window/swapchain bring-up + factory 0xda65e4, swap-on vs swap-off, log whether the expected-HWND publish (G6) and the [+0x40] dispatch run identically.

## Worker scripts (co-located)
- disasm_full_loop.py->_full_loop.txt (0x869c39 CFG); disasm_gate_fns.py->_gate_fns.txt (call_once pair); disasm_focus_poll.py->_focus_poll.txt (0x865fb4 spin gate); find_focuspoll_caller.py->_focuspoll_callers.txt; disasm_dispatcher.py->_dispatcher.txt; find_wedgeobj.py->_wedgeobj.txt + disasm_wedgeobj_ctor.py->_wedgeobj_ctor.txt; find_poll_gate_writers.py->_poll_gate_writers.txt; find_writers.py/validate_scan.py/find_cluster_init.py (scanners; validated vs the 4 known flag writes; guard singletons are a gEnv-style pointer table init via base+offset, not direct lea).
