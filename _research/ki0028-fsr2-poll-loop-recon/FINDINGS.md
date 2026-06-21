# KI-0028 FSR2 poll-loop static recon — FINDINGS

**Verified fact (body-read, 2026-06-20):** the `SleepEx` frame that P-H caught Main
spinning in — `WHGame!ffxFsr2ResourceIsNull+0x36af90` — is a **window-activation
poll loop**, NOT an FSR2 resource wait and NOT a filesystem wait. `ffxFsr2ResourceIsNull`
is merely the nearest exported symbol; the real function is a window/focus poll.

## The function (RVA 0x865fb4, the SleepEx caller)

Clean prologue at `0x865fb4`. The loop (read in this body — verified, not inferred):

```
0x866021  xor edi, edi                 ; i = 0
0x866023  call USER32!GetActiveWindow  ; rax = active window handle   [LOOP TOP]
0x866029  cmp rax, rsi                 ; compare to rsi (expected window handle)
0x86602c  je  exit                     ; exit if active window == expected
0x866048  mov rcx, [global 0x492b890]  ; a system/window manager singleton
0x86604f  test rcx,rcx / je sleep
0x866054  call [rcx_vtbl+0x80]         ; manager method
0x866062  mov rcx, [global 0x492b890]  ; SAME global (g1==g2==0x492b890)
0x866069  call [rcx_vtbl+0x80]
0x86607c  call [+0x2b8]                ; another manager method
0x866081  test al,al / jne exit
0x866085  mov ecx, 5
0x86608a  call KERNEL32!Sleep          ; Sleep(5ms)
0x866090  inc edi; cmp edi,5; jl 0x866023   ; retry up to 5x, then return
0x86609e  ret
```

## Resolved imports / globals (verified)

- `0x86608a call [rip+0x319c6a8]` → IAT slot RVA `0x3a02738` → **KERNEL32!Sleep**.
- `0x866023 call [rip+0x319d237]` → IAT slot RVA `0x3a03260` → **USER32!GetActiveWindow**.
- polled globals g1 (`0x40c5841` rel) and g2 (`0x40c5827` rel) both resolve to
  **RVA 0x492b890** (one window/system-manager singleton; NULL in the static image,
  populated at runtime). Note: 0x492b890 is adjacent to gEnv (id 11, base 0x492b800,
  +0x90 region) — a gEnv-family global. (Confirm the exact identity before relying.)

## Why this is BOUNDED, not the infinite hang

The inner loop runs **at most 5 iterations** (`cmp edi,5; jl`) → ~25ms, then returns
regardless. P-H's two byte-identical 2s-apart Main samples caught Main inside this
short loop's `Sleep`, but the INFINITE repetition is an OUTER loop, not this function.

## The outer caller (read — one call site, NO local back-edge)

Single call site: `0x667ddd  call 0x865fb4`, inside a larger frame/tick-step function
(accesses `[rsi+0x2a30]` counter, `[rsi+0x5e8]`). Straight-line — no back-edge AT this
call. So the window-poll fn is called once per outer step; the repetition is that
`C_Game::CreateInstance`'s outer loop re-runs this whole step, waiting on a completion
condition that never flips. **The outer-loop body above 0x667ddd is NOT yet fully read**
(AP19: not asserting the exact outer back-edge / its exit condition — needs another front).

## What this reframes (the direction change)

KI-0028 is NOT "the FS takeover serves FSR2 wrong content." It is a **window
activation / focus handshake that never completes** — Main polls `GetActiveWindow`
against an expected handle, and the game window never becomes the active window, so
the outer init loop never proceeds → no menu, and the window never enters its normal
message loop → Alt+F4 ignored. The kcdx suspect surface shifts from FS-content to
**whether kcdx's init perturbs window creation / activation / focus / the
window-manager singleton at 0x492b890**.

## Reuse / method

Reused the `pefile + capstone` pattern from `ki0026-ngx-raise-site-recon/`. Scripts:
- `disasm_fsr2_poll_loop.py` — resolve export + disasm the SleepEx site.
- `disasm_loop_body.py` — the full poll-loop body (the listing above).
- `disasm_outer_caller.py` — resolve the Sleep/GetActiveWindow imports + the globals.
- `disasm_find_outer.py` — find + disasm the single outer call site (0x667ddd).

## Next (open in KI-0028)

1. Identify the window-manager singleton at RVA 0x492b890 (is it gEnv->pSystem /
   a window/viewport manager?) and what `[vtbl+0x80]` returns.
2. Read the OUTER loop in CreateInstance (above 0x667ddd) for its true exit condition
   (AP19 — read the body, don't infer).
3. Determine if/how kcdx init touches window creation/activation — the new suspect.

---

## SUPERSEDED above (Reframe 6, 2026-06-21) — the "Main hung in a window-focus
## handshake" conclusion is WITHDRAWN.

The heartbeat (P-H) proved Main advances continuously to tick 58k+ at ~240fps,
AFTER every cdb sample. Main is NOT hung; the byte-identical SleepEx samples were a
RECURRING per-frame pacing/yield helper caught by noninvasive sampling, not a pinned
thread. The whole "window-activation handshake never completes" framing rested on
reading a per-frame helper as a one-time init gate. WITHDRAWN. The premise inverted:
the game RUNS but does not PRESENT a frame / pump window input.

## Static identification of the RenderThread wait-frames — EXHAUSTED (2026-06-21)

KI-0026's identify-by-string-refs method was applied to the three RVAs the
PROBE I RenderThread stack actually returns into (`disasm_identify_renderwait.py`):

| frame RVA | containing-fn entry | size | string refs | verdict |
|-----------|--------------------|------|-------------|---------|
| `0x1de928e` (the `_Cnd_wait` caller) | ~`0x1de9250` | 0x3e | **0** | anonymous condvar-wait helper |
| `0x9acdfb` (one frame up) | ~`0x9acdd0` | 0x2b | **0** | anonymous caller |
| `0xa62b86` (shared bottom frame) | ~`0xa62a9c` | 0xea | **0** | **thread-pool trampoline** — IDENTICAL bottom frame across RenderThread + ShaderCompile + AsyncCommandQueue → it is the generic `thread_start` worker entry, NOT render-specific |

**Decisive negative:** all three are small (0x2b–0xea byte) leaf sync/dispatch
helpers with ZERO string literals. KI-0026's real culprit (`CSystem::FatalError`)
carried a config-path string that named it; these condvar primitives carry none, so
the string-ref method cannot name the subsystem. **The labeled frames
(`NVSDK_NGX_UpdateFeature` / `ffxFsr2ResourceIsNull`) remain nearest-export NOISE
(2–9 MB past export) — confirmed not the real functions, and the real functions are
un-nameable from the static image alone.**

**∴ Static is exhausted.** What the RenderThread waits ON (the condvar's signaller)
is a RUNTIME fact no static read settles (results-driven §4: static first, then the
live probe for behavior no static source answers). The owed work is a LIVE,
INVASIVE capture (the prior captures were `-pv` noninvasive, which Reframe 6 showed
mislead on a running game) — design fork surfaced to the user.

## P-J.3 compute frame IDENTIFIED — entity/AI init, NOT render/present (2026-06-21)

Architect-review (KI-0028 probe-design Gate A) flagged that jumping to "present failure"
skipped the owed §4 static read of the frame Main was ACTUALLY in (P-J.3: `0x536120`
`movss xmm0,[rcx+0x1460]`). Did it (`disasm_pj3_compute_frame.py`, KI-0026 string-ref
method on each containing function up the P-J.3 chain):

- `0x536120` / `0x536018` / `0x534135` / `0x53322e` / `0x53212e` — all compute/math leaves,
  zero string literals (the `0x53xxxx` cluster = a math/transform helper group).
- **`0x36eb39` (the function CALLING that whole cluster) carries the naming strings:**
  `"dummy_no_ai"`, `"player"`, `"<INVALID>"`, and **8 entity-class GUIDs**
  (`7ce6444f-78ab-454c-b008-cdf981b4df4b`, `d71dc3ef-…`, `6212b0b4-…`, `a126227b-…`,
  `568ac26e-…`, `db8fd28b-…`, `ba5379ad-…`, `677e91f4-…`).

**∴ Main's P-J.3 compute was ENTITY / AI / game-object initialization — NOT render, NOT
present, NOT swapchain.** `"dummy_no_ai"` + `"player"` are CryEngine entity archetype/soul
names; the GUIDs are entity-class/component IDs. This is the `C_Game::CreateInstance`
neighborhood doing game-instance/entity construction (consistent with `CreateInstance`
appearing on Main across every sample).

**This REDIRECTS the probe (architect flag B confirmed):** the "present failure" framing
was about to chase a nearest-export label again. The real code Main runs is
instance/entity init. The live shape is more likely **the game is stuck in
`CreateInstance`/entity-init and never reaches the steady-state render/present loop** —
i.e. the wedge is UPSTREAM of present (architect outcome-map row 1), not at present. The
swapped CCryPak perturbs something the instance/entity init consumes (an entity-def file,
a flownode/Lua entity script, a game-data pak the entity system reads) such that
`CreateInstance` loops without completing — while the menu-video RenderThread + the tick
keep running independently.

## Bottom-frame sharing — a real structural fact (2026-06-21)

The PROBE I all-threads dump shows RenderThread (`17d8.42a8`), ShaderCompile
(`17d8.93d0`), and AsyncCommandQueue (`17d8.9620`) ALL bottom out at
`ffxFsr2ResourceIsNull+0x567a86` (RVA `0xa62b86`) → `ucrtbase!thread_start` →
`BaseThreadInitThunk`. That shared frame is the engine's worker-thread pool entry.
So "three threads parked in NGX/FSR2" (the P-E / Reframe-5 reading) overcounts: the
shared NGX-labeled bottom frame is just the thread-pool trampoline mislabeled by
nearest-export. Only the DISTINGUISHING upper frames matter, and those are the
anonymous condvar helpers above — which static cannot name.
