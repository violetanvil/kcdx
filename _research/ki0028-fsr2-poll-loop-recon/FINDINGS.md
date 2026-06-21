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
