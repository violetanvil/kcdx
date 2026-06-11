---
paths:
  - "src/lua_bind.cpp"
  - "src/scripting.*"
  - "src/hooks.cpp"
  - "vendor/lua/**"
  - "include/kcdx/Interfaces.h"
---

# Lua bridge — dual-Lua sentinel hazard

kcdx statically embeds Lua 5.1 from `vendor/lua/`. WHGame.dll independently statically embeds its own Lua 5.1. Both copies operate on one shared `lua_State` / `global_State` / `g->rootgc`. Each copy has its own `static const` sentinel objects (`dummynode_`, etc.) in its own `.rdata`, at a DIFFERENT address.

**The hazard is BIDIRECTIONAL** — whichever copy's GC sweeps a Table the OTHER copy allocated mis-handles the foreign sentinel, because each copy's `t->node != dummynode` guard compares against its OWN `.rdata` address:

- **kcdx → WHGame** (closed by FIX C): a kcdx-allocated GCObject embedding kcdx's static-const sentinel is misinterpreted by WHGame's GC — it compares against its own sentinel address, sees a mismatch, treats kcdx's sentinel as a heap allocation, and frees it → `STATUS_HEAP_CORRUPTION`.
- **WHGame → kcdx** (closed by the KI-0001 fix): kcdx's GC sweeps a WHGame-allocated empty-hash Table whose `t->node` is WHGame's `dummynode_`; kcdx's `t->node != dummynode` guard (kcdx's address) is TRUE, so kcdx frees WHGame's `.rdata` sentinel → `STATUS_HEAP_CORRUPTION`. Surfaced when the chain-mediated `lua_pcall` migration put kcdx's GC on the dispatch path of every `lua_pcall` fire. See `docs/known-issues/KI-0001-save-load-heap-corruption-on-chain-mediated-lua_pcall.md`.

PROBE Q (below) detects only the kcdx→WHGame direction (`block ∈ kcdx.dll image`); it is structurally blind to the WHGame→kcdx direction (the freed block is in WHGame's image). Both directions vanish by construction under FIX A / restructure Phase 11 (one compiled Lua body, one sentinel set).

## Rules

- **Do not introduce new `static const Node*` / `static const TValue*` / similar singletons** in kcdx code or vendored Lua patches. Their addresses end up inside GCObjects on `g->rootgc` and trip WHGame's GC.
- **Use raw Lua C API** for any kcdx surface touching the live `lua_State`. Pattern: `luaL_newmetatable` + `lua_pushcfunction` per method + `lua_setfield` + `lua_setmetatable`.
- **Per-target callback storage**: use Lua registry refs (`luaL_ref` into `LUA_REGISTRYINDEX`, retrieve with `lua_rawgeti`, invoke with `lua_pcall`).
- **Marshaled types** (pointer, value_wrapper_t): push as raw userdata via `lua_newuserdata` + `luaL_setmetatable`.
- **Before adopting any C++↔Lua glue library**, verify it doesn't introduce additional static-const sentinels — run PROBE Q.

## FIX C (in production) — closes the kcdx → WHGame direction

`vendor/lua/ltable.c::setnodevector` patched to always allocate a real 1-Node array when caller passes `size==0`. `luaH_new` patched to use `NULL` as temporary `t->node` placeholder. kcdx never writes the dummynode sentinel into any Table.

## KI-0001 fix (in production) — closes the WHGame → kcdx direction

A Table has TWO `.rdata`-sentinel-bearing fields when empty: `t->node` (the dummynode) AND `t->array` (the empty-array sentinel). `vendor/lua/ltable.c::kcdx_ptr_freeable(p)` (a node-typed view `kcdx_node_freeable` wraps it) replaces the bare `== / != dummynode` comparisons at every site that frees, reallocs, or size-reads EITHER field:

- **`t->node`** — `luaH_free`, `resize`, `luaH_resizearray`, `newkey`, `luaH_getn`/`unbound_search`, `luaH_isdummy`.
- **`t->array`** — `luaH_free` (the array free) and `setarrayvector` (the realloc; a foreign sentinel is dropped to `NULL`/0 so the realloc allocates fresh). `resize`'s array-shrink is sentinel-safe by construction (a sentinel array is empty, never shrinks).

A pointer is freeable ONLY if it is a real heap allocation; ANY loaded-module-image (`.rdata`) pointer — kcdx's own sentinels OR a foreign copy's node/array sentinels — is non-freeable. Discriminator: `VirtualQuery` `MEM_IMAGE` (module image → skip) vs `MEM_PRIVATE`/`MEM_MAPPED` (heap → free); module images and heap allocations are disjoint by OS construction, so the test is robust + ASLR-safe with no sentinel-address harvest and handles any number of distinct sentinels. One `VirtualQuery` per guarded pointer (same order as `luaH_free`'s existing frealloc calls; Performance is the last cornerstone and this guard is temporary). Both FIX C and this fix retire under FIX A / Phase 11.

**Lesson (a node-only first fix crashed again):** the hazard is per-SENTINEL-FIELD, not just the dummynode. Guarding `t->node` alone passed the discriminator unit test but still crashed on the unguarded `t->array` free. Every kcdx GC site that hands a Table's node OR array pointer to the allocator must apply the discriminator. A unit test of the discriminator does NOT prove call-site coverage — only the live save-load repro exercises every site.

## PROBE Q canary (permanent regression guard)

`ArmFreallocProbe` + `HookedFrealloc` in `src/hooks.cpp` MinHook-detour `g->frealloc` and log any call where `block ∈ kcdx.dll image`. Under FIX C this reads zero. If it ever fires in production, a new sentinel has been introduced — investigate the `caller_ra` and `block` address.

## FIX A (harvest substantially complete; shim implementation deferred)

Drop static-link of vendored Lua entirely; resolve every `lua_*`/`luaL_*` symbol from WHGame.dll. Eliminates the dual-Lua design by construction. ~117 symbols.

**Harvest done (2026-05-21)**: 93 of ~117 LUA_API/LUALIB_API + 13 internal helpers + 4 CScriptSystem anchors in the address-library seeds at [`data/db-export/`](../../data/db-export/) (rows 1100-1205 in the pre-2026-05-28 single-CSV seed; renumbered in the three-file split). The reference DB regenerates from those seeds. ~24 functions are confirmed inlined-by-PGO or linker-stripped — those get kcdx-side stubs in the shim.

Critical knowledge for the shim build (layout constants, CScriptSystem anchors, per-function stub strategies) consolidated in [`docs/outstanding-work/fix-a-drop-static-lua.md`](../../docs/outstanding-work/fix-a-drop-static-lua.md). That doc is the canonical source of truth for everything Stage 2B needs.

Still deferred: building `src/lua_shim.{h,cpp}` to consume those IDs, dropping `add_library(lua STATIC)` from CMakeLists, reverting FIX C in `vendor/lua/ltable.c`, and live-verifying PROBE Q reads zero across cap-04.

## Investigation trail

`docs/known-issues/kcdx lua_newtable corrupts the process heap.md`.
