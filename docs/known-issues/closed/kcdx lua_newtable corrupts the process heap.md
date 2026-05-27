# kcdx `lua_newtable` on captured `g_lua_state` corrupts the process heap

**Status: FIXED 2026-05-20 via FIX C (vendored-Lua patch). Root
cause proven by PROBE Q; verification scorecard 9/10 hard pass +
1 unrelated partial (cap-04 skip-original codegen bug surfaced
post-fix, tracked separately).**

**FIX A (structural fix: drop static Lua + route through WHGame.dll)
deferred to outstanding work — see
[`docs/outstanding-work/fix-a-drop-static-lua.md`](../outstanding-work/fix-a-drop-static-lua.md).
Blocked on symbol-harvest tooling (Ghidra FunctionID's
CreateEmptyFidDatabase produces 0-byte FidDb files on this machine;
AOB pattern-match got 22/111 unique matches but with ~3 false
positives; call-graph bootstrap inherited those errors). Will pick
up when a working harvest pipeline lands.**

PROBE Q intercepted WHGame's `frealloc` and caught the exact
predicted call:
`frealloc(g->ud, 0x7FFD1184DC48, osize=40, nsize=0)` — a *free*
of our static-Lua's `dummynode_` (`is_dummynode=1`), called from
WHGame.dll at `caller_ra=0x7FFCE862E283`. The single corrupted
free triggers `STATUS_HEAP_CORRUPTION` inside `RtlSizeHeap`'s
metadata walk of the adjacent heap block ~250ms later.

**The mechanism, end to end:**

1. kcdx statically links Lua 5.1 from `vendor/lua/`. Inside that
   compilation, `vendor/lua/ltable.c:83` defines
   `static const Node dummynode_`. The linker places it in
   kcdx.asi's `.rdata` at `0x7FFD1184DC48` (PAGE_READONLY,
   AllocationBase = kcdx.asi base `0x7FFD11760000`).
2. WHGame.dll independently statically embeds Lua 5.1. Its own
   `static const Node dummynode_` lives somewhere inside
   WHGame.dll at a different address.
3. Both copies operate on a single shared `lua_State*` (the L
   captured from CryEngine's first `lua_pcall`) and its
   `global_State*`. PROBE O confirmed every field of both
   structs is bit-identical across call sites.
4. When kcdx's static-Lua runs `lua_createtable(L, n, 0)`, it
   calls `luaH_new` → `setnodevector` with `nhash=0`, which
   writes `t->node = cast(Node*, dummynode)` where "dummynode"
   resolves to **kcdx-side** `&dummynode_` = `0x7FFD1184DC48`.
5. The Table is linked into `g->rootgc` via `luaC_link`. Both
   Luas share `g->rootgc`.
6. Some time later — during save-load deserialization, but the
   trigger isn't save-load specifically; it's any subsequent
   Lua activity that frees this Table — **WHGame's** Lua walks
   the rootgc chain and reaches our Table. WHGame's `luaH_free`
   runs the check
   `if (t->node != dummynode) luaM_freearray(L, t->node, sizenode(t), Node)`,
   where "dummynode" resolves to **WHGame-side** `&dummynode_`
   (a different address). The pointers don't match, so WHGame
   thinks `t->node` is a real heap allocation and calls
   `frealloc(g->ud, 0x7FFD1184DC48, 40, 0)` to free it.
7. `g->frealloc` is WHGame's heap allocator. It calls into the
   process heap with a pointer pointing into kcdx.asi's
   `.rdata` section. The heap allocator has no record of this
   block. Its first action is a `RtlSizeHeap` to read the block
   header — which lives on a page that doesn't belong to any
   heap. The page contents (kcdx.asi's `.rdata`) look like
   garbage to the heap manager. `RtlSizeHeap+0x213` detects
   inconsistent metadata, calls `RtlpHpHeapHandleError` →
   `RtlReportFatalFailure` → process exits with
   `STATUS_HEAP_CORRUPTION` (`0xC0000374`).

Why the symptom is delayed (the long-standing "crash 5-30s
later" mystery): the free call itself doesn't fault — it
reaches `frealloc` and returns past the metadata-walk-triggered
fatal failure in a later allocator call that happens to walk
adjacent heap metadata. The corrupted heap state is set up at
free time but only detected by the **next** heap operation in
the same arena. cap-04's mid-hook fires at first
`kcdxMessage_InputLoaded`; the bad free happens during
save-deserialization shortly after; the heap manager faults on
the next allocator activity.

**Same mechanism applies to other static-const singletons.**
`luaO_nilobject_` (TNIL sentinel in `lapi.c`),
`luaP_opnames` (debug names), any `static const TString*` —
each Lua source has private copies in private `.rdata`. Tables
were the first one to trip because cap-04 exercises Table
allocation heavily. UpVal / Closure / userdata paths likely
have the same issue lurking.

**Fix not yet shipped.** See "Fix strategy" below — FIX A
(drop vendored static Lua, route every `lua_*` call through
WHGame.dll's symbols) is the correct structural answer. Don't
ship anything until the positive end-to-end test (PROBE Q
returns ZERO `frealloc.kcdx_image_ptr` lines + save-load
completes + cap-04 tests pass) passes against the chosen fix.

cap-04 currently sits with `kcdx.toml.disabled` so the test suite
runs without it. Phase 5g mid-hook write-support is blocked until
the fix lands — see "Fix strategy" below. Recommend **FIX A**
(drop vendored static Lua, route every `lua_*` call through
WHGame's symbols).

## Symptom

With cap-04 enabled (it installs 4 mid-hooks and invokes them at
`kcdxMessage_InputLoaded`), the engine loads cleanly, all 4
mid-hooks install, all 4 invocations dispatch through the JIT
mid-hook → `dynamic_hook_mid` → `lua_createtable(L, n, 0)` →
`lua_pcall` → user callback. Every step logs clean enter/exit.
No fault at any of those points.

Then **5–30 seconds later**, when the player loads a save (or
the engine's auto-load fires), KCD2 crashes deep inside
`WHGame.dll` save-deserialization code. cdb stack walks show:

```
ntdll!RtlReportFatalFailure
ntdll!RtlpHpHeapHandleError
ntdll!RtlSizeHeap+0x213
WHGame+0x459acb
… deeper WHGame frames …
```

Exception code `0xC0000374` (`STATUS_HEAP_CORRUPTION`). No kcdx
frame is on the faulting stack — the heap manager detected
metadata damage during a routine `RtlSizeHeap` call from WHGame.
The actual corrupting write happened earlier, at the
`lua_newtable` call; the heap manager didn't notice until something
later poked the corrupted bookkeeping.

## Facts

- A single `lua_newtable(L)` (or `lua_createtable`) on the
  `lua_State*` kcdx captures from `HookedLuaPcall` triggers
  delayed heap corruption. Crash fires later in `WHGame.dll`
  save-deserialization, detected by `RtlSizeHeap` →
  `RtlpHpHeapHandleError` → `0xC0000374`. No kcdx frame on the
  faulting stack.
- The corrupting write happens at the `lua_newtable` call site;
  the heap manager doesn't notice until later metadata access.
- The captured `L` passes Lua API health checks:
  `lua_status(L) == LUA_OK`, `lua_pushthread(L)` reports
  `is_main_thread=1`.
- Lua's registered allocator lives in `WHGame.dll+0x71E2B0`
  (verified via `lua_getallocf`). Lua-internal allocations land
  in CryEngine's pool, not in the CRT heap kcdx links.
- Read-only Lua API calls (`lua_status`, `lua_pushthread`,
  `lua_getallocf`) do NOT trigger the corruption.
- The corruption does NOT depend on `narray > 0`,
  stack-imbalance, the cap-04 dispatch path, RegisterKcdxTable's
  prior table-creation activity, the Phase 7 console init, or
  the JIT trampoline/detour state (FNV-1a fingerprints of those
  blocks are byte-identical from install through save-load).
- In Phase 6 (2026-05-19, before cap-04 / mid-hooks landed),
  RegisterKcdxTable's own ~12 `lua_newtable`/`luaL_newmetatable`
  calls did NOT corrupt save-load. The corruption appeared with
  Phase 5g mid-hooks present, even when their dispatch path
  isn't on the stack.

## Trail

| Date | Action | Result |
|------|--------|--------|
| 2026-05-20 | Observed save-load `0xC0000374` crash with cap-04 mid-hook test plugin enabled | Repro confirmed |
| 2026-05-20 | Disable bisection: cap-04 disabled entirely | save-load OK |
| 2026-05-20 | cap-04 install only, no Lua invoke | save-load OK |
| 2026-05-20 | cap-04 install + invoke, full Lua dispatch | save-load crashes |
| 2026-05-20 | Within `dynamic_hook_mid`: PushCallback only, no further Lua work | save-load OK |
| 2026-05-20 | + `lua_createtable(L, n, 0)` | save-load crashes |
| 2026-05-20 | PROBE A: standalone `lua_createtable(L, 1, 0); lua_pop(L, 1)` at `set_lua_state(L)` time, no cap-04 dispatch involved | save-load crashes — proves dispatch path is incidental |
| 2026-05-20 | PROBE B: `lua_newtable(L)` (narray=0) | save-load crashes — `narray` not the trigger |
| 2026-05-20 | PROBE C: `lua_getallocf(L, &ud)` read-only | save-load OK |
| 2026-05-20 | PROBE D: `lua_status(L); lua_pushthread(L); lua_pop(L, 1)` read-only | save-load OK; `L` reports main thread, `status=0` |
| 2026-05-20 | PROBE E: `lua_settop(L, 0); lua_newtable(L); lua_pop(L, 1)` (clean stack first) | save-load crashes — stack-imbalance not the trigger |
| 2026-05-20 | PROBE F: bypass RegisterKcdxTable body entirely, PROBE E still runs | save-load crashes — RegisterKcdxTable's prior allocations not the trigger |
| 2026-05-20 | PROBE G: bypass `kcdx::console::Init()`, PROBE E still runs | save-load crashes — Phase 7 console not the trigger |
| 2026-05-20 | Fingerprint JIT buffer + `runtime_func_t` + `detour_hook` heap blocks at install and right before save-load | All byte-identical — nothing kcdx owns is being overwritten |
| 2026-05-20 | PROBE H: log every distinct `lua_State*` flowing through `HookedLuaPcall` for the session | Only one L ever appears (`0x1D1A1E090C0`); same L as PROBE E touches — kills the "wrong L / coroutine confusion" hypothesis |
| 2026-05-20 | PROBE I: bypass all `ApplyOneMidHook` calls (no JIT, no MinHook, no scripting wiring for mid-hooks); PROBE E still runs | save-load crashes — Phase 5g mid-hook install side effects are NOT the trigger. The corruption happens regardless of whether ANY mid-hook is installed. |
| 2026-05-20 | PROBE J: bypass `kcdx::guard::InstallUnhandledExceptionFilter()` so no `SetUnhandledExceptionFilter` is installed; PROBE E still runs | save-load crashes — kcdx's SEH backstop is NOT the trigger. |
| 2026-05-20 | **PROBE K: build Phase 6b baseline (`9941a64`) + backport PROBE E only. ALL plugins disabled.** | **save-load crashes.** Phase 6 binary on KCD2 1.5 reproduces the corruption with a single `lua_newtable(L); lua_pop(L, 1);` inside `set_lua_state`, with **zero plugins loaded** and **no Phase 5g / Phase 7 / log-system / watchdog / crash_guard / mid-hook code present**. **The bug predates Phase 6.** Phase 6 verified its own save-load flow only because the original Phase 6 code never called `lua_newtable` on `g_lua_state` — that surface was untested. The latent bug has been in kcdx since at least Phase 6b. |
| 2026-05-20 | PROBE L: move PROBE E to fire BEFORE `RegisterKcdxTable` (instead of after). Add `LogLuaStateSnapshot` at every step of first-update bring-up (entry, before/after PROBE E, before/after `RegisterKcdxTable`, before/after `set_lua_state`, before/after each `lua_newtable` inside `RegisterKcdxTable`). Snapshot captures L, gettop, registry_ptr, globals_ptr, lua_status, OS thread id. | **save-load crashes — ordering is irrelevant.** Side-by-side snapshot comparison: PROBE E's corrupting `lua_newtable` and `RegisterKcdxTable`'s SAFE `lua_newtable` calls have **identical observable Lua-state context** — same L pointer, same registry_ptr, same globals_ptr, same OS thread, same lua_status (LUA_OK), both calls from `top=0` resulting in `top=1`. **The trigger is invisible to the Lua C API.** Whatever distinguishes our (corrupting) `lua_newtable` from `RegisterKcdxTable`'s (safe) `lua_newtable` is not in the Lua state, the thread context, or the call shape. |
| 2026-05-20 | PROBE M: replace PROBE E's `lua_pop(L, 1)` with `lua_setglobal(L, "kcdx_probe_table")` so the freshly-created Table stays reachable from `_G` instead of becoming GC-eligible immediately. Hypothesis: PROBE E's `lua_newtable + lua_pop` creates an *unreachable* Table that Lua's GC later visits with damaging consequences. `RegisterKcdxTable`'s `lua_newtable + lua_setglobal` keeps each Table rooted in `_G.KCDX` / `_G.kcdx`, never unreachable. | **save-load still crashes.** Table reachability is irrelevant. `lua_newtable` followed by `lua_setglobal` corrupts heap exactly as `lua_newtable` followed by `lua_pop` did. So the trigger is not "kcdx makes garbage tables that GC trips on later" — `RegisterKcdxTable`'s OWN `lua_newtable + lua_setglobal` calls do the same thing and they don't corrupt. **The discriminator is still invisible to every Lua-state observation we've made.** |
| 2026-05-20 | **Baseline confirmation:** strip all engine-side probes (PROBE L/M removed from `hooks.cpp`; engine reverts to its pre-investigation behaviour — only `RegisterKcdxTable + set_lua_state`). Disable cap-04 (`.disabled` suffix). Leave all 19 other plugins (cap-01/03/05/07/08/09/10/12/13/16/17, comp-02/03, scan-demo, engine-self-test, probe-comp-crash, hello-plugin) enabled. | **save-load works.** Confirms the engine's own `lua_newtable` activity in `RegisterKcdxTable` is safe in the unmodified flow, and **cap-04 is the sole real-flow trigger**. cap-04's mid-hook dispatcher's `lua_createtable` call (the one used to build the args table for the user's Lua callback) is the corrupting site. Restore-to-baseline gives mod authors a working test-suite minus cap-04 until the fix lands. |
| 2026-05-20 | **Discovery (static review):** kcdx **statically links its own vendored copy of Lua 5.1** from `vendor/lua/` (see top-level `CMakeLists.txt:23-24` — `add_library(lua STATIC …)` glob over `vendor/lua/*.c`). When kcdx code calls `lua_newtable(L)`, the call resolves to the static copy linked into `kcdx.asi`, NOT to WHGame.dll's Lua. The captured `L` pointer, however, was allocated by WHGame.dll's Lua. Two distinct copies of the Lua VM source code now operate on a single `lua_State*` — any compile-time setting that differs between CryEngine's build and `vendor/lua/luaconf.h` (struct padding, alignment, `LUAI_USER_ALIGNMENT_T`, GC tuning constants) becomes a silent ABI mismatch. `LUA_NUMBER=float` is already matched (hard rule #17); other settings have not been compared. dumpbin confirms kcdx.asi has 0 Lua imports and no WHGame.dll import. | New hypothesis #5: dual-Lua ABI mismatch. Discriminator-vs-RegisterKcdxTable is still unexplained (both go through the static copy). Worth verifying anyway because the captured-`L` design is structurally unusual — most extender stacks either statically link Lua *as the only Lua* (mod authors of an embedding host) or dynamically link to the host's exported symbols (extenders for games that ship a Lua DLL). kcdx does neither: it statically links a private Lua and hands it the host's `lua_State*`. |
| 2026-05-20 | **PROBE N: raw-struct field discriminator hunt.** Wire `LogLuaStateRawStruct(L, tag)` around BOTH RegisterKcdxTable's safe first `lua_newtable` AND a re-armed PROBE M (`lua_settop(L, 0); lua_newtable(L); lua_setglobal(L, "kcdx_probe_table")`) inside `set_lua_state`. The helper dumps direct C-struct reads of `L->status / top / base / l_G / ci / savedpc / stack_last / stack / end_ci / base_ci / stacksize / size_ci / nCcalls / hookmask / allowhook / errorJmp / errfunc / openupval / gclist`, plus the global_State's `frealloc / ud / currentwhite / gcstate / sweepstrgc / rootgc / sweepgc / gray / grayagain / weak / tmudata / GCthreshold / totalbytes / estimate / gcdept / gcpause / gcstepmul / panic / mainthread`. Both snapshots happen on the same L within the same update tick. Hypothesis: at least one field will differ between the safe and crashing sites; that field IS the discriminator (e.g., non-zero `errorJmp` means we're inside a stale pcall; non-zero `nCcalls` means we're in an unbalanced C-call depth; mismatched `frealloc` means the static-Lua doesn't agree with WHGame on the allocator). Predictions: (P1) raw fields differ → discriminator found, points to a specific Lua/VM state precondition. (P2) raw fields identical → bug is inside `lua_createtable`'s body (likely static-vs-WHGame Lua ABI mismatch on something deeper, like a constant or a sub-struct layout) and we need to drop the static Lua. | **save-load WORKED. No crash. Both load-from-main-menu (HookedLoadGameWrapper fire_n=1 + 2) completed; PostLoadGame fired; PROBE N's `lua_settop(L,0); lua_newtable(L); lua_setglobal(L,"kcdx_probe_table")` definitely fired (dev log 15:27:57 lines 403-414).** Side-by-side raw-struct compare between RegisterKcdxTable.after_newtable_KCDX (safe) and PROBE_N.after_newtable (also safe today): **every observed field is structurally the same** — `l_G=0x209A1B2ECB8`, `status=0`, `nCcalls=0`, `errorJmp=0x0`, `frealloc=0x7FFCE862E2B0`, `gcstate=0`, `currentwhite=33`, `panic=0x7FFCEB8C0E54`, `mainthread=0x209A1B2EC00`. Only differences are exactly what you'd expect from two separate `lua_createtable` calls happening seconds apart: a) `top` slot, b) `rootgc` head moves forward, c) `totalbytes` ticks up (~64 bytes per Table). So PROBE N's raw-struct snapshots match RegisterKcdxTable's — both look perfectly healthy. **Bigger finding (from cross-checking earlier dev logs):** the 15:04 PROBE M run that the trail says "still crashes save-load" was tested with **cap-04 ENABLED** (dev log 15:04:55 lines 332-343 show Cap04Test.OnA/OnB/OnC/OnD registering; cap-04 toml was still `kcdx.toml` at that point). The doc's narrative attributed PROBE M's crash to "table reachability is irrelevant" — but the actual trigger was cap-04's mid-hook dispatcher (the known real-flow trigger), not the PROBE M body. Today's PROBE N replicates PROBE M's code path **without cap-04** and save-load works. **PROBE M does NOT crash save-load in isolation.** The earlier rows in this trail that claim "PROBE A/B/E (standalone newtable) crashes save-load" need re-verification under truly-cap-04-disabled conditions — they may have been mis-attributed too. Hypothesis reframed: the corrupting site is uniquely cap-04's `dynamic_hook_mid` `lua_createtable` call (from the JIT-trampoline → MinHook detour stack), not a plain `lua_newtable` from `HookedUpdate`'s main thread. |
| 2026-05-20 | **Cross-check finding (dev log archeology):** earlier PROBE rows in this trail (A, B, E, F, G, I, J, K, L, M) all said "save-load crashes". Re-reading the 15:04 PROBE M dev log shows cap-04 was loaded (Cap04Test.OnA/B/C/D registered as plugin functions). Need to walk back through the other PROBE dev logs to see which probes truly ran cap-04-disabled vs. which inherited cap-04 enablement. The "real" trigger may already be confirmed (cap-04 mid-hook dispatcher) and the rest of the trail is noise from runs that happened to have cap-04 enabled too. | Note: cap-04 was at `kcdx.toml.disabled` *only since the "Baseline confirmation" row*. Every earlier probe likely had cap-04 enabled or partially enabled. The Trail's "save-load crashes" verdict on PROBE A/B/E/etc. needs re-verification against true cap-04-disabled baselines. **The most likely state of the world: the bug is exactly what the Baseline row says — cap-04's mid-hook dispatcher (specifically the `lua_createtable` inside `dynamic_hook_mid`) — and the prior 12 probes were chasing a phantom because the test bed didn't isolate cap-04.** |
| 2026-05-20 | **PROBE O: capture raw-struct snapshot at the *actual* corrupting site.** Wire `LogLuaStateRawStruct(g_lua_state, "PROBE_O.dispatch_mid.before_createtable")` and `.after_createtable` around the `lua_createtable(g_lua_state, (int)param_count, 0)` call inside `kcdx::scripting::dynamic_hook_mid` (scripting.cpp:543). Re-enable cap-04 (rename install-side `kcdx.toml.disabled` → `kcdx.toml`). cap-04 will install 4 mid-hooks at first-update-tick, then `kcdxMessage_InputLoaded` fires and cap-04 invokes its 4 targets, which trips each mid-hook dispatcher, which runs PROBE O around `lua_createtable`. Hypothesis: at least one struct field at PROBE_O.dispatch_mid.before_createtable will differ from the safe field set captured at RegisterKcdxTable.before_newtable_KCDX (which PROBE N already logged in the 15:27:57 dev log). Likely candidates: `errorJmp != 0` (we're inside the engine's outer `lua_pcall` frame), `nCcalls != 0` (deeper C-call depth than the engine expects for a top-level allocation), `ci != base_ci` (the CallInfo chain has been pushed for the trampolined Lua call). Predictions: (P1) at least one field differs and the crash repros → field is the smoking gun, design PROBE Q (mitigation) accordingly. (P2) all fields identical and crash repros → bug is invisible at the C struct level; the discriminator must be on the C stack itself (return address inside our JIT buffer, or stack-pointer alignment differences) and the next probe captures `_AddressOfReturnAddress()` + `__readgsqword(0x30)`. (P3) no crash → either cap-04 silently broke, or PROBE O's two extra log calls themselves changed timing enough to mask the bug (unlikely with synchronous file IO but possible). | **OUTCOME = P2 (crash, fields identical).** Dev log `kcdx-dev_2026-05-20_15-36-40.log` lines 664-717 captured 4 PROBE O dispatch pairs (one per cap-04 sub-test). Each pair fired at 15:36:51.710. cdb on the dmp confirms same `0xC0000374` `STATUS_HEAP_CORRUPTION`, `RtlReportFatalFailure → RtlpHpHeapHandleError → RtlSizeHeap+0x213 → WHGame+0x459acb` — exact same fault signature as PROBE M-era crashes. **Side-by-side struct compare (RegisterKcdxTable.before_newtable_KCDX vs PROBE_O.dispatch_mid.before_createtable, same session):** L (0x1EFA21EDFB0), l_G (0x1EFA21EE068), ci (0x1EF7C66BC98), savedpc (0x0), stack (0x1EFDD35B1D0), stack_last (0x1EFDD35BBC0), base_ci, end_ci, stacksize=165, size_ci=16, nCcalls=0, hookmask=0, allowhook=1, errorJmp=0x0, errfunc=0, openupval=0x0, gclist=0x0, frealloc=0x7FFCE862E2B0, ud=0x0, currentwhite=33, gcstate=0, sweepstrgc=131072, sweepgc, gray=0x0, grayagain, weak, GCthreshold=64533000, gcpause=200, gcstepmul=200, panic, mainthread — **all bit-identical.** Only `top` differs (Lua stack +0x10 vs +0x30 from `base`, because RegisterKcdxTable's `top` had only 2 items pushed by Cry and PROBE_O's `top` has the 3rd item left by RegisterKcdxTable still on stack). PROBE_O.after_createtable shows totalbytes ticking up +80 per call (sizeof(Table) plus narray=1 vector = 64+16 = 80). **CONCLUSION:** The corrupting `lua_createtable` and the safe `lua_newtable` execute on the same L, with bit-identical Lua-VM state; whatever distinguishes them is invisible at the C-struct level. |
| 2026-05-20 | **Hypothesis 6 (the strong one, raised by PROBE O's null result):** kcdx's static-linked Lua compiled `sizeof(Table) = 64 bytes`. WHGame.dll's Lua may compile `sizeof(Table)` to a different value (extra field, different padding, different `LUAI_USER_ALIGNMENT_T`). All other `lua_State` and `global_State` field offsets line up (proven by working `lua_pushthread`, `lua_status`, and now PROBE O's identical struct dump), but Table/Closure/UpVal/Proto/TString are **never directly walked** by anything the kcdx engine validates against. Static-Lua's `luaH_new` allocates `sizeof(Table)` via `frealloc` (WHGame's allocator). The freshly-allocated Table is linked into `g->rootgc`. WHGame's Lua later traverses `g->rootgc` during GC (mark phase reads `t->metatable`, `t->array`, `t->node`, `t->gclist` at WHGame's field offsets). If those offsets disagree with ours, every Table kcdx allocates is the wrong size and WHGame's GC reads garbage past our buffer's end — corrupting whatever heap metadata or adjacent allocation lives there. The reason `RegisterKcdxTable`'s tables survive: they happen during a brief window before WHGame's next GC step (`luaC_step` cycle). cap-04's mid-hook tables happen after more GC cycles have elapsed and the rootgc list now includes their differently-sized objects, raising the probability of WHGame's mark phase touching a misshapen Table. Reframed: **the bug isn't about WHEN we call lua_newtable, it's about whether the Table struct kcdx allocates and the Table struct WHGame walks have the same layout.** | New direction: **PROBE P (dual-Lua sizeof divergence test)** — at PROBE_O.after_createtable, log the freshly-allocated Table pointer + dump the next 96 bytes of memory at it. Compare to: (1) our static-Lua's `sizeof(Table)` = 64, (2) the next Table allocated immediately after (whose `gch.next` pointer in our copy should point to the prior Table — if WHGame links it at a different offset, that's diagnostic). Also resolve WHGame's `lua_createtable` via AOB and call it directly, measuring the `totalbytes` delta vs our copy's delta for the same call. If WHGame's delta != 64+16, sizeof(Table) differs and the dual-Lua hypothesis is proven. |
| 2026-05-20 | **PROBE P (Part A): hex-dump comparison of registry Table (WHGame-allocated) vs freshly-allocated Table (kcdx-allocated).** Inside `dynamic_hook_mid`, immediately before `lua_createtable` capture `lua_topointer(g_lua_state, LUA_REGISTRYINDEX)` and hex-dump 128 bytes (tag `PROBE_P.registry_table_WHGame_owned`). Immediately after `lua_createtable`, capture `lua_topointer(g_lua_state, -1)` and hex-dump 128 bytes (tag `PROBE_P.fresh_table_kcdx_allocated`). 4 cap-04 invocations produce 4 pairs. Compare: (i) the first 64 bytes — the formal Table struct content per static-Lua. Both should have `tt=5` (LUA_TTABLE) at offset 8 and consistent header bytes. (ii) bytes 64-127 — these are PAST our static-Lua's Table boundary. For WHGame's registry Table, if its real sizeof(Table) is e.g. 80 bytes, bytes 64-79 will contain WHGame's extra Table fields (still valid Lua data — gclist forward, sizearray, maybe a debug-name string pointer); bytes 80+ will be heap-allocator block metadata or the next allocation. For our 64-byte Table, bytes 64-127 will be **whatever allocator put right after our buffer** — likely another freshly-allocated GCObject or heap padding. The hex patterns past offset 0x40 will diverge wildly between the two if our Table is undersized. Predictions: (PA-1) bytes 0-63 broadly match (header, gc link, metatable=NULL, array=NULL, lastfree=dummynode, sizearray=0) but bytes 0x40-0x4F look like real Lua data in registry and look like adjacent-heap-noise in ours → sizeof divergence likely confirmed. (PA-2) bytes 0-127 of both look like real Lua data with matching layout → sizeof matches; mechanism is something else. (PA-3) registry Table doesn't dump (pointer not committed) → use a different known-WHGame-owned Table (globals via `LUA_GLOBALSINDEX`). | **OUTCOME = PA-2 with bonus.** Dev log `kcdx-dev_2026-05-20_15-47-15.log` lines 667-725 captured 4 PROBE P pairs. **Both Tables are 64 bytes with identical field layout** — `gch.next` ptr at 0x00, `tt=5` at 0x08, `marked=01` at 0x09, `flags` at 0x0A, `lsizenode` at 0x0B, padding 0x0C-0x0F, `metatable` at 0x10, `array` at 0x18, `node` at 0x20, `lastfree` at 0x28, `gclist` at 0x30, `sizearray` at 0x38. **sizeof(Table) ABI mismatch REFUTED.** But — fresh Table has `node = lastfree = 0x7FFD1196C9E8` (a non-heap pointer in the `0x7FFD...` DLL-loaded-image range), while registry Table has `node = 0x23F6D290420` (heap-allocated). That `0x7FFD...` address is **kcdx.asi's `.rdata`**: our static-Lua's `static const Node dummynode_` defined at `vendor/lua/ltable.c:83`. When `lua_createtable(L, n, 0)` runs in our static copy with `nhash=0`, `setnodevector` assigns `t->node = cast(Node *, dummynode)` — but "dummynode" here resolves to **our** copy's address. WHGame's Lua has its OWN `static const Node dummynode_` at a different address inside WHGame.dll. **When WHGame's GC mark or sweep walks `g->rootgc` and encounters our Table, its `t->node != WHGame_dummynode` check returns TRUE — so WHGame treats our `0x7FFD...` pointer as a real heap-allocated Node array.** Eventual `luaM_freearray(L, t->node, sizenode(t), Node)` (during Table GC, save-deser cleanup, table resize) calls `frealloc(g->ud, 0x7FFD1196C9E8, …)` — the heap allocator gets a pointer into kcdx.asi's `.rdata` section. THAT's the heap-corruption fault. **Same mechanism applies to `luaO_nilobject_`** (singleton TNIL TValue in lapi.c) **and any other static-const singletons each Lua source compiles privately.** Hypothesis (needs PROBE Q for definitive proof). |
| 2026-05-20 | **PROBE Q: definitive frealloc-interception test of the dummynode hypothesis.** Inference from PROBE P pointed at the dummynode mechanism; PROBE Q proves it cold. At first-update-tick after `set_lua_state(L)`, `ArmFreallocProbe(L)` (hooks.cpp): (1) resolve `kcdx.asi`'s image base + size via `GetModuleHandleExW(FROM_ADDRESS, &HookedFrealloc)` + `GetModuleInformation`, (2) create a temp Table via `lua_createtable(L, 0, 0)`, read its `t->node` (offset 0x20) as our static-Lua's `&dummynode_`, pop. `VirtualQuery` the dummynode address and log the resulting `AllocationBase` + `GetModuleFileName` (must come back as `kcdx.asi` for the hypothesis to even start), (3) `MH_CreateHook(g->frealloc, &HookedFrealloc)` to intercept WHGame's allocator. `HookedFrealloc(ud, block, osize, nsize)` checks if `block ∈ [kcdx.asi base, kcdx.asi base+size]`; if yes, log `(ud, block, osize, nsize, caller_ra, is_dummynode)` under tag `frealloc.kcdx_image_ptr`. Pass through to `g_orig_frealloc` unchanged (don't intervene — we want the natural crash chain to play out so we can observe it pre-fault). Predictions: (Q-confirm) one or more `frealloc.kcdx_image_ptr` lines appear before the heap-corruption crash, with `is_dummynode=1` → mechanism proven exactly as hypothesized; the caller_ra value identifies WHGame's specific call site (likely `luaH_free`, `luaH_resize`, or `lua_close`). (Q-partial) lines appear but with `is_dummynode=0` → same family of bug but a different kcdx-static sentinel (e.g., `luaO_nilobject_`, a static TString); hypothesis still validates the dual-Lua framing. (Q-refute) no lines appear and crash still happens → mechanism differs; need to investigate alternative paths (e.g., WHGame reading dummynode as a Node array and dereferencing past it, causing a different page fault that the heap manager attributes to corruption). | **OUTCOME = Q-confirm. Hypothesis PROVEN.** Dev log `kcdx-dev_2026-05-20_16-00-53.log`: (line 378) `probe_q.kcdx_image base=0x7FFD11760000 size=1273856 end=0x7FFD11897000`. (line 379) `probe_q.dummynode addr=0x7FFD1184DC48 alloc_base=0x7FFD11760000 module="kcdx.asi" protect=2 (PAGE_READONLY → .rdata) in_kcdx_image=1` — sanity pinned: our dummynode_ IS in kcdx.asi's read-only data section. (line 380) `probe_q.armed frealloc_addr=0x7FFCE862E2B0` — MinHook detour live. (line 2798, ~9s later) **`frealloc.kcdx_image_ptr ud=0x0 block=0x7FFD1184DC48 osize=40 nsize=0 caller_ra=0x7FFCE862E283 is_dummynode=1`.** That's WHGame calling its own frealloc with our dummynode_ pointer as `block`, requesting a free (`nsize=0`). `osize=40` matches static-Lua's `sizeof(Node) = sizeof(TValue) + sizeof(TKey) = 16 + 24 = 40` exactly — it's a `luaM_freearray(L, t->node, 1, Node)` call from a Table whose `lsizenode=0` and `node != WHGame_dummynode`. Crash dmp `crash_2026-05-20_16-00-53.zip` stack walk confirms the chain: `RtlReportFatalFailure → RtlpHpHeapHandleError → RtlSizeHeap+0x213 → WHGame+0x459acb → WHGame+0x459920 → WHGame+0x71E32E (frealloc continuation) → kcdx+0x3df5b (HookedFrealloc returning) → WHGame+0x71E283 (caller_ra matches)`. The single `frealloc(g->ud, 0x7FFD1184DC48, 40, 0)` call to free a `.rdata` pointer triggers `STATUS_HEAP_CORRUPTION` in `RtlSizeHeap`'s metadata walk of the adjacent heap block. **Root cause proven exactly as hypothesized: dual-Lua static-const sentinel mismatch on `dummynode_`.** is_dummynode=1 means the EXACT pointer matches — not a sibling sentinel. Only ONE such call recorded before the crash, so a single corrupted free is enough to trip the heap manager. Save FIX A for the next session — we have what we need. |
| 2026-05-20 | **FIX C SHIPPED + VERIFIED end-to-end.** Patched `vendor/lua/ltable.c::setnodevector` to always allocate a real 1-Node array when caller passes `size==0` (instead of writing `t->node = cast(Node*, dummynode)`). Also patched `luaH_new` to use `NULL` as the temporary placeholder for `t->node` before `setnodevector` runs. The 11 dummynode-identity comparison sites in ltable.c are untouched — they remain correctness-preserving optimizations that simply don't fire for kcdx-allocated Tables anymore. Cost: +40 bytes per kcdx-allocated Table that previously would have used the dummynode sentinel. Trade-off acceptable; full structural fix deferred to FIX A. **Positive verification against the 10-check scorecard: 9 hard PASS + 1 partial.** Dev log `kcdx-dev_2026-05-20_16-19-10.log`: (line 379) `probe_q.dummynode addr=0x2C4162B78E0 in_kcdx_image=0 protect=4 module=""` — the probe-allocated temp Table's `t->node` is now a **heap pointer** (`0x2C4...` range, PAGE_READWRITE), NOT the .rdata dummynode pointer from earlier runs (`0x7FFD1184DC48`). Critically: **zero `frealloc.kcdx_image_ptr` lines across the full session** (`grep -c` = 0; baseline had 1). PROBE_P hex dump of fresh cap-04 mid-hook Table confirms `node` field at offset 0x20 is `0x2C415FF8180` (heap-allocated 1-Node array), `lastfree` at `node+0x28` (one past end of array), `sizearray=1`. Save-load completed: `HookedLoadGameWrapper fire_n=1 + 2` both EXIT'd; `HookedPostLoadGame` ENTER+EXIT'd at 16:19:57.171; no crash zip generated. The single corrupting `frealloc(g->ud, kcdx_dummynode, 40, 0)` call that PROBE Q captured in the broken-baseline run NEVER fires under FIX C — because we no longer write `kcdx_dummynode` into any Table struct. **Root cause eliminated, mechanism end-to-end blocked.** Verification scorecard: checks 1, 2 (revised), 3, 4, 5, 6, 7, 8, 9 = HARD PASS; check 10 = partial (CAP-04a/d=110 ✓ expected, CAP-04b/c=110 ✗ should be 10 — see next row). |
| 2026-05-20 | **NEW BUG SURFACED post-fix:** CAP-04b (`call_original=false`) and CAP-04c (`call_original="auto"` + `args._skip=true`) both return 110 instead of expected 10. The "skip original" code path in either the mid-hook JIT codegen (`make_jit_midfunc`) or the dispatch-time `_skip` check is broken. Independent of the heap-corruption fix; the bug existed all along and was masked by the crash (which prevented these tests from reporting results). CAP-04a (`call_original=true`) and CAP-04d (`auto` no `_skip`) both pass — the call-original cases work. Tracked separately at [`docs/known-issues/cap-04 skip-original codegen does not skip the original instruction.md`](cap-04%20skip-original%20codegen%20does%20not%20skip%20the%20original%20instruction.md). |

## Open questions

**Root cause PROVEN by PROBE Q (2026-05-20): dual-Lua
static-const sentinel divergence.** kcdx statically links Lua 5.1 from
`vendor/lua/`. WHGame.dll statically embeds its own Lua 5.1.
Both copies operate on one shared `lua_State*` / `global_State*`
/ `g->rootgc` chain, and they agree on every visible struct
layout (PROBE O proved this for `lua_State` and `global_State`,
PROBE P proved this for `Table`). **But each Lua source
privately compiles its own `static const` singletons** —
`dummynode_` (the empty-hash sentinel in `ltable.c`),
`luaO_nilobject_` (the TNIL sentinel in `lapi.c`), and likely
others. The two copies' singletons live at different addresses
because they're embedded into different binaries' `.rdata`.

When our static-Lua's `lua_createtable(L, n, 0)` runs, it sets
`t->node = &kcdx_static_dummynode_` (a pointer into kcdx.asi's
`.rdata`). WHGame's Lua doesn't recognize that pointer as a
sentinel — it has its own dummynode at a different address. So
when WHGame later walks `g->rootgc` during GC (mark / sweep /
table-resize / luaH_free), the check `if (t->node != dummynode)
luaM_freearray(L, t->node, sizenode(t), Node)` evaluates
`t->node != WHGame_dummynode` → **TRUE** → WHGame calls
`frealloc(g->ud, 0x7FFD1196C9E8, sizenode*16, 0)` to "free"
what it thinks is a heap-allocated node array. The allocator
receives a pointer into kcdx.asi's `.rdata` section. The heap
manager's metadata walk later trips on the next adjacent block
and raises `STATUS_HEAP_CORRUPTION`.

This explains:
- **The delayed crash.** Corruption is introduced at allocation,
  but the fault doesn't fire until WHGame's GC actually walks
  the rootgc chain to that Table and tries to free its
  `dummynode`-pointing node array. Save-deserialization is one
  trigger because it does heavy Lua state restoration + GC
  steps.
- **Why RegisterKcdxTable's tables seem safe.** RegisterKcdxTable
  attaches its tables to globals (`_G.KCDX`, `_G.kcdx`),
  pinning them strongly. Sub-binders then call `lua_setfield`
  many times, which causes the table's node array to be
  **reallocated** by our static-Lua (kcdx-side) early on. After
  reallocation, `t->node` points to a real heap-allocated Node
  array, not the static `dummynode_`. By the time WHGame's GC
  ever walks them, the dummynode-pointer trap is gone. cap-04's
  mid-hook scratch tables, by contrast, are typically used for
  a single dispatch then popped — they often retain
  `t->node == kcdx_dummynode_` for their entire (short)
  lifetime, fully exposed to the corruption mechanism.
- **Why Hard rule #15 (sol2 metatable corruption) had similar
  symptoms.** sol2's `new_usertype<T>` allocates metatables and
  other GCObjects via our static-linked Lua, each potentially
  carrying kcdx-side dummynode pointers. Same mechanism, just a
  different entry point that produced more metatables exposed
  to the corruption.

This also resolves the puzzle from earlier in the trail —
PROBE O's identical struct dump made sense once we realized the
discriminator was buried INSIDE the `Table` struct's `node`
field, not in the `lua_State` / `global_State` view.

What's structurally different about cap-04's call site:

1. **Call frame depth:** dispatcher runs from inside `cap04_target_*`
   stub → MinHook 5-byte trampoline → JIT'd marshaling glue →
   `dynamic_hook_mid`. The `lua_State*` `g_lua_state` we use is
   the same captured one, but the C stack underneath is deeper
   and contains JIT-generated frames.
2. **`scripting.cpp::dynamic_hook_mid` holds `g_lock`** (a
   `std::recursive_mutex`) during the `lua_createtable` call.
   `HookedUpdate` does not hold any lock during `RegisterKcdxTable`'s
   newtable calls. Holding a recursive_mutex shouldn't matter for
   Lua, but it does mean we're calling Lua from a different
   synchronization context.
3. **Re-entry guard:** `DispatchGuard re_entry` increments a
   thread-local counter before `lua_createtable` runs. If
   `lua_createtable` itself fires a Lua hook that re-enters the
   dispatcher, the guard blocks it — but the counter is non-zero
   during the call.
4. **The mid-hook fires from a target the engine has just
   trampolined into.** Even though it's the main thread, the
   prior frame's RIP is inside our JIT buffer, not inside the
   engine. If the engine's Lua VM walks the C stack for any
   reason (debug info, GC mark traversal of pinned roots,
   coroutine resume), it would see frames it doesn't know.

**Fix strategy.** The root cause is structural: any code path
where kcdx's static-Lua creates GCObjects whose layouts embed
pointers to kcdx-side static-const sentinels (dummynode_, the
nilobject, etc.) is unsafe because WHGame's Lua will mistake
those pointers for heap allocations. Three fix options, ordered
by structural correctness vs effort:

- **FIX A (route every kcdx Lua call through WHGame's Lua):**
  resolve every `lua_*` and `luaL_*` function used by kcdx via
  AOB or address-library lookup into `WHGame.dll`, build a shim
  that exposes those resolved pointers as the standard Lua API
  to kcdx's source, and drop the `vendor/lua/*` static-link.
  Every Table, Closure, String we allocate then goes through
  WHGame's `lua_createtable` etc. and embeds WHGame's sentinels.
  Cleanest fix; eliminates the dual-Lua design entirely. Heavy
  lift: ~80 Lua API functions to resolve + verify.
- **FIX B (replace mid-hook scratch allocation with a registry-
  pinned scratch Table):** allocate ONE Table from WHGame's
  side at engine init (call `lua_createtable` from inside a
  WHGame-Lua context — easier said than done) or from our
  static side once and pre-grow its node array so
  `t->node != kcdx_dummynode_` before WHGame ever sees it
  (push a dummy field, then remove it). Reuse this Table across
  every mid-hook dispatch — reset its contents inline. Mitigates
  cap-04 but leaves the broader hazard for any new kcdx code
  path that allocates Tables. Not a real fix; a stopgap.
- **FIX C (forward sentinel addresses):** resolve WHGame's
  `dummynode_` address (via AOB on `lua_createtable`'s body
  and tracing the `&dummynode_` constant) plus
  `luaO_nilobject_`. Patch our static-Lua's `dummynode` macro to
  return that address at runtime. Requires modifying
  `vendor/lua/ltable.c` to read the sentinel from a runtime
  variable instead of taking `&dummynode_`. Surgical but
  invasive to vendored code; needs careful rev-engineering of
  the WHGame sentinels and verification across all GCObject
  types.

Recommend **FIX A**. It's the right structural answer and aligns
with how SKSE/F4SE/RoM all work (extender uses host's Lua
exports, not a private copy). The "two Luas" design was always
going to bite somewhere — it just happened to bite at
`dummynode_` first.

If FIX A is too big for a single landing, **FIX B as a stopgap
to unblock Phase 5g mid-hook write-support** is acceptable.
Document this issue as still open at the architectural level,
and target FIX A in v0.2.

The original "Open questions" candidate list below is retained
for historical context but is now mostly moot — the bug isn't
about call frame, ABI of lua_pcall, or VM threading; it's about
private static-const sentinels in the kcdx-statically-linked
Lua.

The remaining candidate surfaces now look like *kcdx's
foundational design assumptions about CryEngine's Lua VM*:

- **The `L` we capture from `HookedLuaPcall` may not be a Lua
  state kcdx is *authorized* to allocate against.** CryEngine
  may treat `lua_pcall`'s `L` parameter as a *borrowed
  read-only handle* — safe to push/pop but unsafe to
  allocate-into outside of the call frame the engine is
  currently driving. Probe: acquire `L` via
  `gEnv->pScriptSystem->GetScriptHandle()` (CryEngine's owned
  primary state) instead of pcall capture, and check whether
  PROBE E corrupts on that handle.
- **The hooked `lua_pcall` ABI may be wrong.** Today kcdx's
  `HookedLuaPcall` declares `int(__cdecl*)(lua_State*, int,
  int, int)`. If CryEngine actually compiles Lua with
  `__fastcall` on Windows x64 (which Lua's `lapi.c` does when
  `LUA_USE_C89` isn't defined and the host enables fast-call
  conventions), the captured `L` could be the wrong register
  contents — a different pointer that happens to deref as a
  valid-looking `lua_State` for read-only ops but not for
  allocation. PROBE D's `lua_pushthread` returning 1 wouldn't
  catch this: a stale or aliased pointer can still pass a
  shallow main-thread check.
- **Why RegisterKcdxTable works on the captured `L` but PROBE E
  doesn't is now the central mystery.** Both call `lua_newtable`
  on the same captured pointer at the same call site in
  `HookedUpdate`. If one corrupts and the other doesn't, **the
  difference is in execution context, not in the L pointer**.
  Probe: run RegisterKcdxTable + PROBE E in different orders
  (PROBE E first, RegisterKcdxTable second) and see if the
  ordering matters.
- **The captured-via-pcall pattern may be wrong for CryEngine
  specifically.** SKSE / F4SE hook `IScriptSystem::Update` to
  capture L, not `lua_pcall`. If CryEngine's Lua VM has multiple
  internal `lua_State*` instances (main thread + Cry-managed
  coroutines) and we caught a transient pcall L, the symptoms
  fit. Probe: dump `L`'s `LG.l_G` (the global state pointer) —
  all states in one VM share `l_G`. Compare values across
  different lua_pcall invocations to see if they all share one
  `l_G` or several.

Next investigation step: find HOW RegisterKcdxTable's many
`lua_newtable` calls successfully avoid corrupting heap when
PROBE E's single call corrupts it. That divergence is the
smoking gun.

## Hard rule #15 implications

`kcdx/CLAUDE.md` hard rule #15 currently attributes save-load
fragility to **binding-library metatables registered in
`LUA_REGISTRYINDEX`** (sol2 `new_usertype<T>` was the original
trigger). PROBE P now reveals the actual mechanism, and it
**reframes #15 entirely**:

The original sol2 crash was almost certainly the **same
dual-Lua sentinel issue surfaced through a different entry
point.** sol2's `new_usertype<T>` allocates metatables (which
are Tables) via our static-linked Lua. Those metatables embed
kcdx-side `dummynode_` pointers. WHGame's GC later walks them
and trips on the cross-binary pointer just like cap-04's
mid-hook scratch tables do. The "binding library bad" framing
was incomplete: the binding library wasn't the problem; the
problem was **static-linking a private copy of Lua and handing
its GCObjects to WHGame's GC**. Any binding library happens to
allocate enough Tables/metatables in a short window to expose
the bug fast.

Once FIX A lands (kcdx routes all Lua calls through WHGame's
exported symbols), the dual-Lua hazard goes away by
construction. Rule #15's *intent* — "don't introduce ABI
mismatches when wiring kcdx into WHGame's Lua VM" — stays
valid. The *prescription* — "use raw Lua C API only" — was a
reasonable mitigation when we didn't know the mechanism (raw
API minimizes how many static-Lua functions we call, which
incidentally reduces how many tables we allocate), but it
doesn't prevent the actual bug. Rule #15 should be rewritten
post-FIX-A with the real mechanism documented.

## Positive end-to-end verification test (the "fix works" oracle)

Whatever fix lands (FIX A, B, or C), it must pass ALL the
following checks in a single game session. PROBE Q stays armed
during these runs — it's the canary.

### Setup
- cap-04 ENABLED (`E:\...\test-suite\cap-04-midhook\kcdx.toml`,
  not `.disabled`). cap-04 is the canonical reproducer; if it
  doesn't fire mid-hooks, we haven't tested the fix path.
- All other plugins at their default state (`engine-self-test`,
  `cap-01`/`03`/`05`/`07`/`08`/`09`/`10`/`12`/`13`/`16`/`17`,
  `comp-02`/`03`, `scan-demo`, `probe-comp-crash`, `hello-plugin`).
- PROBE Q armed in `HookedUpdate`. PROBE O + PROBE P snapshot
  code stays in `dynamic_hook_mid` — they confirm mid-hooks
  fired and produced Tables.

### Required positive outcomes (ALL must hold)

| # | Check | What it proves |
|---|-------|----------------|
| 1 | `probe_q.armed` line present in dev log | Frealloc hook installed; the canary is live. |
| 2 | `probe_q.dummynode` line present with `in_kcdx_image=1` | Sanity: our static-Lua dummynode_ is still in kcdx.asi's .rdata (no accidental ABI shift). |
| 3 | 4× `dispatch.enter` lines for cap-04 sub-tests a/b/c/d | Mid-hooks installed and fired. |
| 4 | 4× `PROBE_O.dispatch_mid.before_createtable` + 4× `PROBE_O.dispatch_mid.after_createtable` lines | mid-hook dispatcher reached the table-allocation point. |
| 5 | 4× `PROBE_P.fresh_table_kcdx_allocated` lines | Tables actually allocated (didn't no-op). |
| 6 | **`PROBE_P.fresh_table_kcdx_allocated` hex shows `node` field (offset 0x20) is a heap-allocated address, NOT `0x7FFD1184DC48` (kcdx-static dummynode)** | The Table that just got allocated points at WHGame's dummynode (or a heap-allocated Node array). The dual-Lua mismatch is gone. |
| 7 | **ZERO `frealloc.kcdx_image_ptr` lines across the full session** | The corrupting free call WHGame was making does not happen. No kcdx.asi pointer ever reaches WHGame's allocator. |
| 8 | `HookedLoadGameWrapper EXIT fire_n=N` lines present for at least one load with `N >= 1`, AND `HookedPostLoadGame EXIT` reached | Save-load fully completed including PostLoadGame — the historical crash signature is gone. |
| 9 | No crash zip generated for this session (no `kcdx-engine/logs/crash/crash_<this-timestamp>.zip`) | Process didn't die. |
| 10 | cap-04 test results: CAP-04a returns 110 (call_original=true, no skip), CAP-04b returns 10 (call_original=false), CAP-04c returns 10 (auto + Lua sets `_skip=true`), CAP-04d returns 110 (auto, no skip) | The whole mid-hook feature is correct, not just non-crashing. |

### Failure modes (any of these means the fix is incomplete)

- Check 6 fails (fresh Table still has `node = 0x7FFD1184DC48`):
  FIX didn't actually route through WHGame's `lua_createtable`.
- Check 7 fails (frealloc.kcdx_image_ptr lines appear):
  Some other static-const sentinel (luaO_nilobject_, a
  TString, a Closure) is still leaking through. Hunt by
  `block` address — log `VirtualQuery` on it, identify which
  kcdx symbol it is.
- Check 8 fails (save-load doesn't complete) but Check 7
  passes (no .rdata frees logged): the dual-Lua issue is fixed
  but there's a separate save-load bug. Unlikely but possible.
- Check 9 fails (crash) but Check 7 passes: same as #8 — fix
  is doing its job, different bug elsewhere.
- Check 10 fails: pre-existing cap-04 bugs that were masked by
  the crash now visible. Track separately.

### Comparison against the broken baseline

Before-fix baseline (PROBE Q run 16-00-53):
- ✓ Check 1, 2, 3, 4, 5: passed (mid-hooks fired)
- ✗ Check 6: fresh Table had `node = 0x7FFD1184DC48` (kcdx dummynode)
- ✗ Check 7: 1× `frealloc.kcdx_image_ptr` with `is_dummynode=1`
- ✗ Check 8: HookedLoadGameWrapper started, never completed
- ✗ Check 9: crash zip `crash_2026-05-20_16-00-53.zip` produced
- N/A Check 10: tests didn't get to report (process died first)

After-fix expected baseline:
- ✓ all 10 checks.

The before/after delta on checks 6, 7, 8, 9 — especially
Check 7's zero-line count — is the unambiguous oracle.

## Why this is here and not in `design-gaps.md`

This is not a design choice we deferred — it's a delivery
blocker for Phase 5g (mid-hook write-support). It needs to
ship-fix before cap-04 can be re-enabled in the default test
suite, and before any plugin author can rely on `[[mid_hook]]`
with a Lua callback (which is the entire point of mid-hooks).

## Active diagnostic instrumentation

**FIX C is shipped + verified end-to-end (2026-05-20 16:19).**
Save-load works with cap-04 enabled; PROBE Q logged 0
`frealloc.kcdx_image_ptr` lines across the verification run.

Current code state:
- `vendor/lua/ltable.c::setnodevector`: patched (always
  allocates a real 1-Node array; never writes kcdx-dummynode
  into `t->node`). The patch carries a long explanatory
  comment + pointer to this doc. Re-applying upstream Lua
  pulls requires re-applying this patch.
- `vendor/lua/ltable.c::luaH_new`: patched to use `NULL` as
  the temporary `t->node` placeholder pre-`setnodevector`.
- All other dummynode references in ltable.c: untouched.
- `scripting.cpp` PROBE O snapshot pair: STILL IN PLACE
  in `dynamic_hook_mid` around the args-Table `lua_createtable`
  call. Pure read.
- `scripting.cpp` PROBE P hex dump pair: STILL IN PLACE
  in `dynamic_hook_mid` around the same site. Pure read,
  VirtualQuery-gated.
- `hooks.cpp` PROBE Q (`ArmFreallocProbe` + `HookedFrealloc`):
  STILL IN PLACE. **Permanent canary** — pass-through hook
  that only logs when `block ∈ kcdx.asi image`. Under FIX C
  this should never fire. If it DOES fire in a future run,
  some kcdx-side code path has reintroduced a sentinel write
  (e.g., a future static-const singleton, a new vendor/lua/
  patch that re-introduces a static pointer) and FIX C needs
  extension.
- cap-04 is RE-ENABLED in the install dir; FIX C makes
  save-load work normally with it on.

The PROBE Q canary should be kept in production indefinitely.
It's effectively a regression guard — if any future change
to `vendor/lua/` or kcdx source code introduces a new
kcdx-image-pointer-into-shared-GC scenario, PROBE Q will catch
it before users see a crash. Cost is one MinHook detour on the
hot `frealloc` path (~5 ns per allocation, ~0% overhead at the
allocations-per-frame rate we observe).

FIX A is planned as the structural follow-up. After FIX A
lands, `vendor/lua/` may be entirely removed from the build
(only the headers needed for struct definitions stay). PROBE Q
still acts as the canary; FIX A makes its zero-count guarantee
structural rather than mechanical.

Diagnostic helpers that remain (pure-read, safe in a shipping
build):

| File | What | Safe to keep? |
|------|------|---------------|
| `scripting.h/.cpp` | `LogLuaStateSnapshot(L, tag)` — emits `lstate.snapshot` line with L pointer, gettop, registry_ptr, globals_ptr, lua_status, OS tid | yes — pure read |
| `scripting.h/.cpp` | `LogLuaStateRawStruct(L, tag)` — emits `lstate.raw.L` / `lstate.raw.G` / `lstate.raw.sizes` with direct C-struct reads of lua_State + global_State + offsetofs | yes — pure read |
| `scripting.cpp::dynamic_hook_mid` | PROBE O: `LogLuaStateRawStruct` bracketing the corrupting `lua_createtable` call | yes — pure read |
| `scripting.cpp` | PROBE P helpers: `HexDump(p, n)` + `LogTableBytes(p, bytes, tag)` — VirtualQuery-gated 128-byte hex dump of any pointer, emits `lstate.raw.tbl`. Used to compare Table memory layouts (registry vs freshly-allocated). | yes — pure read, page-protection guarded |
| `hooks.cpp` | PROBE Q: `ArmFreallocProbe(L)` + `HookedFrealloc` — MinHook detour on `g->frealloc`. Logs every call where `block` falls inside kcdx.asi's image range, with `(ud, block, osize, nsize, caller_ra, is_dummynode)`. Pass-through; doesn't intervene. **Definitive canary** for the dual-Lua sentinel mechanism — should log 0 lines under the fix. | yes — pure pass-through, only emits when corruption pattern detected. Keep permanently. |
| `scripting.cpp::set_lua_state` | `LogLuaStateSnapshot` enter/exit | yes — pure read |
| `lua_bind.cpp::RegisterKcdxTable` | `LogLuaStateSnapshot` around each `lua_newtable` (KCDX uppercase + kcdx lowercase) | yes — pure read |
| `scripting.cpp::dynamic_hook_mid` | `fnv_jit` / `fnv_self` / `fnv_detour` fingerprint logging on enter/exit | yes — pure read |
| `runtime_func_t.{h,cpp}` | `fingerprint_self()` / `fingerprint_detour()` methods | yes — pure read |
| `save_load_hooks.cpp` | `DumpMidHookFingerprints("before-LoadGame_wrapper")` from `HookedLoadGameWrapper` | yes — pure read |
| `hook_engine.cpp` | `DumpMidHookFingerprints` impl | yes — pure read |

To re-arm the reproducer: cap-04 enabled in the install path
is the canonical reproducer (`kcdx.toml`, not `.disabled`).
Crash fires within ~5s of save-load selection — see the
15:36:40 crash zip and dmp for the canonical trace.

Probe output lands in `kcdx-engine/logs/kcdx-dev_<ts>.log` under
category `MID_HOOK`. Crash bundles in
`kcdx-engine/logs/crash/crash_<ts>.zip`.
