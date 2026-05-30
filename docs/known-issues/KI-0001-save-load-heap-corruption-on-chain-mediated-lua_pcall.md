---
id: KI-0001
opened: 2026-05-29
status: fixed
commit_at_filing: 1c01c9d6e25c5b76a9c8c4a8bbc3e8e7f6c6b6a5
---

# KI-0001 — Save-load STATUS_HEAP_CORRUPTION on the chain-mediated lua_pcall path

**Status:** FIXED 2026-05-29 (Option A — the FIX-C mirror in `vendor/lua/ltable.c`). Root cause + verification in §Resolution. Structural cure deferred to restructure Phase 11 (FIX A).

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
| 2026-05-29 | FIX (Option A, node-only): `kcdx_node_freeable` guard on the `t->node` free/size sites only. Re-ran the exact repro. | PARTIAL. That run (17:46:08) did NOT crash, but a LATER run (18:04:20) crashed identically — the 17:46 run did not exercise the array-free path; it was a false all-clear, not a fix verification. |
| 2026-05-29 | RE-OBSERVE (dump `49300.dmp`, cap-66 build): walk the new 0xC0000374 stack + disassemble `luaH_free` to find WHICH of its 3 frees faulted | `luaH_free+0x39` is the **`t->array`** free (`movsxd r8,[rbx+38h]`=sizearray, `rdx=[rbx+18h]`=t->array), NOT the guarded `t->node` free (`+0x14` je-skips correctly). Freed block `rsi=0x7FFEE76E9C40` ∈ WHGame image. The node guard works; **`t->array` is unguarded** and points at a WHGame sentinel too. |
| 2026-05-29 | FIX (Option A, complete): generalize discriminator to `kcdx_ptr_freeable` (any pointer); guard the `t->array` free in `luaH_free` + the `setarrayvector` realloc (drop foreign sentinel to NULL → fresh alloc). `resize` shrink is sentinel-safe by construction (empty array never shrinks). | VERIFIED (`kcdx-dev_2026-05-29_18-10-29.log`). No crash; slot-99 loaded TWICE; survived ~44s past the 2nd load ENTER (crash window is ~18s) under 5883 d2 / 319 d3 (~2.7× the crash run's 2156/175); suite 130→132→134 across the load lifecycle. The array-free path that faulted at 18:04 now exercised heavily + clean. |

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
| PROBE A | `src/hooks.cpp` `HookedFrealloc` + `ArmFreallocProbe` (`IsInWhGameImage`, `g_whgame_image_base/size`) | A frealloc `block` ∈ WHGame.dll image — a static-const sentinel being freed (kcdx's GC freeing WHGame's `dummynode_`). Logs `block`/`osize`/`nsize`/`caller_ra`/`is_free`. | archived (`#if 0`). Root cause: kcdx's GC freed WHGame's `.rdata` dummynode_; fixed by `kcdx_node_freeable` in `vendor/lua/ltable.c`. |
| PROBE Q | `src/hooks.cpp` `HookedFrealloc` (kcdx-image branch) | A frealloc `block` ∈ kcdx.dll image (the FIX-C direction). Reads zero. | durable (permanent FIX-C regression guard, `lua-bridge.md`) |

## Resolution (filled when bug closes — GATED)

**Status:** FIXED (Option A — the FIX-C mirror). Superseded structurally by restructure Phase 11 (FIX A).

**Root cause:** kcdx and WHGame.dll each statically embed Lua 5.1, each with its
own `static const Node dummynode_` in its own `.rdata` at a DIFFERENT address,
and both copies drive ONE shared `global_State` / `g->rootgc`. When kcdx's
vendored garbage collector runs a sweep (`luaC_step` → `luaH_free`,
`vendor/lua/ltable.c:404`) on a `Table` that **WHGame's** Lua allocated with an
empty hash part, that Table's `t->node` field holds **WHGame's** `&dummynode_`.
The free guard `if (t->node != dummynode)` at `ltable.c:405` compares against
**kcdx's** `&dummynode_`; WHGame's sentinel is a different address, so the
condition is TRUE and `luaM_freearray` → `luaM_realloc_` → `g->frealloc`
(WHGame's allocator) is called on WHGame's `.rdata` sentinel. That pointer is in
a mapped module image, not a heap allocation, so `RtlSizeHeap` reads a
nonexistent heap header, sees garbage, and raises `STATUS_HEAP_CORRUPTION`
(0xC0000374). The chain-mediated `lua_pcall` migration (commit `1c01c9d`) made
this path routine: it put kcdx-vendored `lua_pcall → luaD_call → luaC_step`
(kcdx's GC) on the dispatch path of every `lua_pcall` fire — via cap-59's
Lua-kind `before` callback dispatched at `hook_chain.cpp` `DispatchPre:1167`
(`lua_pcall(L, nargs, LUA_MULTRET, 0)`, kcdx's vendored copy) — so under
save-load's heavy Lua activity kcdx's GC sweeps the shared `rootgc` and meets
WHGame-allocated empty-hash Tables. Pre-migration the thin direct `MH_CreateHook`
detour never invoked kcdx's `lua_pcall`/GC on this path, so the latent bug stayed
dormant.

This is the EXACT INVERSE of FIX C (`setnodevector`, `ltable.c:280`). FIX C
stops kcdx from ever *writing* its dummynode into a Table so WHGame's GC won't
free kcdx's sentinel (the kcdx→WHGame direction). It does nothing about kcdx's
GC *running on a WHGame-created Table* (the WHGame→kcdx direction). The dual-Lua
sentinel hazard was always symmetric (FIX C's own comment describes both
directions); only the kcdx→WHGame half had been closed.

**Confirmed by PROBE A** (read-only frealloc classifier, `src/hooks.cpp`
`HookedFrealloc`): fired exactly once, as the last log line before the abort —
`block=0x7FFEE76E9C40` ∈ WHGame.dll image `[0x7FFEE3CA0000, 0x7FFEE97CB000)`,
`nsize=0` (a free), `osize=40` (= `sizeof(Node)` in the vendored layout),
`caller_ra` resolving into kcdx's `luaH_free → luaM_realloc_` frame. The crash
dump `crash_2026-05-29_17-19-57.zip` (`16940.dmp`) is the identical 0xC0000374
stack. PROBE Q stayed silent throughout because its `block ∈ kcdx.dll image`
test is structurally blind to a WHGame-image block (this is the mirror of FIX C,
not FIX C's own direction).

**Fix:** `vendor/lua/ltable.c` — a `kcdx_ptr_freeable(p)` discriminator (a node
view `kcdx_node_freeable` wraps it) replaces the bare `== / != dummynode`
comparisons at EVERY site that frees, reallocs, or size-reads a Table's `t->node`
OR `t->array`:

- **`t->node`:** `luaH_free`, `resize` (old-node free), `luaH_resizearray`
  (size read), `newkey` (grow decision), `luaH_getn`/`unbound_search` (empty-hash
  read), `luaH_isdummy`.
- **`t->array`:** `luaH_free` (the array free — the site that faulted in the
  cap-66 build) and `setarrayvector` (the realloc — a foreign sentinel is dropped
  to `NULL`/0 so the realloc is a fresh allocation, mirroring FIX C's node
  handling). `resize`'s array-shrink (`ltable.c:481` original) is sentinel-safe by
  construction: a sentinel array has `sizearray == 0`, so the shrink branch
  (`nasize < oldasize`) cannot execute on it.

A pointer is freeable ONLY if it is a real heap allocation; ANY loaded-module-
image (`.rdata`) pointer — kcdx's OWN sentinels or a foreign copy's node/array
sentinels — is non-freeable. The discriminator uses `VirtualQuery`:
`MEMORY_BASIC_INFORMATION.Type == MEM_IMAGE` is a module image (sentinel, skip),
`MEM_PRIVATE`/`MEM_MAPPED` is heap (free). Module images and heap allocations are
disjoint by OS construction — a mapped image is never returned by an allocator —
so the test is robust, ASLR-safe, needs no sentinel-address harvest, and handles
ANY number of distinct foreign sentinels (node + array, per Lua copy). One
`VirtualQuery` per guarded pointer; each `luaH_free` already makes 2–3 frealloc
syscalls so the added cost is the same order, the cornerstone order puts
Performance last, and the guard is temporary (retires at FIX A / Phase 11). A
MEM_IMAGE-region cache is the documented follow-up if a measured GC-sweep frame
hitch ever appears — deliberately omitted now to keep the crash-guard obviously
correct.

**Verification:** the node-only fix produced a FALSE all-clear
(`kcdx-dev_2026-05-29_17-46-08.log` did not exercise the array-free path; a later
run, `49300.dmp`, crashed identically on `t->array`). The COMPLETE fix (node +
array) was verified on the exact repro at `kcdx-dev_2026-05-29_18-10-29.log`:
no crash; slot-99 loaded TWICE (`HookedLoadGameWrapper` fire_n=1 AND fire_n=2);
the session ran to 18:12:03 — ~44s past the second load's ENTER, far beyond the
~18s crash window the unfixed engine died in — under a re-entrant storm of 5883
depth=2 / 319 depth=3 (~2.7× the crash run's 2156 / 175), i.e. the array-free
path that faulted at 18:04 was exercised heavily and stayed clean; suite advanced
130→132→134 across `kPreLoadGame`→`kPostLoadGame` (the save-load lifecycle that
previously corrupted the heap dispatched cleanly). The process-discipline lesson:
"no crash" on a run that did not exercise the faulting path is not a fix
verification — the run must demonstrably hit the path that crashed.

**Structural cure (supersedes this fix):** restructure **Phase 11** (FIX A) —
kcdx.dll force-loads WHGame.dll and resolves every `lua_*` from WHGame's compiled
copy via the FIX A shim, dropping kcdx's static-linked Lua entirely. One compiled
Lua body, one sentinel set → the dual-Lua sentinel hazard (both directions) dies
by construction. `restructure-plan.md` §Phase 11 / 11d names this exact mechanism
(line 1955: "kcdx has NO compiled Lua of its own… One body, one sentinel set,
hazard impossible"). Phase 11 is blocked on the FIX A symbol harvest and runs
after Phases 1–10. **CAP-66-save-load-survives must be re-run as part of Phase
11d's verification gate** (whose own criterion — "PROBE Q canary stays silent
across a full save-load cycle, the canonical dual-Lua hazard repro" — subsumes
it). When Phase 11d lands, the `kcdx_node_freeable` guard + PROBE A + cap-66 all
retire together.

**Diagnostic archive:** PROBE A (`src/hooks.cpp` `HookedFrealloc` WHGame-image
branch + `IsInWhGameImage` + `g_whgame_image_base/size` + the `ArmFreallocProbe`
WHGame-image resolution) → archived in place per the probe-archive rule (see the
Active diagnostic instrumentation table). PROBE Q stays `durable` (the FIX-C
regression guard, `lua-bridge.md`).

**Residual (asserted, not probed) + the designated detector.** The "node + array
is the complete set of foreign-sentinel free sites" conclusion is verified for
kcdx's vendored tree (the only `static const` written into a heap GCObject field
then freed is the empty-Table node/array; `luaO_nilobject_` and `loadlib.c`'s
`sentinel_` are returned/compared values, never freed). It rests on WHGame's
embedded Lua 5.1 sharing the standard invariant (its empty-collection sentinels
live in `.rdata`; no novel sentinel in some other GCObject field). That is the
universal Lua 5.1 design and the crash evidence is consistent with it, but it is
an assumption about a foreign binary, not a probe. **PROBE A is the standing
detector:** if a non-ltable foreign-sentinel free is ever suspected, revive it
(`#if 0 → #if 1`) — a `block ∈ WHGame image` fire at a `caller_ra` OTHER than
`luaH_free` / `setarrayvector` means another sentinel field is live and needs its
own guard. The discriminator (`kcdx_ptr_freeable`) would already classify such a
pointer correctly; the open item is only whether every call site invokes it.

**Doc / rule updates:** `.claude/rules/lua-bridge.md` gains the WHGame→kcdx
direction of the hazard + this fix (the rule previously documented only the
kcdx→WHGame / FIX-C direction). `test-plugins/README.md` adds the cap-66 matrix
section + rows.

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

**Mechanism (falsifiable):** *(`ltable.c` line citations below are stock-Lua 5.1
anchors — the kcdx-patched file has shifted line numbers; cite by function name.)*
kcdx and WHGame each statically embed Lua 5.1,
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

**The hazard is per-SENTINEL, not just the dummynode (corrected after the
node-only fix still crashed).** A first fix guarded only `t->node`. The cap-66
build then faulted again with the IDENTICAL stack — re-observation via
disassembly of the new dump (`49300.dmp`) showed the fault was at
`luaH_free+0x39`, the **`t->array`** free (`luaM_freearray(L, t->array,
t->sizearray, TValue)` at `ltable.c:481`), NOT the now-guarded `t->node` free
(`+0x14` correctly skips). The freed block (`rsi=0x7FFEE76E9C40`) is again in
WHGame's image. A WHGame-allocated Table with an empty array part points
`t->array` at WHGame's empty-array `.rdata` sentinel, the SAME mechanism as the
dummynode but on the array field. The earlier "the dummynode is the only
faulting site" conclusion was WRONG — there are at least two foreign sentinels
per Lua copy (node + array), and every kcdx GC site that frees or reallocs
either field must apply the discriminator.
