# FIX A — drop static-linked Lua, route every `lua_*` through WHGame.dll

**Status (2026-05-21):** harvest substantially complete — 93/117 LUA_API/LUALIB_API resolved + 13 verified internal helpers + 4 CScriptSystem anchors. Shim implementation deferred but unblocked. FIX C (vendored Lua patch) is shipped + verified and prevents the heap-corruption crash that motivated this; it stays in place until FIX A's shim is built and lands.

## Current coverage

89 of ~117 LUA_API/LUALIB_API functions identified in WHGame.dll (~79%). Per-row evidence in `data/address-library/seed.csv` rows 1100-1200 (Lua API surface) + 1183, 1184, 1191, 1192, 1201-1205 (internal helpers).

| Bucket | Count | Status |
|---|---|---|
| Lua C API harvested | 93 | verified, in address library 1100-1200 |
| Useful internal helpers harvested | 13 | verified, in address library |
| CScriptSystem class anchors | 4 | verified, in address library 1194-1197 |
| Confirmed-inlined (need kcdx stubs) | ~14 | shim must reimplement |
| Confirmed-stripped by linker | ~7 | shim must reimplement |

Stripped or inlined functions (shim must stub them — these will NEVER have callable RVAs in WHGame.dll):

| Function | Reason | Stub strategy |
|---|---|---|
| `lua_gettop` | inlined (1 instruction) | `(int)(L->top - L->base)` |
| `lua_pushnil/boolean/number/integer/lightuserdata/thread` | inlined | direct TValue write + L->top++ |
| `lua_isuserdata` | inlined (macro = `lua_type==7`) | inline same check |
| `lua_storedebuginfo` / `lua_isstoredebuginfo` | inlined (single byte op at g+0x22) | `((char*)G(L))[0x22]` |
| `lua_status` | inlined (single byte at L+8) | `((unsigned char*)L)[8]` |
| `lua_pushvfstring` | inlined into lua_pushfstring | forward to `luaO_pushvfstring` (we have it) + manual `luaC_step` |
| `lua_yield` | inlined | source body is self-contained — replicate it |
| `luaL_buffinit` / `luaL_register` / `luaL_setn` / `luaL_getn` | inlined | reimplement using already-resolved primitives |
| `luaI_openlib` | fully inlined into all 10 luaopen_X | shim's luaL_register reimplements its body |
| `luaL_unref` / `luaL_newmetatable` | open-coded everywhere by CryEngine | reimplement using already-resolved primitives |
| `lua_atpanic` / `lua_getallocf` / `lua_setallocf` | linker-stripped (CryEngine never calls them) | shim returns errors; not usable |
| `lua_close` (caveat) | reachable but use carefully | we have it at 0x39989A4; shim can expose but warn |
| `lua_equal`, `lua_cpcall`, `lua_tocfunction`, `lua_topointer` | varies (inlined or unreached) | per-function stub |

## Critical knowledge (consolidated from harvest)

These facts MUST inform the shim implementation:

### Layout constants (verified against WHGame's compiled Lua)

| Constant | Value | Where verified |
|---|---|---|
| `sizeof(LG)` (lua_State + global_State packed) | `0x268` | close_state's final `g->frealloc(..., 0x268, 0)` call |
| `global_State.storedebug` field offset | `0x22` from g | lua_newstate body sets `[g+0x22]=1`; CScriptSystem::Init overrides to `[g+0x22]=0` |
| `global_State.totalbytes` field offset | `0x78` from g | lua_newstate sets `[g+0x78]=0x268` |
| `global_State.gcpause` field offset | `0x90` from g | lua_newstate sets `[g+0x90]=200` (LUAI_GCPAUSE) |
| `global_State.gcstepmul` field offset | `0x94` from g | lua_newstate sets `[g+0x94]=200` (LUAI_GCMUL) |
| `global_State.mainthread` field offset | `0xB0` from g | lua_newstate sets `[g+0xB0]=L` |
| `lua_State.l_G` (global_State*) field offset | `0x20` from L | every API call dereferences `[L+0x20]` |
| `LUAI_MAXCSTACK` (max C-call depth) | `0x800` (2048) | lua_checkstack body cmp |
| `LUAL_BUFFERSIZE` (luaL_Buffer.buffer size) | `512` (MSVC BUFSIZ) | from luaconf.h |
| `LUA_NUMBER` (lua_Number type) | `float` | CryEngine override; verified by math wrapper float ops |
| `LUA_INTEGER` (lua_Integer type) | `ptrdiff_t` (8 bytes) | Lua 5.1 stock; matches kcdxLuaApi long long |

### Layout for plugin-facing types

These mirror Lua's exactly; the shim can rely on them being identical between kcdx's vendor/lua headers (kept for type defs) and WHGame's compiled Lua:

- `lua_Debug.i_ci` at offset `0x74` (verified by lua_getlocal body)
- `lua_Debug.short_src` is `char[60]` at offset `0x38` (LUA_IDSIZE=60)
- `luaL_Buffer` layout: `{char* p; int lvl; lua_State* L; char buffer[512];}` (BUFSIZ=512)
- `TValue` size: `0x10` (16 bytes) — `Value` union + `int tt` + 4-byte pad

### Static tables in WHGame.dll's .rdata

| Symbol | RVA | Purpose |
|---|---|---|
| `lualibs[]` | `0x3B8B200` | The luaL_openlibs registry. **Starts at 0x3B8B200 NOT 0x3B8B210** (entry 0 is `{"", luaopen_base}` — empty-name entry). 8 entries + sentinel. |
| `mathlib[]` | `0x3A62AC0` | math library registry (28 entries) |
| `tab_funcs[]` | `0x3A9B0C0` | table library registry (9 entries) |
| `base_funcs[]` | `0x3B52CE0` | basic library registry (24 entries) |
| `co_funcs[]` | `0x3B52BA0` | coroutine library registry (6 entries) |
| `dblib[]` (debug, split) | `0x3D24850` + `0x3D248D0` | debug library (13 entries across two contiguous chunks) |
| `syslib[]` (os, stripped) | `0x3D198C8` | Only 2 entries — `{time, clock}`. CryEngine stripped os.execute/remove/getenv/etc. |
| `strlib[]` (string) | `0x3B52EB0` | string library (15 entries) |
| `pk_funcs[]` (package) | `0x3B53060` | 2 entries — `{loadlib, seeall}` |
| `bitops_funcs[]` | `0x3B8B290` | bit library (8 entries) |

### CryScriptSystem class

- **Vtable @ `0x3B8AF70`** — 69 slots
  - Slot [5] = ExecuteFile
  - Slot [6] = ExecuteBuffer (the CryEngine-side caller of `lua_pcall`)
  - Slot [13] = CreateTable
- **Constructor @ `0x1448E60`** — writes both vtables (primary + 4-slot adjustor at 0x3B8AF50), allocates internal state
- **`CScriptSystem::Init` @ `0x1448F38`** — the Lua-boot anchor. Sole caller of `lua_newstate` (0x14492A8) and `luaL_openlibs` (0x1449600). Sequence:
  1. malloc(0x40) helper + virtual call on parent system
  2. `lua_newstate` → returns L
  3. Stores L on instance (CScriptSystem+0x10) and on global at .data RVA `0x549A0E8`
  4. Sets `[L->l_G + 0x22] = 0` (storedebug=0 — CryEngine memory-save override)
  5. `luaL_openlibs(L)` — installs all 8 standard libraries
  6. Three CryEngine extension-lib registrars: `0x1449698`, `0x1449410`, `0x1449584`
- **Destructor @ `0x39AD63C`** — calls `lua_close` (`0x39989A4`) at site `0x39AD6E2` when `[this+0x10]` is non-null. CryEngine does NOT inline Lua state cleanup; the real lua_close runs.

### Strategic hook points for kcdx

- **`CScriptSystem::Init @ 0x1448F38`** — natural MinHook target for "after Lua boot, before any scripts run." Post-hook captures L and lets kcdx register kcdx-side libraries before CryEngine's bindings + scripts run.
- **`CScriptSystem::ExecuteBuffer` (vtable[6] @ 0x4D46E4)** — pre/post hook to intercept Lua execution paths.
- **gEnv->pScriptSystem write target = .data RVA `0x549A0E8`** (NOT muyuanjin's older 0x4092B828 — that offset was stale by 1.5.1164953). Read this RVA after init to retrieve the CScriptSystem* pointer.

### Confirmed-stripped functions (will never resolve)

These are NOT in WHGame.dll because CryEngine never calls them; the MSVC linker's `/OPT:REF` removed them:

- `lua_atpanic` — CryEngine doesn't register a panic handler (uses `CryFatalError` instead)
- `lua_getallocf` / `lua_setallocf` — CryEngine never queries or replaces the allocator
- `luaL_ref` — CryEngine open-codes the registry pattern at every callsite. **EXCEPTION**: we found one luaL_ref instance at `0x71D118` (one of the CryEngine extension lib registrars stores a callback this way). Available but rare.
- `luaL_unref` — never called in stdlib; CryEngine open-codes the freelist
- `luaL_newmetatable` — fully inlined into luaopen_package's body

### PGO inlining patterns confirmed

- Tiny functions (lua_pushnil, lua_gettop, lua_pushnumber, etc.) inline at every callsite — confirmed by 0 callable RVAs despite ~50+ would-be call sites in stdlib
- `lua_pushvfstring` is inlined into `lua_pushfstring`. The underlying `luaO_pushvfstring` (0x399838C) is reachable directly.
- `lua_newthread` is inlined into `luaB_cocreate`. The apparent first call goes to `luaC_step` (the GC checkGC macro expansion).
- `luaL_register` is inlined into all 10 luaopen_X bodies (verified by 11 "_LOADED" xrefs across 10 enclosing functions — see Walk C report).
- Most lualib-internal helpers (luaI_openlib, luaL_buffinit, addinfo, etc.) are inlined.

## Files that need to change for the shim build

Already shipped:
- `data/address-library/seed.csv` — 106 rows including 93 LUA_API + 13 internals
- `src/address_library.cpp::kEntries[]` — in-source mirror (synced with seed CSV)
- Recon tooling: `_research/phase8-fix-a/{coff_inspect.py, callgraph_walk.py, string_xrefs.py, luaL_reg_scan.py}` + per-function notes

Still to do (Stage 2B — shim build):
- `src/lua_shim.h` — declare `kcdx::lua_shim::LuaApi` function-pointer struct sized for all 117 LUA_API + LUALIB_API; declare `Resolve()`.
- `src/lua_shim.cpp` — define `LUA_API` symbol forwards through `g_api`. For the 93 resolved functions: forward to `address_library::Resolve(N)`. For the ~24 inlined/stripped functions: kcdx-side stubs using the layout constants documented above. luaL_register reimplements its body (luaI_openlib is inlined; reimplementation uses primitives).
- `src/hooks.cpp::Install()` — call `kcdx::lua_shim::Resolve()` after MinHook + address_library are up but before any Lua VM touch. Bail loudly if any required resolve fails.
- `CMakeLists.txt` — remove `add_library(lua STATIC ...)`. Remove `lua` from `target_link_libraries(kcdx PRIVATE ...)`. Keep `target_include_directories(kcdx PRIVATE vendor/lua)` because we still need the headers for struct definitions (lua_State, global_State, Table, Node) used by stubs + PROBE Q.
- `vendor/lua/ltable.c` — revert FIX C's `setnodevector` patch. No longer needed once routing through WHGame.
- `kcdx/.claude/rules/lua-bridge.md` — update to reflect FIX A as the shipped fix; PROBE Q canary description stays.
- `docs/known-issues/closed/kcdx lua_newtable corrupts the process heap.md` — add a "FIX A SHIPPED" row to the trail; FIX C becomes historical.

## Positive verification criteria

Same as FIX C's: PROBE Q canary must log **zero** `frealloc.kcdx_image_ptr` lines across a cap-04-enabled save-load cycle. cap-04 sub-tests run to completion. Save-load completes. No crash zip.

Additionally for FIX A specifically: at engine init, `kcdx::lua_shim::Resolve()` returns true (all required symbols resolved + all stubs registered). If any *required* symbol fails to resolve (vs. a known-stripped one with a stub), kcdx bails before touching the Lua VM.

## What NOT to do

- Don't expect `lua_atpanic` / `lua_getallocf` / `lua_setallocf` to ever resolve. They're stripped from WHGame.dll. Shim stubs them as no-ops or returns errors.
- Don't write a kcdx-side stub for `lua_close` — we have the real RVA (0x39989A4). Use it.
- Don't drop the PROBE Q canary even after FIX A lands. It's a permanent regression guard.
- Don't try to combine FIX A and FIX C in shipped code. Once FIX A's shim is functional, revert FIX C so vendor/lua stays clean for source reference.
- Don't expose `lua_close` / `lua_newstate` / `lua_setallocf` / `lua_atpanic` via the plugin-facing `kcdxLuaApi`. Even if resolvable, they'd let mod authors destroy or replace the game's Lua VM. Keep them resolvable internally only.
- Don't ship a stub that writes a GC pointer (`lua_pushthread`, `lua_replace`) without calling `luaC_barrierf` (0x3997070). Without the barrier, the incremental GC can free live objects.
- Don't assume vendor/lua's struct layouts will be byte-identical to WHGame's in a future game update. If KCD2 updates and a struct field shifts, every shim stub that touches that field breaks silently. Validate at engine init by reading known field values (e.g. confirm `[L+0x20]+0xB0` resolves to L itself after lua_newstate — the mainthread self-pointer invariant).

## Pointers

- Recon artifacts: `_research/phase8-fix-a/` — tooling, per-function notes, harvest CSV
- Seed CSV: `data/address-library/seed.csv`
- Subagent reports: see git history for commits ab11e7c and 5e131c2 (each commit message references the agent IDs that produced the findings)
- Investigation trail (pre-FIX A): `docs/known-issues/closed/kcdx lua_newtable corrupts the process heap.md`
- PROBE Q implementation: `src/hooks.cpp::ArmFreallocProbe` + `HookedFrealloc`. Stays in production through FIX A.
