# PROBE Z7 (KI-0028) — read a gEnv-table singleton's null-ness from a watcher thread

**Question:** is the tick's per-frame renderer-dispatch gate `[0x492b908]` NULL on the
full-swap black arm? (If null, the tick's `0x667ed0` gate — `cmp qword [0x492b908], r14 ; je 0x667f84`
— skips the whole scene-submission block every frame, a candidate for present-alive/`draw_indexed=0`.)

**Verdict (run `kcdx-dev_2026-07-03_01-52-47.log`): NON-NULL → gate is NOT the differentiator.**
Read null for the first ~19 wakes (pre-render-init), then non-null (`0x7ff...c0`) at wake 19 and
stayed non-null the whole ~90s run. `verdict saw_non_null=1 wakes=359`. The renderer singleton IS
installed on the black arm; the render-dispatch block runs; the geometry drop is DEEPER (a gated slot
call at `[singleton+0x240/0x250/0x248]`, or the geometry command built below). See KI-0028 Reframe 14.

## The reusable wiring — read a process-lifetime gEnv-table pointer WITHOUT a hook

The load-bearing lesson: a gEnv-table singleton (no static `.text` writer — installed via a base+offset
pointer table) has its runtime null-ness answered by a **periodic watcher-thread read of the pointer**,
NOT by hooking the hot function that consumes it.

- **First attempt (FAILED): hook the tick dispatcher `0x667b24`** and read `[0x492b908]` on each fire.
  `MH_CreateHook` succeeded but `MH_EnableHook` FAILED (`hook_enable_failed rva=6716196`). Cause: the tick
  is a HOT function already executing on the main thread at the seat, and MinHook must suspend+patch a
  live-executing function while the adjacent DISPATCH_PROBE arms are mid-operation (8 hooks armed in the
  same 60ms window). Patching a hot per-frame function at the seat is fragile — do not.
- **Working shape: a dedicated watcher thread** (PROBE K/S/Y shape, `stall_stack_probe.cpp`):
  ```cpp
  g_gateAddr = (uintptr_t)whgame.base + kRvaGateSingleton;  // resolve once at arm
  CreateThread(..., WatcherMain, ...);  // fire-and-forget; self-exits at kRunMs
  // WatcherMain: for(;;){ Sleep(250); memcpy(&p, (void*)g_gateAddr, 8);
  //   if(!p_null && !sawNonNull){ sawNonNull=true; SHOUT }  // one-shot
  //   sample every N wakes (capped); break at kRunMs; write a VERDICT line }
  ```
  A `Sleep`-cadence watcher is the sanctioned DIAGNOSTIC-poll shape here (not production polling), reused
  across KI-0028 probes (K/S/Y). It reads the same process-lifetime pointer with zero hook risk.

**Revival:** to answer "is singleton [X] ever non-null over run Y" for any gEnv-table pointer, copy the
watcher-thread skeleton, set `kRvaGateSingleton = X`, read `[base+X]` per wake, report a null/non-null
verdict. No MinHook, no hot-function hook. The probe source was `src/fs_takeover/render_gate_probe.{h,cpp}`
(removed on retire); reconstruct from this recipe.

**Outcome→meaning map (for the record):**
- NULL across the run → renderer singleton never installed on the full swap → walk WHO installs it, why the swap derails it.
- NON-NULL (what happened) → gate not the differentiator → the drop is deeper (IB-create vs slot-call submission).
