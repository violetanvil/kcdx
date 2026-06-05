# engine-direct hook migration — 5 remaining sites

## Status

Engine machinery + the canonical first site (`engine.lua_pcall`) shipped in commit `1c01c9d`. 5 sibling engine-direct `MH_CreateHook` sites remain on the raw-MinHook path: `frealloc` canary, `ModManager_ctor` (mod-absorb bracket), `MiniDmpSender` ctor (BugSplat probe), `SaveGame`, `LoadGame`. Each is an independent migration that reuses the existing machinery; one `/execute` cycle per site, paired with one Lua + one C++ test plugin per site.

Per AP4 (`anti-patterns.md` — every hook routes through `hook_chain`), each unmigrated site is a documented compliance gap that the rule rewrite in `hook-engine.md` § "Engine-owned chain entries" already names: `update` is the SOLE documented bootstrap exception. The other 5 sites are debt, not exemption.

## Trigger to revisit

Independent of any other phase — pick by leverage. Recommended order:

1. **`frealloc` canary** first. Cleanest semantics (read-only fingerprint, `before` mode, post-`SetLuaState` install — no carve-out edge case). PROBE Q contract preservation already documented in `lua-callback-threading.md` § Engine bootstrap carve-out. Exercises the machinery at site #2, which is the real test of whether the cycle-1 machinery is reusable.
2. **`SaveGame` + `LoadGame`** together. Both at `src/save_load_hooks.cpp:467` via the same `InstallOne` helper. Both post-`SetLuaState`. TC mods' natural extension points (a TC mod wanting save/load lifecycle without writing a cosave record needs to `kcdx.hook` these). Ship as one cycle.
3. **`ModManager_ctor`** (mod-absorb bracket). Different mode (`replace`) — the bracket fully replaces the native ctor; `pOriginal` intentionally discarded. The engine-replace contract teaching text from cycle 1 (`hook_chain::CanCoexist`) already covers this case; a plugin install at this target is now rejected with the teaching error, which is the correct end state.
4. **`MiniDmpSender` ctor** (the BugSplat ctor install). Pre-VM install under Windows loader lock, now in `src/early_hook.cpp` (the generalized early-install primitive + its BugSplat consumer). PROBE Z (cycle 1, archived in `_research/probe-archive/bugsplat-probe-z.md`) verified `runtime_func_t::make_jit_func` + `branch_pool::AllocateBranch` are loader-lock-safe at this site. The carve-out at `hook_chain.cpp:1075/1209/1341` handles the dead-classifier pre-bootstrap fire path. Lowest-risk migration of the four.

Trigger for picking any one up: an `/execute` cycle slot opens AND no higher-priority phase is in flight.

## Design

The cycle-1 machinery this entry consumes:

- **`ChainEntry::isEngine` + `Chain::isMidEngine` flags** (`src/hook_chain.cpp:210` + `:241-ish` near the mid-fields block). Set true via `hook_chain::AddCEngine` (or `AddCMid` consuming `TakeEngineStamp()` into `Chain::isMidEngine`).
- **`InsertOrdered` front-sort comparator** (`src/hook_chain.cpp:1798-1800`). Engine entries always sort to the front of the chain regardless of declared priority.
- **`CanCoexist` engine-bootstrap teaching text** (`src/hook_chain.cpp:525-ish`). Engine `replace` rejects all plugin coexistence with the canonical teaching error; `before`/`after` engine entries coexist normally per the chain's existing rules.
- **Off-thread carve-out at three dispatch sites** (`src/hook_chain.cpp:1075`, `:1209`, `:1341`). Predicate `e.isEngine && e.kind == ChainEntry::Kind::C` (for `before`/`after`); `chain->isMidEngine && chain->midKind == Kind::C` (for mid). AP6 doesn't apply — engine-stamped C entries have no Lua callback in the dispatch path.

Per-site migration shape (uniform):

| Site | Mode | Identity | Signature source | Special |
|---|---|---|---|---|
| `src/hooks.cpp:247` HookedFrealloc | `before` | `Kind::Engine`, `pluginName="kcdx"`, `name="engine.frealloc_canary"` | `g->frealloc` typedef (runtime-resolved; no seed row — install builds the signature inline) | Read-only fingerprint preserved through DispatchPre. Carve-out must let this through pre-bootstrap. Remove `g_orig_frealloc` global. |
| `src/mod_absorb/ctor_bracket.cpp:365` HookedCtor | `replace` | `Kind::Engine`, `name="engine.modmanager_ctor"` | `ModManager_ctor` seed row (kcdx_id=134) | Bracket fully replaces native ctor; `pOriginal` slot wired by chain but unused by callback. |
| `src/save_load_hooks.cpp:467` (SaveGame) | `before` | `Kind::Engine`, `name="engine.savegame"` | AOB-resolved in-source; install builds the signature inline | Post-`SetLuaState` install (`phase6_install_after_lua_ready`); carve-out not load-bearing but uniform. |
| `src/save_load_hooks.cpp:467` (LoadGame) | `before` | `Kind::Engine`, `name="engine.loadgame"` | AOB-resolved in-source; install builds the signature inline | Same as SaveGame. |
| `src/early_hook.cpp` (BugSplat ctor consumer) | `before` | `Kind::Engine`, `name="engine.bugsplat_ctor"` | `GetProcAddress(BugSplat64.dll, "??0MiniDmpSender@@QEAA@PEB_W000K@Z")`; install builds signature from the typedef | Pre-VM install under loader lock via the early_hook primitive. Carve-out's pre-bootstrap path is load-bearing here. PROBE Z verified loader-lock-safe. |

ABI fit-up per site:

1. Read each site's current callback typedef + body.
2. Cross-check the verified ABI against either the seed row (lua_pcall did this — kcdx_id=1) or the in-source typedef (`g->frealloc`, the `MiniDmpSender` mangled-name decoded ABI).
3. Reshape the callback to the `BuildCDispatchThunk` Before / Replace shape (`void cFn(uintptr_t args[], int* outCount, ...typed args...)` for Before; `Replace` semantics with `pOriginal` slot unused for ctor_bracket).
4. Delete the per-site `g_orig_*` global; the chain owns the original.
5. Wire `hook_chain::AddCEngine(payload, cFn, sig, "kcdx", priority, "engine.<site>", handleId)` at the install site; remove the direct `MH_CreateHook` / `MH_EnableHook` pair.

Test plugins per site — both surfaces, Lua + C++ (`lua-api-surface.md` full-parity invariant):

| Site | Lua row | C++ row |
|---|---|---|
| frealloc | `cap-NN-lua-frealloc-fires` | `cap-NN-cpp-frealloc-fires` |
| ModManager_ctor (replace contract) | `cap-NN-lua-modmanager-reject` (AP15-falsifiable: `:applied()==false` + substring on the canonical engine-bootstrap teaching text) | `cap-NN-cpp-modmanager-reject` |
| BugSplat ctor | `cap-NN-lua-bugsplat-fires` | `cap-NN-cpp-bugsplat-fires` |
| SaveGame | `cap-NN-lua-savegame-fires` (`[manual]` — gated on user save gesture) | `cap-NN-cpp-savegame-fires` |
| LoadGame | `cap-NN-lua-loadgame-fires` (`[manual]` — gated on user load gesture) | `cap-NN-cpp-loadgame-fires` |

`cap-NN` IDs assigned at cycle dispatch from `test-plugins/README.md`.

Doc updates per cycle (per `docs-discipline.md`):

- `docs/cpp/hook.md` + `docs/lua/hook.md` § "Bootstrap targets" — flip each newly-hookable target from "pending migration" to LIVE; the `update` un-hookable note stays.
- Each new test plugin row in `test-plugins/README.md` (capability section + roll-up row).

## Files that need to change

Cycle-by-cycle, NOT all at once:

**`frealloc` cycle:**
- `src/hooks.cpp` — `ArmFreallocProbe` install path: remove direct `MH_CreateHook(frealloc_addr, &HookedFrealloc, &g_orig_frealloc)` pair; reshape `HookedFrealloc` callback to AddC Before ABI; call `hook_chain::AddCEngine` with `name="engine.frealloc_canary"`. Delete `g_orig_frealloc` global.
- `src/hook_chain.cpp` — none (machinery reused as-is).
- `.claude/rules/lua-callback-threading.md` — no change (carve-out paragraph already covers this site).
- `test-plugins/cap-NN-lua-frealloc-fires/` + `test-plugins/cap-NN-cpp-frealloc-fires/` — new.
- `test-plugins/README.md` — append rows.
- `docs/cpp/hook.md` + `docs/lua/hook.md` — § "Bootstrap targets" flip `engine.frealloc_canary` to LIVE.

**`SaveGame + LoadGame` cycle:**
- `src/save_load_hooks.cpp` — `InstallOne` helper migrates; both target installs route through `hook_chain::AddCEngine`. Delete trampoline pointers.
- Test plugins (4 — Lua + C++ × Save + Load).
- Docs flip both.

**`ModManager_ctor` cycle:**
- `src/mod_absorb/ctor_bracket.cpp` — install via `hook_chain::AddCEngine` with `mode=Replace`. The `pOriginal` slot wiring + the chain's required call-original slot at first-touch already handle the case where the engine `replace` discards the original.
- Test plugins (2 — Lua + C++ reject-shape, AP15-falsifiable).
- Docs flip + add the engine-replace teaching error reference.

**`MiniDmpSender` ctor cycle:**
- `src/early_hook.cpp` (the BugSplat ctor consumer) — install via `hook_chain::AddCEngine`. PROBE Z archived in `_research/probe-archive/bugsplat-probe-z.md`; revival hint at the archive header.
- Test plugins (2 — Lua + C++).
- Docs flip.

Each cycle is one `/execute` brief; the manager runs the build gate, step-review gate, and verification-checkpoint per `_shared/orchestrator-loop.md`. Closes when all 5 sites land + all 10 test rows PASS + the rule promise "the only install path is `hook_chain`, with the single documented `update` exception" reads true in the literal sense (not aspirational).

## Pointer to deeper docs

- The cycle-1 mechanism + the dead-classifier carve-out's reason: `docs/known-issues/closed/cap-59-fires picked a one-shot VM-init target that already ran by plugin load.md` § Reframe 2026-05-29c.
- The KI-0001 sentinel-mirror fix (the FIX-C inverse) is unrelated to this entry — it landed at `vendor/lua/ltable.c` and is closed; it shipped while cycle-1 was verifying.
- The PROBE Z verdict (loader-lock safety at the bugsplat install site): `_research/probe-archive/bugsplat-probe-z.md` + the closed cap-59 known-issue § Reframe 2026-05-29b.
