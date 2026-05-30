---
id: KI-0001
opened: 2026-05-29
status: open
commit_at_filing: 1c01c9d6e25c5b76a9c8c4a8bbc3e8e7f6c6b6a5
---

# KI-0001 — Save-load STATUS_HEAP_CORRUPTION on the chain-mediated lua_pcall path

**Status:** OPEN — not yet investigated.

## Symptom (required)

Loading a save crashes ~17 seconds after `HookedLoadGameWrapper ENTER`. Watchdog produces a `crash_<ts>.zip` (BugSplat dmp included; no BugSplat upload). Boot-to-menu is unaffected; the crash fires specifically during the save-load path with the engine-direct lua_pcall migration (commit `1c01c9d`) live.

Faulting stack:

```
ntdll!RtlReportFatalFailure+0x9
ntdll!RtlReportCriticalFailure+0xa9
ntdll!RtlpHeapHandleError+0x12
ntdll!RtlpHpHeapHandleError+0x7a
ntdll!RtlpLogHeapFailure+0x4b
ntdll!RtlSizeHeap+0x213
WHGame+0x459acb                                       (frealloc wrapper, nearest export ffxFsr2ResourceIsNull+0x22322e)
WHGame+0x459920
WHGame!ffxFsr2ResourceIsNull+0x22322e
kcdx!kcdx::hooks::`anonymous namespace'::HookedFrealloc+0x17b
kcdx!luaM_realloc_+0x31
kcdx!luaH_free+0x38
kcdx!luaC_step+0xc20
kcdx!luaC_step+0xe6
kcdx!luaD_call+0x7c
kcdx!luaD_rawrunprotected+0x6f
kcdx!luaD_pcall+0x4e
kcdx!lua_pcall+0x60
kcdx!kcdx::hook_chain::`anonymous namespace'::DispatchPre+0x304
```

Exception code: `0xC0000374` (STATUS_HEAP_CORRUPTION). The OS heap allocator detected metadata corruption when `RtlSizeHeap` was called on a pointer the heap does not own (or whose header has been overwritten).

## Facts (required)

Empirical observations only — quoted from the launch at 2026-05-29 16:25:25 (`kcdx-dev_2026-05-29_16-25-25.log` + `crash_2026-05-29_16-25-25.zip` containing `KingdomCome.exe.44312.dmp`).

- The cycle's intended verification PASSed before the crash: `CAP-59-fires` + `CAP-64-fires` + `CAP-65-classifier-bootstrapped` all reported PASS at 16:25:41; suite line `suite: 129/160 passing as of kPreLoadGame (21 not yet reported)` at 16:25:45.054.
- `HookedLoadGameWrapper ENTER fire_n=1 playline=0 slot=99` logged at 16:25:45.053; matched `HookedLoadGameWrapper EXIT fire_n=1` at 16:25:45.055 (load hooks themselves completed cleanly).
- Between 16:25:45 and the crash at 16:26:02.243, `hook_chain` emitted **2,156 `re-entrant dispatch depth=2`** + **175 `re-entrant dispatch depth=3`** warnings, all at target `0x00007FFEE43BA5A4` (the migrated `engine.lua_pcall` chain entry). Zero depth ≥ 4. Final log line is one of the depth=2 warnings; no fatal/error/exception/crash line in `kcdx-dev.log` — log terminated mid-write when the process aborted.
- Crash dump exception code: `0xC0000374` (STATUS_HEAP_CORRUPTION). Crash thread RIP at `ntdll!RtlReportFatalFailure+0x9`; the heap allocator raised the fatal failure via the heap-handle-error chain (`RtlpHeapHandleError` → `RtlpHpHeapHandleError` → `RtlpLogHeapFailure` → `RtlSizeHeap+0x213`).
- Faulting call chain (from `.ecxr; k 30`):
  - `WHGame+0x459acb` + `WHGame+0x459920` — WHGame's `frealloc` dispatcher; the nearest export `ffxFsr2ResourceIsNull+0x22322e` is a symbol-proximity artifact (multi-MB offset; FSR2 unrelated to actual function).
  - `kcdx!kcdx::hooks::HookedFrealloc+0x17b` — the engine's PROBE Q canary detour at `src/hooks.cpp:247` (still installed via direct `MH_CreateHook`; frealloc was not migrated this cycle).
  - `kcdx!luaM_realloc_+0x31 → kcdx!luaH_free+0x38 → kcdx!luaC_step+0xc20 → kcdx!luaC_step+0xe6 → kcdx!luaD_call+0x7c → kcdx!luaD_rawrunprotected+0x6f → kcdx!luaD_pcall+0x4e → kcdx!lua_pcall+0x60` — the kcdx-vendored Lua 5.1 static-linked copy (`vendor/lua/`). `luaC_step` is the garbage collector; `luaM_realloc_` is the allocator wrapper that delegates to `g->frealloc`.
  - `kcdx!kcdx::hook_chain::DispatchPre+0x304` — the chain's pre-dispatch entered kcdx's vendored `lua_pcall` (firing a Lua-kind chain entry installed at the `engine.lua_pcall` chain target — cap-59's `kcdx.hook.lua_pcall.before(fn)` is the only Lua-kind plugin entry on that chain in this session).
- **PROBE Q canary (`in_kcdx_image=1`) did NOT fire.** Zero `[MID_HOOK] probe_q.*in_kcdx_image=1` lines in the entire log. The signature catalogued in `closed/kcdx lua_newtable corrupts the process heap.md` (kcdx-side static-const sentinel embedded in a GCObject → WHGame's GC views it as a heap allocation → frees it → next allocator activity sees corrupt metadata) is NOT what fired. The canary saw zero kcdx-image allocations in the entire session.
- `HookedFrealloc` body ran exactly once for its install moment (`probe_q.armed` line at 16:25:36.849). The crash stack's `HookedFrealloc+0x17b` is the body firing during the load — but no `probe_q.kcdx_image*` lines fire because the allocator block is NOT in kcdx's image.
- Pre-migration (before commit `1c01c9d`), the engine's lua_pcall hook was a thin direct-`MH_CreateHook` detour. Post-migration, lua_pcall is chain-mediated via `hook_chain::AddCEngine`; the chain's `DispatchPre` is now on the stack of every lua_pcall fire, including the path that reaches kcdx-vendored `luaM_realloc_ → frealloc` via cap-59's installed Lua-kind callback.
- The save-load completion timing differs by ~17 seconds from the existing `save-load crash 0xC8 raised from WHGame.md` Shape 1 (~10s post-`HookedLoadGameWrapper EXIT`). Different exception code (0xC0000374 vs 0xC8 vs 0xC0000005) and fully-symbolicated kcdx Lua GC stack (vs no-kcdx-frame Shape 1 / unsymbolicated `kcdx+0xb6904` Shape 2).

## Trail (required)

| Date | Action | Result |
|------|--------|--------|
| 2026-05-29 | Re-walked `crash_2026-05-29_16-25-25.zip` dmp via `cdb .ecxr; k 30` | CONFIRMED the filed stack verbatim: 0xC0000374 at `RtlSizeHeap` ← WHGame frealloc ← `kcdx!HookedFrealloc` ← `kcdx!luaM_realloc_` ← `kcdx!luaH_free+0x38` ← `kcdx!luaC_step` ← `kcdx!lua_pcall` ← `DispatchPre`. Ground truth matches the report. |
| 2026-05-29 | PROBE A: read-only — flag a frealloc `block` ∈ WHGame.dll image (sentinel-free smoking gun) | CONFIRMED. One fire, the LAST line before abort: `block=0x7FFEE76E9C40` ∈ WHGame image, `nsize=0` (free), `osize=40` (=sizeof(Node)), `caller_ra` in kcdx `luaH_free→luaM_realloc_`. kcdx's GC freed WHGame's `dummynode_`. |

## Open questions

Causal hypotheses; each labeled `(NOT verified)` per the AP17 facts-vs-hypothesis split.

- **(NOT verified — leading; the FIX-C mirror) kcdx's vendored `luaH_free`, running during kcdx's GC sweep on the SHARED `g->rootgc`, frees a WHGame-allocated Table's `t->node` because that node is WHGame's `dummynode_` (a different address than kcdx's), so the `t->node != kcdx_dummynode` guard at `ltable.c:405` is TRUE and `luaM_freearray` is called on WHGame's `.rdata` sentinel — a non-heap pointer `RtlSizeHeap` rejects.** This is the EXACT inverse of FIX C: FIX C (`setnodevector` at `ltable.c:280`) stops kcdx from ever *writing* its dummynode into a Table so WHGame's GC won't free it; it does NOTHING about kcdx's `luaH_free` *running on a WHGame-created Table*. The migration matters because chain-mediated `DispatchPre` now calls kcdx-vendored `lua_pcall` → `luaC_step` (kcdx's GC) on the shared state under save-load's heavy Lua load — pre-migration the thin direct detour never ran kcdx's GC on this path. Explains why PROBE Q stayed silent: the freed block (WHGame's dummynode) is in WHGame's image, not kcdx's, so PROBE Q's `block ∈ kcdx.dll image` test is structurally blind to it. **Probe (PROBE A, read-only):** at kcdx `luaH_free` entry, log `t` + `t->node` + module-ownership classification of `t->node` + a compare against kcdx's `dummynode`. Outcome: a `t->node` in WHGame's image (≠ kcdx dummynode) reaching the `luaM_freearray` free branch → confirmed; only kcdx-image or kcdx-dummynode nodes observed → killed, re-observe.


- **(NOT verified) The chain-mediated lua_pcall path lets kcdx-vendored `luaM_realloc_ → frealloc` see a GCObject WHGame's allocator does not own.** Pre-migration the engine's direct-MH detour did not interpose kcdx-vendored Lua functions on the dispatch path between lua_pcall and the original; post-migration cap-59's Lua-kind chain entry calls kcdx-vendored `lua_pcall → luaD_pcall → luaD_call → luaC_step → luaH_free → luaM_realloc_ → frealloc` on a `lua_State` that WHGame's Lua also touches. **Probe:** at `kcdx!luaM_realloc_` entry, log the `block` address + module-ownership classification + an indicator of which GCObject the GC step is freeing; replay save-load to capture the corrupted block's provenance.
- **(NOT verified) The 2,156 + 175 re-entrant dispatches accumulate state corruption that the pre-migration thin-detour path avoided.** Re-entrant `lua_pcall` is allowed by the chain; the depth=2/3 warnings note "a non-terminating loop here is the hook author's bug." With cap-59's `before` callback installed on lua_pcall, every lua_pcall fire re-enters the chain; under save-load's many-Lua-callback load (saving Lua state, loading Lua state, dispatching `kcdxMessage_PreLoadGame`/`PostLoadGame`) the re-entrant volume may surface a latent corruption pre-migration's thin detour did not. **Probe:** count and dump the live `g->gc.totalbytes` / `g->gc.gcdebt` / `g->gc.GCthreshold` across the re-entrant storm to see whether the GC trigger threshold trips at an inconsistent state; compare against a vanilla (no-cap-59-installed) save-load.
- **(NOT verified) PROBE Q's "block ∈ kcdx.dll image" check has a coverage gap.** The canary monitors the BLOCK address against kcdx's image range. If the corrupting write places a kcdx-side sentinel INSIDE a heap-allocated GCObject (e.g., a `Node*` pointer field of a `Table` allocated by WHGame's Lua but containing a pointer to kcdx's `dummynode_`), the canary doesn't fire — it sees the GCObject's block as WHGame-owned. **Probe:** instrument `luaH_free`'s `Table*` argument at entry: log the table's `node` field, classify each Node pointer for kcdx-image membership, replay save-load.
- **(NOT verified) Unrelated to the cycle — a pre-existing save-load fragility surfacing now because the suite count is higher (more loaded plugins → more GC pressure).** The cycle added cap-64 + cap-65 + cap-59 flips PASS; the absolute plugin count + GC pressure differ from any prior launch. **Probe:** revert just the lua_pcall site migration (Option D from the dispatching conversation), keep all other cycle work; replay the same save-load; if crash gone, the migration is causal; if crash persists, look at the higher plugin count or unrelated drift.

## Hard rule / design implications

(Empty — `/debug` fills in if a `CLAUDE.md` hard rule or `.claude/rules/*.md` line is wrong or incomplete.)

## Active diagnostic instrumentation

| Probe | Site | What it captures | Status |
|-------|------|------------------|--------|
| PROBE A | `src/hooks.cpp` `HookedFrealloc` + `ArmFreallocProbe` (`IsInWhGameImage`, `g_whgame_image_base/size`) | A frealloc `block` ∈ WHGame.dll image — a static-const sentinel being freed (kcdx's GC freeing WHGame's `dummynode_`). Logs `block`/`osize`/`nsize`/`caller_ra`/`is_free`. | live (pending fix-close → archive) |
| PROBE Q | `src/hooks.cpp` `HookedFrealloc` (kcdx-image branch) | A frealloc `block` ∈ kcdx.dll image (the FIX-C direction). Reads zero. | durable (permanent FIX-C regression guard, `lua-bridge.md`) |

## Resolution (filled when bug closes — GATED)

(Empty — `/debug` writes the Resolution after PROBE-X identifies root cause in falsifiable mechanism terms per AP17.)

## See also

- `save-load crash 0xC8 raised from WHGame.md` — open investigation tracking two earlier shapes (0xC8 RaiseException load-time, 0xC0000005 AV boot-time, both pre-migration). KI-0001 is a structurally different third shape (STATUS_HEAP_CORRUPTION with kcdx Lua GC on stack) introduced post-engine-direct-migration commit `1c01c9d`.
- `closed/kcdx lua_newtable corrupts the process heap.md` — the FIX-C dual-Lua sentinel case. KI-0001's PROBE Q signal is silent because KI-0001 is the **mirror** of FIX-C, not the same direction: FIX-C stops kcdx *writing* its dummynode into a Table (so WHGame's GC won't free it); KI-0001 is kcdx's GC *freeing WHGame's* dummynode. Same dual-Lua sentinel hazard, opposite actor. FIX-C remains in production.

## Reframe 2026-05-29: the FIX-C mirror — kcdx's GC frees WHGame's dummynode_

PROBE A (read-only, the smoking-gun frealloc classifier) fired exactly ONCE,
as the last log line before the abort, with `block` inside WHGame.dll's image
(`0x7FFEE76E9C40` ∈ `[0x7FFEE3CA0000, 0x7FFEE97CB000)`), `nsize=0` (a free),
`osize=40` (= `sizeof(Node)` in the vendored layout: `TValue i_val` 16 +
`TKey i_key` 24), and `caller_ra` resolving into kcdx's `luaH_free →
luaM_realloc_` frame. The crash dump (`crash_2026-05-29_17-19-57.zip`,
`16940.dmp`) is the identical 0xC0000374 stack as the filed one.

**Mechanism (falsifiable):** kcdx and WHGame each statically embed Lua 5.1,
each with its own `static const Node dummynode_` at a different `.rdata`
address, and both drive ONE shared `global_State` / `g->rootgc`. The
chain-mediated `lua_pcall` migration (commit `1c01c9d`) put kcdx-vendored
`lua_pcall → luaD_call → luaC_step` (kcdx's GC) on the dispatch path of every
`lua_pcall` fire (via cap-59's Lua-kind `before` callback at
`hook_chain.cpp` `DispatchPre`). During save-load's heavy Lua activity kcdx's
GC enters its sweep phase and calls `luaH_free` (`vendor/lua/ltable.c:404`)
on a Table that **WHGame's** Lua allocated. That Table's `t->node` points to
**WHGame's** `dummynode_`. The guard at `ltable.c:405`
(`if (t->node != dummynode)` — where `dummynode` is **kcdx's**
`&dummynode_`) is TRUE because WHGame's sentinel ≠ kcdx's, so `luaH_free`
calls `luaM_freearray(L, t->node, ...)` → `luaM_realloc_` → `g->frealloc`
(WHGame's allocator) on a pointer that lives in WHGame's `.rdata`, not the
heap. `RtlSizeHeap` reads the (non-existent) heap header, sees garbage, raises
`STATUS_HEAP_CORRUPTION`.

This is the exact inverse of FIX C. FIX C (`setnodevector`, `ltable.c:280`)
stops kcdx from ever *writing* its own dummynode into a Table so WHGame's GC
won't free it; it does NOTHING about kcdx's `luaH_free` *running on a
WHGame-created Table*. The hazard was always symmetric (FIX C's own comment
describes both directions); only the kcdx→WHGame half was closed. Running
kcdx's GC on the shared state was rare pre-migration (the thin direct detour
never invoked kcdx's `lua_pcall`/GC on this path); the migration made it
routine under save-load, surfacing the un-closed half.
