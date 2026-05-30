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
|      |        |        |

## Open questions

Causal hypotheses; each labeled `(NOT verified)` per the AP17 facts-vs-hypothesis split.

- **(NOT verified) The chain-mediated lua_pcall path lets kcdx-vendored `luaM_realloc_ → frealloc` see a GCObject WHGame's allocator does not own.** Pre-migration the engine's direct-MH detour did not interpose kcdx-vendored Lua functions on the dispatch path between lua_pcall and the original; post-migration cap-59's Lua-kind chain entry calls kcdx-vendored `lua_pcall → luaD_pcall → luaD_call → luaC_step → luaH_free → luaM_realloc_ → frealloc` on a `lua_State` that WHGame's Lua also touches. **Probe:** at `kcdx!luaM_realloc_` entry, log the `block` address + module-ownership classification + an indicator of which GCObject the GC step is freeing; replay save-load to capture the corrupted block's provenance.
- **(NOT verified) The 2,156 + 175 re-entrant dispatches accumulate state corruption that the pre-migration thin-detour path avoided.** Re-entrant `lua_pcall` is allowed by the chain; the depth=2/3 warnings note "a non-terminating loop here is the hook author's bug." With cap-59's `before` callback installed on lua_pcall, every lua_pcall fire re-enters the chain; under save-load's many-Lua-callback load (saving Lua state, loading Lua state, dispatching `kcdxMessage_PreLoadGame`/`PostLoadGame`) the re-entrant volume may surface a latent corruption pre-migration's thin detour did not. **Probe:** count and dump the live `g->gc.totalbytes` / `g->gc.gcdebt` / `g->gc.GCthreshold` across the re-entrant storm to see whether the GC trigger threshold trips at an inconsistent state; compare against a vanilla (no-cap-59-installed) save-load.
- **(NOT verified) PROBE Q's "block ∈ kcdx.dll image" check has a coverage gap.** The canary monitors the BLOCK address against kcdx's image range. If the corrupting write places a kcdx-side sentinel INSIDE a heap-allocated GCObject (e.g., a `Node*` pointer field of a `Table` allocated by WHGame's Lua but containing a pointer to kcdx's `dummynode_`), the canary doesn't fire — it sees the GCObject's block as WHGame-owned. **Probe:** instrument `luaH_free`'s `Table*` argument at entry: log the table's `node` field, classify each Node pointer for kcdx-image membership, replay save-load.
- **(NOT verified) Unrelated to the cycle — a pre-existing save-load fragility surfacing now because the suite count is higher (more loaded plugins → more GC pressure).** The cycle added cap-64 + cap-65 + cap-59 flips PASS; the absolute plugin count + GC pressure differ from any prior launch. **Probe:** revert just the lua_pcall site migration (Option D from the dispatching conversation), keep all other cycle work; replay the same save-load; if crash gone, the migration is causal; if crash persists, look at the higher plugin count or unrelated drift.

## Hard rule / design implications

(Empty — `/debug` fills in if a `CLAUDE.md` hard rule or `.claude/rules/*.md` line is wrong or incomplete.)

## Active diagnostic instrumentation

(Empty — `/debug` adds rows per probe authored.)

## Resolution (filled when bug closes — GATED)

(Empty — `/debug` writes the Resolution after PROBE-X identifies root cause in falsifiable mechanism terms per AP17.)

## See also

- `save-load crash 0xC8 raised from WHGame.md` — open investigation tracking two earlier shapes (0xC8 RaiseException load-time, 0xC0000005 AV boot-time, both pre-migration). KI-0001 is a structurally different third shape (STATUS_HEAP_CORRUPTION with kcdx Lua GC on stack) introduced post-engine-direct-migration commit `1c01c9d`.
- `closed/kcdx lua_newtable corrupts the process heap.md` — the FIX-C dual-Lua sentinel case. KI-0001's PROBE Q signal is silent, so it is NOT the same mechanism; FIX-C remains in production.
