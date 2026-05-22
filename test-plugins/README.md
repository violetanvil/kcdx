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
| Auto-pass check | All four sub-tests pass when register state matches the `call_original` contract: CAP-04a returns 110 (original ran, +100), CAP-04b returns 10 (skipped), CAP-04c returns 10 (Lua-skipped via `_skip`), CAP-04d returns 110 (auto without skip = run). |
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

As of 2026-05-20 18:32 dev-log run: **21/21 passing** across every
suite-aggregate emit point (`update tick`, `kPreLoadGame`,
`kPostLoadGame`). The full pass count includes all CAP-* and
COMP-* rows that ship a real test plugin under `test-plugins/`.

| Row | Status | Last verified at SHA | Notes |
|---|---|---|---|
| CAP-01 | ✅ LIVE | `03dd155` | outfit-swap-style byte patch; post-patch AOB unique, pre-patch absent |
| CAP-03 | ✅ LIVE | `03dd155` | `[[hook]] lua_callback` dispatches into pak Lua |
| CAP-04a | ✅ LIVE | `03dd155` | `[[mid_hook]] call_original=true`; returns 110 |
| CAP-04b | ✅ LIVE | `03dd155` | `[[mid_hook]] call_original=false`; returns 10 (original skipped) |
| CAP-04c | ✅ LIVE | `03dd155` | `[[mid_hook]] call_original="auto"` + `args._skip=true`; returns 10 |
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
| CAP-20-addrname | ✅ LIVE | sub-4b | Address Library NAME locator (`address_id = "name"` for kcdx.hook; `api->ResolveAddressByName` for C++). Resolve-layer check: ResolveAddressByName("lua_pcall") == ResolveAddress(1000), exact. Miss-path assert deferred (ready-event-and-handle-assert.md) |
| CAP-21-read | ✅ LIVE | sub-5 | `kcdx.hook` mode=mid capture READ: `c.rax:get()==seed` (name-map captures); add runs → 110 (`cap-21-mid-hook`) |
| CAP-21-write | ✅ LIVE | sub-5 | mode=mid capture WRITE: `c.rax:set(1000)` lands in the real register; add runs on 1000 → 1100 |
| CAP-21-skip | ✅ LIVE | sub-5 | mode=mid run/skip: callback returns `"skip"` → captured `add` never runs → 10 (positional-list captures `c[1]`). Proves the fresh dispatcher does NOT inherit the cap-04-c `args._skip` bug |
| CAP-21-run | ✅ LIVE | sub-5 | mode=mid control: callback returns nothing → captured `add` runs → 110 |
| CAP-22-before | ⏳ PENDING | sub-6 | `kcdx.hook` mode=callsite before: redirected E8 site, Helper sees 10→11 → 111 (`cap-22-callsite-redirect`) |
| CAP-22-after | ⏳ PENDING | sub-6 | mode=callsite after: redirected E8 site, Helper return 110 → 1110 |
| CAP-22-around | ⏳ PENDING | sub-6 | mode=callsite around: redirected E8 site, 2 * orig(10)=2*110 → 220 |
| CAP-22-replace | ⏳ PENDING | sub-6 | mode=callsite replace: redirected E8 site returns 42; Helper not called from this site |
| CAP-22-control-unaffected | ⏳ PENDING | sub-6 | ISOLATION: control caller of the SAME Helper is unchanged (110) — per-call-site, not per-callee |
| CAP-22-callee-unaffected | ⏳ PENDING | sub-6 | ISOLATION: direct Helper(10) unchanged (110) — callee untouched, only call sites rewritten |
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
