# cap-59-fires picked a one-shot VM-init target that already ran by plugin load

**Status:** REOPENED 2026-05-29 — the prior "resolved" close was wrong-by-omission. The retarget to `lua_pcall` exposed a real engine-side bug: any production-MinHook'd target (currently `lua_pcall` + `update`, hooked at boot via `src/hooks.cpp:775` + `:781` calling `MH_CreateHook` directly) is UN-HOOKABLE through the `kcdx.hook` surface because `hook_chain::Add`'s first-touch path also calls `MH_CreateHook` and MinHook returns `MH_ERROR_ALREADY_CREATED`. cap-59 is the first plugin to ever try this; the bug has been latent for the entire history of `kcdx.hook`.

## Symptom

`CAP-59-fires` row reported FAIL in the launch log at `2026-05-29 09:02-09:03`:

```
[09:03:09.260][INFO][engine][LEGACY] hook_chain: appended before 'lua_hook' (plugin 'cap_59_lua_hook_smart_resolver') to target 0x00007FFEEDD399AC (chain now 7)
[09:03:09.629][DEBUG][engine][TEST] REPORT name="CAP-59-fires" pass=false reason="the kcdx.hook.luaopen_math.before(fn) callback did NOT fire between plugin load and input_loaded — the install path wired no detour (handle=kcdx.handle<id=70 name=lua_hook status=applied>, :applied()=true, :reason()=nil). luaopen_math fires once during the lualib init wave well before input_loaded, so a missed fire here means the smart-resolver install never reached the dispatch path"
```

The smart-resolver install reported `:applied()==true` + `:reason()==nil`; the `hook_chain` log showed the entry correctly appended (chain length 7). The callback never fired between plugin load and `input_loaded`. Reframed: the test plugin's own backstop claimed the smart-resolver install was broken; in fact the install was correct and the target was wrong for what the test was trying to observe.

## Facts

- Smart-resolver install path executed correctly: `[REFDB] resolve_hit input_name="luaopen_math" kcdx_id=97 rva=9607596 kind="function" verification_state=verified` resolved + `hook_chain: appended before 'lua_hook' ... to target 0x00007FFEEDD399AC` confirms the chain append.
- The cap-59 install handle reported `:applied()==true` + `:reason()==nil` at the `input_loaded` backstop — install machinery is sound.
- Zero callback-fire events recorded at target `0x00007FFEEDD399AC` anywhere in the log between install (`09:03:09.260`) and the `input_loaded` backstop fire (`09:03:09.629`).
- The Lua state at `L=0x1E256273840` already existed at `09:03:02.690` — first observed in the unrelated `[MID_HOOK] lua_pcall.new_L_seen` probe — meaning the VM had been initialized before this timestamp.
- `luaopen_math` is the `math` library opener invoked from `luaL_openlibs`, which runs ONCE per Lua state during state initialization.
- cap-59's `plugin.lua` runs at `09:03:07.606`; the hook applies at `09:03:09.260`. Both events are 4-7 seconds AFTER the VM was already up and the `luaL_openlibs` library-init wave had completed.
- cap-33 / cap-34 / cap-35 also "hook" `luaopen_math` and their rows report PASS — but those rows assert `:applied()==true` at `kcdx.on("ready")`, not "the callback fired." The cap-35 plugin docstring explicitly frames this: *"assertions fire at kcdx.on('ready') after the apply pass"*. cap-59 is the FIRST row that asserts the callback actually fires.
- Switching the target name from `luaopen_math` to `lua_pcall` (kcdx_id=1, called continuously by every Lua-from-C dispatch) is INSUFFICIENT — the retarget surfaced the engine-side bug that `lua_pcall` is one of the engine's own production-MinHook'd targets (`src/hooks.cpp:775`), and `kcdx.hook` first-touch installs at the same VA hit `MH_ERROR_ALREADY_CREATED`.
- 6 engine sites install detours via direct `MH_CreateHook` today, bypassing `hook_chain`: `src/hooks.cpp:247` (frealloc canary), `:775` (lua_pcall), `:781` (update); `src/mod_absorb/ctor_bracket.cpp:365` (ModManager_ctor); `src/save_load_hooks.cpp:467` (SaveGame/LoadGame); `src/probes/bugsplat_ctor_probe.cpp:149` (BugSplat ctor). All un-hookable from the `kcdx.hook` author surface until migrated.
- `g_L` (`src/hook_chain.cpp:336`) is touched only at dispatch (`DispatchPre`/`DispatchPost`/`MidDispatch`), NOT at install. `hook_chain::Add` + `AddC` install paths (lines 2151–2238) are `g_L`-independent. Install can run pre-VM; dispatch is necessarily post-VM regardless of install timing.
- `update` is the SOLE bootstrap exception — `HookedUpdate` (`src/hooks.cpp:269`) calls `hook_chain::SetLuaState(L)` AND drives per-frame chain dispatch via `DispatchPre`. A chain dispatching through itself self-deadlocks. The `g_L`-at-install premise was a misread; `update`'s exception is dispatch-ordering, not install-timing.
- PROBE Z (`src/probes/bugsplat_ctor_probe.cpp::RunProbeZ`, launch `2026-05-29 10:13:10`): `runtime_func_t::make_jit_func("void", {}, kX64, &stub_pre, nullptr, MiniDmpSender_ctor)` returned `0x7FFF91990000` + dtor completed under loader lock in 1 ms, baseline matrix unchanged at `113/149`. Codegen + branch_pool `VirtualAlloc` + `runtime_func_t` dtor all loader-lock-safe. The bugsplat install (the only loader-lock site of the 6) can migrate to `AddC`.

## Trail

| Date | Action | Result |
|------|--------|--------|
| 2026-05-29 | Read the log directly for callback-fire events at target `0x00007FFEEDD399AC` | Zero fire events anywhere in the log; ground-truth observation. Eliminated "install path didn't wire the detour" hypothesis (install path was correct; the function was never called after install). |
| 2026-05-29 | Cross-referenced `luaopen_math` call-site lifetime against plugin-load timing | `luaopen_math` runs once per Lua state during `luaL_openlibs` at VM creation; the VM was up by `09:03:02.690`; cap-59's hook applied at `09:03:09.260` — 6.5 seconds after the only call site executed. Target-choice identified without a probe. |
| 2026-05-29 | Re-ran cap-59 after `lua_pcall` retarget; read `:reason()` field of the failed install handle | `:reason()="InstallRuntime failed: MH_CreateHook failed (MH_ERROR_ALREADY_CREATED) at 0x00007FFEEF36A5A4"` — retarget did not fix it; surfaced a new mechanism. Eliminated "smart-resolver install path is broken" and "target choice was the whole problem." |
| 2026-05-29 | Static-read `src/hooks.cpp:775` + `src/hook_chain.cpp:2151-2238` + `src/hook_engine.cpp:51-78` | Engine bootstrap calls `MH_CreateHook` directly at lua_pcall + update; `hook_chain::Add` first-touch path also calls `MH_CreateHook`; MinHook is single-owner-per-target → `MH_ERROR_ALREADY_CREATED` is inevitable for any plugin install at an engine-direct-hooked target. Mechanism identified; scope expanded to 6 sites total (frealloc canary, ModManager_ctor, save/load hooks, BugSplat ctor probe also direct-MH). |
| 2026-05-29 | Read `hook_chain.cpp` install path end-to-end to verify the `g_L`-at-install premise | Install path (lines 2151-2238) touches `g_L` nowhere; `g_L` is set by `SetLuaState` (line 2119) and read only at dispatch (lines 990/1135/1349). Killed the HYBRID-for-4 framing built on the false premise. Only `update` is a true bootstrap exception (self-referential dispatch pump). |
| 2026-05-29 | **PROBE Z** — at `kcdx::probes::bugsplat_ctor_probe::Install()` under loader lock, run `runtime_func_t::make_jit_func("void", {}, kX64, &stub_pre, nullptr, /*nearVa=*/MiniDmpSender_ctor)`; log entered / start / ret-or-faulted / dtor_ok | `make_jit_func_ret jit_ptr=0x7FFF91990000` + `runtime_func_dtor_ok` in 1 ms on `tid=26700`; baseline matrix unchanged at `113/149`. First row of outcome map fires: codegen + branch_pool VirtualAlloc + dtor all loader-lock-safe. ALL 5 non-`update` engine-direct sites can migrate to `hook_chain::AddC`. |
| 2026-05-29 | Post-migration launch (engine machinery + lua_pcall site migrated via `AddCEngine`); read log for cap-59 verdict | Engine machinery WIRED: `Hooks installed: lua_pcall (via hook_chain::AddCEngine) + update (direct MH — the documented bootstrap exception)`; `hook_chain: installed C before 'engine.lua_pcall' (plugin 'kcdx')`; `chain now 2` on cap-64 install (FindChain+APPEND path replaces MH_ERROR_ALREADY_CREATED). BUT cap-59's `plugin.lua` never ran; no `kInputLoaded` snapshot reached; no `First update tick with live lua_State` line. Mechanism UNCONFIRMED — three live possibilities (a)/(b)/(c) per AP10 self-check. Halted; designed PROBE α before any fix. |
| 2026-05-29 | **PROBE α** — instrument `HookedUpdate` first-tick latch (per-tick log of `tid`, `IsGameMainThread`, `g_L`, `done`) AND `HookedLuaPcall_Engine` chain callback body (per-fire log of `tid`, `IsGameMainThread`, `L`); both rate-limited; theory-INDEPENDENT; outcome map pre-committed. | **`PROBE_ALPHA lua_pcall_fire` count = 0** across the entire ~10-minute launch (chain C-Before callback body never invoked); `PROBE_ALPHA update_tick` count = 16,962 (HookedUpdate IS firing), every tick `g_L=0x0` and `is_game_main_thread=0`; no `First update tick with live lua_State` line. Pre-committed outcome map (b) re-shaped: it's NOT "lua_pcall fires off-thread" — it's "lua_pcall callback never fires AT ALL through chain dispatch, classifier blocks every fire pre-bootstrap." Mechanism: self-perpetuating dead-classifier chicken-and-egg in the chain dispatcher; classifier depends on a callback the classifier itself gates. Resolution per Reframe 2026-05-29c below. |

## Resolution (SUPERSEDED — see "Reframe 2026-05-29" below; the original retarget did not fix the FAIL)

The prior Resolution claimed the `luaopen_math → lua_pcall` retarget fixed CAP-59-fires. It did not. The retarget exposed a real engine-side bug whose mechanism is now identified by PROBE Z; the actual fix is the 5-site engine migration described in the post-reframe Resolution below.

- ~~**Root cause:** target choice — `luaopen_math` runs once at VM init, before plugin load. Inherited assumption from cap-35's docstring.~~ (Correct as a target-choice analysis; INSUFFICIENT as a Resolution — the retarget surfaced an underlying engine bug the original probe missed.)
- ~~**Fix:** retarget to `lua_pcall`.~~ (Insufficient — `lua_pcall` is one of the engine's own production-MinHook'd targets and is un-hookable via `kcdx.hook` until the engine migrates.)

## Hard rule / design implications (per the post-reframe Resolution below)

- `.claude/rules/hook-engine.md` — its "kcdx.hook + hook_chain is the only hook path authors use" line is currently aspirational. The engine's own production hooks bypass it. The migration restores the rule's literal promise; an updated paragraph documents the single `update` bootstrap exception.
- `.claude/rules/anti-patterns.md` AP4 — the engine has been silently violating its own AP4 for the entirety of `kcdx.hook`'s existence. The migration closes that.
- An author-visible behavior surfaces: `kcdx.hook` now works at lua_pcall, frealloc, ModManager_ctor, BugSplat ctor, SaveGame, LoadGame. Test-suite must add at least one row exercising each (rule: `test-suite.md`); the existing cap-59-fires row covers lua_pcall; new rows for the others land in the migration commit.

## Active diagnostic instrumentation

| Probe | Site | Question | Status |
|---|---|---|---|
| **PROBE Z** | `src/probes/bugsplat_ctor_probe.cpp::RunProbeZ` (called from `Install()` between `MH_Initialize` and `MH_CreateHook`) | Does `runtime_func_t::make_jit_func` (asmjit codegen + `kcdx::trampoline::AllocateBranch`) complete under the Windows loader lock at the bugsplat install site? | ANSWERED 2026-05-29 — YES (`make_jit_func` returned `0x7FFF91990000` + dtor completed under loader lock, full sequence within 1 ms). To be archived in-place via `#if 0` + 4-line header per CLAUDE.md when the migration commit lands. |

---

## Reframe 2026-05-29c (after PROBE α): post-migration dead-classifier regression — the actual Resolution

The post-PROBE-Z migration shipped engine machinery + the lua_pcall site as `hook_chain::AddCEngine`. Re-launch verification showed: engine machinery wired correctly (cap-47 breadcrumb confirms engine entry visible to chain inventory; `chain now 2` on cap-64's plugin install confirms FindChain+APPEND path replaces MH_ERROR_ALREADY_CREATED). BUT cap-59's `plugin.lua` never ran. `lua_plugin_loader::RunAll(L)` (`hooks.cpp:350`) was never reached because `HookedUpdate`'s first-tick latch never crossed `if (L)` because `hooks.cpp::g_L` stayed null for the entire ~10-minute launch.

PROBE α (theory-INDEPENDENT, outcome map pre-committed) confirmed the mechanism in source.

### Verified mechanism (AP17, three hops named)

`hook_chain::SetLuaState` captures `log::g_gameMainThreadId` on its first non-null-L call (via `log::SetGameMainThread`). Until that call runs, `log::IsGameMainThread()` reads `::GetCurrentThreadId() == g_gameMainThreadId` against an unset `g_gameMainThreadId` and returns false for every thread (this is the documented pre-bootstrap behavior, `src/log.h` doc comment line 179). The chain dispatcher gates per-entry callback invocation on `log::IsGameMainThread()` at THREE sites — DispatchPre (`hook_chain.cpp:1075-1080`), DispatchPost (`:1209-1213`), and MidDispatch (`:1341-1347`) — each taking the `OffThreadShouldSkip + continue` path before invoking the per-entry callback. Pre-`SetLuaState`, every fire on every thread is classified off-thread by all three gates.

The bootstrap loop the engine.lua_pcall migration introduced:

- **Hop 1**: the migrated chain C-Before callback `HookedLuaPcall_Engine` (`src/hooks.cpp:88-160`) writes `hooks.cpp::g_L.store(L, std::memory_order_release)` on every fire.
- **Hop 2**: `HookedUpdate` (`src/hooks.cpp:288-360`) — installed by direct `MH_CreateHook` per the documented bootstrap-pump exception in `.claude/rules/hook-engine.md`, deliberately NOT a chain entry — reads `g_L.load(std::memory_order_acquire)` in its first-tick latch (`hooks.cpp:332`) and on `g_L != null` calls `hook_chain::SetLuaState(L)` (`hooks.cpp:340`).
- **Hop 3**: `hook_chain::SetLuaState` calls `log::SetGameMainThread` which captures `log::g_gameMainThreadId`. After this, `log::IsGameMainThread()` starts returning true for the main thread; the chain's three off-thread gates start classifying correctly.

Pre-migration, hop 1 was a direct MinHook detour with no thread classifier in front, so the L-capture ran on every lua_pcall fire and bootstrap completed normally. Post-migration, hop 1 sits behind the chain dispatcher's pre-bootstrap-blocked classifier — every lua_pcall fire pre-bootstrap hits the `!onMainThread → OffThreadShouldSkip → continue` path in DispatchPre at line 1075 BEFORE the per-entry C-Before callback at line 1086, so hop 1 never runs. The loop is self-perpetuating: the classifier never bootstraps because the callback that bootstraps it (hop 1) is gated on the classifier; the latch never crosses `if (L)`; `RunAll(L)` never runs; cap-59's `plugin.lua` never executes.

### Observed evidence (PROBE α, launch 2026-05-29 15:27:47)

- `PROBE_ALPHA lua_pcall_fire` count = **0** across the entire ~10-minute launch (zero invocations of the migrated chain C-Before callback body).
- `PROBE_ALPHA update_tick` count = **16,962** (HookedUpdate IS firing); every tick `g_L=0x0` and `is_game_main_thread=0`. The `is_game_main_thread=0` reading is uninformative pre-bootstrap by construction — it ALSO returns 0 for the genuine main thread because the classifier never captured a `g_gameMainThreadId`. The probe does NOT support an "off-thread vs main-thread" diagnosis of which thread lua_pcall fires were happening on.
- No `First update tick with live lua_State` line (`hooks.cpp:337`) anywhere — the latch never crossed `if (L)`.
- No `kcdx::lua_plugin_loader::RunAll(L)` execution markers — cap-59's `plugin.lua` never reached.

### Resolution (post-PROBE-α)

- **Root cause:** the three-hop bootstrap loop above. Hop 1 (engine.lua_pcall chain C-Before callback) sits behind the chain dispatcher's pre-bootstrap-blocked main-thread classifier; the classifier is bootstrapped by hop 3 which depends on hop 2's latch crossing `if (L)` which depends on hop 1 having run. Self-perpetuating dead-classifier; PROBE α observed zero hop-1 invocations across 10 minutes of game time.
- **Fix:** the engine-stamped C-kind off-thread carve-out at all three chain dispatcher gate sites. Predicate uniformly applied: `e.isEngine && e.kind == ChainEntry::Kind::C` at DispatchPre (`hook_chain.cpp:1075`) + DispatchPost (`:1209`); `chain->isMidEngine && chain->midKind == ChainEntry::Kind::C` at MidDispatch (`:1341`). New `Chain::isMidEngine` field parallel to `ChainEntry::isEngine`; `AddCMid` consumes `TakeEngineStamp()` into it. AP6 (no Lua callback off-thread) does not apply to the carve-out because engine-stamped C entries are kcdx-internal C functions with no Lua callback in their dispatch path; the class is closed by construction (only `AddCEngine` produces an engine stamp; plugins can't claim it). Bypass is zero-cost: one-instruction predicate, no warn line, no map insert.
- **Verification:** post-fix launch confirms (a) `PROBE_ALPHA lua_pcall_fire` count > 0 (engine entry's callback runs); (b) `First update tick with live lua_State` line fires from `hooks.cpp:337` (hop 2's latch crosses); (c) classifier-bootstrapped regression row (new CAP-NN-engine-bootstrap-classifier) PASSes by asserting `log::g_gameMainThreadId != 0` post-`kcdx.on("ready")`; (d) cap-59-fires PASSes from first callback fire (one-shot guarded); (e) CAP-64-fires (C++ peer) PASSes from first callback fire.
- **Diagnostic archive:** PROBE α's finding + wiring (the `HookedUpdate` latch counter + the `HookedLuaPcall_Engine` fire counter) were captured to `_research/probe-archive/hooks-cpp-probes.md` and the probe removed from `src/hooks.cpp` (per the no-residue rule — `working-artifacts.md`). PROBE Z's finding + wiring were captured to `_research/probe-archive/bugsplat-probe-z.md` and removed from `src/probes/bugsplat_ctor_probe.cpp` (the file's live before_game-hook install machinery is unaffected — only its internal PROBE Z smoke test was extracted; verdict: codegen + branch_pool VirtualAlloc + dtor all loader-lock-safe; bugsplat site is migratable when its site lands).
- **Doc updates (same commit):** `.claude/rules/lua-callback-threading.md` § "Engine bootstrap carve-out" added — class rule (engine-stamped C-kind entries bypass; AP6 doesn't apply) + the three-hop loop named explicitly so the rule is self-contained. `.claude/rules/hook-engine.md` § "Engine-owned chain entries" gets a cross-reference to the carve-out rule (since the engine-stamp model is the same).

### What this taught about the discipline

- **AP10 caught after fix #1 failed.** The first post-migration verdict produced a plausible diagnosis ("off-thread filter drops L-capture") that was the right family but wrong specific mechanism. The user halted before any code edit and required a theory-INDEPENDENT probe (PROBE α) with outcome map pre-committed. The probe rejected the simple form of the theory (it would have shown lua_pcall firing off-thread) and surfaced the actual mechanism (zero lua_pcall fires AT ALL through the chain callback; classifier dead pre-bootstrap by construction). The fix shape from the simple theory would have been the same predicate (Option A in spirit — "let the engine entry through the off-thread skip"), but the *justification* would have been wrong, and a future regression of the actual chicken-and-egg under a different shape (e.g., a future engine entry not currently caught by the predicate) would have been mis-diagnosed because the recorded mechanism was wrong.
- **Resolution mechanism must name ALL hops in observable terms.** First draft of the AP17 paragraph collapsed hops 1-3 into "the callback triggers SetLuaState" — eliding the direct-MH `HookedUpdate` intermediary that's deliberately not a chain entry. Resolution rewritten to name all three hops explicitly + the verification path that exercises each.
- **Carve-out design through Gate A, not direct implementation.** The user halted the proposed implementation and required Gate A (architect-review) on the carve-out's design (predicate scope at three sites + rule paragraph scope + log policy). Architect surfaced three sub-questions; user picked B + C + A. One architect audit finding (`g_L` "split across two TUs with conflicting visibility models") was false on verification (both `g_L`s are TU-local in anonymous namespaces; they are two distinct symbols, not one ambiguously-linked); flagged + corrected before forwarding to user.

### Why the prior Resolution was wrong

The `lua_pcall` retarget reframed the bug onto a target that the engine's own production hook already owns. `src/hooks.cpp:775` calls `MinHook::MH_CreateHook` directly at boot (worker thread, well before any plugin loads); when cap-59's `kcdx.hook.lua_pcall.before(fn)` install reaches `hook_chain::Add`'s first-touch path, the chain calls `hook_engine::InstallRuntime` → `MH_CreateHook` AGAIN at the same target — MinHook returns `MH_ERROR_ALREADY_CREATED` and the install handle reports `:applied()=false` + `:reason()="InstallRuntime failed: MH_CreateHook failed (MH_ERROR_ALREADY_CREATED) at 0x00007FFEEDE5A5A4"`. Same FAIL row as the launch on `2026-05-29 10:13:23.956`. cap-59 is the first plugin to ever try to `kcdx.hook` a production-MinHook'd target — the bug has been latent for the entire history of `kcdx.hook`.

### Premise correction — `g_L` is dispatch-only, NOT install-gated

An earlier mistaken framing claimed "5 of 6 engine-direct sites can't migrate to `hook_chain::AddC` because they install before `hook_chain` has a `lua_State*`." That premise was wrong. Reading `src/hook_chain.cpp`:

- `g_L` (declared line 336) is set opportunistically by `SetLuaState` (line 2119: `if (L) g_L = L;`) and read ONLY at dispatch sites (`DispatchPre`/`DispatchPost`/`MidDispatch` around lines 990 / 1135 / 1349).
- The install path (`hook_chain::Add` / `AddC`, lines 2151–2238: `make_jit_func` + `hook_engine::InstallRuntime` + wiring `pOriginal` into the JIT call-original slot) touches `g_L` nowhere.
- Dispatch is necessarily post-VM regardless of how the detour was installed — when a callback fires, Lua is necessarily up.

**Install is `g_L`-independent. The only true bootstrap exception is `update` itself** — `HookedUpdate` (`src/hooks.cpp:269`) is the function that calls `hook_chain::SetLuaState(L)` AND drives per-frame chain dispatch via `DispatchPre`. A chain whose own per-frame dispatch is the hooked target is self-referential — chain dispatch can't dispatch through a chain entry whose dispatcher is itself. That's a true chicken-and-egg about dispatch ordering, NOT about `g_L` at install. `lua_pcall`, `ctor_bracket`, and `bugsplat_ctor` install pre-VM, but pre-VM blocks dispatch (post-VM regardless), not the MinHook/asmjit install — so they can migrate.

### PROBE Z — does asmjit codegen + `branch_pool` `VirtualAlloc` work under loader lock?

The one genuinely-checkable unknown blocking the pre-VM site migrations was whether `runtime_func_t::make_jit_func` (asmjit codegen + `kcdx::trampoline::AllocateBranch` `VirtualAlloc`) completes safely under the Windows loader lock at the bugsplat install site. The bugsplat probe runs from `kcdx.dll` `DllMain` (when BugSplat64.dll is already mapped) AND from the LDR-notification callback — both run under loader lock. This is the one site where loader-lock safety is a real runtime question (`lua_pcall` + `ctor_bracket` install from worker threads, no loader lock; `frealloc` + save/load install post-`SetLuaState`, no loader lock).

**Probe shape:** inside `kcdx::probes::bugsplat_ctor_probe::Install()`, between `MH_Initialize` and the existing `MH_CreateHook`, run a minimal `runtime_func_t::make_jit_func("void", {}, asmjit::Arch::kX64, &stub_pre, nullptr, /*nearVa=*/MiniDmpSender_ctor_addr)`. Heap-allocate `runtime_func_t` + `FuncSignature` (their dtors live OUTSIDE the SEH `__try` frame so `/EHsc`'s no-object-unwind requirement is met). Log ground truth at each step (`entered`, `start_make_jit_func`, `make_jit_func_ret` / `_returned_zero` / `_faulted`, `runtime_func_dtor_ok`). One-shot via `kProbeZRan` compare-exchange. Does NOT install the JIT detour — only exercises codegen + alloc. Real bugsplat ctor hook still installs via the existing `MH_CreateHook` (no behavior change to the bugsplat hook this run).

**Pre-committed outcome → meaning map (excerpt):**

| Log signature | Meaning | Per-site verdict |
|---|---|---|
| Full sequence completes; `make_jit_func_ret != 0`; baseline matrix unchanged | Codegen + branch_pool + dtor all loader-lock-safe | Migrate ALL non-`update` sites to `AddC`. NO adopt path needed. |
| `make_jit_func_faulted` / `_returned_zero` / boot freeze / boot crash | Codegen or alloc not loader-lock-safe | Bugsplat stays direct-MH + adopt path; lua_pcall + ctor_bracket re-evaluate (no loader lock at install). |
| Full sequence + baseline matrix regression | Probe perturbed something; not theory-clean | Redesign probe. |

### PROBE Z result (launch `2026-05-29 10:13:10`)

```
[10:13:10.572][DEBUG][engine][PROBE_Z] entered target=0x7FFF919AC914 note=loader-lock asmjit smoke test at bugsplat install site
[10:13:10.572][DEBUG][engine][PROBE_Z] start_make_jit_func nearVa=0x7FFF919AC914
[10:13:10.572][DEBUG][engine][PROBE_Z] make_jit_func_ret jit_ptr=0x7FFF91990000 verdict=asmjit codegen + branch_pool VirtualAlloc both completed under loader lock — pre-VM sites can migrate to hook_chain::AddC
[10:13:10.572][DEBUG][engine][PROBE_Z] runtime_func_dtor_ok note=rf dtor completed under loader lock
```

All four lines fired in a single millisecond on `tid=26700`. JIT pointer `0x7FFF91990000` sits in WHGame.dll's `branch_pool` window adjacent to the target `0x7FFF919AC914`. Dtor completed without hang. Baseline matrix `113/149` unchanged from the pre-probe run — the probe perturbed nothing. **First row of the outcome map fires unambiguously.**

### Per-site verdict (decided by the probe, not re-litigated)

| Site | Current install | Post-migration |
|---|---|---|
| `src/hooks.cpp:775` lua_pcall | direct `MH_CreateHook` from worker thread | `hook_chain::AddC` with engine-synthetic plugin identity, `HookedLuaPcall` registered as `before` |
| `src/hooks.cpp:781` update | direct `MH_CreateHook` from worker thread | **stays direct-MH** — the ONE bootstrap exception (self-referential dispatch pump: `HookedUpdate` calls `SetLuaState` AND drives chain dispatch; chain dispatching itself deadlocks) |
| `src/hooks.cpp:247` frealloc | direct `MH_CreateHook` from `HookedUpdate` post-`SetLuaState` | `hook_chain::AddC`, `HookedFrealloc` as `before` — PROBE Q's read-only `block ∈ kcdx.dll` fingerprint survives DispatchPre chain mediation (read-only check, not "be the only hook") |
| `src/mod_absorb/ctor_bracket.cpp:365` ModManager_ctor | direct `MH_CreateHook` from worker thread | `hook_chain::AddC` as `replace` (the bracket fully replaces the native ctor — `pOriginal` intentionally discarded per line 380) |
| `src/probes/bugsplat_ctor_probe.cpp:149` BugSplat ctor | direct `MH_CreateHook` under loader lock | `hook_chain::AddC` — PROBE Z confirms codegen + alloc loader-lock-safe |
| `src/save_load_hooks.cpp:467` SaveGame/LoadGame | direct `MH_CreateHook` from `phase6_install_after_lua_ready` post-`SetLuaState` | `hook_chain::AddC`, each as `before` (TC mod's natural extension point) |

### Resolution (post-reframe)

- **Root cause (mechanism):** the engine's own production hooks at `src/hooks.cpp:775` (`lua_pcall`) + `:781` (`update`) + `:247` (frealloc) + `src/mod_absorb/ctor_bracket.cpp:365` (`ModManager_ctor`) + `src/save_load_hooks.cpp:467` (SaveGame/LoadGame) + `src/probes/bugsplat_ctor_probe.cpp:149` (BugSplat ctor) install detours by calling `MinHook::MH_CreateHook` directly, bypassing the `hook_chain` machinery the rest of the engine and every plugin uses. MinHook records each target VA in its internal hooked-targets table. When a `kcdx.hook` author install at one of those targets reaches `hook_chain::Add`'s first-touch path (target absent from `g_chains`), the chain builds a JIT detour and calls `hook_engine::InstallRuntime` → `MH_CreateHook` at that same VA. MinHook returns `MH_ERROR_ALREADY_CREATED`; `InstallRuntime` returns failure; the install handle reports `:applied()=false` + `:reason()="InstallRuntime failed: MH_CreateHook failed (MH_ERROR_ALREADY_CREATED) at <addr>"`. No detour is wired into the chain; the author's callback never dispatches. The control-flow problem is in `hook_chain::Add` (chain-mediated install path) vs `hooks::Install` et al (direct-install path) racing for `MH_CreateHook` ownership at the same target — the engine populated MinHook's internal map first; the chain's first-touch path always loses the race because MinHook is single-owner-per-target. The data-flow problem is that `g_chains` has no entry for these targets — the chain's `FindChain(targetVa)` returns null on first author touch, falsely indicating "this target has never been hooked," when in fact it was hooked at boot through a sibling install path the chain knows nothing about. The original code path made this inevitable because the engine had no shared registry between direct-MH and chain-mediated installs — the two paths each maintain a private view of "which targets are hooked," and the chain's view is always wrong for the engine's own bootstrap hooks.
- **Fix:** to be applied — migrate the 5 non-`update` engine-direct `MH_CreateHook` sites to `hook_chain::AddC` (which routes through the same `g_chains` machinery every plugin install uses; the chain becomes the single registry). `update` keeps its direct `MH_CreateHook` install as the documented bootstrap exception (self-referential dispatch pump: `HookedUpdate` is what calls `hook_chain::SetLuaState` AND drives the chain's per-frame dispatch — making it a chain entry would self-deadlock). Each migrated site uses an engine-synthetic plugin identity (specifics — `pluginName`/`name`/`priority` triple — pending architect-review per Gate A). `.claude/rules/hook-engine.md` updated in the same commit to (a) replace the "`hook_chain` is the only hook path authors use" line with "`hook_chain` is the only install path; `update` is the single bootstrap exception, documented and named" and (b) document the engine-synthetic plugin identity convention so future engine sites follow it.
- **Verification:** post-fix launch shows cap-59-fires PASS (the `lua_pcall` migration eliminates the `MH_ERROR_ALREADY_CREATED` at cap-59's install site — the chain finds the engine's chain entry on `FindChain(targetVa)`, takes the append path, and cap-59's `before` callback dispatches from the chain). New test-plugins rows under `test-plugins/cap-NN-engine-direct-coexist-<site>/` exercise each migrated site to lock the contract (one row per: frealloc, ctor_bracket, bugsplat_ctor, SaveGame, LoadGame) — pending sign-off on the test-row IDs.
- **Diagnostic archive:** PROBE Z to be archived in `src/probes/bugsplat_ctor_probe.cpp` via `#if 0` block + 4-line header:
  ```
  // === ARCHIVED PROBE Z (2026-05-29): VERIFIED — asmjit codegen + branch_pool
  // VirtualAlloc + runtime_func_t dtor all complete under loader lock at the
  // bugsplat install site. Root cause: engine direct-MH sites bypass hook_chain
  // → MinHook returns MH_ERROR_ALREADY_CREATED on plugin install at same
  // target. See: docs/known-issues/cap-59-fires picked a one-shot VM-init
  // target that already ran by plugin load.md §Resolution (post-reframe).
  // Revive by flipping #if 0 → #if 1 if a future loader-lock asmjit/alloc
  // regression is suspected.
  ```
- **Doc updates:** `docs/cpp/hook.md` + `docs/lua/hook.md` add a paragraph noting that engine-internal targets (`lua_pcall`, `update`, frealloc canary, ModManager ctor, BugSplat ctor, SaveGame, LoadGame) are now first-class `kcdx.hook` targets; `update` is called out as the single bootstrap exception (un-hookable from author surface, documented constraint). `test-plugins/README.md` matrix gains the new rows. `.claude/rules/hook-engine.md` § "Conflict engine ownership" + § "`kcdx.hook` chaining" updated with the corrected install-vs-dispatch separation.

### What this taught about the discipline

Two process facts surfaced:

1. **The first close was wrong because it didn't verify the fix.** "Retarget to `lua_pcall`" was a hypothesis; the verification step (re-run the test, observe the callback fires) was skipped because the change "obviously" addressed the target-choice problem. The launch on `2026-05-29 10:13` is the verification that should have run before the first close — and it would have surfaced the engine bug then. Lesson: a fix's repro-passes verification IS part of closing; AP17's "passing repro indistinguishable from masking" applies — but ALSO, an *unrun* repro is indistinguishable from anything. Run the verification.
2. **`g_L`-at-install premise was a misread, not a probe.** I almost dispatched a HYBRID-for-4 fix shape built on a false architectural claim because I read `hook_chain.cpp` once for the install path and once for `g_L` and conflated them. The corrective: when the framing is "this is settled by architecture, no probe needed," sanity-check by reading the code paths involved end-to-end before committing to the framing. The actual probe (PROBE Z) was on a much narrower question once the premise was corrected.

---

## Reframe 2026-05-29a (mid-day): the retarget exposed a real engine bug — the prior Resolution was wrong-by-omission

> A second reframe ("Reframe 2026-05-29b") below adds PROBE Z's verdict + the corrected `g_L`-at-install premise and supersedes the open-questions/design-fork section here. The two reframes are kept in order for the investigation trail.

The cap-59-fires retarget from `luaopen_math` to `lua_pcall` (commit `e5c8d7a`) was VERIFIED in a fresh launch and still FAILed with new evidence: the install handle reports `:reason()=InstallRuntime failed: MH_CreateHook failed (MH_ERROR_ALREADY_CREATED) at 0x00007FFEEF36A5A4`. The retarget surfaced a real engine-side bug the prior Resolution masked.

### New facts (PROBE-FREE — static evidence only)

- cap-59's `lua_pcall` install handle: `:applied()=false`, `:reason()="InstallRuntime failed: MH_CreateHook failed (MH_ERROR_ALREADY_CREATED) at 0x00007FFEEF36A5A4"` (kcdx-dev_2026-05-29_09-28-35.log).
- Engine boot line at `09:28:35.493`: `Hooks installed: lua_pcall + update`. The engine's production lua_pcall hook is installed at boot via `src/hooks.cpp:775` calling `MH_CreateHook` **directly** (not through `hook_chain`).
- `src/hooks.cpp:775-783` calls `MH_CreateHook` directly for both `lua_pcall` and `update`. No `hook_chain::AddC` registration; the chain at these targets does not exist in `g_chains` per `src/hook_chain.cpp:322`.
- `src/hook_chain.cpp:2151-2218`: `hook_chain::Add` first looks up `FindChain(targetVa)` (line 2153). If a chain exists, the new entry APPENDS (no MinHook touch). On first-touch (line 2197), it calls `hook_engine::InstallRuntime` (line 2214) which calls `MH_CreateHook` (hook_engine.cpp:66).
- `MH_CreateHook` is REJECTED with `MH_ERROR_ALREADY_CREATED` when called a second time at a target MinHook has already hooked. MinHook's internal state remembers the engine's direct `MH_CreateHook` call from `hooks.cpp:775`, so cap-59's `hook_chain::Add` first-touch path fails.
- The prior Resolution claimed "smart-resolver install path is verified correct by the same evidence that closed the FAIL" — but the cap-35 PASS rows that "verified" it asserted `:applied()==true` at `kcdx.on("ready")`, not callback fire. cap-35 hooks at the same target (`0xEDD399AC` luaopen_math) ALSO never fired their callbacks (luaopen_math runs once at VM init, before plugin load); the cap-35 install just never reached the same MH_ERROR situation because nothing had production-hooked luaopen_math directly. The cap-35 PASS evidence was about install-machinery applied-state, not about dispatch.
- Zero other `kcdx.hook` test plugins target `lua_pcall` today (grep `test-plugins/`). cap-04 + cap-21's PASS rows use `kcdx.code`-allocated targets (fresh memory NOTHING else hooks), not `lua_pcall`. The "engine SUPPORTS multiple hooks at the same target" claim from my first re-entry brief was correct for `hook_chain`-mediated targets but DOESN'T apply when one of the hooks bypassed `hook_chain` to call `MH_CreateHook` directly.
- The bug affects ALL production-MinHook'd targets, not just lua_pcall. Other direct-MH_CreateHook call sites: `src/hooks.cpp:247` (frealloc canary), `src/mod_absorb/ctor_bracket.cpp:365` (ModManager ctor), `src/probes/bugsplat_ctor_probe.cpp:149` (bugsplat ctor probe), `src/save_load_hooks.cpp:467` (save/load hook). Any of those are un-hookable from the kcdx.hook surface today.

### Updated mechanism (overwrites the prior Resolution's Root cause paragraph)

The engine's production `lua_pcall` hook at `src/hooks.cpp:775` installs by calling `MinHook::MH_CreateHook` directly with no `hook_chain` registration. MinHook records the target VA in its internal hooked-targets table. The `kcdx.hook` author surface dispatches every install through `hook_chain::Add` (`src/hook_chain.cpp:2151`), which on first-touch (no prior `g_chains` entry at that target) builds a new chain detour and calls `hook_engine::InstallRuntime` → `MH_CreateHook`. MinHook returns `MH_ERROR_ALREADY_CREATED` because it already has a hook at that target from the engine's direct call. `hook_chain::Add` returns an install error; the kcdx.hook handle reports `:applied()=false` + `:reason()="InstallRuntime failed: MH_CreateHook failed (MH_ERROR_ALREADY_CREATED) at <addr>"`. No author callback wires; no dispatch happens. The bug is that the engine's own production hooks bypass the `hook_chain` registration the rest of the engine expects all hooks to go through — making those targets un-hookable from the author surface that's supposed to be the only path authors use (per `hook-engine.md` "the only hook path authors use; it supersedes first-wins"). cap-59 is the first plugin to ever try to kcdx.hook a production-MinHook'd target; the bug has been latent for the entire history of `kcdx.hook`.

### Open questions / design fork

Two engine-side fix shapes. Both touch `src/hooks.cpp` (and possibly `src/hook_engine.cpp` / `src/hook_chain.cpp`); both are real engine-design forks that need architect-review before proposing to the user.

**Option A — Migrate the engine's production direct-MH_CreateHook sites to register through `hook_chain::AddC`** so subsequent kcdx.hook plugin calls find a chain at the target and APPEND. Affects `src/hooks.cpp:775` (lua_pcall) + `:781` (update); possibly also `src/hooks.cpp:247` (frealloc canary), `src/mod_absorb/ctor_bracket.cpp:365`, `src/probes/bugsplat_ctor_probe.cpp:149`, `src/save_load_hooks.cpp:467`. The engine's hooks become the first entries in the chain; the chain's dispatch machinery calls them. Requires the engine's hook callbacks (`HookedLuaPcall`, `HookedUpdate`, etc.) to fit the `hook_chain::AddC` ABI contract (`hook_chain.cpp:251` C-callback path; see `hook_interface.cpp:17` for the existing `AddC` consumer pattern).

**Option B — Teach `hook_engine::InstallRuntime` (or `hook_chain::Add`'s first-touch path) to detect `MH_ERROR_ALREADY_CREATED` and adopt the existing MinHook detour into a fresh `hook_chain` chain entry** so subsequent appends work. Requires recovering MinHook's `pOriginal` pointer for the pre-existing hook (the engine retained `g_orig_lua_pcall` at `hooks.cpp:777`; if the adopt path can read that out, the chain can wire it as the call-original slot). Engine-direct hooks stay direct; the chain machinery learns to coexist with them.

Both fixes touch the engine's hook surface and a `.claude/rules/hook-engine.md` clarification is owed in the same commit (the rule's "kcdx.hook + hook_chain is the only hook path authors use" line is currently aspirational — the bug is the gap between the rule's promise and what the engine does). Architect-review owes the fork verdict before any code lands.
