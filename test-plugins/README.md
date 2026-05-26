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
  and the aggregator emits `suite: X/Y passing` to kcdx.log
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

Once installed, every game boot writes a fresh `suite: X/Y
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

## CAP-01: `kcdx.bytes` byte rewrite (Lua) — Phase 4a pilot migration

| Field | Value |
|---|---|
| What | kcdx.bytes applies + bytes match (Phase 4a pilot migration). Replace N bytes at a named site with N other bytes via the pure-Lua `kcdx.bytes{...}` surface, located by `target="outfit_swap_callsite_aob"` (id 1004) + `offset=13` — the disassembler-test NAME locator (robust against the site already being rewritten, unlike a `pattern=` AOB over the mutated bytes). **First plugin migrated off the legacy `[[patch]]` path** — same site, same observable, new mechanism. |
| Channels | (ii) `plugin.lua` `kcdx.bytes` |
| Engine status | LIVE (Phase 4a pilot, kcdx.bytes mechanism, `target=` name locator). |
| Test plugin | [`cap-01-patch/`](cap-01-patch/) |
| Site | The outfit AOB above (named `outfit_swap_callsite_aob`, id 1004) |
| Auto-pass check | plugin.lua reports PASS when `h:applied()==true` AND an independent `kcdx.scan` read-back of the post-rewrite site returns `45 31 F6`. Asserted at `kcdx.on("ready")` after the deferred apply pass. |
| Manual confirm | In-game outfit-swap-in-combat works. [manual] |
| Last result | ✅ PASS (`1d0faf1`, live run 2026-05-25, suite 99/107): applied()=true, read-back 45 31 F6. (First pilot launch caught a pattern=-locator brittleness — cap-39 rewrites the same site first, the AOB no longer matched; fixed by switching to the target= name locator.) |
| Notes | First plugin migrated off the legacy `[[patch]]` path to `kcdx.bytes` (was: `[[patch]]` TOML + verifier DLL). The reference for every other primitive; coverage of "a byte rewrite at this site works" persists, the mechanism changes. |

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

## CAP-03: function hook via `kcdx.hook{before}` (pure Lua)

| Field | Value |
|---|---|
| What | Function hook via `kcdx.hook{before}` on a per-frame CGame::Update callee; the `before` callback bumps a counter (does not deref the `this` ptr). Asserts the hook installed (`applied()==true`) AND fired (counter>0). Phase 4b Batch 1 migration off the legacy `[[hook]]` + `lua_callback` (pak Lua) path — same un-named site (pattern AOB + `signature="void (ptr)"`, the labeled expert hatch for a target the library can't name, AP12-OK), same observable. |
| Channels | (vi) plugin Lua (`plugin.lua` only — no pak, no DLL) |
| Engine status | LIVE-PENDING (Phase 4b Batch 1) |
| Test plugin | `cap-03-hook-lua-callback/` — `plugin.lua` installs the AOB hook and reports CAP-03 from `kcdx.on("ready")`. |
| Auto-pass check | `h:applied()==true` AND `fire_count > 0` at the `ready` event (the per-frame callee fires every tick after install). |
| Last result | PENDING (Phase 4b Batch 1 launch) |
| Notes | Migrated from `[[hook]]`+pak-Lua-callback to `kcdx.hook{before}`. Same site (un-named CGame::Update callee at 0x180865FB4, KCD2 1.5.1164953), same observable (callback fires). The callback bumps a counter and never conditions the original — pure `before` mode. |

## CAP-04: mid-hook (`kcdx.hook` mode=mid) on `kcdx.code`-allocated memory

| Field | Value |
|---|---|
| What | The COMPOSITION of two author surfaces: allocate an executable stub with the Lua `kcdx.code` verb, then mid-hook INTO that allocation with `kcdx.hook` mode=mid, then call the hooked stub to observe the mid took effect on allocated code. The distinguishing axis vs cap-21: cap-21 hooks a stub from the C++ raw `AllocateFromBranchPool` floor (a lightuserdata handed to Lua); cap-04 allocates via the `kcdx.code` author verb and hooks `region:add(3)` (a `kcdx.code` pointer userdata as the `address` locator). cap-30/cap-40 allocate via `kcdx.code` but never hook the result. |
| Channels | (vi) plugin Lua (`kcdx.code` + `kcdx.hook` mode=mid) + (ii) C++ DLL companion (resolves the export, calls the hooked stub). |
| Engine status | LIVE-PENDING (Phase 4b Batch 4) — exercises the existing `kcdx.code` allocate + `kcdx.hook` mode=mid run/skip codegen + the `kcdx.code` export → `ResolveSymbolAs` interlock; no new engine code. |
| Test plugin | `cap-04-midhook/` — `plugin.lua` allocates two fresh 9-byte `kcdx.code` stubs (`mov rax,rcx; add rax,0x64; nop; ret`, `pool="branch"` for rel32 reach, `export="stub_run"`/`"stub_skip"`) and installs a mode=mid hook at +3 (the `add`) on each `region:add(3)`. The C++ companion (`cap-04.dll`) resolves each export via `ResolveSymbolAs(self, …)`, casts to `int(*)(int)`, and calls it with seed=10 on InputLoaded. Lua owns the hook because **skip is Lua-only** (the C++ `kcdxHookInterface::Mid` callback has no return-skip primitive — `hook_chain.cpp` MidCDispatch); Lua can't call a region with an arg (no pointer call method), so C++ does the call. |
| Auto-pass check | CAP-04-mid-on-code-run: stub call returns 110 (callback returns nothing → captured `add` runs on allocated code). CAP-04-mid-on-code-skip: stub call returns 10 (callback returns `"skip"` → captured `add` skipped on allocated code). A failed `ResolveSymbolAs` (kcdx.code alloc or mid install failed in Lua) reports loud FAIL — no silent PENDING. |
| Last result | ✅ PASS (`12d24b3`, live run 2026-05-26): CAP-04-mid-on-code-run → 110 (mid on the kcdx.code stub let the `add` run), CAP-04-mid-on-code-skip → 10 (mid returned `"skip"`, the `add` was skipped on the allocated stub). Both surfaces of the new mid-on-allocated-code interaction confirmed. |
| Notes | Phase 4b Batch 4: redesigned off the legacy `[[trampoline]]`+`[[mid_hook]]` schema (the LAST consumer of those parsers — unblocks the Phase 5 parser deletion). A LITERAL migration would have duplicated cap-21 (which covers the new mode=mid read/write/skip/run on a DLL-floor stub) and cap-30/cap-40 (the `[[trampoline]]` allocate path); the redesign instead tests the genuinely-new mid-hook-on-`kcdx.code`-memory composition neither covers. The legacy CAP-04a/b/c/d rows (incl. the `call_original="auto"` + `args._skip` mechanism, gone in the return-`"skip"` model) are retired with the old schema. |

## CAP-05: Runtime `dynamic_hook` from pak Lua

| Field | Value |
|---|---|
| What | Pak Lua script calls `kcdx.memory.dynamic_hook({ target=..., pre_callback=..., ... })` to install a hook at runtime. Same MinHook + JIT-detour plumbing as `[[hook]]` but driven from Lua at script-load time instead of TOML at engine-init time. |
| Channels | (i) pure pak mod, (vi) plugin Lua |
| Engine status | READY (Phase 5c.7b proved end-to-end in the verify pak — `phase5g_greet_intercept` fired 5/5 at exact shim VA) |
| Test plugin | `cap-05-paklua-runtime/` — pak mod that installs a hook and calls `kcdx.test.report` from the pre_callback. Self-owned: a companion DLL (`cap-05.dll`) registers `kcdx.cap05.probe`, which the pak hooks + calls. |
| Auto-pass check | Hook installed + callback fired ≥ 1 time during boot. |
| Last result | PENDING (test-fixture fix: retargeted off the archived hello-plugin onto cap-05's own `kcdx.cap05.probe`; pak needs rebuild). |
| Notes | This is the novel kcdx capability — Workshop-distributable code injection. Before kcdx, pak Lua had no FFI (`package.loadlib` is CryEngine-compiled-out stub). After kcdx, a pak mod can install function detours. Fixture is now self-owned (`cap-05.dll` registers `kcdx.cap05.probe`) — no dependency on the archived hello-plugin sample (test-suite.md). |

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
| Test plugin | `cap-05-paklua-runtime/` — pak script calls `cfunction_address` on cap-05's own `kcdx.cap05.probe` (registered by `cap-05.dll`), asserts pointer userdata returned. |
| Auto-pass check | Returns pointer userdata for cfunctions, nil for pure-Lua. Userdata is usable as `dynamic_hook.target`. |
| Last result | PENDING (test-fixture fix: retargeted off the archived hello-plugin onto cap-05's own `kcdx.cap05.probe`; pak needs rebuild). |
| Notes | The "find any registered Lua C function's address so I can hook it" primitive. Shares the cap-05 pak fixture; the cfunction is now self-owned (`kcdx.cap05.probe`), no hello-plugin dependency. |

## CAP-12: `kcdxSerializationInterface` (save/load co-save)

| Field | Value |
|---|---|
| What | Plugin registers `SetSaveCallback`/`SetLoadCallback`/`SetRevertCallback`. On save, kcdx fires SaveCallback; plugin writes its counter via the **named-record** API `OpenRecordNamed("counter", version)` + `WriteRecordData` (migrated in commit `9672c57` from the hand-packed FourCC `'CNTR'`). Stored in a `.kcdx` sidecar. On load, LoadCallback fires, plugin walks records and matches by name via `GetNextRecordInfo` + `GetRecordTagName` (strcmp `"counter"`) + `ReadRecordData`. The C++ half of the cross-language named-tag cosave parity (CAP-31 is the Lua half). |
| Channels | (ii) C++ DLL |
| Engine status | ✅ LIVE — `kcdxSerializationInterface` Version 2 (named records: `OpenRecordNamed` + `GetRecordTagName`, appended in commit `64567d9`; original interface Phase 6b, 2026-05-19). |
| Test plugin | `cap-12-serialization/` (C++ DLL) — keeps its explicit C++ `kUID` (a legitimate expert path; C++ name-derived UID is tracked Phase 3 parity debt in `docs/cpp/cosave.md`). |
| Auto-pass check | Plugin writes counter on save, reads it on load, value persists across game restarts. Manual sequence: load any save → quicksave → quit → relaunch → load the quicksave. Pass = the load round-trip reads `counter=N` back via `GetRecordTagName`-matched record with N matching the OnSave count before quit. Registration auto-passes at boot. |
| Last result | ✅ LIVE (2026-05-22, `9672c57`): named-record registration PASS (uid=0x53323143); round-trip confirmed alongside CAP-31 in the same `64/65` run — the named-record migration preserved the counter persistence. Original FourCC path: LIVE 2026-05-19, 44-byte cosave decoded as magic + uid + chunk + counter (u64). |
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
| Auto-pass check | The helper (running from a require'd source) SUBSCRIBES at load to `ts.cap_25_multifile_attribution.multifile_event` and PUBLISHES the bare `multifile_event` from a DEFERRED `input_loaded` callback. The engine stamps the published bare event under the publisher's RESOLVED owner. If the helper's source is attributed to the plugin (fix works), the stamp is `ts.cap_25_multifile_attribution.multifile_event` → matches the subscription → the callback FIRES with `payload.marker=="CAP25_OK"` → PASS. If the source resolves to `"<anon>"`, the stamp is `<anon>.multifile_event` → no match → the callback never fires → row stays PENDING/FAIL. A firing callback with the right payload is unforgeable proof the require'd helper's `kcdx.*` calls resolved to the plugin. No player input. |
| Last result | ✅ LIVE (kcdx-dev 14:32, suite 47/53; `require` resolved `helper.lua` to the plugin's own folder, publish from the deferred callback stamped `ts.cap_25_multifile_attribution.multifile_event` → 1 subscriber → PASS). Re-verify at the next checkpoint after the step-6 require-mechanism change (`package.loaders` → kcdx require closure). |
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
| Auto-pass check | **`CAP-30-alloc`** (synchronous at plugin load — `kcdx.code` allocates immediately): `kcdx.code{ name="cap30_region", size=64 }` returns a live pointer; the accessors take the VALUE only (no offset arg), and `:add(N)` navigates to an offset — `region:set_byte(0xAB)` (at base) + `region:add(4):set_dword(0xDEADBEEF)` (at offset 4, clear of the byte) round-trip through `region:get_byte()==0xAB` + `region:add(4):get_dword()==0xDEADBEEF` (write/read-back proves the region is real + writable — a nil/non-writable region fails the read-back). Folds in the NOP-pad: `kcdx.code{ name="cap30_nop", bytes="C3", size=8 }` → `get_byte()==0xC3` (code head) AND `add(4):get_byte()==0x90` (NOP-padded tail). **`CAP-30-export`** (asserted in `kcdx.on("ready")`, after the apply pass): `kcdx.code{ name="cap30_exported", size=16, export="ts.cap_30_lua_code.region" }` publishes the symbol; a deferred `kcdx.bytes{ target_symbol="ts.cap_30_lua_code.region", original="90", replacement="90" }` (NOP-over-NOP, same-length, idempotent) resolves it at the apply pass. PASS iff the `kcdx.bytes` handle `:applied()==true` — proving `target_symbol` resolved the export to the live region. A never-resolving export → `:applied()==false`/nil → loud FAIL with the reason. No player input. |
| Last result | ⏳ PENDING (boot auto-pass; verified at the checkpoint launch) |
| Notes | Export-interlock proof rationale: there is NO Lua surface that resolves the runtime symbols table (`kcdx.addr` is the Address Library snapshot built once at startup, NOT the `symbols::Register` table that `export` writes to), so the interlock is proven via a `target_symbol` consumer. `kcdx.bytes` is cleaner than `kcdx.hook` for an arbitrary code region: a hook needs a meaningful function entry, but a same-length byte rewrite needs only live writable memory (which the region is), so `:applied()==true` is a real, non-contrived proof the export resolved (`docs/lua/code.md` names `kcdx.hook`/`kcdx.bytes{ target_symbol }` as the export consumers). Split into two rows for diagnosability: `CAP-30-alloc` (the synchronous core — alloc + writable + NOP-pad) and `CAP-30-export` (the deferred cross-feature interlock). |

## CAP-31: `kcdx.cosave.*` Lua save/load persistence (the Lua counterpart of CAP-12)

| Field | Value |
|---|---|
| What | The `kcdx.cosave.*` Lua surface — the Lua mirror of the C++ `kcdxSerializationInterface` (CAP-12). `on_save(fn)` / `on_load(fn)` register the bodies that run inside the engine's open writer / reader windows; `write(tag, version, value)` writes one record (string tag → `OpenRecordNamed`, value → the standalone Lua value codec `src/lua_cosave_serial.cpp`); `records()` yields `for tag, ver, val in ...` on load; `set_uid` is the advanced override. The headline UX: the UID auto-derives from the plugin name (FNV-1a, single-sourced with the C++ `HashTag`) — the author hand-packs no FourCC (the disassembler-test win). |
| Channels | (vi) plugin Lua. C++ PARITY: `kcdxSerializationInterface` already ships the C++ cosave path and CAP-12 (`cap-12-serialization`) exercises it from a DLL — so `kcdx.cosave.*` is the Lua mirror of an already-shipped C++ capability over the SAME engine cosave path (`src/serialization.{h,cpp}` untouched). No C++ cosave work owed; the binder is a thin Lua surface. |
| Engine status | ✅ LIVE (kcdx.cosave feature) — binder `src/lua_bind_cosave.cpp` + value codec `src/lua_cosave_serial.cpp` over the existing `kcdxSerializationInterface`. This row is the regression test only. |
| Test plugin | [`cap-31-cosave/`](cap-31-cosave/) — pure Lua, single `plugin.lua`. NEVER calls `set_uid` (proves the auto-derived UID). |
| Auto-pass check | **`CAP-31-outside-window`** (BOOT-ONLY, synchronous at load): a `kcdx.cosave.write("outside_probe", 1, 123)` at `plugin.lua` top-level — OUTSIDE any `on_save` body, so the writer window is closed — returns `(nil, err)` and `err` mentions writing `outside` the `on_save` body / the `save window` (asserted against the binder's ACTUAL `Lua_Write` text). The standing regression guard that the window guard refuses an out-of-window write instead of silently dropping data. No player input. |
| Manual confirm | **`CAP-31-roundtrip`** [manual]: in `on_save` write one record per supported type — `count`=`3.5` (a non-integer float, exact under `lua_Number`=float), `label`=`"Henry"` (string), `flag`=`true` (boolean), `state`=`{ hp=100, name="Henry", flags={ brave=true } }` (nested table). In `on_load`, iterate `records()` and assert each round-tripped EXACTLY (`count==3.5`, `label=="Henry"`, `flag==true`, `state.hp==100`, `state.name=="Henry"`, `state.flags.brave==true`). PASS iff all types round-tripped — and since cap-31 never calls `set_uid`, the section persisting + reloading at all is the auto-derived-UID proof. NO-COSAVE-YET first boot does NOT false-FAIL: `on_load` reports only when records were actually present (a save made WITH cap-31 loaded); a fresh game / first load with no cap-31 cosave leaves the row PENDING (mirrors CAP-12's "Revert fired, no cosave yet"). **`CAP-31-reject`** [manual]: inside `on_save`, after the valid writes, two NEGATIVE writes the serializer must reject — a function value (`err` mentions `function`) and a cyclic table (`err` mentions `cyclic`) — each returns `(nil, err)` without aborting the valid save. PASS iff both rejections fired with the right error substrings. **Reported at SAVE time** — at the end of `on_save`, the instant the rejected writes are attempted, NOT at load. The rejection is in-process state observable the moment `write()` is called in-window, not persisted cosave data, so the assertion lives in the callback that produces it (reporting it from `on_load` false-FAILED on a load-after-reboot, where `on_save` never ran). Still [manual] — it needs a save gesture to enter the window — but it no longer needs the load half. **Gesture: `CAP-31-roundtrip` needs save (dev mode on), quit, reboot, load; `CAP-31-reject` is confirmed by the save alone.** |
| Last result | ✅ LIVE (2026-05-22, `b8602e6`): all three PASS. `CAP-31-outside-window` boot auto-pass; `CAP-31-reject` PASS at save time (both negative writes rejected inside `on_save`); `CAP-31-roundtrip` PASS after save+reboot+load (count==3.5 float-exact, label, flag, nested state all reconstructed via the auto-derived UID, no set_uid). Suite `64/65` (sole failure the pre-existing, unrelated CAP-04c). v1-ABI compat held — the append-only Version 1→2 interface bump broke no pre-built plugin. |
| Notes | The Lua counterpart of the C++ CAP-12 (`cap-12-serialization`). Cross-language parity is **same-mechanism-both-surfaces**, NOT same-record-cross-plugin: records are scoped per-plugin (per uid), so two DIFFERENT plugins cannot share a record — a shared "counter" across cap-12 and cap-31 would require a shared uid, the FourCC-collision foot-gun the auto-UID model exists to remove. The testable parity is the named-tag MECHANISM round-tripping on BOTH surfaces — CAP-12 (C++ `OpenRecordNamed` + `GetRecordTagName`) and CAP-31 (Lua `write` + `records()`) each independently prove their half of the one capability (`lua-api-surface.md`: same capability, both surfaces). `set_uid` itself is covered by `docs/lua/cosave.md` + the binder's range/zero validation; a dedicated `set_uid` round-trip is a [manual] nicety not built here (the auto-UID path is the common case and is what cap-31 proves). |

## CAP-32: `kcdx.scan` diagnostic AOB scan (Lua top-level verb)

| Field | Value |
|---|---|
| What | The `kcdx.scan` top-level Lua verb — a thin binder over the existing, proven `scan_engine::ResolveScan` (the same locator pipeline `[[scan]]` / `[[patch]]` use; no engine change). `kcdx.scan{ name=, pattern=, module?, offset?, context?, anchor_*?, max_anchor_distance? }` resolves the pattern, logs a concise diagnostic, and **ALWAYS RETURNS a table** on a resolved scan: `{ count = <int>, matches = { { addr=<kcdx.memory.pointer>, module=<string>, offset=<int, module-relative> }, ... }, addr = <first match's addr, or nil when count==0> }`. The hand-written `pattern` is the LABELED expert AOB hatch (kcdx.scan IS the dev-time tool an expert uses to discover an address they then NAME) — by-design here, not an AP12 hex-burden defect. |
| Channels | (vi) plugin Lua. The Lua `kcdx.scan` verb is the surface over `scan_engine::ResolveScan` (`src/scan_engine.{h,cpp}`); it superseded the legacy `[[scan]]` TOML entry (retired with its scan-demo showcase — `kcdx.scan` is the supported surface). |
| Engine status | ✅ LIVE (kcdx.scan feature, the binder `src/lua_bind_scan.cpp` over `scan_engine::ResolveScan` — sub-1 `6cb1e98`, sub-2 `0cb2295`). No engine change; this row is the regression test only. |
| Test plugin | [`cap-32-scan/`](cap-32-scan/) — pure Lua, single `plugin.lua`. All three rows resolve + report synchronously at plugin load (boot-only). |
| Auto-pass check | **`CAP-32-resolve`**: `kcdx.scan{ name="cap32_outfit_swap", pattern="48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0", module="WHGame.dll" }` (the LIVE-PROVEN unique outfit-swap pattern — `WHGame.dll+0x56174C`, formerly the scan-demo showcase site, now retired) → asserts `count==1`, `addr~=nil`, `matches[1]` present, `matches[1].module=="WHGame.dll"`, `matches[1].addr~=nil`, `type(matches[1].offset)=="number"`. Does NOT assert a per-build absolute address — count/module/non-nil-addr are the falsifiable resolve proof. **`CAP-32-nomatch`**: a bogus 20-byte pattern (`DE AD BE EF ...`, cannot match) → asserts `type(r)=="table"`, `count==0`, `addr==nil`, `#matches==0` — the always-a-table contract (no-match is a count==0 RESULT, NOT a nil return / NOT an error). **`CAP-32-badinput`**: `kcdx.scan({})` (a table, but missing the required `name`+`pattern`) → asserts `r==nil`, `type(err)=="string"`, and `string.find(err, "name", 1, true)~=nil` (the binder checks `name` first and its teaching error names the missing `name` field) — bad input is `(nil, err)`, NOT a table. No player input. |
| Last result | ✅ LIVE (2026-05-22, `76c7eda`): all three PASS. `CAP-32-resolve` resolved `cap32_outfit_swap` to `WHGame.dll+0x56174C` (count==1, attributed module, non-nil pointer); `CAP-32-nomatch` count==0/addr==nil/empty-matches; `CAP-32-badinput` (nil, err) naming `name`. The scan_engine refactor regressed nothing — the `[[scan]]` TOML path (`outfit_swap_site`) still emits its exact documented `matches: 1` + byte-dump output in the same run. |
| Notes | The Lua-surface regression for `kcdx.scan` — and now the SOLE scan regression: the legacy `[[scan]]` TOML entry + its scan-demo showcase were retired (cap-32 covers the `kcdx.scan` surface that stays; `[[scan]]` is deleted in Phase 5). `CAP-32-resolve` uses the live-proven unique outfit-swap pattern + module (`WHGame.dll+0x56174C`) so the resolve assertion rests on an already-verified site, not a fresh RE guess. The three rows pin the binder's two return contracts: the always-a-table result (`CAP-32-resolve` count==1 + `CAP-32-nomatch` count==0/addr==nil/empty-matches) vs. the `(nil, teaching-err)` bad-input path (`CAP-32-badinput`). Module-not-loaded is also a count==0 result (not asserted here — WHGame.dll is always loaded; the no-match row covers the count==0 contract). Cross-plugin opt-in scanning (scan the game + opted-in plugin DLLs in one call) is deferred to its own feature — tracked in [`docs/outstanding-work/cross-plugin-scan.md`](../docs/outstanding-work/cross-plugin-scan.md). |

## CAP-33: author-declared named targets (`targets.toml`) + `kcdx.alias`

| Field | Value |
|---|---|
| What | The author-declared named-target surface — the disassembler-test "share guarantee" (`cornerstones.md` §36). A plugin declares `[[target]]` rows in a `targets.toml` sidecar (bare `name` + one locator of `pattern`/`rva`/`address_id`/`target_symbol` + optional `signature`); the engine stamps the `<pluginname>` prefix from `[plugin].name`. The name then resolves from `kcdx.hook{ target=<name> }` / `kcdx.bytes{ target=<name> }` by self > engine > other precedence, incl. a `pattern` target resolved BY NAME end-to-end (address from the AOB via the patch engine, ABI from the target's `signature`). The explicit `"<pluginname>.<name>"` form is unambiguous; `kcdx.alias(short, "plugin.name")` declares a plugin-scoped local handle. |
| Channels | (vi) plugin Lua. C++ PARITY: the C++ author-target / alias registration interface (`kcdxTargetInterface`) + the namespaced `ResolveAddressByNameAs(handle, name)` overload are restructure parity-debt (NYI mirror in `docs/cpp/targets.md` + `docs/cpp/alias.md` + `docs/cpp/planned.md`), built in the C++ phase. |
| Engine status | ✅ LIVE (author-targets feature, committed steps 2–6) — `src/target_manifest.cpp` (the `targets.toml` parser), `address_library::{RegisterAuthorTarget,ResolveByName,ResolveSignatureByName,FindResolvedAuthorTarget}`, `hook_chain::ResolveLocator` pattern/symbol-by-name routing, `kcdx.bytes` `target=` resolution, `src/lua_bind_alias.cpp` (`kcdx.alias`). This row is the regression test only. |
| Test plugin | [`cap-33-author-targets/`](cap-33-author-targets/) — pure Lua + `targets.toml`. Declares a `pattern` target with a `signature` (`openlibs_by_pattern`, the verified `.text`-unique 16-byte luaL_openlibs entry AOB, seed id 1190 — the §36 pattern-by-name row), a verified-`address_id` target (`luaopen_math_by_id`, id 1172 — a DISTINCT verified leaf that NOTHING entry-hooks, called once at Lua init; by RVA, no scan) for the prefix + alias proofs, and a verified-`address_id` bytes target (`bool_leaf_safe_site`, lua_toboolean id 1124 — a DISTINCT verified leaf that NOTHING hooks, pristine prologue). |
| Auto-pass check | All five rows assert `:applied()` at `kcdx.on("ready")` (boot-only, no player input). **`CAP-33-pattern-by-name`** (THE §36 HEADLINE): `kcdx.hook{ target="openlibs_by_pattern" }` with NO `signature=` — the pure "author names a target BY AOB PATTERN and hooks it by name" proof. The author-declared PATTERN target (luaL_openlibs id 1190, a `.text`-unique 16-byte entry AOB confirmed by `_research/phase8-fix-a/aob_scan.py`) resolves the ADDRESS and the target's `signature` carries the ABI, both from the bare name with zero hex at the call site. Resolves end-to-end because luaL_openlibs is entry-hooked by NOBODY (pristine prologue → the by-name scan finds it). **`CAP-33-engine-tier`**: `kcdx.hook{ target="kcdx.luaL_loadfile" }` (engine seed under reserved `kcdx` author, 2-segment explicit form) resolves — engine tier coexists with the author's own targets. **`CAP-33-prefixed`**: the explicit `"ts.cap_33_author_targets.luaopen_math_by_id"` (3-segment `<author>.<plugin>.<bare>`, address_id=1172, RVA — unhooked verified leaf) resolves directly. **`CAP-33-alias`**: `kcdx.alias("up", "ts.cap_33_author_targets.luaopen_math_by_id")` then `kcdx.hook{ target="up" }` resolves via the alias. **`CAP-33-bytes-by-name`**: `kcdx.bytes{ target="bool_leaf_safe_site", original="48", replacement="48" }` (address_id=1124; idempotent no-op — byte 0 of the lua_toboolean entry is 0x48, read from WHGame.dll; a DISTINCT verified leaf NOTHING hooks → pristine prologue, original-verify correct) → `:applied()==true` proves the author-target resolved to a writable VA. |
| Last result | ✅ PASS 6/6 (kcdx@03e6bd0, 2026-05-23, suite 66/74) — engine-tier, prefixed, alias, bytes-by-name, COMP-12-self-wins, AND `CAP-33-pattern-by-name` (the §36 headline). The §36 row was unblocked by minting a verified `.text`-unique entry AOB for the unhooked luaL_openlibs (seed id 1190) and repointing the pattern row to it; confirmed in-game (`RESULT name=CAP-33-pattern-by-name verdict=PASS`). The whole author-targets feature is in-game-verified. |
| Notes | The whole-feature regression for author-declared targets. `CAP-33-pattern-by-name` is the cornerstones §36 proof (a pattern site named ONCE hookable BY NAME with zero hex/ABI, the shareability guarantee). The blocker — no verified `.text`-unique entry AOB for an UNHOOKED function — was resolved 2026-05-23: `_research/phase8-fix-a/aob_scan.py` minted + uniqueness-confirmed luaL_openlibs's 16-byte entry AOB (id 1190; entry-hooked by nobody, so the prologue stays pristine for the by-name scan, vs the prior id-1003 target cap-03 overwrites). See [`docs/outstanding-work/section36-pattern-target-aob.md`](../docs/outstanding-work/section36-pattern-target-aob.md) (RESOLVED). NEW plugins use VALID bare `[plugin].name` (`cap_33_author_targets`, charset `[a-z0-9_]`) under `[plugin].author = "ts"` — the target/alias registry keys on the full triple `<author>.<plugin>.<bare>` (`naming-namespaces.md`). |

## CAP-34: 2-dot namespace model (`<author>.<plugin>.<bare>`)

| Field | Value |
|---|---|
| What | The 2-dot namespace refactor: every shared name is identified as `<author>.<plugin>.<bare>` (three components, two dots). `[plugin].author` + `[plugin].name` are the manifest source of truth; the resolver keys every shared surface (targets, aliases, exports, pub/sub events, cosave records) on this triple with bare-name self > engine > other precedence and a reserved `kcdx` engine author. The explicit forms are unambiguous by dot count: 1 dot `kcdx.<seedname>` (engine seed under reserved root), 2 dots `<author>.<plugin>.<event>` (plugin-identity surfaces — pub/sub, aliases), 3 dots `<author>.<plugin>.<bare>` (cross-plugin exports). |
| Channels | (vi) plugin Lua. C++ PARITY: mirrors CAP-33's namespacing under the same C++ author-target / alias interface (`kcdxTargetInterface`) once it lands; no new C++ surface is owed by THIS row — the model itself is parity-shared with CAP-33's mirror. |
| Engine status | ✅ LIVE (2-dot namespace refactor steps 1–6) — `[plugin].author` split (step 1), resolver triple keying (steps 2–4), bare-name self > engine > other under reserved `kcdx` author (steps 4–5), pub/sub events stamped as `<author>.<plugin>.<event>` (step 5), corpus re-migration (39 test plugins + builtin + hello-plugin all carry `author='ts'`/`'kcdx_builtin'`/`'violetanvil'`, step 6). This row is the regression test only. |
| Test plugin | [`cap-34-two-dot-namespace/`](cap-34-two-dot-namespace/) — pure Lua + `targets.toml`. Declares ONE bare author-target (`ui_pump_self`, registered as `ts.cap_34_two_dot_namespace.ui_pump_self`, locating luaopen_math by `address_id=1172` — the same verified-unhooked leaf cap-33's prefix/alias rows use; RVA-resolved, no scan; entry-hooked by nobody). Five hook installs reference the target via the resolver tiers under test. |
| Auto-pass check | All five rows assert `:applied()` at `kcdx.on("ready")` (boot-only, no player input). **`CAP-34-explicit-2dot`**: `kcdx.hook{ target="ts.cap_34_two_dot_namespace.ui_pump_self" }` — explicit 3-segment `<author>.<plugin>.<bare>` form resolves directly. **`CAP-34-explicit-1dot-kcdx`**: `kcdx.hook{ target="kcdx.luaL_loadfile" }` — the 2-segment `kcdx.<seedname>` form (engine seed under reserved `kcdx` author) resolves. **`CAP-34-bare-self`**: `kcdx.hook{ target="ui_pump_self" }` (BARE — no prefix typed) resolves to this plugin's own target via the self-tier. **`CAP-34-alias-2dot`**: `kcdx.alias("short", "ts.cap_34_two_dot_namespace.ui_pump_self")` then `kcdx.hook{ target="short" }` resolves via alias substitution. **`CAP-34-cross-plugin-2dot`**: `kcdx.hook{ target="ts.cap_33_author_targets.luaopen_math_by_id" }` — a 3-segment reference to ANOTHER plugin's target (cap-33's verified-id row) resolves via the other-plugin tier. |
| Last result | ⏳ PENDING — boot-only assertions; verified at the next checkpoint launch. |
| Notes | The whole-feature regression for the 2-dot namespace model. Distinct from CAP-33 (which proves author-declared TARGETS land end-to-end with one-segment legacy naming + alias) by exercising the dot-depth-aware explicit forms (1/2/3 dots) AND bare-name self-tier under the new `<author>.<plugin>.<bare>` keying. Cross-plugin row depends on cap-33-author-targets being installed in the suite (always the case); in isolation it fails gracefully via `:applied()==false` with no impact on the other four. Target choice (luaopen_math, id 1172) is the established prior art from cap-33's prefix/alias rows: a verified leaf NOTHING entry-hooks, RVA-resolved, immune to prologue-overwrite — the install IS the proof the name resolved, the hook never needs to fire. Multiple before-hooks on the same address are an established pattern (CAP-20-chain). |

## CAP-36: `kcdxHookInterface` (C++ mirror of `kcdx.hook.*`) end-to-end (Phase 3 sub-1 step 5-main, chunk 5)

| Field | Value |
|---|---|
| What | The C++ DLL author's path to the `kcdx.hook.*` model via `kcdxHookInterface` v1 — the six sub-verb install methods (`Before`/`After`/`Around`/`Replace`/`Mid`/`Callsite`), the four query methods (`IsApplied`/`GetReason`/`GetName`/`Uninstall`), the typed-per-mode callback ABIs the engine's `BuildCDispatchThunk` JIT emits (`src/dynamic_call_jit.cpp:361-429`; **Before**: `void cFn(uintptr_t args[], int* outCount, /* typed args */)` with args[]/outCount the mutation back-channel + the typed args read-only pass-through; **After non-void**: `<typed_return> cFn(<typed_return> origReturn, /* typed args */)`; **After void**: `void cFn(/* typed args */)`; **Around**: `<typed_return> cFn(<typed call_original>, /* typed args */)` with `call_original` arriving as a pointer-width register the C author calls via a typedef'd typed function pointer; **Replace**: `<typed_return> cFn(/* typed args */)` with no prepended args), and the `opts.owningPlugin` identity threading authors do by hand (no wrapper) so the self > engine > other-plugin precedence resolves the calling plugin (`naming-namespaces.md`). The disassembler test (`cornerstones.md` / AP12): for a DLL-internal stub target the engine carries no name, so this plugin uses the labeled `[advanced]` `opts.address` locator + an explicit `opts.signature` — the right tier for a self-defined target (declare once, no shareability owed). CAP-36 is the C++ peer of cap-35 (Uninstall) + cap-20 (modes) + the chain-coexistence proof for the chunk-1 ChainEntry tagged union (Lua + C entries on one chain). |
| Channels | (ii) C++ DLL. C++ PARITY: this row IS the C++ parity for `kcdx.hook.*` (`docs/cpp/hook.md`); the Lua surface is at parity per cap-20 / cap-21 / cap-22 / cap-33 / cap-34 / cap-35. |
| Engine status | LIVE (chunks 1-4 wired the ABI end-to-end: `b629e14` `kcdxHookInterface` ABI v1 in `include/kcdx/Interfaces.h` + the 10 thunks in `src/hook_interface.cpp` + the per-mode codegen in `src/dynamic_call_jit.cpp::BuildCDispatchThunk` + `AddC`/`AddCMid`/`AddCCallsite` chain routing; `0c0756a` + earlier the registry handle + `Uninstall` engine path; chunk 5 IS the verification). |
| Test plugin pair | [`cap-36-cpp-hook-interface/`](cap-36-cpp-hook-interface/) (the C++ DLL — owns all 7 rows + the assertions) + [`cap-36-cpp-hook-interface-lua/`](cap-36-cpp-hook-interface-lua/) (the Lua sibling — owns the Lua half of the crosslang chain; depends on the C++ plugin via `[[plugin.dependencies]]` so the C++ plugin loads first and the kcdx.cap36.* Lua functions are reachable). The C++ plugin uses load-order priority 30; the Lua sibling priority 70 (C++ before fires first, Lua before second, the chain order is value-distinguishable). |
| Auto-pass check | All 7 rows assert at `kcdxPlugin_PostGameLoad` (the after_game C++ export — `src/hooks.cpp:455`, after `ApplyZone(AfterGame)`:440 and before `FireEngineMessage(InputLoaded)`:462; every hook is LIVE by then). The C++ plugin defines a noinline stub per row (unique-tag-volatile-read defeats `/OPT:ICF`, belt-and-suspenders with the CMakeLists `/OPT:NOICF` link flag — the cap-20 prior art), installs a hook on each via `opts.address = (uintptr_t)&stub` + `opts.signature = "i32 (i32 seed)"` + `opts.owningPlugin = g_self`, re-invokes each stub from PostGameLoad, and asserts the observed return value matches the row's PASS contract. **`CAP-36-cpp-hook-before`**: `Cap36_Add_Before(10)==111` (typed Before ABI: cb writes `args[0]=seed+1`, `*outCount=1`; original +100 → 11+100=111). **`CAP-36-cpp-hook-after`**: `Cap36_Add_After(10)==1110` (typed After non-void ABI: cb receives origReturn=110 and returns origReturn+1000). **`CAP-36-cpp-hook-around`**: `Cap36_Add_Around(10)==220` (typed Around ABI: cb receives a typed `call_original` fn pointer + seed=10, returns 2*call_original(10)=2*110). **`CAP-36-cpp-hook-replace`**: `Cap36_Add_Replace(10)==42` (typed Replace ABI: original never runs; cb returns 42). **`CAP-36-cpp-hook-uninstall`**: two-phase — pre: `IsApplied==1`, `Cap36_Add_Uninstall(10)==5110` (cb writes seed+5000; +100=5110); after `Uninstall(handle)`: `IsApplied==0`, `Cap36_Add_Uninstall(10)==110` (un-hooked, callback no longer fires). The C-side peer of `CAP-35-uninstall-basic`. **`CAP-36-cpp-hook-raw-floor`**: same shape as the Before row but the install + query interfaces both come from a fresh `api->QueryInterface(kcdxInterface_Hook, kcdxHookInterface_Version)` rather than the cached pointer — proves the floor-4 contract (raw interface works without `Kcdx.h` or any helper layer). Observed value `Cap36_Add_RawFloor(10)==111`. **`CAP-36-cpp-hook-crosslang`**: value-distinguishable proof that Lua + C entries coexist on one chain — `Cap36_Crosslang(10)==122 = ((seed+1)*2)+100` (C++ before adds 1 first by load-order priority 30, Lua before then multiplies by 2 by priority 70, stub adds 100). Uniquely distinguishable from C++-only (111), Lua-only (120), reversed order (121), and no hooks (110). Corroborated by a Lua-side flag (`g_lua_hook_fired`) AND the Lua callback's observed seed (must equal 11, proving the Lua entry sees the post-C++ mutated arg, hence both entries on the SAME chain at the SAME site). The PROOF the chunk-1 ChainEntry tagged union was worth building. An InputLoaded backstop listener (the cap-29 design) reports a loud FAIL on every row if PostGameLoad never fired, so a missing after-phase export never leaves silent PENDING. No player input. |
| Last result | ⏳ PENDING (boot auto-pass; verified at the checkpoint launch) |
| Notes | The C++ parity proof of the kcdx.hook model — first non-Lua-binder consumer of the `kcdxHookInterface` v1 surface. cap-36 is the PURE RAW-FLOOR regression net: all 7 rows install through the raw `g_hook->Before/After/Around/Replace/...` with hand-written mangled cFn callbacks + explicit `opts.signature = "i32 (i32 seed)"` + `opts.owningPlugin = g_self` (the raw-floor row specifically proves this works WITHOUT a wrapper that would stash it for the author). It does NOT include or use `Kcdx.h` — the WRAPPER is tested wholly by **CAP-37**. The raw `opts.address` locator is the right tier here because the stubs are DLL-internal (engine carries no name; declare-once / no shareability owed — `cornerstones.md` §"author-declared targets are SHAREABLE" applies to cross-plugin author-targets, not a plugin's own stubs). Stub-target idiom + ICF defeat + PostGameLoad-re-invoke pattern lifted from cap-20 / cap-21 / cap-22 / cap-29 (the established prior art). |

## CAP-37: `Kcdx.h` empowered wrapper end-to-end (Phase 3 sub-1 step 6)

| Field | Value |
|---|---|
| What | The C++ `include/kcdx/Kcdx.h` empowered wrapper over the raw `kcdxHookInterface` (CAP-36). The author writes a NATURAL callback typed in the original target's signature; the wrapper's per-mode adapter codegen carries the mangled cFn ABI (`uintptr_t args[], int* outCount` for Before, the origReturn-prepend for After, the typed `call_original` for Around) and the `sig_traits<R(Args...)>` type→DSL trait derives `opts.signature` from `<Sig>` on the no-name (raw-address) path (B2). Six rows exercise the four sub-verb helpers (`kcdx::hook::Before/After/Around/Replace<Sig,&fn>` via their `Try*` forms), the `Try*` handle return, and the type→DSL trait on multiple arg types. This is the C++ AP12 / disassembler-test win (`cornerstones.md`): the mangled cFn ABI is the engine's heavy lifting, hidden behind the natural callback. |
| Channels | (ii) C++ DLL. C++ PARITY: this is C++ template sugar over `kcdxHookInterface` — single-surface (`docs/cpp/wrapper.md`), no Lua mirror owed (Lua's dynamic marshaling means the Lua author never sees a mangled cFn ABI, so there is nothing for a kcdx interface to hide on the Lua side; the capability — typed hooks — is at full parity through the raw `kcdxHookInterface` / `kcdx.hook.*`). |
| Engine status | LIVE (header-only consumer-side sugar — `b5e548a` shipped `include/kcdx/Kcdx.h` over the LIVE `kcdxHookInterface` v1; engine DLL unchanged). |
| Test plugin | [`cap-37-kcdx-wrapper/`](cap-37-kcdx-wrapper/) — native C++ DLL. Self-contained typed stubs (one per row, `/OPT:NOICF` + per-stub unique tag), `kcdxPlugin_Load` installs all 6 hooks via the wrapper, `kcdxPlugin_PostGameLoad` re-invokes each stub + asserts. The wrapper is behavior-preserving sugar, so the 4 sub-verb rows produce the SAME observable values cap-36's raw rows do (111 / 1110 / 220 / 42). |
| Auto-pass check | All 6 rows assert at `kcdxPlugin_PostGameLoad` (the after_game export — every hook LIVE by then; the cap-36 pattern). **`CAP-37-wrapper-before`**: NATURAL `void(int& seed){ seed += 1; }` → `Cap37_Before(10)==111` (the wrapper's by-ref write-back + auto `*outCount`). **`CAP-37-wrapper-after`**: NATURAL `int(int origReturn, int seed)` returns `origReturn+1000` → `Cap37_After(10)==1110` (non-void After origReturn-prepend). **`CAP-37-wrapper-around`**: NATURAL `int(int(*call_original)(int), int seed)` returns `2*call_original(seed)` → `Cap37_Around(10)==220` (typed call_original pass-through). **`CAP-37-wrapper-replace`**: NATURAL `int(int seed){ return 42; }`, original never runs → `Cap37_Replace(10)==42`. **`CAP-37-wrapper-try-handle`**: `TryBefore<int(int), &cb>` returns a non-zero `kcdxHookHandle` AND `K.hook->IsApplied(h)==true` after the apply pass (the wrapper's Try\* handle path, vs the void `Before` form). **`CAP-37-wrapper-typemap`**: install over `int(int, float, void*)` — the wrapper derives `i32 (i32, f32, ptr)` from `<Sig>`; by-ref cb mutates the i32, f32 + ptr survive marshaling → `Cap37_TypeMap(10, 5.0f, &sentinel)==1016` = `(10→11) + (int)5.0f + 1000-for-non-null-ptr`. An InputLoaded backstop (the cap-29 design) reports a loud FAIL on every row if PostGameLoad never fired. No player input. |
| Last result | ⏳ PENDING (boot auto-pass; verified at the checkpoint launch) |
| Notes | The AP7 close-out for the `Kcdx.h` wrapper (step 6 shipped the wrapper but satisfied its bar by migrating cap-36's 4 rows onto it, which deleted cap-36's raw-floor coverage of After/Around/Replace and left the wrapper's own machinery — the type→DSL trait, the by-ref Before write-back, the void-vs-non-void After split, the typed Around `call_original`, the Try\* handle path — with no falsifiable row). This plugin grows the suite by 6 wrapper rows; cap-36 reverts to the pure raw floor. The `typemap` row is the catch for a future `sig_traits` / `dsl_token` regression: a wrong i32/f32/ptr→token mapping fails the no-name install or mis-marshals a slot, perturbing the observed 1016. Stub-target idiom + ICF defeat + PostGameLoad-re-invoke pattern from cap-36 / cap-20 / cap-29. |

## CAP-39: `kcdxBytesInterface` (C++ mirror of `kcdx.bytes`) end-to-end (Phase 3 sub-2 step 4)

| Field | Value |
|---|---|
| What | The C++ DLL author's path to the `kcdx.bytes` model via `kcdxBytesInterface` v1 — the single `Register(const kcdxBytesOptions*) -> kcdxBytesHandle` install method (ONE operation, not sub-verbs: a byte rewrite writes `replacement` at a located site) + the four query methods (`IsApplied`/`GetReason`/`GetName`/`Uninstall`), the exactly-one-locator + replacement-required + original==replacement-length validation, the `target = "<name>"` common-path name-resolution (the disassembler test, `cornerstones.md` / AP12 — a NAME, not hex), and the deferred-apply contract (a non-zero handle is registered, not yet applied; the `VirtualProtect`+`memcpy` defers to the end-of-zone apply pass so the conflict engine sees every plugin's intent). The `opts.owningPlugin` identity threading authors do by hand drives self > engine > other resolution (`naming-namespaces.md`). Uninstall is the one divergence from `kcdxHookInterface`: a byte rewrite has NO revert path (the original bytes aren't retained), so `Uninstall` returns false + teaches rather than reverting — flipping status while the rewrite stays live would be AP13. The coexist decision: `kcdxBytesInterface` = the DEFERRED, locator-based, conflict-resolved registration surface; `kcdxMemoryInterface::WriteBytes` = the IMMEDIATE raw-write floor (both built, documented as peers in `docs/cpp/bytes.md`). |
| Channels | (ii) C++ DLL. C++ PARITY: this row IS the C++ parity for `kcdx.bytes` (`docs/cpp/bytes.md`); the Lua surface is at parity per **cap-01** (the `[[patch]]`/byte-rewrite baseline) + cap-33-bytes-by-name + cap-30-export. Both surfaces of the byte-rewrite capability now ship a regression (parity-is-tested, `lua-api-surface.md`). |
| Engine status | LIVE (steps 1-3 wired the ABI end-to-end: `90fd1cf` `kcdxBytesInterface` ABI v1 in `include/kcdx/Interfaces.h` (header-only); `14a0333` `src/bytes_interface.cpp` engine impl + `QueryInterface(kcdxInterface_Bytes)` wire; `16f0c98` `K.bytes` accessor on `Kcdx.h`. This plugin (step 4) is the verification). |
| Test plugin | [`cap-39-cpp-bytes/`](cap-39-cpp-bytes/) — native C++ DLL. `kcdxPlugin_Load` Inits the `K` wrapper + registers ONE byte rewrite via `K.bytes->Register`; `kcdxPlugin_PostGameLoad` (the after_game export, after `ApplyZone(AfterGame)`, by then the deferred write is LIVE) asserts both rows. Builds via its own CMakeLists (DLL build), like cap-36 / cap-29. No `/OPT:NOICF` (no internal stubs — it targets a WHGame.dll site by name). |
| Test design (why named-target, not self-host) | A self-host via `pattern =` over the plugin's OWN module was REJECTED after reading the engine: both the pattern scanner (`scan_engine.cpp` `ScanAll`) and the patch locator (`patch_engine.cpp` `Resolve` → `ResolveUniquePatternMatch`) scan `pe::ExecutableSections` ONLY, and `kcdxBytesOptions` has no raw `address` locator (unlike `kcdxHookOptions`) — so a plugin's own marker buffer in writable `.data` is unscannable. The robust observable is the SAME verified-safe rewrite cap-01 proves (`44 8A F0` → `45 31 F6` at `outfit_swap_callsite_aob`, Address Library id 1004 — `mov r14b,al` → `xor r14d,r14d`), driven through the C++ `kcdxBytesInterface` instead of Lua `kcdx.bytes` — the direct parity mirror. **Conflict entanglement resolved, not blind:** cap-01's `[[patch]]` rewrites this same site with the SAME replacement; two same-replacement writers is conflict_engine `WriteOnWriteFull` — NOT a rejection (both apply, later wins; `conflict_engine.cpp:59-70`). Whichever applies first writes; the other idempotent-skips (`patch_engine.cpp` `VerifyOriginalAtAddr` verdict==0 → `ApplyPatch` returns true → `Status::Applied`). The end state of the site is `45 31 F6` in EVERY interleaving and cap-39's handle reaches `Applied` either way, so the observable has NO fragile dependency on cap-01's apply order. cap-39's registration applies via `lua_registry` `ApplyZone` → `patch::ApplyPatch` (re-resolves against current DLL state); it does not ride the conflict_engine preflight matrix. |
| Auto-pass check | Both rows assert at `kcdxPlugin_PostGameLoad` (the after_game C++ export; the deferred write is LIVE by then — the cap-29 / cap-36 pattern). **`CAP-39-cpp-bytes-register`**: `K.bytes->Register(target="outfit_swap_callsite_aob", original="44 8A F0", replacement="45 31 F6")` returns a NON-ZERO handle at Load; at PostGameLoad `K.bytes->IsApplied(handle)==true` AND `K.memory->ReadBytes(site, 3)` reads `45 31 F6` (site resolved via `K.memory->ScanPattern` on the post-rewrite context). The deferred-apply-through-C++ proof. FALSIFIABLE: a broken `Register` (zero handle / not applied / bytes unchanged) → FAIL with the observed handle/IsApplied/live-bytes in the reason. **`CAP-39-cpp-bytes-uninstall-rejected`**: `K.bytes->Uninstall(handle)==false` (a byte rewrite has no revert path), `IsApplied(handle)` STILL true (NOT reverted), and the site STILL reads `45 31 F6`. The no-revert teaching, peer of cap-35's bytes-error row. FALSIFIABLE: an Uninstall that returns true, an IsApplied that flips false, or a site reverted to `44 8A F0` → FAIL (a revert would be AP13). An InputLoaded backstop (the cap-29 design) reports a loud FAIL on both rows if PostGameLoad never fired. No player input. |
| Last result | ⏳ PENDING (boot auto-pass; verified at the checkpoint launch) |
| Notes | The C++ parity proof of the `kcdx.bytes` model — first non-Lua-binder consumer of the `kcdxBytesInterface` v1 surface, and the AP7 + docs-discipline deliverable that makes Phase 3 sub-2 "done". Uses the `K.bytes` + `K.memory` wrapper (not the raw `QueryInterface` floor — cap-36 already proves the raw floor for the sibling hook interface; cap-39's focus is the bytes capability end-to-end, idiomatic through the wrapper). The named-target common path is the disassembler-test win for bytes (the author types `target = "outfit_swap_callsite_aob"`, never the RVA). Site choice reuses cap-01's verified-safe transformation by SITE only — cap-01 is NOT edited; the coexistence is benign same-replacement `WriteOnWriteFull` (see Test design). |

## CAP-40: `kcdxTrampolineInterface` v2 (C++ mirror of `kcdx.code`) end-to-end (Phase 3 sub-3 step 4)

| Field | Value |
|---|---|
| What | The C++ DLL author's path to the `kcdx.code` model via `kcdxTrampolineInterface` **v2** — the two high-level peers of the raw `AllocateFromBranchPool`/`LocalPool` floor: `Allocate(const kcdxCodeOptions*) -> void*` (the all-in-one allocate+fill+NOP-pad+export call, the direct mirror of Lua `kcdx.code{...}`) and `Export(owner, bareName, addr) -> bool` (the standalone symbol-table publish for an address the plugin already holds). `kcdxCodeOptions` carries `owningPlugin`+`name`+`bytes`/`bytesSize`+`size`+`pool`+`exportName`; the "set bytes OR size" rule, the `pool` enum (default branch), and the bare-`exportName`/engine-prefix discipline all mirror the Lua field set. The raw `AllocateFrom*Pool` floor coexists (`Allocate` is built on it). The `exportName`/`Export` publish closes the cross-plugin symbol gap end-to-end: publish via `Allocate`/`Export`, consume via `K.api->ResolveSymbolAs`. |
| Channels | (ii) C++ DLL. C++ PARITY: this row IS the C++ parity for `kcdx.code` (`docs/cpp/code.md`); the Lua surface is at parity per the Lua `kcdx.code` coverage. Both surfaces of the code-allocation + export capability now ship a regression (parity-is-tested, `lua-api-surface.md`). |
| Engine status | LIVE (steps 1-3 wired the ABI end-to-end: header decl of `kcdxCodeOptions` + `Allocate`/`Export` appended to `kcdxTrampolineInterface` with `kcdxTrampolineInterface_Version` bumped to `2u`; `src/trampoline.cpp` `Thunk_Allocate` (alloc + memcpy + NOP-pad + export-register) + `Thunk_Export` (standalone `symbols::Register`); `K.code` on `Kcdx.h` already fetching `kcdxTrampolineInterface` at version 2u — step 3 owed no wrapper code change, this plugin verifies K.code reaches the v2 methods). This plugin (step 4) is the verification. |
| Test design (why self-hosting, no game site) | `kcdx.code` allocation is PLUGIN-OWNED memory drawn from the trampoline pools — NOT a game site. So unlike cap-39 (bytes) there is NO cap-01-style conflict entanglement and NO dual-Lua boundary issue: the plugin allocates its own executable memory and reads it straight back. The allocate row even CALLS the allocated region (cast to `int(*)()`) — the bytes (`mov eax,42; ret`) are a valid, self-contained `int()` function with no relocations / no external calls / trivial calling convention, in a `PAGE_EXECUTE_READWRITE` branch-pool region, so cast-and-call is safe and is the strongest falsifiable proof the allocation is genuinely executable (a non-executable or mis-filled region would crash or return ≠ 42). The export rows resolve via `K.api->ResolveSymbolAs(K.self, "<bare>")` — NOT bare `ResolveSymbol`: the engine stores the export under the `<author>.<plugin>.<bare>` namespace key, so a bare anonymous `ResolveSymbol` resolves other-only and MISSES the plugin's own export; `ResolveSymbolAs` threads `K.self` as the owner so the self-tier resolves it (the exact call the Lua side uses for a self-export). |
| Test plugin | [`cap-40-cpp-code/`](cap-40-cpp-code/) — native C++ DLL. `kcdxPlugin_Load` Inits the `K` wrapper, `Allocate`s two regions (one plain, one with `exportName`) + `Export`s a static; `kcdxPlugin_PostGameLoad` (the after_game export, after `ApplyZone(AfterGame)`) asserts all three rows — the export resolutions are a cross-phase proof the Load-time symbol table survives into after_game. Builds via its own CMakeLists (DLL build), like cap-36 / cap-39. No `/OPT:NOICF` (the allocated region is the call target, not a folded internal stub). |
| Auto-pass check | All three rows assert at `kcdxPlugin_PostGameLoad` (the cap-29 / cap-36 / cap-39 pattern). **`CAP-40-cpp-code-allocate`**: region != null AND read-back head bytes == `B8 2A 00 00 00 C3` AND tail `[6,10)==0x90` AND the executed region returns **42**. **`CAP-40-cpp-code-export`**: `Allocate(exportName="cap40_region")` region != null AND `ResolveSymbolAs(K.self, "cap40_region")` == the region address. **`CAP-40-cpp-code-export-standalone`**: `Export(K.self, "cap40_standalone", &static)==true` AND `ResolveSymbolAs(K.self, "cap40_standalone")` == that static's address. An InputLoaded backstop (the cap-29 / cap-36 / cap-39 design) reports a loud FAIL on all three rows if PostGameLoad never fired. No player input. |
| Last result | ✅ 3/3 PASS (`38f9dd5`, live run 2026-05-25, suite 99/107): region executed and returned 42; both exports resolved via `ResolveSymbolAs(self,...)` to the published addresses. |
| Notes | The C++ parity proof of the `kcdx.code` model — first non-Lua-binder consumer of `Allocate`/`Export`, and the AP7 + docs-discipline deliverable that makes Phase 3 sub-3 "done". Uses the `K.code` wrapper field (Init already fetches `kcdxTrampolineInterface` at version 2u, so exercising `K.code->Allocate`/`Export` IS the step-3 verification that K.code reaches the v2 methods — no separate wrapper test owed). The named-export common path is the disassembler-test win for code (the author types a bare `exportName`, never the prefix or the RVA). `docs/cpp/code.md` flipped NYI→LIVE; `docs/cpp/index.md` code map row updated to reflect the full surface Built; this row + the sub-3 ledger entry. |

## CAP-41: `GetConflictReport` folds in `kcdx.bytes` (`kcdxBytesInterface::Register`) patches as a fourth source

| Field | Value |
|---|---|
| What | `kcdxInterface::GetConflictReport(target, out, cap)` now folds in **`kcdx.bytes`** patches (registered via `kcdxBytesInterface::Register`) as a FOURTH conflict source. A `kcdx.bytes` patch routes through the `lua_registry` `Kind::Bytes` path, NOT the legacy `[[patch]]` `g_patches` list — so before this feature `GetConflictReport` walked only `g_patches` + `g_hooks` (the legacy conflict_engine resolved lists) + `hook_chain` (the COMP-14 third source) and was BLIND to a bytes-Register patch: querying a `kcdx.bytes`-patched VA returned no entry for that bytes patch. The fold reports each `Kind::Bytes` entry with `kind == kcdxConflictEntryKind_Patch`. The four sources are now: legacy `[[patch]]` + legacy `[[hook]]` + `kcdx.hook` (hook_chain) + `kcdx.bytes` (Register). |
| Channels | (ii) C++ DLL (`kcdxInterface::GetConflictReport` + `kcdxBytesInterface::Register`). The conflict-report introspection is C++-only today; the Lua mirror (`kcdx.conflict`) is tracked parity-debt (`docs/outstanding-work/lua-conflict-report-mirror.md`, NYI in `docs/cpp/index.md`) — this row WIDENS what the eventual Lua mirror must expose, it does not change that the mirror is still owed. |
| Engine status | LIVE (steps 1-2 — committed before this test step — made `Thunk_GetConflictReport` in `src/interfaces.cpp` walk `lua_registry` `Kind::Bytes` entries and fold them in as a fourth source after the legacy patch + legacy-hook + hook_chain loops). This plugin (step 3) is the AP7 + docs-discipline verification. |
| Test plugin | [`cap-41-cpp-bytes-conflict-report/`](cap-41-cpp-bytes-conflict-report/) — native C++ DLL; `test_suite_only`; owns the row. `kcdxPlugin_Load` Inits the `K` wrapper + registers ONE byte rewrite via `K.bytes->Register` (the SAME verified-safe rewrite cap-39 / cap-01 prove — `target="outfit_swap_callsite_aob"` id 1004, `offset=13`, `original="44 8A F0"`, `replacement="45 31 F6"`, `idempotent=true` — with a DISTINCT entry name `cap41_bytes_patch` so the report query can find THIS plugin's entry). `kcdxPlugin_PostGameLoad` (the after_game export, after `ApplyZone(AfterGame)`, by then the bytes patch is LIVE) resolves the patched VA via `K.memory->ScanPattern` (the cap-39 `ResolveSiteForReadback` helper, ctx+20) and queries `GetConflictReport(siteVA)`. Builds via its own CMakeLists (DLL build), like cap-39 / cap-40. No `/OPT:NOICF` (no internal stubs — it targets a WHGame.dll site by name). |
| Test design (why by-NAME, not by-count) | The site `outfit_swap_callsite_aob` (id 1004) is patched by MULTIPLE suite entries with the SAME replacement, all idempotent-coexist: **cap-01** (a Lua/`[[patch]]` entry — the legacy `g_patches` source), **cap-39** (a `kcdxBytesInterface::Register` bytes patch — the new fourth source, name `cap39_outfit_swap_rewrite`), and **cap-41** itself (name `cap41_bytes_patch`). Two-plus same-replacement writers on one site is conflict_engine `WriteOnWriteFull` — NOT a rejection: every writer applies, the second+ idempotent-skips (`patch_engine.cpp` `VerifyOriginalAtAddr` verdict==0 → `ApplyPatch` returns true). So `GetConflictReport(siteVA)` returns MULTIPLE `kind=Patch` entries and the COUNT GROWS as the suite grows — a fixed "exactly N" assertion would flake. The assertion scans for EXACTLY ONE entry named `cap41_bytes_patch`, requires `kind == kcdxConflictEntryKind_Patch` and `applied != 0`, and the FAIL reason lists EVERY returned entry (name/kind/applied) so a miss is diagnosable. This co-location is a STRONGER proof than an isolated site: it shows the merge folds the bytes-Register source ALONGSIDE the legacy `g_patches` source (cap-01) AND the other bytes-Register entry (cap-39) at the SAME VA, all sorted/returned together — the fourth source is merged, not replacing. |
| Auto-pass check | `CAP-41-bytes-in-conflict-report` asserts at `kcdxPlugin_PostGameLoad` (the after_game C++ export; the bytes patch is LIVE by then — the cap-39 pattern). A PRECONDITION guard runs FIRST: `K.bytes->IsApplied(handle)==true` AND `siteVA` resolved — if either fails the row FAILs with a precondition reason so a report-blindness verdict is NEVER conflated with the patch not applying / the site not resolving. Then `K.api->GetConflictReport(siteVA, entries, /*cap=*/16)` and PASSES iff EXACTLY ONE returned entry is named `cap41_bytes_patch`, with `kind == kcdxConflictEntryKind_Patch` and `applied != 0`. FALSIFIABLE: PRE-FEATURE (step 2 absent) `GetConflictReport` walked only `g_patches`+`g_hooks`+`hook_chain` → 0 matches for `cap41_bytes_patch` → FAIL; POST-FEATURE the fourth source folds it in → 1 match, kind=Patch, applied → PASS; a match with `kind != Patch` → the fold used the wrong kind; `applied == 0` → the accessor reported it not-applied despite `IsApplied` true (an accessor range/flag bug). An InputLoaded backstop (the cap-39 design) reports a loud FAIL if `PostGameLoad` never fired. No player input. |
| Last result | ✅ PASS (`275c288`, live run 2026-05-25, suite 102/109): `GetConflictReport(0x…1759)` at the kcdx.bytes-patched site returned `[cap41_bytes_patch(kind=Patch,applied=1), cap_01_outfit_swap_rewrite(kind=Patch,applied=1)]` — exactly one match for `cap41_bytes_patch`, kind=Patch, applied. The fourth source (bytes-Register) folds in ALONGSIDE the legacy `g_patches` entry (cap-01) at the same VA. cap-39 (2/2) + cap-40 (3/3) + all legacy GetConflictReport callers still PASS — the four-source merge is non-disruptive. |
| Notes | The AP7 + docs-discipline close of the "GetConflictReport reports `kcdx.bytes`" feature. Proves the C++ conflict report SEES the `kcdx.bytes` (Register) source — previously the bytes-Register patch existed only as a `Kind::Bytes` `lua_registry` entry, never in any report. The COMP-14 sibling closed the `kcdx.hook` (hook_chain) third source; this closes the bytes-Register fourth source. Site choice reuses cap-39's (and cap-01's) verified-safe transformation by SITE only — neither cap-01 nor cap-39 is edited; the co-location IS the proof that the fourth source merges alongside the legacy `g_patches` (cap-01) and the other bytes-Register (cap-39) entries. Docs landed with it: `docs/cpp/hook.md` §"Conflict report" prose extended to the four sources, and `docs/outstanding-work/lua-conflict-report-mirror.md` updated to note the C++ bytes-Register source is now covered (the Lua mirror stays owed). |

## CAP-42: `kcdxHookInterface::Mid` v2 C++ return-skip parity (parity mirror of Lua mid `return "skip"`)

| Field | Value |
|---|---|
| What | The C++ mid hook's **return-skip channel** — `kcdxHookInterface` v2 gave the `Mid` callback an `int` return (`kcdxMidResult`: `Run = 0` / `Skip = 1`) so a C++ mid hook can SKIP the captured instruction, the parity mirror of the Lua mid callback's `return "skip"`. v1's `Mid` callback was `void` — no skip channel from C++ at all. The return slot was previously unused (a mid hook never returns a value to the hooked function), so the repurpose costs no prior capability; every capture write (`values[i].value_*`) still applies in BOTH the run and skip cases. This is the FIRST consumer of `kcdxHookInterface::Mid` — cap-36 exercised Before/After/Around/Replace/Uninstall but NOT Mid (its register/memory captures don't templatize, so the wrapper has no Mid helper and cap-36 skipped it). cap-42 is the C++ PEER of CAP-21-skip / CAP-21-run (the Lua mid run/skip rows). |
| Channels | (ii) C++ DLL (`kcdxHookInterface::Mid` v2). C++ PARITY: this row IS the C++ parity for the mid run/skip capability; the Lua surface is at parity per CAP-21-skip / CAP-21-run (`docs/lua/hook.md` §mid). Both surfaces now ship a regression for the mid run/skip channel (parity-is-tested, `lua-api-surface.md`). |
| Engine status | LIVE (steps 1-2 of this cycle — committed before this test step — gave the C++ `Mid` callback the `int` return (`kcdxMidResult`) in `include/kcdx/Interfaces.h` (`kcdxHookInterface_Version` bumped to `2u`) and wired the return into the engine's skip-original flag the mid JIT consumes). This plugin (step 3) is the AP7 + docs-discipline verification. |
| Test plugin | [`cap-42-cpp-mid-skip/`](cap-42-cpp-mid-skip/) — native C++ DLL; `test_suite_only`; NO `plugin.lua`, NO scripting interface (the install is wholly via `kcdxHookInterface::Mid` from C++). `kcdxPlugin_Load` QueryInterfaces Hook (v2) + Trampoline + Messaging, allocates TWO controlled stubs from the branch pool (the cap-21 `AllocStub` idiom — `int fn(int)->seed+100`, capture site at `+2` = the `add rax,0x64`; branch pool NOT raw `VirtualAlloc`, the cap-21 ASLR-reachability reason: MinHook's mid-trampoline allocator needs the stub within ±2 GB of WHGame), and installs a C++ mid hook on each via `kcdxHookInterface::Mid(nullptr, &cb, &opts)` with `opts.address = stub+2`, one positional `rax:i64` capture, `opts.owningPlugin = g_self`. The two callbacks are the v2 int-return shape `int cFn(kcdxHookCaptureValue*, int)` — one returns `kcdxMidResult_Skip`, the other `kcdxMidResult_Run`. Builds via its own CMakeLists (DLL build), like cap-36 / cap-39 / cap-40. |
| Auto-pass check | Both rows assert at `kcdxMessage_InputLoaded` (after `ApplyZone`, the mid detours are LIVE — cap-21's timing; boot-only, no player input). Each calls its stub directly (the detour fires for any caller). **`CAP-42-cpp-mid-skip`**: callback returned `kcdxMidResult_Skip` → the captured `add rax,0x64` was SKIPPED → `g_skip.fn(10)==10`. **`CAP-42-cpp-mid-run`**: callback returned `kcdxMidResult_Run` → the `add` RAN → `g_run.fn(10)==110`. FALSIFIABLE: skip row returns 110 (the add ran despite Skip — the skip channel didn't take, the exact pre-feature void-ABI behavior) → FAIL; run row returns 10 (skipped despite Run, i.e. the int-return spuriously skips on 0) → FAIL. An InputLoaded backstop reports a loud FAIL on both rows if the install bailed at Load (Hook/Trampoline QueryInterface null, branch-pool alloc null, or a `Mid()` install returned 0) so neither row sits silent-PENDING. |
| Last result | ✅ PASS (`77686bc`, live run 2026-05-26): CAP-42-cpp-mid-skip → `fn(10)==10` (the C++ mid callback returned `kcdxMidResult_Skip` → the `add rax,0x64` was SKIPPED — the v2 return-skip channel works from C++, impossible pre-feature), CAP-42-cpp-mid-run → `fn(10)==110` (returned `kcdxMidResult_Run` → the `add` ran, no spurious skip on 0). No boot crash — the asmjit int-return thunk threading is sound. All pre-built v1-era C++ plugins still PASS under the v2 version bump (56/56 manifests valid). |
| Notes | The AP7 + docs-discipline close of the "C++ mid return-skip parity" feature, and the FIRST C++ `kcdxHookInterface::Mid` consumer (cap-36 covered the other five sub-verbs but Mid has no wrapper helper, so it was untested from C++ until now). Two distinct stubs/allocations (like cap-21's distinct stubs) so the two hooks don't interfere. Kept to the 2 skip/run rows — cap-21-write already covers capture-WRITE on the Lua side, and the run/skip rows prove capture READ implicitly (the callback receives the `values[]` array); a C++ capture-write row would add no distinct coverage of the skip feature this cycle ships. Docs landed with it: `docs/cpp/hook.md` flipped to v2 + the Mid §"Run / skip" subsection (the `int`-return `kcdxMidResult` contract + a skip snippet, the stale void-ABI prose replaced), and `docs/lua/hook.md` §mid gained the one-line C++ mirror cross-reference. |

## CAP-43: localization runtime-dump probe, step 1 (minimal live probe)

| Field | Value |
|---|---|
| What | Step 1 of the localization runtime-dump feature — a MINIMAL, dev-mode, observe-only probe (`src/probes/loc_dump_probe.{cpp,h}`, armed from the worker thread alongside `hooks::Install`) whose ONLY job is to settle two gating unknowns in-game before any dump machinery (table walk, key↔id map, output format — all LATER steps) is built: **(1)** hooking the `CLocalizedStringsManager` CONSTRUCTOR (`FUN_1809f0ce4`, RVA `0x9f0ce4`) captures the manager `this` (RCX / arg 1) into an atomic; **(2)** hooking the by-INT-ID getter (loc-manager vtable **slot 1**, offset `0x8`, decompiled ABI `char* (this, uint id)`) fires with a readable `(caller-return-address, id)` per call. The probe NEVER mutates args/return — it always calls the original unmodified. Manager capture route is the CTOR hook (user decision — mirrors `bugsplat_ctor_probe`, sidesteps the unpinned `ISystem` getter slot). The getter target is resolved at runtime off the captured instance's LIVE vtable (`(*(void***)this)[1]`) — no hardcoded getter RVA, no ASLR arithmetic. RVAs are PROBE-LOCAL LABELED CONSTANTS (user-approved AP1 deferral for this diagnostic step; seed-ID promotion lands at feature graduation, noted in-code). |
| Channels | Engine-internal probe (no author surface). C++ PARITY: n/a — this is engine diagnostic machinery, not a `kcdx.*` / `kcdx*Interface` surface; it owes no Lua/C++ mirror (single-surface by construction — it is the engine probing the game, not an author-facing capability). |
| Engine status | LIVE-PENDING (step 1 of the loc runtime-dump feature). Probe armed from `WorkerThread` after `hooks::Install` (WHGame.dll mapped + MinHook initialized), before CryEngine constructs the loc manager. Dev-mode-gated + idempotent. |
| Test plugin | [`cap-43-loc-dump/`](cap-43-loc-dump/) — MANIFEST-ONLY STUB (no `plugin.lua`, no DLL). The behavior under test is the engine probe, so the engine reports the rows directly via `kcdx::test::ReportResult` (same pattern as cap-23's `src/lua_plugin_loader.cpp` report + the `engine-self-test` stub). The stub's `test_names = ["cap-43-loc-ctor-capture", "cap-43-loc-byid-getter"]` pre-registers both rows as PENDING until the probe fires. |
| Auto-pass check | Each row reports PASS from the probe's FIRST observation, one-shot guarded (hook-fire-self-report convention — NOT a polled fire-count at ready/input_loaded). **`cap-43-loc-ctor-capture`**: the ctor detour fired and stored a non-null manager `this`. **`cap-43-loc-byid-getter`**: the slot-1 getter detour fired (caller-RA + id read via the `char*(this,uint id)` ABI) on its first call. The `LOC_DUMP` dev-log lines (`manager_captured this=…`, `byid_getter_fire caller_ra=… this=… id=…`) are the raw observable behind the rows. |
| Last result | ✅ PASS (`5fc5264`, live run 2026-05-26): both rows PASS. `cap-43-loc-ctor-capture` → ctor hook installed at WHGame.dll+0x9f0ce4, `manager_captured this=0x2A9EBB43800`. `cap-43-loc-byid-getter` → getter hook installed at vtable[1], fired with readable **sequential ids (0,1,2,3,…)** — confirms the `char*(this,uint id)` ABI AND the int-ID = per-language vector-index model. Two distinct `caller_ra` values request each id (the caller↔id bridge edges, captured live). No fault — the two-arg ctor fix holds (the prior one-arg ctor typedef crashed in-game, AP2, fixed before this run). |
| Notes | The probe IS the verification of the slot-1 ABI: a readable `id` in the live `LOC_DUMP` log confirms the `char* (this, uint id)` shape (RE outcome C, LOC-MANAGER-FINDINGS.md). The getter hook is installed lazily, ONCE, from inside the ctor detour AFTER calling the original ctor (the `*this = vtable` store must have run for `(*(void***)this)[1]` to be live). Step 1 deliberately stops at "does the ctor capture `this` + does the getter fire with readable args" — the key↔id table walk and the `caller↔id↔key` accumulation are later steps. |

## CAP-35: Lua `handle:uninstall()` (Phase 3 sub-1 extended, steps 1+2)

| Field | Value |
|---|---|
| What | The Lua-side `handle:uninstall()` method on `kcdx.hook` handles + the underlying `kcdx::hook_chain::Uninstall(handleId)` engine teardown. Author calls `h:uninstall()` on a hook handle; the engine erases the matching `ChainEntry` (or clears `midHandleId` for a mid chain) under `g_chainsMu`, `luaL_unref`s the callback so it can't fire again, and `SetStatus(Status::Removed)` flips the registry status atomic so subsequent `h:applied()` reads `false` and `tostring(h)` renders `"removed"`. The MinHook detour + JIT trampoline stay session-lifetime (the documented dual-Lua-VM safety stance — DispatchPre/Post release `g_chainsMu` before `lua_pcall`, so a Chain pointer held across pcall must outlive the in-flight call; the empty-entries guards at `DispatchPre`:749 + `DispatchPost`:809 already make a drained chain a safe no-op shim). Method-chaining contract: `:uninstall()` returns the handle userdata itself. Idempotent at every layer — unknown / already-Removed ids return true. Non-Hook handle kinds (today only `kcdx.bytes`) raise a teaching `luaL_error` naming the kind because the patch engine has no revert path; silently flipping status while patched bytes remain live in memory would be AP13. |
| Channels | (vi) plugin Lua. C++ PARITY: the C++ peer (`kcdxHookInterface::Uninstall`) is the C++ feature step 7 of the same Phase 3 sub-1 extended cycle; tracked as the cap-NN-cpp-hook-interface row when it lands. |
| Engine status | ✅ LIVE (Phase 3 sub-1 extended steps 1+2+3 — `abbb4c6` `hook_chain::Uninstall` + `Status::Removed`; `0c0756a` `H_uninstall` metatable method + `docs/lua/hook.md` row; `d03ffb1` cap-35 regression). |
| Test plugin | [`cap-35-uninstall/`](cap-35-uninstall/) — pure Lua. Five before-hooks on `kcdx.luaopen_math` (2-segment engine-seed form, the same shape `CAP-34-explicit-1dot-kcdx` uses; the seed carries the verified `i32 (ptr L)` signature so no inline `signature=` is needed) drive the four `kcdx.hook` uninstall rows. One `kcdx.bytes` registration with a well-formed-but-deliberately-non-matching 16-byte AOB (`DE AD BE EF ...`) + idempotent 1-byte `0x90`-over-`0x90` write drives the non-Hook teaching-error row; no live memory is written because the apply scan finds zero matches. |
| Auto-pass check | All five rows assert at `kcdx.on("ready")` (boot-only, no player input). **`CAP-35-uninstall-basic`**: assert `:applied()==true` pre-uninstall, call `:uninstall()`, assert `:applied()==false` post. The core lifecycle. **`CAP-35-uninstall-idempotent`**: call `:uninstall()` twice; assert both calls returned the handle userdata (`rawequal(returned, h)`, the self-return chaining contract); assert `:applied()==false` after. Engine-layer idempotence (`hook_chain::Uninstall` returns true on unknown / already-handled ids) makes the second call a safe no-op. **`CAP-35-uninstall-tostring`**: capture `tostring(h)` before + after `:uninstall()`; assert before contains `"applied"`, after contains `"removed"`, and before ≠ after (the `H_tostring` Removed case step 1 added). **`CAP-35-uninstall-chain-survives`**: install TWO before-hooks (A + B) on the same target under distinct names; assert both `:applied()==true` pre; uninstall A only; assert A `:applied()==false` and B STILL `:applied()==true` — proves `chain.entries.erase` removes only the one entry, the chain trampoline + remaining entries stay healthy (multi-hook-on-same-target pattern from CAP-20-chain). **`CAP-35-uninstall-bytes-error`**: register a `kcdx.bytes` with the well-formed bogus pattern (the binder accepts; the apply pass flips status to Failed but the Entry exists with `Kind::Bytes`); call `:uninstall()` inside `pcall`; assert `pcall` returned `false` (the teaching `luaL_error` fired); assert the error message contains `"kcdx.bytes"` AND `"not yet supported"` (the exact substrings step 2's `luaL_error` emits at `lua_registry.cpp`:183-188). FALSIFIABLE: if step 1 mis-erased a chain entry, if step 2's `SetStatus(Removed)` didn't flip the status atomic, if `H_tostring`'s Removed case was missed, or if `H_uninstall`'s non-Hook default branch silently flipped status instead of raising — at least one row FAILs with the actual observed values in the FAIL reason. |
| Last result | ✅ LIVE (2026-05-24, `d03ffb1`): all 5 rows PASS. Live log at 17:57:05: `RESULT name=CAP-35-uninstall-basic verdict=PASS` (`:applied()` true→false); `RESULT name=CAP-35-uninstall-idempotent verdict=PASS` (both calls returned self via rawequal); `RESULT name=CAP-35-uninstall-tostring verdict=PASS` (tostring `status=applied`→`status=removed`); `RESULT name=CAP-35-uninstall-chain-survives verdict=PASS` (A uninstalled, B stayed applied — entries.erase entry-selective); `RESULT name=CAP-35-uninstall-bytes-error verdict=PASS` (pcall raised, error contains `kcdx.bytes` AND `not yet supported`). Suite `77/85` — exactly the prior `72/80` baseline + 5 cap-35 rows; no regression on any existing hook row (CAP-20-* / 21-* / 22-* / 33-* / 34-* / COMP-03 / COMP-12 all unchanged). Sole FAIL: CAP-20-target-nosig, the standing parallel-chat issue unrelated to this feature. |
| Notes | The AP7 close-out for the Lua side of `handle:uninstall()`. The C++ peer (`kcdxHookInterface::Uninstall`) is the C++ feature at step 7 of the same Phase 3 sub-1 extended cycle and will get its own matrix row when it lands. Target choice (luaopen_math via `kcdx.luaopen_math` 2-segment form) is the established prior art from cap-33/cap-34/comp-12 — a verified-unhooked leaf NOTHING entry-hooks, called once at Lua boot and never again, so the install is the proof and the hooks never need to fire during the test run. The bytes-error row uses a well-formed bogus AOB so the binder appends a `Kind::Bytes` Entry (the H_uninstall switch keys on Kind, not Status — Failed-status bytes still raise the same teaching error as Applied-status would) while writing nothing to live memory. |

## CAP-38: sig-mismatch gate (named target + explicit-signature conflict)

| Field | Value |
|---|---|
| What | The signature-resolution gate on the **named-target + explicit-`signature` conflict**. When a hook names a target that carries a verified Address-Library ABI AND the author ALSO passes an explicit `signature`, the engine USED to trust the explicit sig outright and never cross-check it against the verified ABI it had in hand — so a named target + a WRONG explicit signature was **silently accepted** (an AP12 footgun on the exact surface AP12 protects, AP13 if left deferred). The gate's core behavior is **(c) keep the explicit sig + signal**: the engine consults the verified ABI to **detect** the conflict, emits a teaching diagnostic naming the target + both signatures + that the explicit one is used as-authored, then **proceeds** with the explicit sig (the deliberate-override case stays authoritative — the expert override is honored, the author may know better than the seed). This removes the SILENT part of the footgun without disabling override. The diagnostic SEVERITY now splits by `kcdx::hook_signature::ClassifyConflict`: a **Hard** conflict (arg-count delta OR return-register-width delta — the call frame is mis-described, a crash risk on a live engine function, e.g. the cap-38 / 0xC8 save-load crash) logs at **ERROR** (action `explicit_overrides_verified_hard`, `severity=hard`, `crash_risk=true`); a **Soft** conflict (same arg count + same return width, per-slot type nuance only) stays at **WARN** (action `explicit_overrides_verified`, `severity=soft`). Both surfaces gated: C++ `src/hook_interface.cpp` `ResolveSignature` + Lua `src/lua_bind_hook.cpp` signature resolution; the severity split is `ClassifyConflict` (declared in `hook_signature.h`, body in `hook_chain.cpp` next to the untouched `SignaturesCompatible`). |
| Channels | (ii) C++ DLL + (vi) plugin Lua — BOTH surfaces tested (parity-is-tested, `lua-api-surface.md`). |
| Engine status | LIVE — the gate in `ResolveSignature` (`src/hook_interface.cpp`) + the Lua signature-resolution branch (`src/lua_bind_hook.cpp`). The hard/soft severity split is `kcdx::hook_signature::ClassifyConflict` (declared in `hook_signature.h`, body in `hook_chain.cpp`); a Hard conflict (arg-count or return-width delta) escalates the gate diagnostic WARN → **ERROR** so a known-crash-risk override on a live engine function is legible (the cap-38 / 0xC8 fix — `docs/known-issues/save-load crash 0xC8 raised from WHGame.md`). Resolution is UNCHANGED — explicit sig still wins, install still proceeds (behavior-(c)); only severity + the hard/soft action token changed. `SignaturesCompatible` is NOT touched (the chain-coexistence path at `hook_chain.cpp` still uses it). Closes the sig-mismatch-gate debt tracked in `docs/outstanding-work/restructure-plan.md`. |
| Test plugin pair | [`cap-38-sig-mismatch-gate/`](cap-38-sig-mismatch-gate/) (the C++ DLL — `kcdxHookInterface`) + [`cap-38-sig-mismatch-gate-lua/`](cap-38-sig-mismatch-gate-lua/) (the Lua sibling — `kcdx.hook`). Both name `kcdx.luaopen_table` (engine seed id 1173, verified ABI `i32 (ptr L)`) and pass the deliberately-WRONG explicit signature `void (ptr L)` (same arg count, RETURN-WIDTH delta — i32 collapsed to void → NOT `SignaturesCompatible` → the gate fires). `luaopen_table` is gameplay-COLD (lualibs[] entry 2, called once at Lua-init before this hook installs) and entry-hooked by nobody, so the wrong-ABI thunk is installed but NEVER FIRES — no corruption, no crash (the cap-33 cold-leaf idiom). |
| Auto-pass check | **`CAP-38-cpp-gate-proceeds`** (C++, at `kcdxPlugin_PostGameLoad`, boot-only): the gated install PROCEEDED — non-zero handle, `IsApplied()==true` (the cap-33/34/35 install-is-the-proof idiom, identical to the Lua peer). FALSIFIABLE against a hypothetical (a)-reject impl: a reject gives handle 0 / IsApplied false → row FAILS. The gate is an INSTALL-TIME check; the row asserts install-proceeds ONLY and does NOT assert the detour FIRES (the target is gameplay-cold and never fires — firing tests nothing about the gate). C-dispatch FIRING is proven by cap-36's six own-function rows. **`CAP-38-lua-gate-proceeds`** (Lua, at `kcdx.on("ready")`, boot-only): `kcdx.hook{ target="kcdx.luaopen_table", signature="void (ptr L)", before=... }` returns a handle whose `:applied()==true` (the install proceeded — same idiom; a reject would leave `applied()==false` + non-empty `:reason()` → FAIL). No player input. The wrong-ABI thunk is installed on the cold leaf but never invoked, so no Lua-state corruption. |
| Manual confirm | **`CAP-38-cpp-gate-warn`** + **`CAP-38-lua-gate-warn`** [manual] (log-assert, the COMP-12 pattern): the orchestrator confirms the gate's **ERROR-level HARD-conflict** line fired in the engine log post-run. cap-38's mismatch is a RETURN-WIDTH delta (explicit `void (ptr L)` vs verified `i32 (ptr L)` — same arg count, return register collapsed to void) → `ClassifyConflict` returns **Hard** → the gate logs at **ERROR**, not WARN. EXACT line — **LEVEL=Error**, category `HOOK_SIG_GATE`, action **`explicit_overrides_verified_hard`** (greppably distinct from the soft WARN token `explicit_overrides_verified`), keys `target` (`kcdx.luaopen_table`) / `plugin` (the owning plugin name) / `explicit_sig` (`void (ptr L)`) / `verified_sig` (`i32 (ptr L)`) / `used` (`explicit`) / `severity` (`hard`) / `crash_risk` (`true`) / `note`. TWO lines fire — one per surface, distinguished by the `plugin` key (`cap_38_sig_mismatch_gate` for C++, `cap_38_sig_mismatch_gate_lua` for Lua). This row catches THREE regressions: a SILENT-trust regression (no line at all), a downgrade to the soft WARN token, and a downgrade to WARN level. Not auto-asserted because the post-fix behavior (install proceeds, handle non-zero) is byte-identical to pre-fix; only the ERROR line distinguishes them. (Row name keeps the historical `-gate-warn` suffix for matrix continuity; the asserted line is now ERROR-level — see the severity note in *What* / *Engine status*.) |
| Last result | ⏳ PENDING (retargeted to `kcdx.luaopen_table`; auto rows boot-only; `[manual]` log-assert rows confirmed at the checkpoint launch by the orchestrator log read) |
| Notes | Closes the sig-mismatch-gate AP12/AP13 debt. Target choice (`kcdx.luaopen_table`, id 1173) is a verified seed whose clean `i32 (ptr L)` ABI a `void (ptr L)` explicit sig cleanly mismatches on RETURN WIDTH (i32 → void is a Hard `ClassifyConflict` delta; same arg count, so no hypothetical arg-count chain issue applies). **The target is gameplay-COLD and entry-hooked by nobody** (lualibs[] entry 2, called once at Lua library-init at boot and never during gameplay; luaopen_math 1172 and luaL_openlibs 1190 are taken by cap-33/34/comp-12 but 1173 is free) — cap-38's hook installs at the first-update-tick AFTER that single call, so the wrong-ABI thunk is installed but NEVER FIRES → no register/stack corruption, no crash (the cap-33 cold-leaf idiom). The two surfaces install the SAME wrong sig, so they are `SignaturesCompatible` with each other and coexist on one solo chain (nobody else hooks 1173, so `applied()` reflects the GATE only). **This RESOLVES the always-on-suite crash:** the prior target was the HOT `lua_settable` (id 1186), whose wrong-ABI thunk fired on the live save-load path and crashed the game (the 0xC8 root cause, `docs/known-issues/save-load crash 0xC8 raised from WHGame.md`); a wrong-ABI thunk corrupts the call frame whenever it fires regardless of observer politeness, so the fix is a cold no-fire target, not a polite observer. The gate-ERROR legibility (commit 928fa61) and the crash diagnostics (FAULTED_CULPRIT / FAULTED_FIRE, 301b766) remain the permanent guard. |

## CAP-45: load-time engine-modification inventory mechanism

| Field | Value |
|---|---|
| What | The load-path engine-modification inventory (`kcdx::modification_inventory::LogInventory`, `src/modification_inventory.cpp`): one reusable function shared by boot (`src/hooks.cpp` first-update-tick, after every patch/hook applies) AND each save-load start (`src/save_load_hooks.cpp` `HookedLoadGameWrapper` ENTER). Reads the LIVE modification sources — `hook_chain::g_chains` (live `kcdx.hook` plugin hooks, via `hook_chain::GetAllChainTargets()`) plus a `RegisterModification`'d registry of the fixed engine (`lua_pcall` / `update` / `frealloc`), lifecycle (save/load), and dev-probe installs. Emits a per-category SUMMARY (`plugin_hook` / `engine` / `lifecycle` / `probe` counts + total + an order-independent XOR-fold fingerprint over ALL target VAs) at Info (always-on, build-to-build diffable), per-target DETAIL (VA + category + name) at Debug (dev-only), and refreshes a cached pre-formatted summary string the crash guard dumps verbatim from its SEH handler (`src/crash_guard.cpp` `FAULTED_INVENTORY`, zero-allocation). Diagnostics for the 0xC8 save-load crash (`docs/known-issues/save-load crash 0xC8 raised from WHGame.md`). |
| Channels | Engine self-report (manifest-only stub holds the name; the engine calls `kcdx::test::ReportResult` directly — same pattern as cap-43 / cap-44). |
| Engine status | LIVE (this cycle) — `modification_inventory.{h,cpp}` (`RegisterModification` / `LogInventory` / `LastInventorySummary` / `LastTotalModifications`); `hook_chain::GetAllChainTargets()` accessor; boot + load-start call sites; engine/lifecycle/probe install-site registrations; crash-guard culprit-walk + cached-inventory dump; Release PDB emission. |
| Test plugin | [`cap-45-load-hook-inventory/`](cap-45-load-hook-inventory/) — manifest-only stub. |
| Auto-pass check | `cap-45-load-inventory`: the engine, right after the boot `LogInventory` call, reports PASS iff the live inventory's total modification count is nonzero (`LastTotalModifications() > 0`). **Boot-only** (no player input). |
| Last result | ⏳ pending (PASS expected): the inventory now reads the live sources, and the engine self-instrumentation hooks (`lua_pcall` / `update` / `frealloc`) register on every boot, so `total > 0` holds even before any plugin hook lands. Confirmed by the next dev-mode launch. |
| Notes | The diagnostic value (a diffable load-path fingerprint + crash-time inventory) is realized: the inventory reflects the LIVE modification set (`hook_chain::g_chains` + the save/load lifecycle hooks + the engine self-instrumentation hooks + dev probes), categorized so the SUMMARY is meaningful rather than a flat count. The dead legacy `hook_engine::g_hooks` / `g_patches` / `g_mid_hooks` vectors are NOT read (empty post-Phase-5); their removal is a separate later cycle. |

## CAP-46: per-session log-stamp correctness (dev log filename)

| Field | Value |
|---|---|
| What | The per-session log stamp (`kcdx::log::g_sessionStamp`, set set-once via `kcdx::log::EnsureSessionStamp`, `src/log.cpp`) and the dev log's derived filename. Bug fix: `SetDevMode(true)` opened the dev log using `g_sessionStamp`, but it fired from the DllMain-phase path (`dllmain.cpp` `RunBeforeGameZoneInDllMain` → config `dev_mode` parse → `SetDevMode`) BEFORE the worker thread's `log::Init()` set the stamp. So the dev log opened as `kcdx-dev_.log` (empty stamp): every dev-mode session overwrote the same file, and the watchdog crash-bundler's `kcdx-dev_<stamp>.log` lookup (`src/watchdog/main.cpp`) never matched — dev logs were missing from crash bundles. Fix: `EnsureSessionStamp()` sets the stamp set-once (`std::call_once`, loader-lock-safe); called from the DllMain path before dev mode can open the dev log, kept by `Init()`, and called defensively in `SetDevMode`. Engine log + dev log now derive their filename from ONE stamp. |
| Channels | Engine self-report (manifest-only stub holds the name; the engine calls `kcdx::test::ReportResult` directly in `src/hooks.cpp` — same pattern as cap-43 / cap-44 / cap-45). |
| Engine status | LIVE (this cycle) — `log::EnsureSessionStamp()` (set-once stamp); `Init()` + `SetDevMode` call it instead of assigning the stamp unconditionally; `dllmain.cpp` `RunBeforeGameZoneInDllMain` calls it before `LoadAllConfigs`; `log::DevLogName()` read-only observability accessor. |
| Test plugin | [`cap-46-session-stamp/`](cap-46-session-stamp/) — manifest-only stub. |
| Auto-pass check | `cap-46-session-stamp`: the engine, at boot (right after the cap-45 self-report, long after the dev log is open), reports PASS iff `log::SessionStamp()` is non-empty AND `log::DevLogName()` equals `"kcdx-dev_" + SessionStamp() + ".log"`. **Boot-only** (no player input). |
| Last result | ⏳ pending (PASS expected): `EnsureSessionStamp` runs in the DllMain path before the dev-mode enable opens the dev log, so the dev log is named `kcdx-dev_<stamp>.log` with a non-empty stamp shared with the engine log. Confirmed by the next dev-mode launch. |
| Notes | Verifies the fix at the observable: the dev log filename, which is what the watchdog crash-bundler greps for (`kcdx-dev_<stamp>.log`, `src/watchdog/main.cpp:288`). A regression to the empty-stamp form (`kcdx-dev_.log`) flips this row to FAIL. The `DevLogName()` accessor is the test's observability hook into the otherwise-private dev stream filename. |

## CAP-47: crash breadcrumb (per-detour fire ring) + owner-named inventory

| Field | Value |
|---|---|
| What | Three cohesive crash-diagnostic changes, one commit (motivated by the 0xC8 save-load crash, `docs/known-issues/save-load crash 0xC8 raised from WHGame.md`, which is invisible because nothing logs WHAT kcdx was executing at the fault). **Part 1 — GUARD culprit-walk repair** (`src/crash_guard.cpp` `WalkToCulprit`): rewritten from `StackWalk64` (which silently returned nothing — kcdx never calls `SymInitialize`, so the x64 symbol-handler unwind callbacks resolved no frames → `FAULTED_CULPRIT` never emitted, PROBE I) to the x64-NATIVE unwinder `RtlLookupFunctionEntry` + `RtlVirtualUnwind` (no symbol handler, allocation-free, SEH-filter-safe) in a bounded 64-frame loop seeded from the faulting CONTEXT, with leaf-frame fallback (read return addr off `Rsp`, advance 8). **Part 2 — per-detour fire breadcrumb** (`src/modification_inventory.{h,cpp}` `RecordFire` / `LastFires`, `FireRecord`, `kFireRingSize=32`): a fixed 32-slot ring written at the `hook_chain` dispatch chokepoints (`DispatchPre` / `DispatchPost` / `MidDispatch`) — always-on, zero-allocation, no-log (relaxed atomic seq bump + four plain stores) — dumped newest-first by the crash guard (`GUARD FAULTED_FIRE`, Error, always-on) after `FAULTED_INVENTORY`. Names the last hook(s) the game executed before dying. **Part 3 — owner-named inventory DETAIL** (`hook_chain::GetAllChainTargets` now returns `{va, plugin, hook}` per chain; `src/modification_inventory.cpp` `plugin_hook` DETAIL logs `plugin=<owner> hook=<name>` instead of the generic `name=kcdx.hook`). |
| Channels | Engine self-report (manifest + a `plugin.lua` that leaves the breadcrumb; the engine calls `kcdx::test::ReportResult` directly in `src/hooks.cpp` per-tick update — same self-report pattern as cap-43/44/45/46, but with a Lua plugin to drive a real detour fire). C++ PARITY: n/a — this is engine crash-diagnostic machinery (SEH filter + dispatch chokepoints + inventory enumeration), not a `kcdx.*` / `kcdx*Interface` author surface; single-surface by construction (it instruments the engine, it is not a capability an author invokes). |
| Engine status | LIVE (this cycle) — `crash_guard.cpp` native-unwinder `WalkToCulprit`; `modification_inventory.{h,cpp}` `RecordFire`/`LastFires`/`FireRecord`/`kFireRingSize`; `hook_chain.cpp` three RecordFire call sites + `GetAllChainTargets` `{va,plugin,hook}` return + `ChainTarget` struct; `crash_guard.cpp` `FAULTED_FIRE` dump; `modification_inventory.cpp` owner-named DETAIL. |
| Test plugin | [`cap-47-crash-breadcrumb/`](cap-47-crash-breadcrumb/) — manifest + `plugin.lua`. The Lua allocates an `int(int)` stub via `kcdx.code{pool="branch"}`, installs a `before` hook (`cap47_breadcrumb_fire`) on it, and `kcdx.memory.dynamic_call`s the stub from `kcdx.on("ready")` (post-ApplyZone) so the detour fires exactly once and leaves a Pre **and** a Post fire-ring breadcrumb — a non-empty chain records at both the DispatchPre and DispatchPost chokepoints, so one call writes two ring entries; the newest is the Post fire, with identical `plugin`/`hook` values (cap-04 / cap-20-dyncall idiom). |
| Auto-pass check | `cap-47-crash-breadcrumb` (**boot-only**, engine self-report, per-tick update one-shot): PASS iff **(a)** `modification_inventory::LastFires()` returns >= 1 fire (the ring recorded the boot detour fire) AND **(b)** `hook_chain::GetAllChainTargets()` yields >= 1 chain with a non-empty, non-`"kcdx.hook"` owner plugin name (Part 3 attribution). Reported from the per-tick update (NOT the cap-45/46 first-update-tick one-shot), retried each tick until a fire is seen so a slow `ready` cannot fail it. The reason string names the newest fire's plugin/hook/seq + an example owner. The **culprit-walk (Part 1)** needs a real fault to exercise — **[manual]**, covered by the live 0xC8 crash repro (a post-fix crash run shows a `GUARD FAULTED_CULPRIT` line naming the first non-kernel frame + `GUARD FAULTED_FIRE` lines naming the last detours). |
| Last result | ⏳ pending (PASS expected): the plugin's `before` detour fires once at `ready`, so by the following tick the ring is non-empty and the chain carries `ts.cap_47_crash_breadcrumb` as owner. Confirmed by the next dev-mode launch; the Part-1 culprit-walk by the next live 0xC8 crash bundle. |
| Notes | Closes the two diagnostic defects PROBE I flagged on the 0xC8 known-issue: "No FAULTED_CULPRIT line emitted" (Part 1 root cause was the missing `SymInitialize` for `StackWalk64`; fixed by the native unwinder) and "Inventory DETAIL names every chain kcdx.hook" (Part 3). The fire breadcrumb (Part 2) is the new link: the 0xC8 stack is pure-WHGame with no kcdx frame, but a kcdx detour fired ~10s earlier — `FAULTED_FIRE` now names it. Design decision: the record path is ALWAYS-ON (not dev-gated) because the buffer's only value is at a crash and crashes happen in production; the record is literally four stores + one relaxed atomic, cheaper than the dev-gate branch itself, and the brief mandates no per-fire log so there is nothing to gate. The ring lives in `modification_inventory` (not a new module) because crash_guard already `#include`s it and it already owns the crash-context dump concern. |

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

## COMP-02: `kcdx.bytes` patch + `kcdx.hook` detour on overlapping bytes (cross-engine coexist PROBE)

| Field | Value |
|---|---|
| Scenario | ONE plugin (`comp-02.dll`) installs BOTH a `kcdx.bytes` IDENTITY patch (`48` → `48`, a genuine no-op) via `kcdxBytesInterface::Register` AND a `kcdx.hook` REPLACE detour (callback returns `false`, the migration of the legacy `31 C0 C3` xor-eax,ret) via `kcdxHookInterface::Replace`, both on the SAME function-entry VA. The patch routes through the PATCH engine (`g_patches` / `conflict_engine`); the hook routes through the SEPARATE `hook_chain` engine. |
| Site | `IsInCombat_callsite_26b` (Address Library id 1006, pattern-hit RVA `0x5605BC`), `offset = -4` → the FUNCTION ENTRY. The id-1006 seed prose: "RVA stored is the pattern-hit position; function entry is at RVA-4 … apply offset = -4, as in comp-02-hook-on-patch." Both surfaces (`target="IsInCombat_callsite_26b"` + `offset=-4`) land on the IDENTICAL VA → genuinely the SAME site. Distinct from COMP-03's `0x566040`; AOB ends in `3C 02` vs COMP-03's `3C 01`. Hook signature `bool (ptr self)` (seed carries none for id 1006). id 1006 is comp-02-EXCLUSIVE in the suite (no other plugin targets it). |
| The PROBE (UNVERIFIED) | Does a `kcdx.bytes` patch (patch engine) + a `kcdx.hook` detour (`hook_chain`) on ONE site BOTH apply (coexist) in the NEW engines — the way the legacy `[[patch]]`+`[[hook]]` did (conflict_engine logged "Both apply, no action needed"; MinHook relocated the patched prologue into its trampoline)? Or does one engine reject/clobber the other? UNKNOWN until the first launch. The `GetConflictReport` assertion at the function-entry VA IS the readout. |
| Test plugin | [`comp-02-hook-on-patch/`](comp-02-hook-on-patch/) — native C++ DLL. `kcdxPlugin_Load` Inits the `K` wrapper (which exposes both `K.bytes` and `K.hook`) and installs BOTH halves; `kcdxPlugin_PostGameLoad` (the after_game export, after `ApplyZone(AfterGame)` — both engines' apply passes done; the cap-39 / comp-03-B pattern) resolves the function-entry VA and queries `GetConflictReport`. Builds via its own CMakeLists (DLL build). |
| Query-VA derivation | `K.api->ResolveAddressByName("IsInCombat_callsite_26b")` returns the id-1006 BASE (pattern-hit RVA `0x5605BC` mapped to a runtime VA); the verifier subtracts 4 to reach the function entry. That entry VA catches BOTH sources: the bytes patch is reported if queryVA ∈ `[appliedPatchAddr, appliedPatchAddr+1)` = `[entry, entry+1)` (replacement `48` is 1 byte at base−4); the hook is reported if queryVA == the chain's `targetVa` = base−4. (Contrast comp-03-B, which queries `ResolveAddressByName("IsInCombat_callsite_with_stack_frame")` DIRECTLY — for id 1007 the pattern-hit IS the entry, no −4.) |
| Engine status | READY (Phase 4b Batch 3) |
| Auto-pass check | At `kcdxPlugin_PostGameLoad`: `GetConflictReport(function-entry VA)` PASSES iff EXACTLY 2 entries — one named `comp02_patch` with `kind=Patch` AND `applied!=0`, one named `comp02_hook` with `kind=Hook` AND `applied!=0`. Classified by NAME+kind (not blind count) so a FAIL names precisely which source is missing / not-applied. OUTCOME MAP: **PASS** → cross-engine coexist HOLDS (patch engine + hook_chain coexist on one site, like the legacy). **FAIL, one of ours present-but-applied=0** → coexist does NOT hold (new hook_chain may not relocate/survive a kcdx.bytes patch the way legacy MinHook did, OR the engines don't see each other's overlap — a REAL FINDING: engine gap or intentional reframe). **FAIL, `<` both present (report-blind / one absent)** → a `GetConflictReport` VA-matching issue, DISTINCT from the coexist question. The FAIL reason lists every returned entry (name/kind/applied) and distinguishes "entry absent" from "entry present but applied=0". An InputLoaded backstop reports loud FAIL if PostGameLoad never fired. If EITHER install CALL returns a 0 handle (a registration error), COMP-02 FAILs at Load with the teaching reason. No player input. |
| Last result | ✅ PASS (`6697dbd`, live run 2026-05-25, suite 102/109): cross-engine coexist HOLDS. Engine log shows the fixed order — `[comp02_patch] applied successfully at 0x…05B8: 48 -> 48` (patch FIRST, on pristine bytes), then `hook_chain: installed C replace 'comp02_hook'` (hook SECOND, detours the patched prologue); NO byte-mismatch abort. `GetConflictReport(entry VA)` = [comp02_hook(Hook=applied), comp02_patch(Patch=applied)] — both present, both applied. The kcdx.bytes patch engine + the kcdx.hook hook_chain coexist on one site, like the legacy `[[patch]]`+`[[hook]]`. The first launch (probe) FAILED with the hook applied first (patch byte-verify saw `E9`, aborted) — root cause the apply-order comparator's name tiebreak (`comp02_hook` < `comp02_patch` at one plugin priority); fixed by the `lua_registry` ApplyZone kind rank (`Kind::Bytes` before `Kind::Hook`, commit `6697dbd`). |
| Notes | Phase 4b Batch 3 migration off the legacy `[[patch]]`+`[[hook]]` surface onto `kcdx.bytes` + `kcdx.hook`, on ONE site, in ONE plugin. The most common patch+hook coexistence case in real mods. The identity `48`→`48` patch is PRESERVED as a genuine no-op — the test is about the OVERLAP applying, not a behavior change. No `[load_order].priority` needed: a patch + a hook in one plugin are not a replace-vs-replace conflict. `offset = -4` on a NAMED target is exercised on BOTH `kcdxBytesOptions.offset` ("applied after locator resolution") and `kcdxHookOptions.offset` ("applied after resolution"). |

## COMP-03: Two `kcdx.hook{replace}` on the same named function (cross-plugin)

| Field | Value |
|---|---|
| Scenario | Plugin A (pure Lua) and Plugin B (C++ DLL) both install a `replace` at the same named function entry. |
| Engine behavior expected | Cross-plugin first-wins via PLUGIN load-order priority: the lower-`[load_order].priority` plugin's entry sorts first (apply pass orders by plugin priority asc, name asc), does the first-touch, and wins; the higher one is CanCoexist-rejected (replace-vs-replace is exclusive). |
| Test plugin pair | [`comp-03-hook-on-hook-A/`](comp-03-hook-on-hook-A/) (winner, pure Lua, `[load_order].priority=10`) + [`comp-03-hook-on-hook-B/`](comp-03-hook-on-hook-B/) (loser, C++ DLL verifier, `[load_order].priority=20`) |
| Site | `IsInCombat_callsite_with_stack_frame` (Address Library id 1007, WHGame.dll RVA `0x566040`, function entry). Distinct from COMP-02's `0x5605BC`; AOB ends in `3C 01` vs COMP-02's `3C 02`. Replace signature `bool (ptr self)` (seed carries none for id 1007). |
| Engine status | READY (Phase 4b) |
| Auto-pass check | Plugin B's DLL resolves the target VA via `ResolveAddressByName("IsInCombat_callsite_with_stack_frame")`, calls `GetConflictReport(va)` at `kcdxPlugin_PostGameLoad`, asserts exactly 2 entries — one `comp03_a` applied!=0, one `comp03_b` applied==0, both kind=Hook. InputLoaded backstop reports loud FAIL if the after-phase never fires. |
| Last result | ✅ PASS (`4c66a2b`, live run 2026-05-25, suite 101/108): GetConflictReport at the id-1007 site = [comp03_a(hook=OK), comp03_b(hook=ABORTED)] — A ([load_order].priority 10) won the replace, B (20) CanCoexist-rejected, both kind=Hook. Phase 4b Batch 2 migration off legacy `[[hook]]` bytes= first-wins onto cross-plugin `kcdx.hook{replace}` (verified via the extended GetConflictReport). |
| Notes | Both replaces return `false` (the migration of the legacy `31 C0 C3` xor-eax,ret detour). The function is a combat-state predicate; returning false at boot is harmless (player not in combat in the title flow). The PLUGIN priority is the only deterministic lever — there is no per-hook priority knob on `kcdx.hook`. |

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
| Auto-pass check | On `input_loaded`, A publishes `outfit_changed` with `{ x=42, name="Noble" }`. B's `kcdx.on("ts.comp_09_pubsub_a.outfit_changed", fn)` callback fires and reports `COMP-09-pubsub` PASS iff it received a table payload with `payload.x==42 && payload.name=="Noble"`. A correct fire ALSO proves A's publisher namespace resolved (the event only reaches B if stamped exactly under A's name) AND the identity-from-inside-a-callback probe (publish ran from within A's input_loaded callback). No player input. |
| Last result | ⏳ PENDING (in-game verified at the checkpoint launch) |
| Notes | Identity-probe outcome map: B fires with the right payload → publish identity resolves (`OwningPluginForCurrentCall` stamps the publisher correctly even from inside a dispatched callback). Never fires / wrong payload → a RegisterScriptOwner coverage gap (wrong namespace) or a payload-by-reference break — surface before sub-9 lands. Anonymous publisher (`OwningPluginForCurrentCall` → "") is NOT dropped: warned + fired under `"<anon>.event"`. The require'd-module identity gap (`lua_registry.cpp:485`) is not exercised here (would need a require'd helper whose path was never RegisterScriptOwner'd) — flagged for a later sub-test if a real plugin hits it. |

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
| Engine behavior expected | A `[load_order].priority` 30, B 70. Run-order = `["a.before", "b.before", "a.after", "b.after"]`. |
| Channels | (vi) plugin Lua. C++ PARITY: the both-phase execution model + the `lua_after` slot are language-agnostic engine behavior; the C++ both-phase ordering test (cap-29) is the NEXT step in this feature — COMP-11 is the Lua-side proof. |
| Engine status | LIVE (per-entry-zone execution model step 7) — uses the `[entrypoints].lua` + `.lua_after` slots over `kcdx.publish` / `kcdx.on` (sub-9); the before-phase priority order is real per the step-7 `RunAll` priority sort. |
| Test plugin pair | [`comp-11-both-phase-order-a/`](comp-11-both-phase-order-a/) (priority 30, the ASSERTER — subscribes + collects + asserts; owns the row) + [`comp-11-both-phase-order-b/`](comp-11-both-phase-order-b/) (priority 70, pure publisher). Both pure Lua, both slots each. |
| Cross-plugin recording | Step 6 made require'd modules per-plugin isolated (COMP-10), so two separate-owner plugins CANNOT share a require'd module — the only cross-plugin channel is `kcdx.publish` / `kcdx.on` (sub-9, COMP-09). Each slot publishes the bare event `phase_token` with payload `{ slot="before"\|"after" }`; the engine stamps it `<publisher>:phase_token` (publisher in the event NAME, slot in the payload). |
| Deterministic ordering | Option B (lowest-priority-asserter), no missed-token hole: A is the LOWEST priority (30), so A's plugin.lua runs FIRST in RunAll — before ANY token publishes. A subscribes to both plugins' `phase_token` events at the TOP of its plugin.lua, BEFORE publishing its own `a.before`. `kcdx.publish` fires subscribers SYNCHRONOUSLY, so every token (A's own `a.before` published right after subscribing, B's `b.before` later in RunAll, both afters in RunAfterEntrypoints) publishes after A's subscriptions are live → none can be missed. Had A not been lowest priority, `b.before` could publish before A subscribed — that is the hole option B closes. |
| Auto-pass check | `COMP-11-both-phase-order`: at `input_loaded` (first update tick, after RunAll + RunAfterEntrypoints) A reads the collected ordered sequence and reports PASS iff it equals `["a.before", "b.before", "a.after", "b.after"]`. The single ordered comparison proves the phase boundary (both befores before any after) AND the per-phase priority interleave (30 before 70 in each phase) at once — no weaker subset of tokens can false-pass it. No player input. |
| Last result | ✅ LIVE (kcdx-dev 17:17 — run-order == `["a.before","b.before","a.after","b.after"]`; phase boundary + per-phase priority interleave both confirmed) |
| Notes | The Lua-side proof of the both-phase per-entry-zone execution model (step 7). Distinct from COMP-09 (single publish/subscribe, payload-by-reference) — COMP-11 reuses the COMP-09 pub/sub mechanism as a cross-plugin RECORDER for run-order, asserting the engine's RunAll-then-RunAfterEntrypoints phase boundary + the load-order priority interleave WITHIN each phase. The C++ mirror (cap-29) is the next step. |

## COMP-12: cross-plugin author-target name collision (self > engine > other)

| Field | Value |
|---|---|
| Scenario | Two plugins (A + B) EACH declare a target with the SAME bare name `combat_check` in their own `targets.toml`. A's is a GOOD locator (luaopen_math by `address_id=1172` — verified RVA + ABI; an UNHOOKED verified leaf, located by id, no scan, so the hook resolves AND installs); B's is a DELIBERATELY-BOGUS non-matching pattern. A third reference — here plugin A itself — hooks the bare name `combat_check`. The bare collision must resolve by self > engine > other (`naming-namespaces.md`): the CALLING plugin's OWN target wins. |
| Engine behavior expected | A's `kcdx.hook{ target="combat_check" }` resolves A's OWN target (self tier), not B's. The hook applies (A's good locator). The bare collision ALSO triggers the once-per-session `NAMESPACE` warn naming both owners + teaching the prefix fix. |
| Channels | (vi) plugin Lua. C++ PARITY: the C++ author-target registration + its precedence resolution is restructure parity-debt (NYI mirror in `docs/cpp/targets.md`); this is the Lua-side proof. |
| Engine status | ✅ LIVE (author-targets feature) — precedence in `address_library::{ResolveByName,ResolveSignatureByName,FindResolvedAuthorTarget}` (shared once-per-session bare-collision warn dedup). This row is the regression test only. |
| Test plugin pair | [`comp-12-target-collision/comp_12_target_a/`](comp-12-target-collision/comp_12_target_a/) (priority default; the ASSERTER — owns the row) + [`comp-12-target-collision/comp_12_target_b/`](comp-12-target-collision/comp_12_target_b/) (the COLLIDER — declares the same bare name, bogus locator, asserts nothing). Both pure Lua + `targets.toml`, valid bare `[plugin].name`. |
| Auto-pass check | `COMP-12-self-wins` (boot-only): at `kcdx.on("ready")`, A's `kcdx.hook{ target="combat_check" }` handle reads `:applied()==true` — falsifiable proof self won, because had B's bogus pattern leaked into A's resolution the hook would have failed to apply (the pattern matches nothing). No player input. |
| Manual confirm | `COMP-12-collision-warn` [manual]: the once-per-session bare-collision warn line (category `NAMESPACE`, naming both `combat_check` owners) is observable in the log. Not auto-asserted — the auto-assertion is that SELF resolved, not the warn text. |
| Last result | ✅ PASS (kcdx@a35d339, 2026-05-23) — A's id-1172 target resolved + installed; `COMP-12-self-wins :applied()==true`; the `NAMESPACE bare_name_collision ... resolved_to="self" winner="comp_12_target_a"` warn fired in the log. Repointed from id 1003 (already entry-hooked by cap-03 → first-wins loss) to the unhooked id 1172. |
| Notes | The cross-plugin precedence proof for author-declared targets (COMP companion to CAP-33). A self-wins is value-distinguishable: A's GOOD locator (luaopen_math by `address_id=1172` — an UNHOOKED verified leaf; resolves + installs; located by id so no scan, immune to the entry-prologue-overwrite problem an entry AOB would hit) vs. B's BOGUS pattern (would fail to apply if it leaked), so `:applied()==true` cannot false-pass. B's target carries a signature so its only failure mode is "pattern did not resolve", never "missing ABI". |

## COMP-13: zone_gate rejection path end-to-end (subject + observer)

| Field | Value |
|---|---|
| What | The end-to-end proof of zone_gate's rejection path: a plugin declaring `[load_order].zone = "before_game"` that "calls" an After-required API gets rejected at config-load time AND the rejection is queryable from another plugin via `kcdx.plugin.is_rejected`. Two plugins: the SUBJECT (`ts.comp_13_zone_gate_subject`) declares `zone="before_game"` so the synthetic After-required capability entry `kcdx.zone_gate_test_after_only` (`src/zone_gate.cpp` `kCapabilities[]`) trips its `engineAccepted=false` flip — its `plugin.lua` is INERT BY DESIGN and never runs; the OBSERVER (`ts.comp_13_zone_gate_observer`, zone-compatible default `after_game`, NOT rejected) reads `kcdx.plugin.is_rejected("ts.comp_13_zone_gate_subject")` at `kcdx.on("ready")` and asserts `(rejected==true, reason contains "after_game")`. End-to-end proof of all three rejection-path mechanisms together: the capability table check at `Check()`, the `EvaluateAllPlugins` per-plugin loop that records into `g_rejected` and flips `engineAccepted`, AND the `kcdx.plugin.is_rejected` accessor. |
| Channels | (vi) plugin Lua. C++ PARITY: there is no `kcdx.plugin.is_rejected` C++ mirror today — `kcdxPluginInterface::IsRejected(name)` is restructure parity-debt (the zone_gate Lua accessor landed first as the Lua-side proof; a C++ accessor lands in the C++ phase). |
| Engine status | ✅ LIVE (zone_gate feature, sub-1 `448d9ff` two-flag + sub-2 `5828373` gating engine + wiring + log enrichment + sub-3 `a3bd3df` synthetic stub + `kcdx.plugin.is_rejected` + docs + sub-4 `b1718ed` comp-13 regression; two checkpoint-surfaced follow-ups landed: `836f568` manifest-key correction in the subject (at that time `[plugin].default_position` was the per-plugin schema; the Phase-7 zone-rework subset has since renamed it to `[load_order].zone`, which the subject now uses) + `9960f13` engine-side g_rejected key-shape fix (zone_gate stores + queries the 2-dot `<author>.<plugin>` form to match the cross-plugin `kcdx.plugin.is_rejected` query shape per naming-namespaces.md)). |
| Test plugin pair | [`comp-13-zone-gate-subject/`](comp-13-zone-gate-subject/) (the REJECTED — declares `zone=before_game`, asserts nothing, plugin.lua never runs) + [`comp-13-zone-gate-observer/`](comp-13-zone-gate-observer/) (the ASSERTER — zone-compatible, owns the row). Both pure Lua. |
| Deterministic ordering | NO race. `zone_gate::EvaluateAllPlugins` runs in `LoadAllConfigs` at config-load time — well BEFORE any plugin.lua runs and well BEFORE the after_game apply pass. The subject's rejection is recorded into `g_rejected` synchronously at config-load and flips its `engineAccepted=false` before any init site reaches it. By the time the observer's `kcdx.on("ready")` handler fires (end of the after_game apply pass), the rejection has been on file for orders of magnitude longer than any plugin-load ordering window; the query reads state set well in the past. |
| Auto-pass check | `COMP-13-zone-reject` (boot-only): at `kcdx.on("ready")`, the observer reads `kcdx.plugin.is_rejected("ts.comp_13_zone_gate_subject")` and reports PASS iff `rejected==true` AND `reason` is a string AND `string.find(reason, "after_game", 1, true)~=nil` (plain-find — substring search, no Lua patterns). The substring `"after_game"` is verified against the binder's reason-string shape at `src/zone_gate.cpp::Check()` lines 114-122 (`"declared zone='before_game' but calls kcdx.zone_gate_test_after_only (requires zone='after_game' in kcdx 0.2.0)"` — the `requires zone='after_game'` clause is the match site). No player input. |
| Last result | ✅ LIVE (2026-05-24, `9960f13`): PASS. Live log at 16:25:09: `RESULT name=COMP-13-zone-reject verdict=PASS reason="subject 'ts.comp_13_zone_gate_subject' rejected by zone_gate with reason 'declared zone='before_game' but calls kcdx.zone_gate_test_after_only (requires zone='after_game' in kcdx 0.2.0)' (contains 'after_game'...)"`. End-to-end: subject resolved to `zone=before_game`, gate emitted PLUGIN_REJECTED for `'ts.comp_13_zone_gate_subject'`, both DLL Load + Lua entrypoint skipped via the enriched `(rejected by zone_gate: ...)` skip-log, subject's regression-canary absent from the log, observer's accessor returned `(true, reason)` with the right substring. Suite `72/80` (sole FAIL: CAP-20-target-nosig, parallel-chat in-flight, unrelated). |
| Notes | End-to-end proof of the zone_gate rejection path through both plugin-init paths (the gate's flip propagates through `RunAll`'s skip-log) AND the `kcdx.plugin.is_rejected` accessor (the cross-plugin observable). The synthetic capability entry `kcdx.zone_gate_test_after_only` is the gate-trigger today (the only non-`Either` row in `kCapabilities[]`); when a real API flips to non-Either in a future kcdx version, this row's mechanism enforces it directly without re-wiring — only the trigger changes. Subject's `plugin.lua` is inert-by-design: the gate flips `engineAccepted=false` at config-load before any RunAll site reaches it, so its file body is never executed (a canary `kcdx.log.info` line is there as a regression sentinel). |

## COMP-14: GetConflictReport reports kcdx.hook (hook_chain) entries incl. the rejected loser

| Field | Value |
|---|---|
| What | `GetConflictReport(target)` now reports `kcdx.hook` (hook_chain) entries — BOTH the live-chain winner (`applied != 0`) AND the `CanCoexist`-rejected loser (`applied == 0`) — not just the legacy conflict_engine patch/hook lists. Pre-feature a `kcdx.hook` target returned 0 entries; this is the C++-side close of that gap (the Lua mirror remains owed, `docs/outstanding-work/lua-conflict-report-mirror.md`). |
| Engine status | ✅ LIVE — `f91a7d4` (hook_chain records `chain->rejected` per target + `GetParticipantsAtTarget(va)` yields winners from `entries` + losers from `rejected`) + `5f7d997` (`Thunk_GetConflictReport` in `src/interfaces.cpp` merges those as a third source after the legacy patch + legacy-hook loops). `[[mid_hook]]`/mode=mid conflicts are NOT reported by design (mid rejects via sole-ownership, not CanCoexist — same contract as the legacy path). |
| Channels | C++ DLL (`kcdxInterface::GetConflictReport` + `kcdxHookInterface::Replace`). The conflict-report introspection is C++-only today; the Lua mirror (`kcdx.conflict`) is tracked parity-debt (NYI in `docs/cpp/index.md`). |
| Test plugin | [`comp-14-conflict-report-hook-chain/`](comp-14-conflict-report-hook-chain/) (C++ DLL; `test_suite_only`; owns the row). Installs TWO `replace` hooks on ONE plugin-local stub `Comp14_Target` via `opts.address`. Replace-vs-replace is exclusive in v1; the deferred apply pass orders by `(priority asc, name asc)` (NOT call order), so the name tiebreak decides — `comp14_a_winner` sorts first → first-touch → applied, `comp14_b_loser` sorts second → CanCoexist-rejected. Deterministic within one plugin via the name sort, no priority/second-plugin needed (cap-20's precedent). |
| Auto-pass check | `COMP-14-conflict-report` (boot-only): at `kcdxPlugin_PostGameLoad` (after the apply pass) calls `api->GetConflictReport(reinterpret_cast<uintptr_t>(&Comp14_Target), entries, 8)` and PASSES iff it returns exactly 2 entries, one `applied != 0` named `comp14_a_winner` and one `applied == 0` named `comp14_b_loser`, both `kind == kcdxConflictEntryKind_Hook`. Falsifiable: 0 = the hook_chain third source never ran (pre-feature); 1 = the rejected loser was discarded (pre-step-1); 2-both-applied = CanCoexist wrongly let two replaces coexist. The query key `&Comp14_Target` is the SAME VA the two installs resolved to (`opts.address` → `ResolveLocator` returns it verbatim → chain `targetVa`), so this also proves `GetConflictReport(va)`'s `va` matches hook_chain's `targetVa` for the address-locator path. An InputLoaded backstop loud-FAILs if `PostGameLoad` never fired. No player input. |
| Last result | ✅ PASS (`4677088`, live run 2026-05-25, suite 101/108): GetConflictReport(0x…1620)=2 entries [comp14_a_winner(applied=1,kind=Hook), comp14_b_loser(applied=0,kind=Hook)]; the engine log confirms b_loser was CanCoexist-rejected ("target already has a 'replace' hook"). Legacy GetConflictReport callers (comp-02, comp-03-B) + all pre-built C++ rows still PASS — third-source addition is non-disruptive (AP11 + legacy path unbroken). |
| Notes | The AP7 + docs-discipline close of the "GetConflictReport covers hook_chain" feature. Proves the C++ conflict report SEES the new kcdx.hook surface incl. the CanCoexist-rejected loser — the loser previously existed only as a Failed Lua-registry handle, never in any report. Docs landed with it: `docs/cpp/hook.md` §"Conflict report" + the `docs/cpp/index.md` map row (Lua mirror marked NYI), and `docs/outstanding-work/lua-conflict-report-mirror.md` updated to note the C++ gap is now closed. |

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
(the `⏳ PENDING [manual]` save/load rows need an in-game gesture; the
former `cap-04-c` standing FAIL is retired with the legacy `[[mid_hook]]`
schema — CAP-04 is now the mid-on-`kcdx.code` composition). Earlier roll-up:
**21/21 passing** as of the 2026-05-20 18:32 dev-log run, before the
Phase 2b `kcdx.hook` / `kcdx.command` / per-entry-zone subs landed.

| Row | Status | Last verified at SHA | Notes |
|---|---|---|---|
| CAP-01 | ✅ PASS (1d0faf1) | _Phase 4a pilot_ | outfit-swap-style byte rewrite, now via `kcdx.bytes` with a `target=` NAME locator (Phase 4a pilot migration off legacy `[[patch]]`); `applied()==true` + read-back of post-rewrite site `45 31 F6` |
| CAP-03 | ⏳ PENDING | _test-fix_ | function hook via `kcdx.hook{before}` on an un-named CGame::Update callee (pattern AOB + `signature="void (ptr)"`), callback fires (counter>0); migrated off legacy `[[hook]]`+pak. ROOT CAUSE of the PASS-then-FAIL flip (found 2026-05-26 via fresh-frame probe): a STALE LEGACY PAK `<game>/mods/kcdx_test_cap_three/` — the pre-migration pak-Lua CAP-03 (replaced in-repo by `plugin.lua` in `66461b0`) was never removed from the live install; it reported CAP-03 FAIL into the same aggregator from a load event, overwriting the migrated plugin's genuine PASS. FIX: deleted the orphan pak from `mods/` (live-install only — the repo already has no `cap_three`). The migrated `plugin.lua` was already correct; its `_G` PASS-latch is harmless belt-and-suspenders, NOT the cause (it can't suppress a separate module's report). |
| CAP-04-mid-on-code-run | ✅ PASS (`12d24b3`) | _Phase 4b Batch 4_ | `kcdx.hook` mode=mid installed on a `kcdx.code`-allocated stub; callback returns nothing → captured `add` runs → stub call returns 110. Composes the two author verbs (kcdx.code allocate + kcdx.hook mid on the allocation); distinct from cap-21 (C++-floor stub) and cap-30/cap-40 (allocate, never hook). Migrated off legacy `[[trampoline]]`+`[[mid_hook]]` (`cap-04-midhook`) |
| CAP-04-mid-on-code-skip | ✅ PASS (`12d24b3`) | _Phase 4b Batch 4_ | mode=mid on the same `kcdx.code` stub; callback returns `"skip"` → captured `add` skipped → stub call returns 10. Proves skip-original codegen reaches a `kcdx.code`-allocated target. Last legacy `[[trampoline]]`/`[[mid_hook]]` consumer retired (C++ Mid now also skips via the v2 int-return — see CAP-42) |
| CAP-05 | ⏳ PENDING | _test-fix_ | pak Lua `dynamic_hook` install. Test-fix: fixture retargeted off the archived hello-plugin onto cap-05's own `kcdx.cap05.probe` (registered by new companion `cap-05.dll`) — self-owned (test-suite.md). Pak needs rebuild (`build-pak.ps1`) |
| CAP-07 | ✅ LIVE | `03dd155` | trampoline branch / local pool allocations |
| CAP-08 | ✅ LIVE | `03dd155` | engine messages + lifecycle |
| CAP-09 | ✅ LIVE | `03dd155` | `kcdxTaskInterface` round-trip |
| CAP-10 | ✅ LIVE | `03dd155` | `kcdxScriptingInterface` C++ → Lua round-trip |
| CAP-11 | ⏳ PENDING | _test-fix_ | `kcdx.lua.cfunction_address` resolution. Test-fix: addresses cap-05's own `kcdx.cap05.probe` (companion `cap-05.dll`) instead of the archived hello-plugin's `kcdx.hello.greet` — self-owned (test-suite.md). Pak needs rebuild |
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
| CAP-20-target-nosig | ⏳ PENDING | _test-fix_ | `kcdx.hook{ target = "IConsole_AddCommand", ... }` (a verified row with NO seed signature, no explicit `signature=`) is REJECTED SYNCHRONOUSLY (`nil` + teaching error) — the engine never invents a signature (AP2). Asserted inline at registration: `h==nil` + `err:find("has no signature")` + `err:find("needs an ABI")`. Test-fix: expected substring was the stale `"no verified signature"` (never emitted) → matched to the actual teaching message; still requires nil + the teaching contract, not weakened (`cap-20-hook-modes`) |
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
| COMP-09-pubsub | ✅ LIVE | sub-9 | `kcdx.publish` cross-plugin pub/sub: A publishes `outfit_changed` `{x=42,name="Noble"}` from its `input_loaded` handler; B's `kcdx.on("ts.comp_09_pubsub_a.outfit_changed", fn)` asserts the table arrived by reference + the publisher namespace resolved (`comp-09-pubsub-a` + `comp-09-pubsub-b`) |
| CAP-25-multifile-attribution | ✅ LIVE | multi-file | multi-file `require` + complete source attribution: a require'd `helper.lua` subscribes to `ts.cap_25_multifile_attribution.multifile_event` + publishes bare `multifile_event` from a deferred `input_loaded` callback; the callback fires (PASS) ONLY if the helper's `kcdx.*` calls resolved to the plugin not `<anon>`. Lua-surface parity (C++ splits natively) (`cap-25-multifile-attribution`) |
| CAP-26-command-roundtrip | ✅ LIVE | step 2 | `kcdx.command` + `kcdx.console.execute` Lua round-trip: registers `cap26_cmd`, self-fires `kcdx.console.execute("cap26_cmd 42 hello")` at `input_loaded`; PASS iff execute returned true AND callback fired AND `#args==2`, `args[1]=="42"`, `args[2]=="hello"`, `args.raw` contains `"cap26_cmd"`. The Lua analog of the C++ CAP-13; `kcdx.console.execute` is the Lua-parity side of the existing C++ `ExecuteString` (`cap-26-lua-command`) |
| CAP-27-immediate | ✅ LIVE | step 5 | `kcdx.command` IMMEDIATE arm (Lua): `cap27_immediate` registered from the `lua_after` slot (post-`console::Init`, `g_ready=true` → `RegisterCommandNow` direct) and self-fired `kcdx.console.execute("cap27_immediate 9 beta")` in after.lua; PASS iff execute returned true AND callback fired AND `#args==2`, `args[1]=="9"`, `args[2]=="beta"`, `args.raw` contains `"cap27_immediate"`. The Lua mirror of CAP-13's C++ immediate path (`cap-27-command-timing-arms`) |
| CAP-27-coexist | ✅ LIVE | step 5 | `kcdx.command` deferred/immediate coexistence boundary: `cap27_deferred` (registered from plugin.lua → queued, flushed at `console::Init`) AND `cap27_immediate` (registered from after.lua → immediate) BOTH dispatch in after.lua with their own args; PASS iff deferred fired `("7","alpha")` AND immediate fired `("9","beta")` — both land in `g_slots` without clobber. Distinct from CAP-26 (deferred arm only) (`cap-27-command-timing-arms`) |
| CAP-30-alloc | ✅ LIVE | cap-30 | `kcdx.code` allocation + live writable region + NOP-pad (synchronous at load): `kcdx.code{ size=64 }` returns a live `kcdx.memory.pointer`; the accessors take the VALUE only and `:add(N)` navigates to an offset, so `:set_byte(0xAB)`+`:add(4):set_dword(0xDEADBEEF)` round-trip via `:get_byte()`/`:add(4):get_dword()`; `kcdx.code{ bytes="C3", size=8 }` reads `get_byte()==0xC3` (head) + `add(4):get_byte()==0x90` (NOP-padded tail). The Lua-verb counterpart of CAP-07. A nil/non-writable region fails the read-back (`cap-30-lua-code`) |
| CAP-30-export | ✅ LIVE | cap-30 | `kcdx.code` export-symbol interlock (asserted at `ready`): `kcdx.code{ export="ts.cap_30_lua_code.region" }` publishes the address (`symbols::Register`); a deferred `kcdx.bytes{ target_symbol="ts.cap_30_lua_code.region", original="90", replacement="90" }` (NOP-over-NOP, idempotent) resolves it at the apply pass; PASS iff the handle `:applied()==true` — proving `target_symbol` resolved the export to the live region. No Lua symbol-resolve surface exists (`kcdx.addr` is the Address Library snapshot, not the runtime symbols table), so `kcdx.bytes` is the consumer (`cap-30-lua-code`) |
| COMP-11-both-phase-order | ✅ LIVE | step 7 | both-phase Lua execution model: two plugins (A prio 30 asserter, B prio 70 publisher), each with a `lua` + `lua_after` slot; each slot publishes a phase token via `kcdx.publish`; A collects via `kcdx.on` and at `input_loaded` asserts the run-order == `["a.before","b.before","a.after","b.after"]` — proving the phase boundary (RunAll before RunAfterEntrypoints) AND the per-phase load-order priority interleave. Lowest-priority-asserter timing guarantees no missed token (`comp-11-both-phase-order-a` + `comp-11-both-phase-order-b`) |
| CAP-29-both-phase-dll | ✅ LIVE | cap-29 | both-phase C++ lifecycle: a single C++ DLL exporting BOTH `kcdxPlugin_Load` AND `kcdxPlugin_PostGameLoad`; a monotonic seq counter records Load seq=1, PostGameLoad seq=2; PostGameLoad asserts load_ran && post_seq>load_seq → proves both exports fired AND Load ran before PostGameLoad. First live exercise of the sub-4 PostGameLoad export; the C++ parity mirror of COMP-11. An InputLoaded backstop (registered in Load) reports a loud FAIL if PostGameLoad never fired, so the row never sits silently PENDING. Pure C++ DLL — builds via its own CMakeLists, not build.ps1 (`cap-29-both-phase-dll`) |
| CAP-33-engine-tier | ✅ PASS | author-targets | `kcdx.hook{ target="kcdx.luaL_loadfile" }` (engine seed under reserved `kcdx` author root, 2-segment explicit form) resolves — engine tier of self>engine>other coexists with the author's own targets. PASS kcdx@a35d339 2026-05-23 (`cap-33-author-targets`) |
| CAP-33-prefixed | ✅ PASS | author-targets | the explicit `"ts.cap_33_author_targets.luaopen_math_by_id"` (3-segment `<author>.<plugin>.<bare>`, address_id=1172, RVA — unhooked verified leaf, no scan) resolves directly. PASS kcdx@a35d339 2026-05-23 (`cap-33-author-targets`) |
| CAP-33-alias | ✅ PASS | author-targets | `kcdx.alias("up", "ts.cap_33_author_targets.luaopen_math_by_id")` + `kcdx.hook{ target="up" }` resolves via the local alias to the verified-id target. PASS kcdx@a35d339 2026-05-23 (`cap-33-author-targets`) |
| CAP-33-bytes-by-name | ✅ PASS | author-targets | `kcdx.bytes{ target="bool_leaf_safe_site" }` (lua_toboolean, address_id=1124 — a DISTINCT verified leaf NOTHING hooks → pristine prologue) resolves to a writable VA; idempotent no-op write (0x48 over 0x48, byte 0 read from WHGame.dll) applies. PASS kcdx@a35d339 2026-05-23 (`cap-33-author-targets`) |
| CAP-33-pattern-by-name | ✅ PASS | author-targets | THE §36 headline (`kcdx.hook{ target="openlibs_by_pattern" }`, no signature) — author-declared PATTERN target (luaL_openlibs id 1190, a verified `.text`-unique 16-byte entry AOB minted by `_research/phase8-fix-a/aob_scan.py`) supplies BOTH address and ABI by name. luaL_openlibs is entry-hooked by nobody so the prologue stays pristine for the by-name scan. PASS kcdx@03e6bd0 2026-05-23 (suite 66/74) — the §36 share guarantee proven end-to-end. See [`docs/outstanding-work/section36-pattern-target-aob.md`](../docs/outstanding-work/section36-pattern-target-aob.md) (RESOLVED). |
| COMP-12-self-wins | ✅ PASS | author-targets | cross-plugin author-target name collision: A + B both declare bare `combat_check`; A's is the GOOD locator (luaopen_math by address_id=1172, RVA — unhooked, resolves+applies), B's a BOGUS pattern. A hooks the bare name and self>engine>other picks A's OWN target — applied()==true (B's bogus pattern would fail if it leaked). Bare-collision NAMESPACE warn observed firing (resolved_to=self). PASS kcdx@a35d339 2026-05-23 (`comp-12-target-collision`) |
| COMP-13-zone-reject | ⏳ PENDING | zone_gate | zone_gate rejection-path end-to-end: subject (`ts.comp_13_zone_gate_subject`, zone=before_game) is rejected at config-load by the synthetic After-required entry `kcdx.zone_gate_test_after_only`; observer (zone-compatible, NOT rejected) asserts at `kcdx.on('ready')` that `kcdx.plugin.is_rejected("ts.comp_13_zone_gate_subject")` returns `(true, reason)` with reason containing `"after_game"` (the API's required zone, per `src/zone_gate.cpp::Check()`). End-to-end proof of the gate's flip + the cross-plugin `kcdx.plugin.is_rejected` accessor (`comp-13-zone-gate-subject` + `comp-13-zone-gate-observer`) |
| CAP-34-explicit-2dot | ⏳ PENDING | 2-dot-ns | `kcdx.hook{ target="ts.cap_34_two_dot_namespace.ui_pump_self" }` — explicit 3-segment `<author>.<plugin>.<bare>` form resolves directly under the 2-dot model (`cap-34-two-dot-namespace`) |
| CAP-34-explicit-1dot-kcdx | ⏳ PENDING | 2-dot-ns | `kcdx.hook{ target="kcdx.luaL_loadfile" }` — the 2-segment `kcdx.<seedname>` form (engine seed under reserved `kcdx` author) resolves (`cap-34-two-dot-namespace`) |
| CAP-34-bare-self | ⏳ PENDING | 2-dot-ns | `kcdx.hook{ target="ui_pump_self" }` (BARE — no prefix typed) resolves to THIS plugin's own target via self-tier (owningAuthor=ts, owningPlugin=cap_34_two_dot_namespace) (`cap-34-two-dot-namespace`) |
| CAP-34-alias-2dot | ⏳ PENDING | 2-dot-ns | `kcdx.alias("short", "ts.cap_34_two_dot_namespace.ui_pump_self")` + `kcdx.hook{ target="short" }` — alias substitutes the 3-segment form before the precedence walk (`cap-34-two-dot-namespace`) |
| CAP-34-cross-plugin-2dot | ⏳ PENDING | 2-dot-ns | `kcdx.hook{ target="ts.cap_33_author_targets.luaopen_math_by_id" }` — 3-segment cross-plugin reference resolves via the other-plugin tier of self>engine>other (`cap-34-two-dot-namespace`) |
| CAP-35-uninstall-basic | ✅ LIVE | `d03ffb1` | `kcdx.hook{ target="kcdx.luaopen_math" }`, at ready `:applied()==true` pre, `:uninstall()`, `:applied()==false` post — core handle:uninstall() lifecycle (step 2 `SetStatus(Removed)` flips the registry status atomic) (`cap-35-uninstall`) |
| CAP-35-uninstall-idempotent | ✅ LIVE | `d03ffb1` | second hook on `kcdx.luaopen_math`; at ready call `:uninstall()` twice — assert both calls return the handle userdata (self-return chaining contract) AND `:applied()==false` after (engine-layer idempotence: `hook_chain::Uninstall` returns true on unknown / already-handled ids) (`cap-35-uninstall`) |
| CAP-35-uninstall-tostring | ✅ LIVE | `d03ffb1` | third hook on `kcdx.luaopen_math`; at ready capture `tostring(h)` before + after `:uninstall()` — before contains `"applied"`, after contains `"removed"`, before ≠ after (the `H_tostring` Removed case step 1 added at lua_registry.cpp:204) (`cap-35-uninstall`) |
| CAP-35-uninstall-chain-survives | ✅ LIVE | `d03ffb1` | TWO before-hooks A + B on `kcdx.luaopen_math` (distinct names); both `:applied()==true` pre; uninstall A only; A `:applied()==false`, B STILL `:applied()==true` — proves `chain.entries.erase` removes only the one entry, trampoline + remaining entries stay live (multi-hook pattern from CAP-20-chain) (`cap-35-uninstall`) |
| CAP-35-uninstall-bytes-error | ✅ LIVE | `d03ffb1` | `kcdx.bytes` with a well-formed bogus 16-byte AOB (`DE AD BE EF ...`) + idempotent `0x90`-over-`0x90` write (no live memory written — apply scan finds 0 matches); at ready call `:uninstall()` inside `pcall` — `pcall` returns false (teaching `luaL_error` raised), error contains `"kcdx.bytes"` AND `"not yet supported"` (the exact substrings step 2's default branch emits at lua_registry.cpp:183-188 because the patch engine has no revert path — silently flipping status would be AP13) (`cap-35-uninstall`) |
| CAP-35-off-thread-skip | ⏳ PENDING | step 5-main | `kcdx.hook{ off_thread = "skip", before = function() end, target = "kcdx.luaopen_math" }` — parse test for the new `off_thread` knob (Phase 3 sub-1 step 5-main chunk 3 Lua parser). PASS iff the binder returns a non-nil handle (the string parsed cleanly + the payload field was threaded). The hook never fires this run; the install IS the proof (`cap-35-uninstall`) |
| CAP-35-off-thread-bogus | ⏳ PENDING | step 5-main | `kcdx.hook{ off_thread = "bogus", ... }` — teaching-error test. PASS iff the binder returns `(nil, err)` AND the error message contains the substring `"off_thread"` (lua-api-surface.md §"errors teach" — name the field so the author can find + fix it). FALSIFIABLE: a too-permissive parser (silent default-to-Marshal, missing validation) returns a non-nil handle → assert fails (`cap-35-uninstall`) |
| CAP-36-cpp-hook-before | ⏳ PENDING | chunk 5 | C++ DLL `kcdxHookInterface::Before(nullptr, &cb, &opts)` with `opts.address`+`opts.signature` on a DLL-internal stub; cb is the typed Before ABI (`void cFn(uintptr_t args[], int* outCount, /* typed args */)`); writes `args[0]=seed+1`, `*outCount=1`; PostGameLoad re-invokes the stub: `Cap36_Add_Before(10)==111` (`cap-36-cpp-hook-interface`) |
| CAP-36-cpp-hook-after | ⏳ PENDING | chunk 5 | C++ DLL `kcdxHookInterface::After` typed non-void ABI (`<typed_return> cFn(<typed_return> origReturn, /* typed args */)`); cb returns `origReturn+1000`; `Cap36_Add_After(10)==1110` (`cap-36-cpp-hook-interface`) |
| CAP-36-cpp-hook-around | ⏳ PENDING | chunk 5 | C++ DLL `kcdxHookInterface::Around` typed ABI (`<typed_return> cFn(<typed call_original>, /* typed args */)`); cb returns `2*call_original(seed)`; `Cap36_Add_Around(10)==220` — proves the typed call_original fn pointer arrival in RCX (D-c-fn-abi-2) (`cap-36-cpp-hook-interface`) |
| CAP-36-cpp-hook-replace | ⏳ PENDING | chunk 5 | C++ DLL `kcdxHookInterface::Replace` typed ABI (`<typed_return> cFn(/* typed args */)`); cb returns 42; original never runs; `Cap36_Add_Replace(10)==42` (`cap-36-cpp-hook-interface`) |
| CAP-36-cpp-hook-uninstall | ⏳ PENDING | chunk 5 | C++ DLL `kcdxHookInterface::Uninstall(handle)` lifecycle peer of CAP-35-uninstall-basic. Pre: `IsApplied==1`, hooked Cap36_Add_Uninstall(10)==5110; post-Uninstall: `IsApplied==0`, un-hooked Cap36_Add_Uninstall(10)==110 (cb no longer fires) (`cap-36-cpp-hook-interface`) |
| CAP-36-cpp-hook-raw-floor | ⏳ PENDING | chunk 5 | C++ DLL bypasses any wrapper; uses raw `api->QueryInterface(kcdxInterface_Hook, kcdxHookInterface_Version)` directly + threads `opts.owningPlugin = g_self` by hand. Same +1 mutation as CAP-36-cpp-hook-before but on a separate stub via the freshly-fetched interface ptr; `Cap36_Add_RawFloor(10)==111` (`cap-36-cpp-hook-interface`) |
| CAP-36-cpp-hook-crosslang | ⏳ PENDING | chunk 5 | Cross-language chain proof: paired Lua plugin (`cap-36-cpp-hook-interface-lua/`, priority 70) + C++ plugin (priority 30) both install a before-hook on the SAME `Cap36_Crosslang` stub VA (the C++ DLL hands the VA to the Lua sibling via `kcdx.cap36.addr_crosslang()`). C++ before fires first (+1), Lua before fires second (*2), stub adds 100: `Cap36_Crosslang(10)==122 = ((seed+1)*2)+100`. Uniquely distinguishable from C-only (111) / Lua-only (120) / reversed (121) / no-hooks (110). Corroborated by a Lua→C++ flag (`set_lua_fired`) AND the Lua-observed seed (must be 11, proving same-chain-same-site). PROOF the chunk-1 ChainEntry tagged union mediates Lua + C entries on one chain (`cap-36-cpp-hook-interface` + `cap-36-cpp-hook-interface-lua`) |
| CAP-37-wrapper-before | ⏳ PENDING | step 6 / Kcdx.h | C++ DLL via **Kcdx.h** `kcdx::hook::TryBefore<int(int), &cb>(K, nullptr, &opts)` (no-name `opts.address` on a DLL-internal stub; owningPlugin + derived signature auto-threaded — B2); cb is the NATURAL by-ref `void(int& seed){ seed += 1; }`. Proves the wrapper's by-ref Before write-back + auto `*outCount`. `Cap37_Before(10)==111` (`cap-37-kcdx-wrapper`) |
| CAP-37-wrapper-after | ⏳ PENDING | step 6 / Kcdx.h | C++ DLL via **Kcdx.h** `kcdx::hook::TryAfter<int(int), &cb>`; NATURAL `int(int origReturn, int seed)` returns `origReturn+1000`. Proves the wrapper's non-void After origReturn-prepend adapter. `Cap37_After(10)==1110` (`cap-37-kcdx-wrapper`) |
| CAP-37-wrapper-around | ⏳ PENDING | step 6 / Kcdx.h | C++ DLL via **Kcdx.h** `kcdx::hook::TryAround<int(int), &cb>`; NATURAL `int(int(*call_original)(int), int seed)` returns `2*call_original(seed)`. Proves the wrapper's typed `call_original` fn-ptr (pass-through Around adapter). `Cap37_Around(10)==220` (`cap-37-kcdx-wrapper`) |
| CAP-37-wrapper-replace | ⏳ PENDING | step 6 / Kcdx.h | C++ DLL via **Kcdx.h** `kcdx::hook::TryReplace<int(int), &cb>`; NATURAL `int(int seed){ return 42; }`; original never runs. Proves the wrapper's return-only Replace. `Cap37_Replace(10)==42` (`cap-37-kcdx-wrapper`) |
| CAP-37-wrapper-try-handle | ⏳ PENDING | step 6 / Kcdx.h | C++ DLL: `kcdx::hook::TryBefore<int(int), &cb>` returns a **non-zero** `kcdxHookHandle`; assert `K.hook->IsApplied(h)==true` after the apply pass. Proves the wrapper's Try\* handle path hands the handle back (vs the void `Before` form that swallows it). FALSIFIABLE: a zero handle or `IsApplied==false` → FAIL (`cap-37-kcdx-wrapper`) |
| CAP-37-wrapper-typemap | ⏳ PENDING | step 6 / Kcdx.h | C++ DLL: install over a multi-type stub `int(int, float, void*)` via `kcdx::hook::TryBefore<int(int, float, void*), &cb>` — the wrapper must derive the DSL `i32 (i32, f32, ptr)` from `<Sig>` for the no-name install to succeed + the slots to marshal. by-ref cb mutates the i32; f32 + ptr must survive. `Cap37_TypeMap(10, 5.0f, &sentinel)==1016` = `(10→11) + (int)5.0f + 1000-for-non-null-ptr`. The row that catches a future type→token regression — a wrong mapping fails the install or perturbs the observed value (`cap-37-kcdx-wrapper`) |
| CAP-38-cpp-gate-proceeds | ⏳ PENDING | sig-gate | sig-mismatch gate (C++ `kcdxHookInterface`): named target `kcdx.luaopen_table` (verified `i32 (ptr L)`, id 1173 — gameplay-cold, unhooked) + WRONG explicit `opts.signature="void (ptr L)"` (return-width delta) → the gate ERRORs + PROCEEDS with the explicit sig (behavior-c). At `PostGameLoad`: handle != 0 && `IsApplied()==true` (install-is-the-proof, identical to the Lua peer). Falsifiable vs an (a)-reject impl (handle 0 / IsApplied false → FAIL). Asserts install-proceeds ONLY — the gate is an install-time check; the row does NOT assert the detour fires (the cold target never fires; cap-36's own-function rows own the C-dispatch FIRING proof). Retargeted from the HOT `lua_settable` (id 1186), whose wrong-ABI thunk fired on the save-load path and crashed the suite (0xC8). (`cap-38-sig-mismatch-gate`) |
| CAP-38-cpp-gate-warn | ⏳ PENDING [manual] | sig-gate | the C++ gate-ERROR HARD line. Orchestrator greps the engine log for category `HOOK_SIG_GATE`, action `explicit_overrides_verified_hard`, `plugin=cap_38_sig_mismatch_gate`, `explicit_sig="void (ptr L)"`, `verified_sig="i32 (ptr L)"`, `used=explicit`, `severity=hard`, `crash_risk=true`. Pre-fix (silent trust) NO line fires; a downgrade to the soft WARN token also FAILS. The falsifiable signal the gate exists + escalates a Hard (return-width) conflict to ERROR. (`cap-38-sig-mismatch-gate`) |
| CAP-38-lua-gate-proceeds | ⏳ PENDING | sig-gate | sig-mismatch gate (Lua `kcdx.hook`): named target `kcdx.luaopen_table` (id 1173, gameplay-cold, unhooked) + WRONG explicit `signature="void (ptr L)"` (return-width delta vs verified `i32 (ptr L)`) → gate ERRORs + PROCEEDS. At `kcdx.on("ready")`: `h:applied()==true` (install-is-the-proof). Falsifiable vs an (a)-reject impl (`applied()==false` + non-empty `:reason()` → FAIL). The wrong-ABI thunk is installed on the cold leaf but never fires (cap-33 cold-leaf idiom). (`cap-38-sig-mismatch-gate-lua`) |
| CAP-38-lua-gate-warn | ⏳ PENDING [manual] | sig-gate | the Lua gate-ERROR HARD line. Orchestrator greps category `HOOK_SIG_GATE`, action `explicit_overrides_verified_hard`, `plugin=cap_38_sig_mismatch_gate_lua` (same keys as the C++ row, distinguished by `plugin`; `severity=hard`, `crash_risk=true`). Pre-fix NO line fires; a downgrade to the soft WARN token FAILS. (`cap-38-sig-mismatch-gate-lua`) |
| CAP-39-cpp-bytes-register | ✅ PASS (2b2e6f5) | sub-2 | C++ DLL `kcdxBytesInterface::Register` (via `K.bytes->Register`) deferred byte rewrite by NAMED target: `target="outfit_swap_callsite_aob"` (id 1004), `original="44 8A F0"`, `replacement="45 31 F6"` (the disassembler-test common path — a name, not hex). At Load `Register` returns a non-zero handle; at PostGameLoad `IsApplied(handle)==true` AND `K.memory->ReadBytes(site,3)==45 31 F6`. The deferred-apply-through-C++ proof; C++ peer of cap-01's Lua `kcdx.bytes`. FALSIFIABLE: zero handle / not applied / bytes unchanged → FAIL (`cap-39-cpp-bytes`) |
| CAP-39-cpp-bytes-uninstall-rejected | ✅ PASS (2b2e6f5) | sub-2 | C++ DLL `kcdxBytesInterface::Uninstall(handle)==false` — a byte rewrite has NO revert path; `IsApplied` stays true and the site stays `45 31 F6` (NOT reverted). The no-revert teaching, peer of cap-35's bytes-error row. FALSIFIABLE: Uninstall true / IsApplied flips false / site reverted to `44 8A F0` → FAIL (a revert would be AP13) (`cap-39-cpp-bytes`) |
| CAP-40-cpp-code-allocate | ✅ PASS (38f9dd5) | sub-3 | C++ DLL `kcdxTrampolineInterface::Allocate` (via `K.code->Allocate`): `bytes="B8 2A 00 00 00 C3"` (mov eax,42; ret), `bytesSize=6`, `size=10`, `pool=branch`. At PostGameLoad: region != null AND first 6 bytes read back == the written bytes AND tail `[6,10)==0x90` (NOP-pad) AND casting region to `int(*)()` and CALLING it returns **42** (executable proof). The all-in-one alloc+fill+pad peer of Lua `kcdx.code{...}`. FALSIFIABLE: null / wrong head / non-0x90 pad / call != 42 → FAIL (`cap-40-cpp-code`) |
| CAP-40-cpp-code-export | ✅ PASS (38f9dd5) | sub-3 | C++ DLL `K.code->Allocate(exportName="cap40_region")` (BARE name; engine stamps `<author>.<plugin>` prefix). At PostGameLoad: region != null AND `K.api->ResolveSymbolAs(K.self, "cap40_region")` == the allocated region address. Proves publish-via-Allocate + consume-via-ResolveSymbol — the `export=` gap sub-3 closes end-to-end. ResolveSymbolAs (NOT bare ResolveSymbol) is required: the symbol is stored under the prefix, so the self-tier resolver needs the owner handle to find the plugin's own export. FALSIFIABLE: unresolved / resolves to a different address → FAIL (`cap-40-cpp-code`) |
| CAP-40-cpp-code-export-standalone | ✅ PASS (38f9dd5) | sub-3 | C++ DLL `K.code->Export(K.self, "cap40_standalone", &static)==true` for an address the plugin already holds (no allocation). At PostGameLoad: `ResolveSymbolAs(K.self, "cap40_standalone")` == that address. Proves the standalone publish path. FALSIFIABLE: Export false / resolves wrong → FAIL (`cap-40-cpp-code`) |
| CAP-41-bytes-in-conflict-report | ✅ PASS (`275c288`) | conflict-report | C++ DLL: `kcdxInterface::GetConflictReport` now folds in `kcdx.bytes` (`kcdxBytesInterface::Register`) patches as a FOURTH source (`kind=Patch`). Registers the SAME named-target rewrite cap-39 proves (`outfit_swap_callsite_aob` id 1004, `44 8A F0`→`45 31 F6`) with a DISTINCT name `cap41_bytes_patch`. At `PostGameLoad` (precondition `IsApplied==true` && site resolved FIRST), `GetConflictReport(siteVA)` returns the merged entries and EXACTLY ONE is named `cap41_bytes_patch` with `kind=kcdxConflictEntryKind_Patch` and `applied!=0`. By-NAME not by-count: the site is co-located (cap-01 g_patches + cap-39 + cap-41 bytes-Register, all same-replacement idempotent-coexist → count grows). FALSIFIABLE: pre-feature 0 matches (bytes-Register invisible) → FAIL; kind!=Patch → wrong fold; applied==0 → accessor bug (`cap-41-cpp-bytes-conflict-report`) |
| CAP-42-cpp-mid-skip | ✅ PASS (`77686bc`) | mid-return-skip | C++ DLL: FIRST consumer of `kcdxHookInterface::Mid` (v2). A C++ mid hook at a controlled stub's `+2` capture site (`add rax,0x64`), capturing `rax:i64` (positional), callback returns `kcdxMidResult_Skip` → the `add` is SKIPPED → `fn(10)==10`. THE proof the v2 int-return skip channel works from C++ (pre-feature, the void Mid ABI, this was impossible). C++ PEER of CAP-21-skip (Lua `return "skip"`). FALSIFIABLE: returns 110 (add ran despite Skip) → the skip did not take → FAIL (`cap-42-cpp-mid-skip`) |
| CAP-42-cpp-mid-run | ✅ PASS (`77686bc`) | mid-return-skip | C++ DLL: a SECOND stub + C++ mid hook (`kcdxHookInterface::Mid`) capturing `rax:i64`, callback returns `kcdxMidResult_Run` → the `add` RUNS → `fn(10)==110`. Control: proves Run (0) lets the captured instruction execute AND that the int-return does not spuriously skip on 0. C++ PEER of CAP-21-run. FALSIFIABLE: returns 10 (skipped despite Run) → FAIL (`cap-42-cpp-mid-skip`) |
| COMP-02 | ✅ PASS (`6697dbd`) | _migration_ | `kcdx.bytes` patch + `kcdx.hook` detour on ONE function-entry VA (id 1006, offset −4); cross-engine coexist. `GetConflictReport(entry VA)` = 2 entries — `comp02_patch`(kind=Patch,applied) + `comp02_hook`(kind=Hook,applied). Patch engine + hook_chain coexist like legacy `[[patch]]`+`[[hook]]`. The probe launch caught a patch-after-hook apply-order bug (patch byte-verify aborted on the hook's `E9`); fixed by the ApplyZone kind rank (Bytes before Hook, `6697dbd`) — patch writes pristine bytes first, hook then detours the patched prologue (`comp-02-hook-on-patch`) |
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
