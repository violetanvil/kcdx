# kcdx capability test matrix

The "did we crack the vault" exercise. SKSE-class flexibility means
kcdx has to handle the full range of things real mods do — byte
rewrites, function detours, hooks, new game systems, save
serialization, console commands, cross-plugin APIs, conflicts,
load order, version drift. This matrix catalogs every primitive
kcdx exposes (today or planned), every plausible authoring
channel, and every competition/collision case, then walks each
through a live test that says PASS / FAIL / DEFERRED-PHASE-X.

It started life as the outfit-swap-in-combat repro matrix, hence
the folder name; we kept the name because outfit-swap is the
load-bearing live test for several of these rows. The matrix
itself is **phase-agnostic** — rows that test capabilities the
engine doesn't yet implement are marked `DEFERRED <reason>` and
get filled in when the engine catches up.

The matrix lives in [`kcdx/examples/outfit-swap-test-matrix/`](.).
Each test that needs its own plugin gets a subfolder
(e.g., `outfit-swap-patch/`); shared assets stay at this level.

---

## How to read this doc

Three sections:

1. **Capability rows** — one per kcdx primitive. Each row says
   what the primitive *does*, what *authoring channel(s)* it's
   reachable through, what the test plugin looks like, what
   PASS means, and the live result. Rows for unimplemented
   primitives carry `DEFERRED <phase>` instead of a live result.
2. **Competition rows** — one per collision/conflict scenario.
   Tests two-or-more plugins interacting on the same target or
   the same Lua registration.
3. **Real-world mod scenarios** — end-to-end thought experiments
   ("can someone write a magic-system mod with kcdx?"). For each
   scenario, walks the list of capabilities it needs and notes
   which rows above prove it's achievable.

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
| In-game test | Enter combat (draw weapon, attack NPC, take a hit). Try to open inventory and change outfit. Vanilla = popup "You can't switch outfits in combat." Patched = swap succeeds, popup never appears. |

Other tests target other sites; their site info lives in the
row itself.

---

## Isolation protocol

For each run:

1. Disable every other test plugin (rename `kcdx.toml` →
   `kcdx.toml.disabled-for-matrix`).
2. Verify only `dev-mode-enable/kcdx.toml` and the current test's
   plugin are active.
3. Verify mempatch.asi is NOT installed (`mempatch.toml` not
   present in `plugins/`).
4. Launch game, load save, run the in-game test specified by the
   row.
5. Capture timestamp; read kcdx.log + kcdx-dev.log + kcd.log.
6. Fill the row's Result table.
7. Disable the test plugin again before moving on.

Pak mods (`<game>/mods/`) come with their own enable/disable
dance — rename the folder to `<name>.disabled-for-matrix`.

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
| Test plugin | [`outfit-swap-patch/`](outfit-swap-patch/) |
| Site | The outfit AOB above |
| PASS = | kcdx.log: pattern matches=1, context matches=1, applied successfully. In-game: outfit swap works in combat. |
| Result | ✅ PASS (kcdx@918d5fb, applied at `0x00007FFCF9051759`, user confirmed) |
| Notes | Mempatch-compatible. The reference for every other primitive. |

## CAP-02: `[[hook]]` + `bytes` (TOML, native trampoline)

| Field | Value |
|---|---|
| What | Install MinHook detour at resolved address. Detour code is raw bytes provided in TOML. Original instructions get relocated into MinHook's trampoline. |
| Channels | (iii) `kcdx.toml` |
| Engine status | READY (Phase 4) |
| Test plugin | _TBD — need a target where we can write a meaningful detour without crashing_ |
| Site | _TBD_ |
| PASS = | kcdx-dev.log: DYNAMIC_HOOK/install-ok. In-game: detour observably changed behavior. |
| Result | _TBD_ |
| Notes | Bytes can be any 5+ byte sequence that does something useful (set a register, jump elsewhere). Hardest to test because we'd need to actually write working detour shellcode for an interesting target. |

## CAP-03: `[[hook]]` + `lua_callback` (TOML, dispatch to Lua)

| Field | Value |
|---|---|
| What | Hook a function; on entry the dispatch shim calls a named Lua function that decides whether to let the original run (`return true`) or skip it. |
| Channels | (iii) `kcdx.toml` + a pak-Lua-side function registration |
| Engine status | READY (Phase 5f) |
| Test plugin | `examples/phase5f-lua-callback-test/` (already exists, proven) |
| Site | A recurring-fire engine function (not the outfit registrar — that's a one-shot init) |
| PASS = | kcdx-dev.log: SHIM/enter fires N times matching trigger count. In-game: observable effect when the Lua callback returns false. |
| Result | ✅ PRE-VERIFIED (Phase 5f acceptance, kcd.log captured `Phase5fTest.Greet fired (count=1..3)` against the test hook target) |
| Notes | Outfit-swap NOT a good fit (one-shot init), kept the existing phase5f-lua-callback-test as the proof. Worth re-running under the matrix run cadence to capture a fresh row. |

## CAP-04: `[[mid_hook]]` register capture + Lua override

| Field | Value |
|---|---|
| What | Hook at an arbitrary instruction inside a function (not just entry). Capture named registers (rax, r14, etc) before the instruction, pass them to a Lua callback as a table, callback may modify the table, modified values written back to CPU registers before resuming. |
| Channels | (iii) `kcdx.toml` |
| Engine status | DEFERRED — Phase 5g design limit. Current MinHook-based mid-hook re-executes the captured instruction after our callback returns, overwriting any register override. Need a new primitive (true instruction-replacement mode or `call_original=false` flag) for v0.2. |
| Test plugin | `outfit-swap-midhook/` (built earlier as a demonstration of the limit) |
| Site | The outfit AOB above, override r14 to 0 |
| PASS = | In-game: outfit swap works in combat |
| Result | ❌ EXPECTED FAIL (re-execution issue). Run to document failure mode precisely. |
| Notes | Whether v0.2 needs a new primitive depends on use cases. Many mid-hook use cases (read register, log it, don't override) work fine with the current primitive; only "skip the instruction" doesn't. |

## CAP-05: Runtime `dynamic_hook` from pak Lua

| Field | Value |
|---|---|
| What | Pak Lua script calls `kcdx.memory.dynamic_hook({ target=..., pre_callback=..., ... })` to install a hook at runtime. Same MinHook + JIT-detour plumbing as `[[hook]]` but driven from Lua at script-load time instead of TOML at engine-init time. |
| Channels | (i) pure pak mod, (vi) plugin Lua |
| Engine status | READY (Phase 5c.7b proved end-to-end in the verify pak — `phase5g_greet_intercept` fired 5/5 at exact shim VA) |
| Test plugin | `outfit-swap-paklua-mod/` (Workshop-distributable pak) |
| Site | Outfit AOB above |
| PASS = | kcdx-dev.log: DYNAMIC_HOOK/install-ok. In-game: outfit swap works. |
| Result | _TBD — build it_ |
| Notes | This is the novel kcdx capability — Workshop-distributable code injection. Before kcdx, pak Lua had no FFI (`package.loadlib` is CryEngine-compiled-out stub). After kcdx, a pak mod can install function detours. |

## CAP-06: Runtime `dynamic_call` from pak Lua (call game function)

| Field | Value |
|---|---|
| What | Pak Lua script calls `kcdx.memory.dynamic_call({ target=..., return_type=..., param_types=... })` to get a callable userdata that invokes a native game function with marshaled args/return. |
| Channels | (i), (vi) |
| Engine status | READY (Phase 5c.7c) |
| Test plugin | Already exercised in verify pak (`dynamic_call bogus-target` returns userdata cleanly). Real-target call has no live verification yet. |
| Site | _TBD — pick a known-safe game function (e.g., something that just returns a constant)_ |
| PASS = | Callable userdata invokes the target, returns a sensible value. |
| Result | _TBD_ |
| Notes | Counterpart to CAP-05. Together they let pak Lua do bidirectional native interop — read game state via dynamic_call, modify it via dynamic_hook. |

## CAP-07: `[[trampoline]]` allocation (branch / local pool)

| Field | Value |
|---|---|
| What | Reserve executable memory within ±2GB of WHGame.dll (branch pool, for 5-byte rel32 reachable detours) or anywhere (local pool, for general JIT). Used internally by `[[hook]]` and `dynamic_hook`. Exposed to C++ plugins via `kcdxTrampolineInterface`. |
| Channels | (ii) C++ DLL, indirectly (i) via dynamic_hook, (iii) via `[[hook]]` |
| Engine status | READY (Phase 4) |
| Test plugin | `examples/hello-plugin/` already exercises both pool allocations |
| PASS = | hello-plugin.log: `branch-pool alloc OK ... in rel32 range = YES` and `local-pool alloc OK` |
| Result | ✅ PRE-VERIFIED (Phase 4 acceptance) |
| Notes | Foundational, used by everything that installs detours. |

## CAP-08: `kcdxMessagingInterface` (engine lifecycle messages)

| Field | Value |
|---|---|
| What | Subscribe to engine events: `kPostLoad`, `kPostPostLoad`, `kInputLoaded`, `kNewGame`, `kPreLoadGame`, `kPostLoadGame`, `kSaveGame`, `kDeleteGame`. Plugin-to-plugin dispatch also supported. |
| Channels | (ii) C++ DLL |
| Engine status | READY (Phase 3 for the events that exist; PreLoadGame/PostLoadGame/SaveGame/DeleteGame DEFERRED — Phase 6 save-hook required) |
| Test plugin | `examples/hello-plugin/` subscribes + logs received messages; `examples/messaging-pair/` does plugin-to-plugin |
| PASS = | hello-plugin.log shows received messages with correct type/name |
| Result | ✅ PARTIAL (PostLoad/PostPostLoad/InputLoaded confirmed; game-lifecycle messages awaiting Phase 6) |
| Notes | _ |

## CAP-09: `kcdxTaskInterface` (queue work for main thread)

| Field | Value |
|---|---|
| What | `AddTask(task)` queues a callback for next `update` tick on the main thread. Used so worker-thread plugins can safely touch CryEngine state. |
| Channels | (ii) C++ DLL |
| Engine status | READY (Phase 3) |
| Test plugin | `examples/hello-plugin/` enqueues a HelloTask, drained on first update tick |
| PASS = | hello-plugin.log shows `HelloTask::Run on main thread` |
| Result | ✅ PRE-VERIFIED |
| Notes | _ |

## CAP-10: `kcdxScriptingInterface` — C++ exposes Lua functions

| Field | Value |
|---|---|
| What | `RegisterFunction(handle, table, name, fn, userdata)` makes a C++ function callable from pak Lua as `kcdx.<table>.<name>(...)`. Uses the kcdxLuaApi function-pointer struct for the C++ side's Lua C API access (no direct lua.h include). |
| Channels | (ii) C++ DLL + (i)/(vi) on the calling side |
| Engine status | READY (Phase 5e) |
| Test plugin | `examples/hello-plugin/` registers `kcdx.hello.greet` and `kcdx.hello.add`; verify pak calls them and reports results |
| PASS = | Pak Lua sees `kcdx.hello.greet` and `kcdx.hello.add` as callable, results match C++ implementations |
| Result | ✅ PRE-VERIFIED (verify pak captured `hello, Michael, from hello-plugin` and `add(3,4)=7`) |
| Notes | Core capability for "new game systems" mods — magic, perks, custom inventory, etc. all use this surface. |

## CAP-11: `kcdx.lua.cfunction_address` (resolve C address of a Lua-callable)

| Field | Value |
|---|---|
| What | Pak Lua passes a function (Lua-side) and gets back a `kcdx.memory.pointer` userdata holding the C function pointer (if any). Returns nil + error for pure-Lua functions. |
| Channels | (i), (vi) |
| Engine status | READY (Phase 5c.7d post-LUA_NUMBER fix) |
| Test plugin | verify pak (`cfunction_address(System.LogAlways)` returns pointer userdata) |
| PASS = | Pointer userdata returned for cfunctions, nil for pure-Lua. Userdata is usable as `dynamic_hook.target`. |
| Result | ✅ PRE-VERIFIED (`5gDEMO intercept #1..#3` captured against `kcdx.hello.greet`) |
| Notes | The "find any registered Lua C function's address so I can hook it" primitive. |

## CAP-12: `kcdxSerializationInterface` (save/load co-save)

| Field | Value |
|---|---|
| What | Plugin registers `SetSaveCallback`/`SetLoadCallback`/`SetRevertCallback`. On save, kcdx fires SaveCallback; plugin writes records via `OpenRecord` + `WriteRecordData`. Stored in a `.kcdx` sidecar file alongside the save. On load, LoadCallback fires, plugin walks records via `GetNextRecordInfo` + `ReadRecordData`. |
| Channels | (ii) C++ DLL |
| Engine status | DEFERRED — Phase 6. Requires save-hook + co-save file format. |
| Test plugin | DEFERRED |
| PASS = | Plugin writes counter on save, reads it on load, value persists across game restarts. |
| Result | DEFERRED-PHASE-6 |
| Notes | Essential for any mod that has persistent state per save (perks added by mod, custom inventory, magic-spell-known list, etc). |

## CAP-13: `[[command]]` console commands

| Field | Value |
|---|---|
| What | Register a console command (callable from KCD2's `-console` window) that dispatches to Lua or C++. |
| Channels | (iii) `kcdx.toml`, (ii) C++ DLL |
| Engine status | DEFERRED — Phase 7. Needs Ghidra session on `pConsole.RegisterCommand` calling convention. |
| Test plugin | DEFERRED |
| PASS = | `se_dump_address 12345` callable from in-game console, prints a value via System.LogAlways. |
| Result | DEFERRED-PHASE-7 |
| Notes | Critical for dev workflows and for cheat/debug mods. |

## CAP-14: Address Library (`kcdxInterface::ResolveAddress`)

| Field | Value |
|---|---|
| What | Plugin calls `api->ResolveAddress(uint64_t id)` to get a runtime VA. The id-to-RVA mapping ships with kcdx as a CSV compiled into a binary lookup table, with per-game-version entries so the same id keeps working across KCD2 patches. |
| Channels | (ii) C++ DLL |
| Engine status | DEFERRED — Phase 7. Currently returns 0 (stub). |
| Test plugin | DEFERRED |
| PASS = | Plugin calls ResolveAddress(known_id), gets a VA, dereferences it, gets sensible data. |
| Result | DEFERRED-PHASE-7 |
| Notes | The SKSE-equivalent that lets plugins survive KCD2 patches without re-doing AOB scans. |

## CAP-15: `inlinePatchesToml` (C++ plugin ships patches inline)

| Field | Value |
|---|---|
| What | `kcdxPluginVersionData::inlinePatchesToml` field holds a TOML string parsed by the loader BEFORE `kcdxPlugin_Load` fires. Plugin gets to ship its byte rewrites alongside its DLL without a sidecar `kcdx.toml`. |
| Channels | (iv) |
| Engine status | _TBD — check whether the loader currently parses this field_ |
| Test plugin | _TBD — build a DLL with the outfit-swap patch in its inlinePatchesToml_ |
| PASS = | kcdx.log shows the patch applied from the DLL's inline TOML, no sidecar TOML needed. In-game: outfit swap works. |
| Result | _TBD_ |
| Notes | If READY, this is the cleanest way for C++ plugins to ship "I need this byte to change for my code to work" without a parallel TOML. |

## CAP-16: Plugin dependencies + topo-sort (`dependencies` array)

| Field | Value |
|---|---|
| What | `kcdxPluginVersionData::dependencies` array names other plugins this one depends on, with min-version constraints. Loader topologically sorts before issuing `Plugin_Load`. |
| Channels | (ii) C++ DLL |
| Engine status | READY (Phase 2 acceptance — see `examples/messaging-pair/` for the ordering proof) |
| PASS = | Plugin B loads AFTER plugin A even when filesystem order would have B first. |
| Result | ✅ PRE-VERIFIED (Phase 2) |
| Notes | _ |

## CAP-17: `EnumeratePlugins` (introspection)

| Field | Value |
|---|---|
| What | C++ plugin calls `api->EnumeratePlugins(buf, cap)` to get the list of loaded plugins (handle, name, version). |
| Channels | (ii) C++ DLL |
| Engine status | READY (Phase 2) |
| PASS = | hello-plugin.log reports `N plugin(s) loaded total` matching reality. |
| Result | ✅ PRE-VERIFIED |
| Notes | Used for conflict diagnostics, config UIs that enumerate co-loaded mods. |

## CAP-18: Pak mod resource overrides (XML / Lua / Schematyc)

| Field | Value |
|---|---|
| What | Standard CryEngine pak mod: drop a pak that contains modified `Libs/Tables/*.xml`, `Scripts/*.lua`, etc. The game loads the modded version instead of the vanilla one. Workshop-distributable. |
| Channels | (i) |
| Engine status | NATIVE (not a kcdx feature — CryEngine pak system) |
| PASS = | XML/Lua/asset change visible in game. |
| Result | ✅ NATIVE (the existing `inventory-in-dialogue/`, `easytoseeherbs/` pak mods demonstrate this works without kcdx) |
| Notes | Documented for completeness — many real mods are pure pak mods that don't need kcdx at all. |

## CAP-19: UI / Scaleform injection

| Field | Value |
|---|---|
| What | Inject Flash UI widgets, hook Scaleform events, register new HUD elements. |
| Channels | (ii) probably C++ DLL once exposed |
| Engine status | DEFERRED — v0.2 (`kcdxScaleformInterface` equivalent), separate Ghidra session |
| Result | DEFERRED-v0.2 |
| Notes | Big surface area; lots of mods will want this eventually. |

---

# Section 2: Competition / collision rows

## COMP-01: Two `[[patch]]` entries on the same address

| Field | Value |
|---|---|
| Scenario | Plugin A and Plugin B both declare `[[patch]]` entries that resolve to the same VA. |
| Engine behavior expected | conflict_engine pre-flight detects overlap. Lower-priority-number plugin wins. Loser's apply is aborted with a log line naming the winner. |
| Test plugin | _TBD — build A=outfit-swap-patch, B=outfit-swap-patch-conflict (same bytes, different priority)_ |
| Engine status | READY (conflict_engine ships) |
| PASS = | kcdx.log: CONFLICT record with both names. Only winner's bytes land. |
| Result | _TBD_ |

## COMP-02: `[[patch]]` + `[[hook]]` on overlapping bytes

| Field | Value |
|---|---|
| Scenario | Patch writes bytes at address X..X+2. Hook installs a 5-byte rel32 jmp at address X..X+4 (overlaps). |
| Engine behavior expected | conflict_engine notes the overlap. Patch applies first, MinHook relocates the patched bytes into its trampoline so the patch survives inside the hook's call-original path. Both apply. |
| Engine status | READY (existing `conflict-test-hook-on-patch/` exercises this) |
| Result | ✅ PRE-VERIFIED — `HookOverlapsEarlierPatch=1` in kcdx.log Conflict engine summary, both apply cleanly |
| Notes | This is the most common patch+hook coexistence case in real mods. |

## COMP-03: Two `[[hook]]` on the same function

| Field | Value |
|---|---|
| Scenario | Plugin A and Plugin B both install hooks at function entry X. |
| Engine behavior expected | First-hook-wins. Second hook aborted with a plain-English log line naming the first plugin. |
| Test plugin | `examples/conflict-test-hook-on-hook/` (already exists) |
| Engine status | READY (Phase 4b) |
| PASS = | Second hook's install-failed log line names the first plugin. |
| Result | ✅ PRE-VERIFIED |
| Notes | Chained hooks are explicitly v0.2+ (Hard rule #8). |

## COMP-04: `[[patch]]` + runtime `dynamic_hook` on same address

| Field | Value |
|---|---|
| Scenario | TOML `[[patch]]` modifies bytes at X. A pak-Lua-driven `kcdx.memory.dynamic_hook` later tries to install at the same X. |
| Engine behavior expected | The runtime install path sees the existing patched bytes in the first-wins map, aborts cleanly. |
| Test plugin | _TBD — patch + pak mod targeting same VA_ |
| Engine status | READY (first-wins map covers both channels per Phase 5c.7b.2) |
| PASS = | kcdx-dev.log: runtime DYNAMIC_HOOK/install-failed cites the prior patch. |
| Result | _TBD_ |

## COMP-05: Plugin Lua registration overrides another plugin's

| Field | Value |
|---|---|
| Scenario | Plugin A and Plugin B both `RegisterFunction("hello", "greet", ...)`. Last registration wins (replaces earlier). |
| Engine behavior expected | Whichever runs `Plugin_Load` later overwrites the earlier. Optionally: warn in log. |
| Test plugin | _TBD — clone hello-plugin twice with different greet implementations_ |
| Engine status | _TBD — confirm behavior; may need to add a warn_ |
| PASS = | Pak Lua sees plugin B's implementation; log has a warning naming plugin A as overridden. |
| Result | _TBD_ |

## COMP-06: Plugin B depends on plugin A's Lua registration

| Field | Value |
|---|---|
| Scenario | Plugin A `RegisterFunction`s `kcdx.magic.castSpell`. Plugin B's `Plugin_Load` reads `kcdx.magic.castSpell` and wraps it. Needs A loaded first. |
| Engine behavior expected | B's `kcdxPluginVersionData::dependencies` lists A; topo-sort ensures A loads first. |
| Engine status | READY (Phase 2 dependencies + Phase 5e registration both ship) |
| Test plugin | _TBD — build magic-system-stub (A) + magic-extender (B) with dependency_ |
| PASS = | B's Plugin_Load reads A's registration without error; combined behavior works in pak Lua. |
| Result | _TBD_ |
| Notes | The "ecosystem" case. SKSE has this via SKSE plugin-to-plugin Messaging; kcdx has it via the kcdx.* Lua namespace + Messaging. |

## COMP-07: Pak resource override + DLL function detour collide

| Field | Value |
|---|---|
| Scenario | Pak mod overrides a Lua script in `scripts/system/something.lua`. DLL plugin hooks a C++ function that calls that script. The two modifications interact. |
| Engine behavior expected | Each operates in its own channel; outcomes depend on what the Lua and the hook each do. kcdx doesn't try to detect this (out of scope — pak resources aren't kcdx's domain). |
| Test plugin | _TBD if we want it; arguably out of scope_ |
| Engine status | OUT-OF-SCOPE (deliberate) |
| Result | _N/A_ |

## COMP-08: Load-order determinism across game restarts

| Field | Value |
|---|---|
| Scenario | Same set of conflicting plugins, multiple game restarts, verify same winner each time. |
| Engine behavior expected | Apply order = topo-sort(dependencies) → sort(priority asc, name asc). Deterministic. |
| Engine status | READY |
| PASS = | Run game N times with same plugin set, kcdx.log shows identical apply order each time. |
| Result | _TBD — run 3x_ |

---

# Section 3: Real-world mod scenarios

Sanity-check the matrix by walking real mod ideas end-to-end. For
each, list the capability rows the mod needs and note if any are
DEFERRED.

## Scenario A: "Combat tweaks" (the outfit-swap case)

Small mod, single byte rewrite. Maps to **CAP-01** alone. Already
proven via Test 1.

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

# Section 4: Live result roll-up

Quick-reference table updated as rows land. One line per CAP or
COMP, status only. Detail lives in the row above.

| Row | Status | Notes |
|---|---|---|
| CAP-01 | ✅ PASS | outfit-swap-patch, Test 1 |
| CAP-02 | _TBD_ | _ |
| CAP-03 | ✅ PRE-VERIFIED | phase5f-lua-callback-test |
| CAP-04 | ❌ EXPECTED FAIL | Phase 5g design limit; documented |
| CAP-05 | _TBD_ | outfit-swap-paklua-mod |
| CAP-06 | _TBD partial_ | bogus-target shape verified; real target untested |
| CAP-07 | ✅ PRE-VERIFIED | hello-plugin |
| CAP-08 | ✅ PARTIAL | engine messages ready; game lifecycle DEFERRED Phase 6 |
| CAP-09 | ✅ PRE-VERIFIED | hello-plugin task |
| CAP-10 | ✅ PRE-VERIFIED | kcdx.hello.* round-trip |
| CAP-11 | ✅ PRE-VERIFIED | verify pak 5gDEMO |
| CAP-12 | DEFERRED-PHASE-6 | save/load |
| CAP-13 | DEFERRED-PHASE-7 | console commands |
| CAP-14 | DEFERRED-PHASE-7 | Address Library |
| CAP-15 | _TBD_ | inlinePatchesToml — confirm loader handles |
| CAP-16 | ✅ PRE-VERIFIED | messaging-pair |
| CAP-17 | ✅ PRE-VERIFIED | EnumeratePlugins |
| CAP-18 | ✅ NATIVE | CryEngine pak system |
| CAP-19 | DEFERRED-v0.2 | Scaleform |
| COMP-01 | _TBD_ | two-patch overlap |
| COMP-02 | ✅ PRE-VERIFIED | conflict-test-hook-on-patch |
| COMP-03 | ✅ PRE-VERIFIED | conflict-test-hook-on-hook |
| COMP-04 | _TBD_ | patch + runtime hook |
| COMP-05 | _TBD_ | Lua registration override |
| COMP-06 | _TBD_ | dependency chain in practice |
| COMP-07 | OUT-OF-SCOPE | pak + DLL cross-channel |
| COMP-08 | _TBD_ | determinism across restarts |

---

# Section 5: How to fill in this doc

Each row goes through this loop:

1. Confirm the row is READY (engine supports the primitive being
   tested) or mark DEFERRED-PHASE-X and skip.
2. Build the test plugin under `outfit-swap-test-matrix/<rowid>/`.
3. Disable every other test plugin (isolation protocol above).
4. Install just this row's plugin.
5. Launch game, run the in-game test, report yes/no/notes.
6. Read kcd.log + kcdx.log + kcdx-dev.log, fill the row's
   Result table + the roll-up at section 4.
7. Commit. The row's Build field cites the commit SHA so
   anyone reading later can reproduce.
