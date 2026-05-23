# kcdx test suite

This folder is the kcdx regression test suite. Each subfolder is a
**plugin that's permanently installed in the developer's game**.
On every boot, dev mode gates whether the suite runs; when it
does, each test plugin self-checks and reports pass/fail to kcdx,
which aggregates results into kcdx.log.

**Normal workflow going forward:** every time we boot the game,
the suite tells us how many tests are passing. Before committing
anything that touches `src/` or `include/`, we re-run (i.e. launch
the game with dev mode on, read the summary), and the matrix
roll-up below records the state at the commit SHA we're about to
land. Green-everywhere is the baseline.

## How it works

Each test plugin opts into being suite-gated with one TOML key:

```toml
[kcdx]
test_suite_only = true
```

- **Dev mode off** (production / end-user installs): kcdx silently
  skips every entry in the file. C++ DLLs early-return at
  `Plugin_Load` (also silently). Pak-Lua test scripts check
  `kcdx.dev.is_enabled()` and return. Zero log noise, zero
  behavioral change — production users never see the suite in
  their logs.
- **Dev mode on** (developer creates `<kcdx-engine>/engine.toml`
  with `dev_mode = true`): every test runs its check, calls
  `ReportTestResult(...)` (C++) or `kcdx.test.report(...)` (Lua),
  and the aggregator emits `Test suite: X/Y passing` to kcdx.log
  on each engine lifecycle message.

The gating mechanic + reporting API are documented in
[`docs/dev-mode.md`](../docs/dev-mode.md).

**Some tests can't auto-run on boot** — e.g., "open inventory in
combat and try to swap outfit" requires the developer to perform
an in-game gesture. Those rows are flagged `[manual]` and the
test plugin still reports `PASS` if its passive checks succeed
(plugin loaded, patch applied, hook installed) — the gesture is
how the developer confirms the user-facing effect.

## Folder layout

```
test-plugins/
  README.md                <- this doc, the matrix
  cap-01-patch/            <- one plugin per CAP/COMP row
    kcdx.toml
    (optional .cpp + CMakeLists.txt if it ships a DLL)
    (optional pak/ subdir if it ships a pak)
  cap-05-paklua-runtime/
    kcdx.toml
    pak/
      Data/cap_05.pak
      mod.manifest
  comp-01-two-patch-overlap/
    kcdx.toml
  ...
```

Folder naming convention: `<row-id>-<short-name>/`, lowercase,
dashes between words. The row ID (CAP-XX or COMP-XX) matches the
matrix below; the short-name is the primitive being tested.

## Installation

To enable the suite on a dev machine:

1. Build kcdx (`pwsh ./build.ps1`) and install `kcdx.asi` to
   `<game>/Bin/Win64MasterMasterSteamPGO/plugins/`.
2. Create `<game>/Bin/Win64MasterMasterSteamPGO/kcdx-engine/engine.toml`
   (the `kcdx-engine/` folder is auto-created on first kcdx launch, or
   you can `mkdir` it yourself) with at minimum:

   ```toml
   [kcdx]
   dev_mode = true
   ```

   This is the engine-config file (lives next to `kcdx.asi`). It's
   what turns on dev mode and unlocks the test suite. See
   `kcdx/docs/dev-mode.md` for the full schema.
3. For each test plugin under `test-plugins/<folder>/`: copy the
   folder to the same `plugins/` directory (or the pak to
   `<game>/mods/`). The suite is intended to be "drop all of
   these in, leave them there forever."

Once installed, every game boot writes a fresh `Test suite: X/Y
passing` line to kcdx.log. If a number changes, something
regressed.

---

## Site under test (where most byte-level tests land)

| What | Value |
|---|---|
| Module | `WHGame.dll` |
| AOB (Tier 1) | `48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0` |
| Offset of bytes | 13 |
| Original bytes | `44 8A F0` (`mov r14b, al`) |
| Patched bytes | `45 31 F6` (`xor r14d, r14d`) |
| Game build | release_1_5_1164953_841 |
| Effect | `IsInCombat()` result is replaced with 0 in the gating register that decides whether the `next_outfit` action binding is enabled |
| Manual in-game test | Enter combat (draw weapon, attack NPC, take a hit). Try to open inventory and change outfit. Vanilla = popup "You can't switch outfits in combat." Patched = swap succeeds, popup never appears. |

This site is convenient because it's a well-characterized byte
patch with a clear in-game observable. Other tests target other
sites; their site info lives in the row itself.

---

# Section 1: Capability rows

For each kcdx primitive: what it does, where the test lives,
what the live result is.

## CAP-01: `[[patch]]` byte rewrite (TOML, declarative)

| Field | Value |
|---|---|
| What | Replace N bytes at a resolved address with N other bytes. Locator pipeline (pattern + context + anchor_string) handles AOB drift. |
| Channels | (iii) `kcdx.toml`, (iv) `inlinePatchesToml` |
| Engine status | READY (Phase 1) |
| Test plugin | [`cap-01-patch/`](cap-01-patch/) |
| Site | The outfit AOB above |
| Auto-pass check | Plugin reports PASS when patch applies cleanly (kcdx.log line `applied successfully at 0x... 44 8A F0 -> 45 31 F6`). The DLL companion verifies post-apply bytes match. |
| Manual confirm | In-game outfit-swap-in-combat works. [manual] |
| Last result | ✅ PASS (kcdx@918d5fb 2026-05-19, manual confirmed) |
| Notes | Mempatch-compatible. The reference for every other primitive. |

## CAP-02: `[[hook]]` + `bytes` (TOML, native trampoline)

| Field | Value |
|---|---|
| What | Install MinHook detour at resolved address. Detour code is raw bytes provided in TOML. Original instructions get relocated into MinHook's trampoline. |
| Channels | (iii) `kcdx.toml` |
| Engine status | READY (Phase 4) |
| Test plugin | _TBD — need a target where we can write a meaningful detour without crashing_ |
| Auto-pass check | Hook installs (kcdx-dev.log DYNAMIC_HOOK/install-ok), detour bytes verifiable. |
| Last result | _TBD_ |
| Notes | Bytes can be any 5+ byte sequence that does something useful (set a register, jump elsewhere). Hardest to test because we'd need to actually write working detour shellcode for an interesting target. |

## CAP-03: `[[hook]]` + `lua_callback` (TOML, dispatch to Lua)

| Field | Value |
|---|---|
| What | Hook a function; on entry the dispatch shim calls a named Lua function that decides whether to let the original run (`return true`) or skip it. |
| Channels | (iii) `kcdx.toml` + a pak-Lua-side function registration |
| Engine status | READY (Phase 5f) |
| Test plugin | _TBD — port `examples/phase5f-lua-callback-test/` to `test-plugins/cap-03-hook-lua-callback/`_ |
| Auto-pass check | SHIM/enter fires expected number of times during boot. |
| Last result | ✅ PRE-VERIFIED on existing examples plugin (needs port to test-plugins/ for auto-reporting) |
| Notes | Outfit-swap NOT a good fit (one-shot init), use the existing phase5f-lua-callback-test pattern. |

## CAP-04: `[[mid_hook]]` register capture + Lua override

| Field | Value |
|---|---|
| What | Hook at an arbitrary instruction inside a function (not just entry). Capture named registers (rax, r14, etc) before the instruction, pass them to a Lua callback as a table. `call_original` knob controls whether the captured instruction runs after the callback: `true` (default, original runs), `false` (compile-time skip — original NEVER runs), or `"auto"` (runtime decision via `args._skip = true` from Lua). |
| Channels | (iii) `kcdx.toml` |
| Engine status | LIVE (2026-05-20, commit `03dd155`) — three-mode codegen in `make_jit_midfunc` + hde64 auto-decode of `stack_restore_offset` + `g_mid_skip_original` atomic flag interlock between dispatcher and JIT. |
| Test plugin | `cap-04-midhook/` — 4 sub-tests covering all three modes (a=true, b=false, c=auto+_skip, d=auto+no-skip). Each fires against a self-contained 9-byte trampoline target with `add rax, 0x64` at the hook site; sub-test invokes the target with seed=10 and asserts the expected return value. |
| Auto-pass check | Three of the four sub-tests pass when register state matches the `call_original` contract: CAP-04a returns 110 (original ran, +100), CAP-04b returns 10 (skipped), CAP-04d returns 110 (auto without skip = run). CAP-04c is the one known FAIL: it returns 110, not the expected 10 — the legacy `args._skip` auto-skip path does NOT skip the original. This is the standing pre-existing red; the new `kcdx.hook` mode=mid does NOT inherit it (CAP-21-skip PASSES). |
| Last result | LIVE 2026-05-20 — CAP-04a/b/c/d all PASS in the 18:32 dev-log run. |
| Notes | Mid-hook register MUTATION via `args[1]:set(...)` is still v0.1-deferred (kcdxLuaApi lacks Call/Pcall — see `docs/design-gaps.md` #11). CAP-04 verifies the harder problem (skip-original codegen); register mutation lands when `kcdxLuaApi` gets `Call`. |

## CAP-05: Runtime `dynamic_hook` from pak Lua

| Field | Value |
|---|---|
| What | Pak Lua script calls `kcdx.memory.dynamic_hook({ target=..., pre_callback=..., ... })` to install a hook at runtime. Same MinHook + JIT-detour plumbing as `[[hook]]` but driven from Lua at script-load time instead of TOML at engine-init time. |
| Channels | (i) pure pak mod, (vi) plugin Lua |
| Engine status | READY (Phase 5c.7b proved end-to-end in the verify pak — `phase5g_greet_intercept` fired 5/5 at exact shim VA) |
| Test plugin | _TBD `cap-05-paklua-runtime/` — pak mod that installs a hook and calls `kcdx.test.report` from the pre_callback_ |
| Auto-pass check | Hook installed + callback fired ≥ 1 time during boot. |
| Last result | _TBD_ |
| Notes | This is the novel kcdx capability — Workshop-distributable code injection. Before kcdx, pak Lua had no FFI (`package.loadlib` is CryEngine-compiled-out stub). After kcdx, a pak mod can install function detours. |

## CAP-06: Runtime `dynamic_call` from pak Lua (call game function)

| Field | Value |
|---|---|
| What | Pak Lua script calls `kcdx.memory.dynamic_call({ target=..., return_type=..., param_types=... })` to get a callable userdata that invokes a native game function with marshaled args/return. |
| Channels | (i), (vi) |
| Engine status | READY (Phase 5c.7c) |
| Test plugin | _TBD — pick a known-safe game function (e.g., something that just returns a constant) and call it_ |
| Auto-pass check | Callable userdata invokes the target and returns the expected value. |
| Last result | _TBD partial — verify pak proved the shape works, real target untested_ |
| Notes | Counterpart to CAP-05. Together they let pak Lua do bidirectional native interop — read game state via dynamic_call, modify it via dynamic_hook. |

## CAP-07: `[[trampoline]]` allocation (branch / local pool)

| Field | Value |
|---|---|
| What | Reserve executable memory within ±2GB of WHGame.dll (branch pool, for 5-byte rel32 reachable detours) or anywhere (local pool, for general JIT). Used internally by `[[hook]]` and `dynamic_hook`. Exposed to C++ plugins via `kcdxTrampolineInterface`. |
| Channels | (ii) C++ DLL, indirectly (i) via dynamic_hook, (iii) via `[[hook]]` |
| Engine status | READY (Phase 4) |
| Test plugin | _TBD — port `examples/hello-plugin/`'s pool-alloc check to a test plugin_ |
| Auto-pass check | Branch-pool alloc returns address in rel32 range from WHGame.dll base. Local-pool alloc returns any executable address. |
| Last result | ✅ PRE-VERIFIED on hello-plugin |
| Notes | Foundational, used by everything that installs detours. |

## CAP-08: `kcdxMessagingInterface` (engine lifecycle messages)

| Field | Value |
|---|---|
| What | Subscribe to engine events: `kPostLoad`, `kPostPostLoad`, `kInputLoaded`, `kNewGame`, `kPreLoadGame`, `kPostLoadGame`, `kSaveGame`, `kDeleteGame`. Plugin-to-plugin dispatch also supported. |
| Channels | (ii) C++ DLL |
| Engine status | READY (Phase 3 for the events that exist; PreLoadGame/PostLoadGame/SaveGame/DeleteGame DEFERRED — Phase 6 save-hook required) |
| Test plugin | _TBD — subscribe to all available message types, assert each fires at least once during boot_ |
| Auto-pass check | Each subscribed message received during the session. |
| Last result | ✅ PARTIAL (PostLoad/PostPostLoad/InputLoaded confirmed; game-lifecycle messages awaiting Phase 6) |
| Notes | _ |

## CAP-09: `kcdxTaskInterface` (queue work for main thread)

| Field | Value |
|---|---|
| What | `AddTask(task)` queues a callback for next `update` tick on the main thread. Used so worker-thread plugins can safely touch CryEngine state. |
| Channels | (ii) C++ DLL |
| Engine status | READY (Phase 3) |
| Test plugin | _TBD — enqueue a task from worker thread, assert it runs on main thread within N ticks_ |
| Auto-pass check | Task callback fires, thread ID matches kcdx's stashed main-thread ID. |
| Last result | ✅ PRE-VERIFIED on hello-plugin |
| Notes | _ |

## CAP-10: `kcdxScriptingInterface` — C++ exposes Lua functions

| Field | Value |
|---|---|
| What | `RegisterFunction(handle, table, name, fn, userdata)` makes a C++ function callable from pak Lua as `kcdx.<table>.<name>(...)`. Uses the kcdxLuaApi function-pointer struct for the C++ side's Lua C API access (no direct lua.h include). |
| Channels | (ii) C++ DLL + (i)/(vi) on the calling side |
| Engine status | READY (Phase 5e) |
| Test plugin | _TBD — register a C function from a test DLL, call it from a paired test pak Lua, assert round-trip works_ |
| Auto-pass check | Pak Lua sees the registered function as callable; result matches C++ implementation. |
| Last result | ✅ PRE-VERIFIED on hello-plugin + verify pak |
| Notes | Core capability for "new game systems" mods — magic, perks, custom inventory, etc. all use this surface. |

## CAP-11: `kcdx.lua.cfunction_address` (resolve C address of a Lua-callable)

| Field | Value |
|---|---|
| What | Pak Lua passes a function (Lua-side) and gets back a `kcdx.memory.pointer` userdata holding the C function pointer (if any). Returns nil + error for pure-Lua functions. |
| Channels | (i), (vi) |
| Engine status | READY (Phase 5c.7d post-LUA_NUMBER fix) |
| Test plugin | _TBD — pak script calls cfunction_address on a known cfunction, asserts pointer userdata with non-zero VA returned_ |
| Auto-pass check | Returns pointer userdata for cfunctions, nil for pure-Lua. Userdata is usable as `dynamic_hook.target`. |
| Last result | ✅ PRE-VERIFIED (verify pak 5gDEMO) |
| Notes | The "find any registered Lua C function's address so I can hook it" primitive. |

## CAP-12: `kcdxSerializationInterface` (save/load co-save)

| Field | Value |
|---|---|
| What | Plugin registers `SetSaveCallback`/`SetLoadCallback`/`SetRevertCallback`. On save, kcdx fires SaveCallback; plugin writes records via `OpenRecord` + `WriteRecordData`. Stored in a `.kcdx` sidecar file alongside the save. On load, LoadCallback fires, plugin walks records via `GetNextRecordInfo` + `ReadRecordData`. |
| Channels | (ii) C++ DLL |
| Engine status | READY (Phase 6b, shipped 2026-05-19) |
| Test plugin | `cap-12-serialization/` (C++ DLL) |
| Auto-pass check | Plugin writes counter on save, reads it on load, value persists across game restarts. Manual sequence: load any save → quicksave → quit → relaunch → load the quicksave. Pass = `kPostLoadGame` reports "Load round-trip: read counter=N from cosave" with N matching the number of OnSave calls before quit. |
| Last result | LIVE 2026-05-19 — 15/15 suite passing after roundtrip; 44-byte cosave at `<saves>/<basename>.kcdx` decoded as XCDX magic + uid + chunk + counter (u64). |
| Notes | Essential for any mod that has persistent state per save (perks added by mod, custom inventory, magic-spell-known list, etc). |

## CAP-13: `[[command]]` console commands

| Field | Value |
|---|---|
| What | Register a console command callable from KCD2's `~` in-game console (and any other dispatch path that goes through `IConsole::ExecuteString`). v0.1 ships the C++ DLL path via `kcdxConsoleInterface::{RegisterCommand, ExecuteString, GetArgCount, GetArg, GetCommandLine}`; the TOML `[[command]]` schema lands in Phase 7b once a plugin needs declarative registration. |
| Channels | (ii) C++ DLL |
| Engine status | READY (Phase 7, shipped 2026-05-20). |
| Test plugin | `cap-13-console-command/` (C++ DLL) |
| Auto-pass check | At `kInputLoaded`: (1) registers `kcdx_test_cap13` via `RegisterCommand`, (2) self-fires `ExecuteString("kcdx_test_cap13 hello world")` synchronously, (3) the callback (running inside ExecuteString) captures `argc` + each `GetArg(i)` into globals, (4) after ExecuteString returns, the plugin verifies `argc==3 && arg[0]=="kcdx_test_cap13" && arg[1]=="hello" && arg[2]=="world"`. PASS only if every check holds. Any future regression in the Phase 7 dispatch chain (Address Library ids 2000/2001/2002, the vtable[33] AddCommand semantic, the IConsoleCmdArgs vtable layout, or the kcdxConsoleInterface ABI) fails this test on the next game launch — no manual gesture required. |
| Last result | LIVE 2026-05-20 — `register+ExecuteString+callback roundtrip ok (argc=3, command_line='kcdx_test_cap13 hello world')`. |
| Notes | Critical for dev workflows and for cheat/debug mods. The plugin-facing callback signature is `void(const kcdxConsoleCmdArgs*)`; the kcdx engine wraps CryEngine's `IConsoleCmdArgs` vtable so plugins don't depend on it directly. The vtable[32] vs vtable[33] swap discovered by the Phase 7 DISPATCH-INVESTIGATION is the only reason this test exists — without it, future builds could silently regress into the script-string overload and only manifest as "command doesn't fire from `~` console" with no other signal. |

## CAP-14: Address Library (`kcdxInterface::ResolveAddress`)

| Field | Value |
|---|---|
| What | Plugin calls `api->ResolveAddress(uint64_t id)` to get a runtime VA. The id-to-RVA mapping ships compiled into `kcdx.dll` at build time from `data/address-library/seed.csv`. Per-game-version entries gate resolution, so the same id keeps working across KCD2 patches (each patch adds new rows for its build identifier; rows for older builds stay around for compatibility-mode plugins). |
| Channels | (ii) C++ DLL, (iii) `kcdx.toml` via `address_id = N` (peer to `pattern` / `target_symbol` in `[[patch]]` / `[[hook]]` / `[[mid_hook]]`) |
| Engine status | READY (Phase 7, shipped 2026-05-20). |
| Test plugin | DEFERRED — covered indirectly by the [[command]] surface (CAP-13 exercises ids 1009 + 2000 + 2001 transitively). A dedicated CAP-14 plugin would only add a `ResolveAddress(1000)` sanity check; not blocking. |
| Auto-pass check | None as of v0.1; relies on CAP-13's transitive verification. |
| Last result | LIVE 2026-05-20 (transitively, via CAP-13). |
| Notes | The SKSE-equivalent that lets plugins survive KCD2 patches without re-doing AOB scans. Seed contains 12 verified rows (1000–1011 + 2000–2003) and 6 vtable-index constants (3000–3005) reserved for future `[[vtable_hook]]`. Authors add new ids by editing the CSV + the in-source mirror at `src/address_library.cpp::kEntries[]`. |

## CAP-15: `inlinePatchesToml` (C++ plugin ships patches inline)

| Field | Value |
|---|---|
| What | `kcdxPluginVersionData::inlinePatchesToml` field holds a TOML string parsed by the loader BEFORE `kcdxPlugin_Load` fires. Plugin gets to ship its byte rewrites alongside its DLL without a sidecar `kcdx.toml`. |
| Channels | (iv) |
| Engine status | _TBD — check whether the loader currently parses this field_ |
| Test plugin | _TBD — build a DLL with the outfit-swap patch in its inlinePatchesToml_ |
| Auto-pass check | Patch applies from the DLL's inline TOML (no sidecar TOML), post-apply bytes verifiable. |
| Last result | _TBD_ |
| Notes | If READY, this is the cleanest way for C++ plugins to ship "I need this byte to change for my code to work" without a parallel TOML. |

## CAP-16: Plugin dependencies + topo-sort (`dependencies` array)

| Field | Value |
|---|---|
| What | `kcdxPluginVersionData::dependencies` array names other plugins this one depends on, with min-version constraints. Loader topologically sorts before issuing `Plugin_Load`. |
| Channels | (ii) C++ DLL |
| Engine status | READY (Phase 2 acceptance — see `examples/messaging-pair/` for the ordering proof) |
| Test plugin | _TBD — port the messaging-pair pattern, assert load order_ |
| Auto-pass check | Plugin B's `Plugin_Load` observes Plugin A's state being already initialized. |
| Last result | ✅ PRE-VERIFIED on messaging-pair |
| Notes | _ |

## CAP-17: `EnumeratePlugins` (introspection)

| Field | Value |
|---|---|
| What | C++ plugin calls `api->EnumeratePlugins(buf, cap)` to get the list of loaded plugins (handle, name, version). |
| Channels | (ii) C++ DLL |
| Engine status | READY (Phase 2) |
| Test plugin | _TBD — call EnumeratePlugins, assert returned count ≥ 1 and self is in the list_ |
| Auto-pass check | Count matches the number of loaded plugins; entries include this plugin's name and handle. |
| Last result | ✅ PRE-VERIFIED on hello-plugin |
| Notes | Used for conflict diagnostics, config UIs that enumerate co-loaded mods. |

## CAP-18: Pak mod resource overrides (XML / Lua / Schematyc)

| Field | Value |
|---|---|
| What | Standard CryEngine pak mod: drop a pak that contains modified `Libs/Tables/*.xml`, `Scripts/*.lua`, etc. The game loads the modded version instead of the vanilla one. Workshop-distributable. |
| Channels | (i) |
| Engine status | NATIVE (not a kcdx feature — CryEngine pak system) |
| Test plugin | _N/A — CryEngine-level capability, not kcdx-tested_ |
| Last result | ✅ NATIVE (the existing `inventory-in-dialogue/`, `easytoseeherbs/` pak mods demonstrate this works without kcdx) |
| Notes | Documented for completeness — many real mods are pure pak mods that don't need kcdx at all. |

## CAP-19: UI / Scaleform injection

| Field | Value |
|---|---|
| What | Inject Flash UI widgets, hook Scaleform events, register new HUD elements. |
| Channels | (ii) probably C++ DLL once exposed |
| Engine status | DEFERRED — v0.2 (`kcdxScaleformInterface` equivalent), separate Ghidra session |
| Test plugin | DEFERRED |
| Last result | DEFERRED-v0.2 |
| Notes | Big surface area; lots of mods will want this eventually. |

## CAP-22: `kcdx.hook` mode="callsite" (single call-site redirect)

| Field | Value |
|---|---|
| What | Redirect ONE specific E8 near-call (rewrite its rel32 displacement) so only that caller is routed through the hook chain; every other caller of the same callee is untouched. `mode = "callsite"` is the explicit SCOPE selector; the behavior (before/after/around/replace) is attached under its own key and wraps the CALLED function at that one site. Locator: `target_callsite = { pattern \| address_id \| rva }`. The install reads the opcode at the site (rejects non-E8 indirect calls loudly), computes the original callee VA from the displacement, builds a chain trampoline over that callee (reusing the function-entry DispatchPre/Post + call_original spine), verifies rel32 reachability, and rewrites the 4 displacement bytes. Conflict mediation: the chain is keyed by the call-site VA, so two plugins redirecting the SAME site are load-order-mediated via `hook_chain::CanCoexist`; two plugins redirecting DIFFERENT sites to the same callee never collide. |
| Channels | (vi) plugin Lua (C++ `kcdxHookInterface` mirror is restructure parity-debt, built in the C++ phase) |
| Engine status | LIVE-PENDING (Phase 2b sub-6) — binder reconcile (`mode="callsite"` scope + behavior key), `hook_chain::AddCallsite` install path, E8-opcode + rel32-reach install-time checks, callsite branch in `CanCoexist`. Awaiting checkpoint launch. |
| Test plugin | `cap-22-callsite-redirect/` (C++ DLL + plugin.lua). Shared `Cap22_Helper` callee; five distinct callers (four redirected: before/after/around/replace, one control). Each caller makes a real noinline E8 call to Helper; `/OPT:NOICF` + per-caller unique volatile tags keep them distinct. The DLL scans each redirected caller's body for the E8-to-Helper, converts to a module RVA, and hands plugin.lua the `target_callsite = { rva = "cap-22.dll @ rva 0x..." }` locator (exercising the rva escape-hatch form). |
| Auto-pass check | On `kInputLoaded` (after ApplyZone), the DLL calls each caller(10): before→111, after→1110, around→220, replace→42. Isolation: the control caller of the SAME Helper →110 AND a direct `Cap22_Helper(10)`→110, both UNAFFECTED — proving the redirect is per-call-site, not per-callee. |
| Last result | ⏳ PENDING (in-game verified at the checkpoint launch) |
| Notes | restructure-plan design-gap #1 (canonical BugSplat MiniDmpSender-ctor-callsite case). v1 handles only the E8 direct near-call; FF /2 and FF 15 indirect calls are rejected at install with the actual opcode named. |

## CAP-23: plugin.lua error line-info quality (AP12 #3)

| Field | Value |
|---|---|
| What | When a `plugin.lua` raises a RUNTIME error, the loader-captured error text must carry a `:<line>:` file:line marker (piece 1: `storedebug` toggled on across the load) AND a `"stack traceback:"` (piece 2: the `lua_pcall` errfunc that fetches `debug.traceback`), then route it to the engine log + the plugin's own log (piece 3). This row regression-guards that the captured text quality survives. |
| Channels | (vi) plugin Lua (loader-side assertion in `src/lua_plugin_loader.cpp`) |
| Engine status | LIVE-PENDING (AP12 #3) — fixture-agnostic loader assertion in `RunAll`'s runtime-error branch + deliberate-error fixture. Awaiting checkpoint launch. |
| Test plugin | [`cap-23-lua-error/`](cap-23-lua-error/) — `plugin.lua` deliberately indexes a nil global (a runtime error with a live stack, so it carries both line + traceback). The error IS the test; it never gets "fixed". |
| Auto-pass check | The loader reports `cap-23-lua-error-lineinfo` PASS when a `plugin.lua` runtime error's captured text carries `:<digits>:` AND `"stack traceback:"`. FIXTURE-AGNOSTIC: the loader never checks the plugin's name — it asserts on whatever runtime-error text it captured; the deliberate-error fixture is what reliably triggers it each boot. Production-quiet via `ReportResult`'s own dev-mode early-return (no loader-side fixture check). |
| Last result | ⏳ PENDING (in-game verified at the checkpoint launch) |
| Notes | Pieces 1–3 (storedebug line numbers, errfunc traceback, plugin-log routing) live in `src/lua_plugin_loader.cpp` and are the feature under test. Multiple-report semantics: if more than one plugin.lua errors in a boot, `ReportResult("cap-23-lua-error-lineinfo", ...)` is called once per erroring plugin and last-call-wins (test.h) — fine, since every erroring plugin should carry line info, so any of them passing is the signal. |

## CAP-24: `kcdx.on` lifecycle-event bridge

| Field | Value |
|---|---|
| What | `kcdx.on(name, fn)` (the sub-7 verb) gains the 9 game-lifecycle events that mirror the engine's existing `kcdxMessage_*` catalog: `post_load`, `post_post_load`, `input_loaded`, `new_game`, `pre_load_game`, `post_load_game`, `save_game`, `load_game_selected`, `delete_game`. A PURE BRIDGE — one engine-internal hook in `messaging::FireEngineMessage` fans each mapped `kcdxMessage_*` out to that event name's `kcdx.on` Lua subscribers. The 3 save/load events (`save_game`, `load_game_selected`, `delete_game`) pass the save basename (e.g. `"save561.whs"`) as the callback's single arg (a copy — the engine-owned `const char*` is never retained); the other 6 fire with no args. Fires EVERY time the message fires (e.g. `save_game` on every save), pcall-isolated per callback (a throwing callback logs loud + does not abort the others or the engine). `"ready"` is unchanged (sub-7 per-plugin post-apply signal; not a lifecycle event). |
| Channels | (vi) plugin Lua (C++ `kcdxMessagingInterface` is the existing parity surface — CAP-08 — so no new C++ mirror is owed; this binds the same engine messages to Lua) |
| Engine status | LIVE-PENDING (Phase 2b sub-8) — `src/lua_lifecycle.{h,cpp}` registry + dispatch, `kcdx.on` binder lifecycle branch in `src/lua_bind_on.cpp`, engine-internal bridge in `src/messaging.cpp::FireEngineMessage`. Awaiting checkpoint launch. |
| Test plugin | [`cap-24-lifecycle-events/`](cap-24-lifecycle-events/) — pure Lua. Subscribes to `input_loaded` (auto), `save_game` (manual), `post_load_game` (manual); reports from each callback. |
| Auto-pass check | `CAP-24-input-loaded`: `kcdx.on("input_loaded", ...)` fires on the first update tick every boot → reports PASS with no player input. This is the standing regression guard that the bridge wires `kcdx.on` through to the engine dispatch. |
| Manual confirm | `CAP-24-save-game` [manual]: save in-game → callback asserts it got a non-empty basename string. `CAP-24-post-load-game` [manual]: load a save → callback fires (proves a no-arg lifecycle event reaches Lua). |
| Last result | ⏳ PENDING (in-game verified at the checkpoint launch) |
| Notes | Engine-internal listener placement: mirrors `serialization::OnEngineMessage` — a direct call in `FireEngineMessage`, NOT a `RegisterListener` call (the messaging `RegisterListener` thunk requires a valid `PluginHandle` and rejects engine-internal subscribers, so engine-internal consumers use the direct-dispatch path). Empirical: of the 3 save/load events only `save_game` + `load_game_selected` actually pass a basename at their fire sites today; the `delete_game` fire site passes none (degrades to a no-arg call — `FireLifecycle` never pushes a NULL string to Lua). |

## CAP-25: multi-file plugin + complete source attribution

| Field | Value |
|---|---|
| What | A plugin's `plugin.lua` can `require("helper")`; kcdx-owned `require` (per-chunk fenv) resolves `"helper"` against the plugin's OWN folder (a sibling `helper.lua`). At the resolve+compile point kcdx calls `RegisterScriptOwner(helperPath, pluginName)` so EVERY source a plugin loads (entrypoint AND require'd helper) lands in `g_scriptOwners` owned by the plugin → a `kcdx.*` call from inside the helper resolves to the plugin (via the unchanged `OwningPluginForCurrentCall` frame walk), NOT `"<anon>"`. The completeness criterion: identity resolves identically from any plugin source. |
| Channels | (vi) plugin Lua (Lua-surface PARITY test — C++ DLL plugins split across files NATIVELY; the linker handles it and a DLL's identity is its handle, not a Lua source path, so NO C++ mirror is owed; this brings the Lua `require` path to parity and exercises it) |
| Engine status | LIVE — kcdx-owned `require` via a per-chunk fenv + per-resolve `RegisterScriptOwner` (commits `7640fde` probe + `3371de0` searcher; per-entry-zone step 6 replaced the `package.loaders` searcher with the kcdx require closure + `<owner>:<modname>` namespaced cache that bypasses `_LOADED` — see COMP-10. The CAP-25 attribution capability is unchanged: every resolved module is still `RegisterScriptOwner`'d at its compile point). |
| Test plugin | [`cap-25-multifile-attribution/`](cap-25-multifile-attribution/) — a SINGLE pure-Lua plugin split across `plugin.lua` (entrypoint; `require`s the helper) + `helper.lua` (the file under test). |
| Auto-pass check | The helper (running from a require'd source) SUBSCRIBES at load to `kcdx.cap-25-multifile-attribution:multifile_event` and PUBLISHES the bare `multifile_event` from a DEFERRED `input_loaded` callback. The engine stamps the published bare event under the publisher's RESOLVED owner. If the helper's source is attributed to the plugin (fix works), the stamp is `kcdx.cap-25-multifile-attribution:multifile_event` → matches the subscription → the callback FIRES with `payload.marker=="CAP25_OK"` → PASS. If the source resolves to `"<anon>"`, the stamp is `<anon>:multifile_event` → no match → the callback never fires → row stays PENDING/FAIL. A firing callback with the right payload is unforgeable proof the require'd helper's `kcdx.*` calls resolved to the plugin. No player input. |
| Last result | ✅ LIVE (kcdx-dev 14:32, suite 47/53; `require` resolved `helper.lua` to the plugin's own folder, publish from the deferred callback stamped `kcdx.cap-25-multifile-attribution:multifile_event` → 1 subscriber → PASS). Re-verify at the next checkpoint after the step-6 require-mechanism change (`package.loaders` → kcdx require closure). |
| Notes | The multi-file `require` + complete-attribution regression: proves a require'd helper's `kcdx.*` calls resolve to the plugin, not `<anon>`. Lua-surface parity (C++ splits natively — no C++ mirror owed). Deterministic ordering mirrors COMP-09: subscribe-at-load (synchronous during `require`), publish-from-`input_loaded` (first update tick, after all plugin.lua loaded), so the subscription is registered before the publish fires. Publishing from inside the deferred callback is the hardest identity case (no plugin.lua / helper top-level frame live). |

## CAP-26: `kcdx.command` + `kcdx.console.execute` round-trip

| Field | Value |
|---|---|
| What | The Lua console-command round-trip. `kcdx.command{ name=, description=, callback= }` (step 1, commit `d41cb0b`) registers a console command from Lua; `kcdx.console.execute(commandLine)` (step 2, this row) runs a command line through CryEngine's `IConsole::ExecuteString` — the SAME synchronous, same-(main-)thread dispatch user `~`-console input uses, so the registered command's callback fires same-stack before `execute` returns (AP6-safe). `kcdx.console.*` is a GROUPED capability domain (like `kcdx.log.*`), `execute` a positional "do a thing" string call — NOT a 7th top-level verb. The callback receives the args table: the ARRAY of typed args (`args[1]`..`#args`, EXCLUDING `GetArg(0)`/the command name) PLUS `args.raw` (the full command line). |
| Channels | (vi) plugin Lua. C++ PARITY: `kcdxConsoleInterface::ExecuteString` already exists (Interfaces.h:1087) and CAP-13 drives it from a DLL — so `kcdx.console.execute` is the Lua mirror of an already-shipped C++ capability, closing a parity gap (the Lua surface previously only REGISTERED). No C++ console work owed; the Lua binder just calls the existing thunk. |
| Engine status | LIVE-PENDING (step 2) — `Lua_ConsoleExecute` + the `kcdx.console` sub-table added to `src/lua_bind_command.cpp::bind()` (built like `kcdx.log.*`; over the existing console `ExecuteString`, no `console.{h,cpp}` / `Interfaces.h` change). Awaiting checkpoint launch. |
| Test plugin | [`cap-26-lua-command/`](cap-26-lua-command/) — pure Lua. Registers `cap26_cmd`, subscribes `input_loaded`, self-fires via `kcdx.console.execute`, asserts the recorded args. |
| Auto-pass check | `CAP-26-command-roundtrip`: at `input_loaded` (first update tick, every boot — the deterministic trigger CAP-13 uses), `kcdx.console.execute("cap26_cmd 42 hello")` fires the command synchronously. The fired line tokenizes UNAMBIGUOUSLY (3 space-separated tokens, no quotes / no spaces-in-arg) → command name + exactly two args. Reports PASS iff: `execute` returned `true` (IConsole live) AND the callback fired AND `#args==2`, `args[1]=="42"`, `args[2]=="hello"`, `args.raw` contains `"cap26_cmd"`. A never-firing callback reports FAIL "callback never fired" (catches a broken round-trip). No player input. |
| Last result | ⏳ PENDING (boot auto-pass; verified at the checkpoint launch) |
| Notes | The Lua analog of the C++ CAP-13 (`cap-13-console-command`): same register + self-fire-via-ExecuteString + recorded-args-assert pattern, driven from pure Lua instead of a DLL. `kcdx.console.execute` is the Lua-parity side of the existing C++ `ExecuteString` — added as a `kcdx.console.*` domain verb, NOT a top-level verb. `args[1..]` exclude `GetArg(0)` per the `kcdx.command` contract; `args.raw` is the full `GetCommandLine()` line. |

## CAP-27: `kcdx.command` registration TIMING ARMS (immediate + coexist)

| Field | Value |
|---|---|
| What | The `kcdx.command` registration TIMING arms. `console::Thunk_RegisterCommand` (`src/console.cpp:263`) has two arms keyed on `g_ready` at call time: `g_ready==false` → QUEUE the command (deferred), flushed later by `console::Init`/`FlushPendingCommands` (`console.cpp:322`); `g_ready==true` → `RegisterCommandNow` directly (IMMEDIATE). A `kcdx.command` from `plugin.lua` (RunAll, `hooks.cpp:331`, BEFORE `console::Init` at `hooks.cpp:409`) hits the deferred arm; a `kcdx.command` from a `lua_after` slot (`hooks.cpp:431`, AFTER `console::Init`) hits the IMMEDIATE arm. CAP-27 exercises BOTH from one plugin and asserts both dispatch — pinning the queue-then-flush boundary (deferred-queued + immediate commands coexisting in `g_slots` without clobber). |
| Channels | (vi) plugin Lua. C++ PARITY: CAP-13 (`cap-13-console-command`) already drives the IMMEDIATE arm from a DLL (registers at `kInputLoaded`, post-Init); CAP-27 is the Lua MIRROR of that immediate path. No C++ console work owed — `kcdx.command` + `kcdx.console.execute` already exist; CAP-27 only exercises the lua_after timing. |
| Engine status | LIVE-PENDING (per-entry-zone execution model step 5) — no engine change; uses the `[entrypoints].lua_after` slot (built earlier in this feature) over the existing `kcdx.command` / `kcdx.console.execute` surface. Awaiting checkpoint launch. |
| Test plugin | [`cap-27-command-timing-arms/`](cap-27-command-timing-arms/) — pure Lua, BOTH entrypoint slots. `plugin.lua` registers `cap27_deferred` (deferred arm); `after.lua` registers `cap27_immediate` (immediate arm), self-fires both via `kcdx.console.execute`, asserts each. Shared state via a require'd `state.lua` (CAP-25 searcher pattern). NO zone declaration (default after_game) — the axis is the registration-timing arm, not the zone. |
| Auto-pass check | In `after.lua` (lua_after, post-Init, IConsole live — no input_loaded wait): `CAP-27-immediate` PASSES iff `kcdx.console.execute("cap27_immediate 9 beta")` returned true AND the immediate command's callback fired AND `#args==2`, `args[1]=="9"`, `args[2]=="beta"`, `args.raw` contains `"cap27_immediate"` (the IMMEDIATE arm — Lua mirror of CAP-13). `CAP-27-coexist` PASSES iff BOTH `cap27_deferred` (queued in plugin.lua, flushed at Init; fired with `"7"`,`"alpha"`) AND `cap27_immediate` (fired with `"9"`,`"beta"`) dispatched with their own correct args (the queue-then-flush boundary). Each fired line is 3 space-separated tokens (no quotes / no spaces-in-arg) → unambiguous tokenization. A never-firing callback reports FAIL loudly. No player input. |
| Last result | ⏳ PENDING (boot auto-pass; verified at the checkpoint launch) |
| Notes | Distinct from CAP-26: CAP-26 registers only from `plugin.lua` so it exercises ONLY the deferred arm; CAP-27 adds the IMMEDIATE arm (lua_after, `RegisterCommandNow` direct — the Lua mirror of CAP-13's C++ immediate path) + the deferred/immediate coexistence boundary. The `lua_after` self-fire works because `console::Init` (`hooks.cpp:409`) precedes `RunAfterEntrypoints` (`hooks.cpp:431`), so IConsole is up and the deferred queue is flushed by lua_after time. |

## CAP-30: `kcdx.code` allocation + NOP-pad + export-symbol interlock

| Field | Value |
|---|---|
| What | The `kcdx.code` Lua verb (binder + doc landed in commit `0f17e68`) — the Lua-surface successor to the v0.1 `[[trampoline]]` schema. `kcdx.code{ name=, bytes=, size=, pool=, export= }` allocates executable memory from the trampoline pool **immediately at the call**, copies optional hex `bytes` into the head, NOP-pads (`0x90`) the tail to `size`, optionally publishes the address under `export` as a symbol (`symbols::Register`), and returns a **live `kcdx.memory.pointer` userdata** to the region. CAP-30 is the Lua-verb counterpart of CAP-07 (the C++/`[[trampoline]]` allocation row). |
| Channels | (vi) plugin Lua. C++ PARITY: `kcdxTrampolineInterface` already ships the C++ trampoline allocation (CAP-07 exercises it); `kcdx.code` is the Lua mirror over the SAME engine paths (`trampoline::Allocate{Branch,Local}` + `symbols::Register`). No C++ trampoline work owed — the binder is a thin Lua surface over the already-shipped engine. |
| Engine status | LIVE-PENDING (kcdx.code feature, sub-1 commit `0f17e68`) — no engine change; the binder (`src/lua_bind_code.cpp`) is a thin Lua surface over the existing trampoline pool + symbol table. This row is the regression test only. Awaiting checkpoint launch. |
| Test plugin | [`cap-30-lua-code/`](cap-30-lua-code/) — pure Lua, single `plugin.lua`. Allocates regions, writes/reads them back via the returned pointer, and consumes the export through a deferred `kcdx.bytes{ target_symbol=... }`. |
| Auto-pass check | **`CAP-30-alloc`** (synchronous at plugin load — `kcdx.code` allocates immediately): `kcdx.code{ name="cap30_region", size=64 }` returns a live pointer; the accessors take the VALUE only (no offset arg), and `:add(N)` navigates to an offset — `region:set_byte(0xAB)` (at base) + `region:add(4):set_dword(0xDEADBEEF)` (at offset 4, clear of the byte) round-trip through `region:get_byte()==0xAB` + `region:add(4):get_dword()==0xDEADBEEF` (write/read-back proves the region is real + writable — a nil/non-writable region fails the read-back). Folds in the NOP-pad: `kcdx.code{ name="cap30_nop", bytes="C3", size=8 }` → `get_byte()==0xC3` (code head) AND `add(4):get_byte()==0x90` (NOP-padded tail). **`CAP-30-export`** (asserted in `kcdx.on("ready")`, after the apply pass): `kcdx.code{ name="cap30_exported", size=16, export="kcdx.cap-30-lua-code.region" }` publishes the symbol; a deferred `kcdx.bytes{ target_symbol="kcdx.cap-30-lua-code.region", original="90", replacement="90" }` (NOP-over-NOP, same-length, idempotent) resolves it at the apply pass. PASS iff the `kcdx.bytes` handle `:applied()==true` — proving `target_symbol` resolved the export to the live region. A never-resolving export → `:applied()==false`/nil → loud FAIL with the reason. No player input. |
| Last result | ⏳ PENDING (boot auto-pass; verified at the checkpoint launch) |
| Notes | Export-interlock proof rationale: there is NO Lua surface that resolves the runtime symbols table (`kcdx.addr` is the Address Library snapshot built once at startup, NOT the `symbols::Register` table that `export` writes to), so the interlock is proven via a `target_symbol` consumer. `kcdx.bytes` is cleaner than `kcdx.hook` for an arbitrary code region: a hook needs a meaningful function entry, but a same-length byte rewrite needs only live writable memory (which the region is), so `:applied()==true` is a real, non-contrived proof the export resolved (`docs/lua-api.md` §kcdx.code names `kcdx.hook`/`kcdx.bytes{ target_symbol }` as the export consumers). Split into two rows for diagnosability: `CAP-30-alloc` (the synchronous core — alloc + writable + NOP-pad) and `CAP-30-export` (the deferred cross-feature interlock). |

---

# Section 2: Competition / collision rows

## COMP-01: Two `[[patch]]` entries on the same address

| Field | Value |
|---|---|
| Scenario | Plugin A and Plugin B both declare `[[patch]]` entries that resolve to the same VA. |
| Engine behavior expected | conflict_engine pre-flight detects overlap. Lower-priority-number plugin wins. Loser's apply is aborted with a log line naming the winner. |
| Test plugin | _TBD `comp-01-two-patch-overlap/` — two siblings under one folder, both target the outfit AOB with different priorities_ |
| Engine status | READY (conflict_engine ships) |
| Auto-pass check | kcdx.log CONFLICT record names both plugins; only winner's bytes land at the address. |
| Last result | _TBD_ |

## COMP-02: `[[patch]]` + `[[hook]]` on overlapping bytes

| Field | Value |
|---|---|
| Scenario | Patch writes bytes at address X..X+2. Hook installs a 5-byte rel32 jmp at address X..X+4 (overlaps). |
| Engine behavior expected | conflict_engine notes the overlap. Patch applies first, MinHook relocates the patched bytes into its trampoline so the patch survives inside the hook's call-original path. Both apply. |
| Test plugin | _TBD — port `examples/conflict-test-hook-on-patch/` to `comp-02-hook-on-patch/`_ |
| Engine status | READY |
| Auto-pass check | `HookOverlapsEarlierPatch=1` line in kcdx.log; both entries apply cleanly. |
| Last result | ✅ PRE-VERIFIED on existing examples plugin |
| Notes | This is the most common patch+hook coexistence case in real mods. |

## COMP-03: Two `[[hook]]` on the same function

| Field | Value |
|---|---|
| Scenario | Plugin A and Plugin B both install hooks at function entry X. |
| Engine behavior expected | First-hook-wins. Second hook aborted with a plain-English log line naming the first plugin. |
| Test plugin pair | [`comp-03-hook-on-hook-A/`](comp-03-hook-on-hook-A/) (winner, priority 100) + [`comp-03-hook-on-hook-B/`](comp-03-hook-on-hook-B/) (loser, priority 200, DLL verifier) |
| Site | Sister IsInCombat wrapper at WHGame.dll RVA `0x566040` (distinct from COMP-02's `FUN_1805605b8`; AOB ends in `3C 01` vs COMP-02's `3C 02`). |
| Engine status | READY (Phase 4b) |
| Auto-pass check | Plugin B's DLL re-resolves the target, calls `GetConflictReport`, asserts exactly 2 entries with `comp-03-A` applied and `comp-03-B` aborted. |
| Last result | _TBD — installable as of this commit; awaiting live run_ |
| Notes | Chained hooks are explicitly v0.2+ (Hard rule #8). Both detour bodies are `31 C0 C3` (always-return-false); the function is a combat-state predicate, returning false at boot is harmless. |

## COMP-04: `[[patch]]` + runtime `dynamic_hook` on same address

| Field | Value |
|---|---|
| Scenario | TOML `[[patch]]` modifies bytes at X. A pak-Lua-driven `kcdx.memory.dynamic_hook` later tries to install at the same X. |
| Engine behavior expected | The runtime install path sees the existing patched bytes in the first-wins map, aborts cleanly. |
| Test plugin | _TBD — patch + pak mod targeting same VA_ |
| Engine status | READY (first-wins map covers both channels per Phase 5c.7b.2) |
| Auto-pass check | kcdx-dev.log: runtime DYNAMIC_HOOK/install-failed cites the prior patch. |
| Last result | _TBD_ |

## COMP-05: Plugin Lua registration overrides another plugin's

| Field | Value |
|---|---|
| Scenario | Plugin A and Plugin B both `RegisterFunction("hello", "greet", ...)`. Last registration wins (replaces earlier). |
| Engine behavior expected | Whichever runs `Plugin_Load` later overwrites the earlier. Optionally: warn in log. |
| Test plugin | _TBD — two test DLLs registering the same name_ |
| Engine status | _TBD — confirm behavior; may need to add a warn_ |
| Auto-pass check | Pak Lua sees plugin B's implementation; log has a warning naming plugin A as overridden. |
| Last result | _TBD_ |

## COMP-06: Plugin B depends on plugin A's Lua registration

| Field | Value |
|---|---|
| Scenario | Plugin A `RegisterFunction`s `kcdx.magic.castSpell`. Plugin B's `Plugin_Load` reads `kcdx.magic.castSpell` and wraps it. Needs A loaded first. |
| Engine behavior expected | B's `kcdxPluginVersionData::dependencies` lists A; topo-sort ensures A loads first. |
| Test plugin | _TBD — magic-system-stub (A) + magic-extender (B) with dependency_ |
| Engine status | READY |
| Auto-pass check | B's Plugin_Load reads A's registration without error; combined behavior works in pak Lua. |
| Last result | _TBD_ |
| Notes | The "ecosystem" case. SKSE has this via SKSE plugin-to-plugin Messaging; kcdx has it via the kcdx.* Lua namespace + Messaging. |

## COMP-07: Pak resource override + DLL function detour collide

| Field | Value |
|---|---|
| Scenario | Pak mod overrides a Lua script in `scripts/system/something.lua`. DLL plugin hooks a C++ function that calls that script. The two modifications interact. |
| Engine behavior expected | Each operates in its own channel; outcomes depend on what the Lua and the hook each do. kcdx doesn't try to detect this (out of scope — pak resources aren't kcdx's domain). |
| Test plugin | OUT-OF-SCOPE (deliberate) |
| Last result | _N/A_ |

## COMP-08: Load-order determinism across game restarts

| Field | Value |
|---|---|
| Scenario | Same set of conflicting plugins, multiple game restarts, verify same winner each time. |
| Engine behavior expected | Apply order = topo-sort(dependencies) → sort(priority asc, name asc). Deterministic. |
| Test plugin | _TBD — assertion that runs three game restarts and compares apply order. Hard to auto-test in a single boot; may need a "previous boot's apply order" persisted in a sidecar file the plugin reads on each boot_ |
| Engine status | READY |
| Auto-pass check | Apply order matches the previous boot's apply order (read from sidecar). |
| Last result | _TBD_ |

## COMP-09: `kcdx.publish` cross-plugin pub/sub

| Field | Value |
|---|---|
| Scenario | Plugin A `kcdx.publish("event", payload)` broadcasts a custom Lua event; Plugin B `kcdx.on("A-name:event", fn)` in a different plugin hears it with the payload. A Lua-NATIVE layer (Phase 2b sub-9) sharing the kcdx.on subscriber registry (lua_lifecycle's `g_subscribers`) — NOT the C++ `kcdxMessage` wire format. Payload is passed BY REFERENCE (no copy/serialize): any Lua value (table/string/number/nested); a table is shared by reference (publish-immutable-by-convention). Namespacing: the publisher names the BARE event; the engine prepends the owning plugin (`OwningPluginForCurrentCall`) → `"<publisher>:event"`. A subscriber always uses the `"plugin:event"` form. kcdx.on extended to route any name containing `:` to the shared registry; a bare non-lifecycle name stays a teaching error. |
| Channels | (vi) plugin Lua (C++ `kcdx.publish` mirror is restructure parity-debt, built in the C++ phase) |
| Engine status | LIVE-PENDING (Phase 2b sub-9) — `src/lua_bind_publish.{h,cpp}` (the `kcdx.publish` verb), custom-event branch in `src/lua_bind_on.cpp`, `RegisterCustomCallback` + `FirePublish` in `src/lua_lifecycle.{h,cpp}`, bound at `src/lua_bind.cpp` `RegisterKcdxTable`. Awaiting checkpoint launch. |
| Test plugin pair | [`comp-09-pubsub-a/`](comp-09-pubsub-a/) (publisher) + [`comp-09-pubsub-b/`](comp-09-pubsub-b/) (subscriber, owns the row). Both pure Lua. |
| Deterministic ordering | B subscribes at plugin.lua-LOAD time; A publishes from inside its own `kcdx.on("input_loaded", ...)` handler (fires on the first update tick, AFTER all plugin.lua loaded + all subscriptions registered). A top-level publish at A's load would race B's subscription — publishing from input_loaded makes it deterministic regardless of A-vs-B load order. |
| Auto-pass check | On `input_loaded`, A publishes `outfit_changed` with `{ x=42, name="Noble" }`. B's `kcdx.on("kcdx.comp-09-pubsub-a:outfit_changed", fn)` callback fires and reports `COMP-09-pubsub` PASS iff it received a table payload with `payload.x==42 && payload.name=="Noble"`. A correct fire ALSO proves A's publisher namespace resolved (the event only reaches B if stamped exactly under A's name) AND the identity-from-inside-a-callback probe (publish ran from within A's input_loaded callback). No player input. |
| Last result | ⏳ PENDING (in-game verified at the checkpoint launch) |
| Notes | Identity-probe outcome map: B fires with the right payload → publish identity resolves (`OwningPluginForCurrentCall` stamps the publisher correctly even from inside a dispatched callback). Never fires / wrong payload → a RegisterScriptOwner coverage gap (wrong namespace) or a payload-by-reference break — surface before sub-9 lands. Anonymous publisher (`OwningPluginForCurrentCall` → "") is NOT dropped: warned + fired under `"<anon>:event"`. The require'd-module identity gap (`lua_registry.cpp:485`) is not exercised here (would need a require'd helper whose path was never RegisterScriptOwner'd) — flagged for a later sub-test if a real plugin hits it. |

## COMP-10: cross-plugin `require`-cache isolation

| Field | Value |
|---|---|
| Scenario | Two plugins (A + B) EACH ship a DIFFERENT `helper.lua` under the SAME bare module name `"helper"` (A's returns `{marker="A"}`, B's returns `{marker="B"}`) and BOTH do `require("helper")`. Each must get ITS OWN file. The pre-fix gap (AP13): ONE shared `lua_State` → ONE shared `_LOADED` module cache; stock `ll_require` keys `_LOADED` by the BARE module name BEFORE any loader runs, so whichever plugin loaded first poisons `_LOADED["helper"]` and the other silently gets the WRONG module — a cross-plugin mis-resolution that a per-plugin-folder file-resolver (runs only on a cache MISS) cannot fix at the cache layer. |
| Engine behavior expected | kcdx OWNS `require` for plugin chunks (per-chunk fenv whose `require` is a kcdx C closure) and namespaces the cache by the owning plugin: key `"<owner>:<modname>"`, BYPASSING `_LOADED` entirely. So A's `require("helper")` resolves to A's `helper.lua` + A's cache slot, B's to B's — no collision. A SECOND `require("helper")` in the same plugin (same owner+key) is a kcdx cache hit returning the SAME table. |
| Channels | (vi) plugin Lua. C++ PARITY: N/A — language-specific. `require` is Lua-only; a C++ plugin is one DLL with no `require` analog, so there is NO C++ mirror owed (the "N/A, language-specific" case, not a parity debt). |
| Engine status | LIVE (per-entry-zone execution model step 6) — `src/lua_require_searcher.{h,cpp}` (kcdx require closure + `<owner>:<modname>` namespaced cache as a registry-ref'd GC Lua table + shared kcdx env table with `__index`→`_G` + `ResolveAndCompile`) + the entrypoint-fenv set in `src/lua_plugin_loader.cpp::LoadOneFileGuarded`. |
| Test plugin pair | [`comp-10-require-isolation-a/`](comp-10-require-isolation-a/) (`helper.lua`→`marker="A"`) + [`comp-10-require-isolation-b/`](comp-10-require-isolation-b/) (`helper.lua`→`marker="B"`). Both pure Lua; each owns + reports its own row. |
| Auto-pass check | At plugin load each plugin does `local h1 = require("helper"); local h2 = require("helper")`. `COMP-10-require-isolation-a` PASSES iff `h1.marker=="A"` (isolation — B's helper did NOT leak) AND `h1==h2` (within-plugin cache hit — second require returns the same table). `-b` is the mirror expecting `"B"`. A wrong marker = the collision is live (FAIL before this fix, PASS after); `h1~=h2` = the kcdx cache missed. No player input. |
| Last result | ✅ LIVE (kcdx-dev 17:15 — comp-10-a `marker=A` + same-table, comp-10-b `marker=B` + same-table; per-plugin `REQUIRE` resolves confirm each got its OWN helper.lua — no cross-plugin cache collision) |
| Notes | The fix-now of the cross-plugin require-cache collision (AP13) flagged against the multi-file `require` feature. Two assertions per plugin: cross-plugin ISOLATION (different file per plugin under the same bare name) + within-plugin CACHE HIT (same table on re-require). The cross-window cache-hit case (a `plugin.lua` require + a `lua_after` require both hitting the same `<owner>:state` slot) is additionally exercised by CAP-27's shared `state.lua`. Owner-scoping of nested requires comes from the live `OwnerScope` (synchronous, main-thread) + the closure setting the kcdx fenv on EVERY chunk it loads — NOT from fenv inheritance (lexical only; OP_CLOSURE). `_LOADED` is bypassed, not mirrored — stock non-plugin code not seeing a plugin's module under the bare name IS the isolation. |

## COMP-11: both-phase Lua execution model (cross-plugin, both slots)

| Field | Value |
|---|---|
| Scenario | Two plugins (A + B) at DIFFERENT load-order priorities, EACH with a `lua` (before) slot AND a `lua_after` (after) slot. Each slot, when it runs, publishes a phase token naming its slot. PASS iff the recorded run-order is `[all before-slots, in load-order priority]` THEN `[all after-slots, in load-order priority]`. Proves BOTH (1) the PHASE BOUNDARY — every before-slot runs before every after-slot (RunAll over all plugin.lua → ApplyZone → RunAfterEntrypoints over all lua_after, the sub-3 hooks.cpp ordering) AND (2) the CROSS-PLUGIN PRIORITY INTERLEAVE — within each phase the lower-priority plugin's slot runs first (RunAll + RunAfterEntrypoints both iterate in load-order priority asc, sub-2 + sub-3). |
| Engine behavior expected | A `default_priority` 30, B 70. Run-order = `["a.before", "b.before", "a.after", "b.after"]`. |
| Channels | (vi) plugin Lua. C++ PARITY: the both-phase execution model + the `lua_after` slot are language-agnostic engine behavior; the C++ both-phase ordering test (cap-29) is the NEXT step in this feature — COMP-11 is the Lua-side proof. |
| Engine status | LIVE (per-entry-zone execution model step 7) — uses the `[entrypoints].lua` + `.lua_after` slots over `kcdx.publish` / `kcdx.on` (sub-9); the before-phase priority order is real per the step-7 `RunAll` priority sort. |
| Test plugin pair | [`comp-11-both-phase-order-a/`](comp-11-both-phase-order-a/) (priority 30, the ASSERTER — subscribes + collects + asserts; owns the row) + [`comp-11-both-phase-order-b/`](comp-11-both-phase-order-b/) (priority 70, pure publisher). Both pure Lua, both slots each. |
| Cross-plugin recording | Step 6 made require'd modules per-plugin isolated (COMP-10), so two separate-owner plugins CANNOT share a require'd module — the only cross-plugin channel is `kcdx.publish` / `kcdx.on` (sub-9, COMP-09). Each slot publishes the bare event `phase_token` with payload `{ slot="before"\|"after" }`; the engine stamps it `<publisher>:phase_token` (publisher in the event NAME, slot in the payload). |
| Deterministic ordering | Option B (lowest-priority-asserter), no missed-token hole: A is the LOWEST priority (30), so A's plugin.lua runs FIRST in RunAll — before ANY token publishes. A subscribes to both plugins' `phase_token` events at the TOP of its plugin.lua, BEFORE publishing its own `a.before`. `kcdx.publish` fires subscribers SYNCHRONOUSLY, so every token (A's own `a.before` published right after subscribing, B's `b.before` later in RunAll, both afters in RunAfterEntrypoints) publishes after A's subscriptions are live → none can be missed. Had A not been lowest priority, `b.before` could publish before A subscribed — that is the hole option B closes. |
| Auto-pass check | `COMP-11-both-phase-order`: at `input_loaded` (first update tick, after RunAll + RunAfterEntrypoints) A reads the collected ordered sequence and reports PASS iff it equals `["a.before", "b.before", "a.after", "b.after"]`. The single ordered comparison proves the phase boundary (both befores before any after) AND the per-phase priority interleave (30 before 70 in each phase) at once — no weaker subset of tokens can false-pass it. No player input. |
| Last result | ✅ LIVE (kcdx-dev 17:17 — run-order == `["a.before","b.before","a.after","b.after"]`; phase boundary + per-phase priority interleave both confirmed) |
| Notes | The Lua-side proof of the both-phase per-entry-zone execution model (step 7). Distinct from COMP-09 (single publish/subscribe, payload-by-reference) — COMP-11 reuses the COMP-09 pub/sub mechanism as a cross-plugin RECORDER for run-order, asserting the engine's RunAll-then-RunAfterEntrypoints phase boundary + the load-order priority interleave WITHIN each phase. The C++ mirror (cap-29) is the next step. |

---

# Section 3: Real-world mod scenarios

Sanity-check the matrix by walking real mod ideas end-to-end. For
each, list the capability rows the mod needs and note if any are
DEFERRED.

## Scenario A: "Combat tweaks" (the outfit-swap case)

Small mod, single byte rewrite. Maps to **CAP-01** alone. Already
proven via CAP-01 test plugin.

## Scenario B: "Better hud" (UI addition with custom data)

Mod displays player stamina/stamina-regen in a custom HUD widget.
Needs: **CAP-19** (Scaleform injection — DEFERRED) + **CAP-06**
(read game state via dynamic_call to grab stamina values) +
**CAP-08** (subscribe to gameplay-tick message to refresh the
widget). Buildable when v0.2 ships Scaleform.

## Scenario C: "Persistent perk system" (new game mechanic)

Mod adds a perk that the player buys with XP. Perk persists
across saves. Needs: **CAP-10** (expose `kcdx.perks.buy()` to
pak Lua) + **CAP-12** (save/load perks state — DEFERRED Phase 6)
+ **CAP-13** (console command to grant perks for testing —
DEFERRED Phase 7) + **CAP-18** (pak resource override for the
perks XML).

## Scenario D: "Magic system" (full new system, SKSE-class)

Player can cast spells. Spells have effects, mana cost,
cooldowns. Needs: **CAP-10** (`kcdx.magic.cast`) + **CAP-12**
(save mana/known-spells — DEFERRED) + **CAP-13** (console
commands for testing — DEFERRED) + **CAP-08** (subscribe to
input events for hotkeys) + **CAP-18** (XML/Lua for spell
definitions) + **CAP-19** (UI for spell selection — DEFERRED).
Plus extension points: **CAP-06** for dependent mods to
read magic state, **CAP-10** for them to register new spells.

## Scenario E: "Workshop-distributable code injection"

Mod author wants to ship a single .pak that does some byte
rewriting (e.g., the outfit-swap). Needs to work via Steam
Workshop (which only accepts paks). Maps to **CAP-05** alone.
This is the novel kcdx capability — before kcdx, this was
impossible (pak Lua had no FFI).

## Scenario F: "Cross-plugin ecosystem" (the hard one)

Plugin A is a magic system. Plugin B adds new spells to A.
Plugin C adds a UI for spell selection that works with both
A's vanilla spells and B's additions. Needs: **CAP-16**
(dependencies — A then B then C) + **CAP-10** (each plugin's
Lua surface) + **CAP-06** (C reads A's registered spells) +
**COMP-05** (registration override behavior) + **COMP-06**
(dependency chain proven). Buildable today modulo the
registration-override warning ergonomics.

---

# Section 4: Live roll-up

Auto-updated by the developer after each suite run. One row per
CAP/COMP, status + commit SHA.

As of the last checkpoint (2026-05-22): **58/60 passing** across every
suite-aggregate emit point (`update tick`, `kPreLoadGame`,
`kPostLoadGame`) — the full pass count includes all CAP-* and
COMP-* rows that ship a real test plugin under `test-plugins/`; see
the per-row matrix below for the exact ✅ LIVE / ⏳ PENDING breakdown
(the `⏳ PENDING [manual]` save/load rows need an in-game gesture, and
`cap-04-c` is the one standing pre-existing FAIL). Earlier roll-up:
**21/21 passing** as of the 2026-05-20 18:32 dev-log run, before the
Phase 2b `kcdx.hook` / `kcdx.command` / per-entry-zone subs landed.

| Row | Status | Last verified at SHA | Notes |
|---|---|---|---|
| CAP-01 | ✅ LIVE | `03dd155` | outfit-swap-style byte patch; post-patch AOB unique, pre-patch absent |
| CAP-03 | ✅ LIVE | `03dd155` | `[[hook]] lua_callback` dispatches into pak Lua |
| CAP-04a | ✅ LIVE | `03dd155` | `[[mid_hook]] call_original=true`; returns 110 |
| CAP-04b | ✅ LIVE | `03dd155` | `[[mid_hook]] call_original=false`; returns 10 (original skipped) |
| CAP-04c | ❌ FAIL | `03dd155` | `[[mid_hook]] call_original="auto"` + `args._skip=true`; returns 110 (expected 10) — the legacy `args._skip` mid-hook bug: the auto-skip path does NOT skip the original. The one standing pre-existing FAIL; superseded by `kcdx.hook` mode=mid (CAP-21-skip PASSES, proving the new dispatcher does not inherit it). |
| CAP-04d | ✅ LIVE | `03dd155` | `[[mid_hook]] call_original="auto"`, no `_skip`; returns 110 |
| CAP-05 | ✅ LIVE | `03dd155` | pak Lua `dynamic_hook` install |
| CAP-07 | ✅ LIVE | `03dd155` | trampoline branch / local pool allocations |
| CAP-08 | ✅ LIVE | `03dd155` | engine messages + lifecycle |
| CAP-09 | ✅ LIVE | `03dd155` | `kcdxTaskInterface` round-trip |
| CAP-10 | ✅ LIVE | `03dd155` | `kcdxScriptingInterface` C++ → Lua round-trip |
| CAP-11 | ✅ LIVE | `03dd155` | `kcdx.lua.cfunction_address` resolution |
| CAP-12 | ✅ LIVE | `03dd155` | `kcdxSerializationInterface` cosave roundtrip |
| CAP-13 | ✅ LIVE | `03dd155` | `[[command]]` console command registration |
| CAP-14 | DEFERRED | _ | Address Library — verified transitively via CAP-13's `address_id`-based hooks (1009 + 2000 + 2001). No dedicated test plugin needed for v0.1. |
| CAP-15 | OUT-OF-SCOPE-v0.1 | _ | inline-patches authoring style |
| CAP-16 | ✅ LIVE | `03dd155` | dependency-A → dependency-B messaging pair |
| CAP-17 | ✅ LIVE | `03dd155` | `EnumeratePlugins` |
| CAP-18 | ✅ NATIVE | _ | CryEngine pak system; nothing kcdx-specific to verify |
| CAP-19 | DEFERRED-v0.2 | _ | Scaleform — not a kcdx feature |
| CAP-20-before | ✅ LIVE | sub-4 | `kcdx.hook` mode=before mutates arg via return; original runs (`cap-20-hook-modes`) |
| CAP-20-after | ✅ LIVE | sub-4 | `kcdx.hook` mode=after mutates return value |
| CAP-20-replace | ✅ LIVE | sub-4 | `kcdx.hook` mode=replace; original skipped, return overridden |
| CAP-20-around | ✅ LIVE | sub-4 | `kcdx.hook` mode=around wraps original via `orig()` call_original; doubles result |
| CAP-20-chain | ✅ LIVE | sub-4 | two before hooks on one target chain in load order |
| CAP-20-wstr | ✅ LIVE | sub-4 | wstr arg read + mutate (UTF-16↔UTF-8 marshal + string pinning) |
| CAP-20-conflict | ✅ LIVE | sub-4 | two replace hooks on one target; load-order-loses (first wins, second rejected) |
| CAP-20-dyncall | ✅ LIVE | sub-4 | `kcdx.memory.dynamic_call` i32(i32) arg+return round-trip (10→110); regression for the LUA_NUMBER=float JitTrampoline bug |
| CAP-20-addrname | ✅ LIVE | sub-4b | Address Library NAME locator (`address_id = "name"` for kcdx.hook; `api->ResolveAddressByName` for C++). Resolve-layer check: ResolveAddressByName("lua_pcall") == ResolveAddress(1000), exact. Miss-path assert now in CAP-20-addrname-miss |
| CAP-20-addrname-miss | ⏳ PENDING | sub-7 | `kcdx.hook` with a bad `address_id` NAME ("cap20_addrname_miss") fails to apply; asserted in `kcdx.on("ready")`: `handle:applied()==false` + non-empty `:reason()` (`cap-20-hook-modes`) |
| CAP-20-conflict-rejected | ⏳ PENDING | sub-7 | the load-order-losing replace (`hConflictB`) is rejected; asserted in `kcdx.on("ready")`: `applied()==false` + non-empty `:reason()` (the Lua-side rejection assert for CAP-20-conflict) |
| CAP-20-ready | ⏳ PENDING | sub-7 | `kcdx.on("ready", fn)` fires once after this plugin's zone apply pass; a SUCCEEDED handle (`hConflictA`, the conflict winner) reads `applied()==true` inside the callback. PENDING (never reports) if ready never fires |
| CAP-20-target | ✅ LIVE | AP12 | `kcdx.hook{ target = "luaL_loadfile", before=... }` installs with NO hand-written `signature=` — the name supplies the verified ABI (Address Library carries it). `applied()==true` in `kcdx.on("ready")` is the end-to-end proof (had the name carried no signature the binder would have rejected). Target chosen: id 1002 — verified, signature populated, dormant post-install, not a production kcdx hook (`cap-20-hook-modes`) |
| CAP-20-target-nosig | ✅ LIVE | AP12 | `kcdx.hook{ target = "IConsole_AddCommand", ... }` (a verified row with NO seed signature, no explicit `signature=`) fails to install with the no-ABI teaching error — the engine never invents a signature (AP2). `applied()==false` + non-empty `:reason()` in `kcdx.on("ready")` (`cap-20-hook-modes`) |
| CAP-20-locator-default | ✅ LIVE | AP12 | `kcdx.hook{ before=... }` with NO locator is rejected SYNCHRONOUSLY (`nil` + teaching error), and the error LEADS with the common path: `err:find("target")` and `err:find("name")` both match (the two-tier steer names `target="<name>"`, not the advanced raw locators). Asserted inline at registration (`cap-20-hook-modes`) |
| CAP-20-locator-exclusive | ✅ LIVE | AP12 | `kcdx.hook{ target="luaL_loadfile", address=0x1000, before=... }` (two locators in different slots) is rejected SYNCHRONOUSLY with the exactly-ONE-locator error: `err:find("ONE")` matches. Proves locator mutual-exclusion + the two-tier wording (target normal, advanced locators mutually exclusive). Asserted inline at registration (`cap-20-hook-modes`) |
| CAP-21-read | ✅ LIVE | sub-5 | `kcdx.hook` mode=mid capture READ: `c.rax:get()==seed` (name-map captures); add runs → 110 (`cap-21-mid-hook`) |
| CAP-21-write | ✅ LIVE | sub-5 | mode=mid capture WRITE: `c.rax:set(1000)` lands in the real register; add runs on 1000 → 1100 |
| CAP-21-skip | ✅ LIVE | sub-5 | mode=mid run/skip: callback returns `"skip"` → captured `add` never runs → 10 (positional-list captures `c[1]`). Proves the fresh dispatcher does NOT inherit the cap-04-c `args._skip` bug |
| CAP-21-run | ✅ LIVE | sub-5 | mode=mid control: callback returns nothing → captured `add` runs → 110 |
| CAP-22-before | ✅ LIVE | sub-6 | `kcdx.hook` mode=callsite before: redirected E8 site, Helper sees 10→11 → 111 (`cap-22-callsite-redirect`) |
| CAP-22-after | ✅ LIVE | sub-6 | mode=callsite after: redirected E8 site, Helper return 110 → 1110 |
| CAP-22-around | ✅ LIVE | sub-6 | mode=callsite around: redirected E8 site, 2 * orig(10)=2*110 → 220 |
| CAP-22-replace | ✅ LIVE | sub-6 | mode=callsite replace: redirected E8 site returns 42; Helper not called from this site |
| CAP-22-control-unaffected | ✅ LIVE | sub-6 | ISOLATION: control caller of the SAME Helper is unchanged (110) — per-call-site, not per-callee |
| CAP-22-callee-unaffected | ✅ LIVE | sub-6 | ISOLATION: direct Helper(10) unchanged (110) — callee untouched, only call sites rewritten |
| cap-23-lua-error-lineinfo | ✅ LIVE | AP12 | loader reports PASS when a captured plugin.lua runtime error carries `:<line>:` + `"stack traceback:"`; fixture-agnostic assertion over any runtime error, deliberate-error fixture (`cap-23-lua-error`) triggers it each boot |
| CAP-24-input-loaded | ✅ LIVE | sub-8 | `kcdx.on("input_loaded", fn)` fires on the first update tick every boot → auto-pass; proves the lifecycle bridge wires kcdx.on to the engine kcdxMessage_* dispatch (`cap-24-lifecycle-events`) |
| CAP-24-save-game | ⏳ PENDING [manual] | sub-8 | `kcdx.on("save_game", fn)` fires on every in-game save with the basename arg; callback asserts a non-empty string. Needs a save gesture (`cap-24-lifecycle-events`) |
| CAP-24-post-load-game | ⏳ PENDING [manual] | sub-8 | `kcdx.on("post_load_game", fn)` fires after a load (no-arg lifecycle event). Needs a load gesture (`cap-24-lifecycle-events`) |
| COMP-09-pubsub | ✅ LIVE | sub-9 | `kcdx.publish` cross-plugin pub/sub: A publishes `outfit_changed` `{x=42,name="Noble"}` from its `input_loaded` handler; B's `kcdx.on("kcdx.comp-09-pubsub-a:outfit_changed", fn)` asserts the table arrived by reference + the publisher namespace resolved (`comp-09-pubsub-a` + `comp-09-pubsub-b`) |
| CAP-25-multifile-attribution | ✅ LIVE | multi-file | multi-file `require` + complete source attribution: a require'd `helper.lua` subscribes to `kcdx.cap-25-multifile-attribution:multifile_event` + publishes bare `multifile_event` from a deferred `input_loaded` callback; the callback fires (PASS) ONLY if the helper's `kcdx.*` calls resolved to the plugin not `<anon>`. Lua-surface parity (C++ splits natively) (`cap-25-multifile-attribution`) |
| CAP-26-command-roundtrip | ✅ LIVE | step 2 | `kcdx.command` + `kcdx.console.execute` Lua round-trip: registers `cap26_cmd`, self-fires `kcdx.console.execute("cap26_cmd 42 hello")` at `input_loaded`; PASS iff execute returned true AND callback fired AND `#args==2`, `args[1]=="42"`, `args[2]=="hello"`, `args.raw` contains `"cap26_cmd"`. The Lua analog of the C++ CAP-13; `kcdx.console.execute` is the Lua-parity side of the existing C++ `ExecuteString` (`cap-26-lua-command`) |
| CAP-27-immediate | ✅ LIVE | step 5 | `kcdx.command` IMMEDIATE arm (Lua): `cap27_immediate` registered from the `lua_after` slot (post-`console::Init`, `g_ready=true` → `RegisterCommandNow` direct) and self-fired `kcdx.console.execute("cap27_immediate 9 beta")` in after.lua; PASS iff execute returned true AND callback fired AND `#args==2`, `args[1]=="9"`, `args[2]=="beta"`, `args.raw` contains `"cap27_immediate"`. The Lua mirror of CAP-13's C++ immediate path (`cap-27-command-timing-arms`) |
| CAP-27-coexist | ✅ LIVE | step 5 | `kcdx.command` deferred/immediate coexistence boundary: `cap27_deferred` (registered from plugin.lua → queued, flushed at `console::Init`) AND `cap27_immediate` (registered from after.lua → immediate) BOTH dispatch in after.lua with their own args; PASS iff deferred fired `("7","alpha")` AND immediate fired `("9","beta")` — both land in `g_slots` without clobber. Distinct from CAP-26 (deferred arm only) (`cap-27-command-timing-arms`) |
| CAP-30-alloc | ⏳ PENDING | cap-30 | `kcdx.code` allocation + live writable region + NOP-pad (synchronous at load): `kcdx.code{ size=64 }` returns a live `kcdx.memory.pointer`; the accessors take the VALUE only and `:add(N)` navigates to an offset, so `:set_byte(0xAB)`+`:add(4):set_dword(0xDEADBEEF)` round-trip via `:get_byte()`/`:add(4):get_dword()`; `kcdx.code{ bytes="C3", size=8 }` reads `get_byte()==0xC3` (head) + `add(4):get_byte()==0x90` (NOP-padded tail). The Lua-verb counterpart of CAP-07. A nil/non-writable region fails the read-back (`cap-30-lua-code`) |
| CAP-30-export | ⏳ PENDING | cap-30 | `kcdx.code` export-symbol interlock (asserted at `ready`): `kcdx.code{ export="kcdx.cap-30-lua-code.region" }` publishes the address (`symbols::Register`); a deferred `kcdx.bytes{ target_symbol="kcdx.cap-30-lua-code.region", original="90", replacement="90" }` (NOP-over-NOP, idempotent) resolves it at the apply pass; PASS iff the handle `:applied()==true` — proving `target_symbol` resolved the export to the live region. No Lua symbol-resolve surface exists (`kcdx.addr` is the Address Library snapshot, not the runtime symbols table), so `kcdx.bytes` is the consumer (`cap-30-lua-code`) |
| COMP-11-both-phase-order | ✅ LIVE | step 7 | both-phase Lua execution model: two plugins (A prio 30 asserter, B prio 70 publisher), each with a `lua` + `lua_after` slot; each slot publishes a phase token via `kcdx.publish`; A collects via `kcdx.on` and at `input_loaded` asserts the run-order == `["a.before","b.before","a.after","b.after"]` — proving the phase boundary (RunAll before RunAfterEntrypoints) AND the per-phase load-order priority interleave. Lowest-priority-asserter timing guarantees no missed token (`comp-11-both-phase-order-a` + `comp-11-both-phase-order-b`) |
| CAP-29-both-phase-dll | ✅ LIVE | cap-29 | both-phase C++ lifecycle: a single C++ DLL exporting BOTH `kcdxPlugin_Load` AND `kcdxPlugin_PostGameLoad`; a monotonic seq counter records Load seq=1, PostGameLoad seq=2; PostGameLoad asserts load_ran && post_seq>load_seq → proves both exports fired AND Load ran before PostGameLoad. First live exercise of the sub-4 PostGameLoad export; the C++ parity mirror of COMP-11. An InputLoaded backstop (registered in Load) reports a loud FAIL if PostGameLoad never fired, so the row never sits silently PENDING. Pure C++ DLL — builds via its own CMakeLists, not build.ps1 (`cap-29-both-phase-dll`) |
| COMP-02 | ✅ LIVE | `03dd155` | conflict-test hook-on-patch |
| COMP-03 | ✅ LIVE | `03dd155` | hook-on-hook A + B; conflict report verified |
| PROBE-COMP-CRASH | ✅ LIVE | `03dd155` | conflict-report-crash regression guard |
| COMP-01, COMP-04, COMP-05, COMP-06, COMP-08 | DEFERRED-v0.2 | _ | conflict-matrix completeness lives in `_research/` recon docs; v0.1 ships CAP-* coverage of the primary primitives |
| COMP-07 | OUT-OF-SCOPE | _ | pak + DLL cross-channel; not a v0.1 surface |

---

# Section 5: Authoring a new test plugin

Loop for each row that needs a real test plugin:

1. Create the folder `test-plugins/<row-id>-<short-name>/` (e.g.
   `cap-05-paklua-runtime/`).
2. Drop in a `kcdx.toml` with `[kcdx] test_suite_only = true`.
3. If the test needs a DLL: add `CMakeLists.txt` + `.cpp` next to
   the TOML. In `kcdxPlugin_Load`, check `dev::IsEnabled()`,
   run the test logic, call `api->ReportTestResult(handle,
   "<row-id>", pass, "<one-sentence reason>")`.
4. If the test needs a pak: add a `pak/` subdir with `Data/*.pak`
   + `mod.manifest`. The pak's Lua script checks
   `kcdx.dev.is_enabled()`, runs the test, calls
   `kcdx.test.report("<row-id>", pass, "<reason>")`.
5. Install the plugin to the game, launch with dev mode on,
   verify the suite picks up the new test and reports it.
6. Update the row's Last result + Section 4 roll-up + commit.

Folder hygiene: don't pollute test-plugins/ with utility files
(build scripts, README per plugin, etc.). The plugin should be
the minimum viable thing that asserts the capability. If it
needs README text, put it inline in the plugin's kcdx.toml as a
comment block.
