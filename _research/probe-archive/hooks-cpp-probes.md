# `src/hooks.cpp` archived probes — PROBE α + PROBE A

Two probes lived as inline `#if 0` blocks inside LIVE functions in `src/hooks.cpp`.
Extracted here; the live functions (`HookedLuaPcall_Engine`, `HookedFrealloc`,
`ArmFreallocProbe`, `HookedUpdate`) returned to pure production logic.

The PROBE Q canary (`block ∈ kcdx.dll image` check in `HookedFrealloc` +
`ArmFreallocProbe` + `IsInKcdxImage` + `g_kcdx_dummynode`) is the PERMANENT
regression guard per `lua-bridge.md` — it stays LIVE in `src/hooks.cpp` and is NOT
part of this archive. Only PROBE α and PROBE A's WHGame-image-classification arm
were extracted.

---

## PROBE α — chain dispatcher fires post-carve-out (lua_pcall + update)

**Verdict:** VERIFIED — chain C-Before callback runs on lua_pcall fires post-carve-out
(lua_pcall_fire count=5, rate-limited; bootstrap classifier captured
g_gameMainThreadId; cap-59 + cap-64 + cap-65 all PASS). HookedUpdate fires (16,962
ticks observed in the dead-classifier-state launch); the first-tick latch crosses
`if(L)` post-carve-out.
**Root cause:** dead-classifier chicken-and-egg in the chain dispatcher — fixed by
the `isEngine && Kind::C` carve-out at `hook_chain.cpp:1075 / :1209 / :1341`.
**Backlink:** `docs/known-issues/` cap-59 KI §Reframe 2026-05-29c + §Resolution (post-PROBE-α). (Closed.)
**Revival hint:** re-instrument if a future regression of the L-bootstrap loop or the
chain-dispatch bootstrap is suspected — re-add the two blocks below.

### Wiring — block 1: lua_pcall fire counter (in `HookedLuaPcall_Engine`, after `(void)args;`)

```cpp
{
    static std::atomic<int> probe_alpha_pcall_count{0};
    int n = probe_alpha_pcall_count.fetch_add(1, std::memory_order_relaxed);
    if (n < 5) {
        LOG_DEBUG_KV("PROBE_ALPHA", "lua_pcall_fire",
            log::KV("fire_n", (int64_t)n),
            log::KV("tid", (int64_t)::GetCurrentThreadId()),
            log::KV("is_game_main_thread", (int64_t)(log::IsGameMainThread() ? 1 : 0)),
            log::KV("L_arg", (void*)L));
    }
}
```

### Wiring — block 2: update-tick counter (in `HookedUpdate`, before the `if (!done...)` latch)

```cpp
{
    static std::atomic<int> probe_alpha_tick_count{0};
    int tick_n = probe_alpha_tick_count.fetch_add(1, std::memory_order_relaxed);
    if (tick_n < 5 || !done.load(std::memory_order_acquire)) {
        lua_State* probeL = g_L.load(std::memory_order_acquire);
        LOG_DEBUG_KV("PROBE_ALPHA", "update_tick",
            log::KV("tick_n", (int64_t)tick_n),
            log::KV("tid", (int64_t)::GetCurrentThreadId()),
            log::KV("is_game_main_thread", (int64_t)(log::IsGameMainThread() ? 1 : 0)),
            log::KV("g_L", (void*)probeL),
            log::KV("done", (int64_t)(done.load(std::memory_order_acquire) ? 1 : 0)));
    }
}
```

---

## PROBE A — kcdx GC frees WHGame's `.rdata` dummynode (WHGame-image classification arm)

**Verdict:** CONFIRMED — `HookedFrealloc` fired once as the last line before the
0xC0000374 abort with `block ∈ WHGame image`, `nsize=0` (free), `osize=40`
(=sizeof(Node)), `caller_ra` in kcdx `luaH_free→luaM_realloc_`.
**Root cause:** kcdx's GC freed WHGame's `.rdata` `dummynode_` sentinel (the FIX-C
mirror, WHGame→kcdx direction); fixed by `kcdx_node_freeable` in `vendor/lua/ltable.c`.
**Backlink:** `docs/known-issues/KI-0001-save-load-heap-corruption-on-chain-mediated-lua_pcall.md` §Resolution.
**Revival hint:** re-instrument if a dual-Lua GC hazard recurs. PROBE A *extends*
the live PROBE Q canary with a WHGame-image classification arm; re-add the three
blocks below alongside the still-live PROBE Q machinery.

### Wiring — globals + helper (file scope, beside `g_kcdx_image_base`)

```cpp
uintptr_t g_whgame_image_base = 0;
size_t    g_whgame_image_size = 0;

static bool IsInWhGameImage(const void* p) {
    if (!p || !g_whgame_image_base) return false;
    uintptr_t addr = reinterpret_cast<uintptr_t>(p);
    return addr >= g_whgame_image_base &&
           addr <  g_whgame_image_base + g_whgame_image_size;
}
```

### Wiring — classification arm (in `HookedFrealloc`, as `else if` after the live `IsInKcdxImage(block)` block)

```cpp
else if (IsInWhGameImage(block)) {
    void* ret_slot = _AddressOfReturnAddress();
    void* caller_ra = ret_slot ? *static_cast<void**>(ret_slot) : nullptr;
    LOG_DEBUG_KV("MID_HOOK", "probe_a.whgame_image_block",
        log::KV("ud",         ud),
        log::KV("block",      block),
        log::KV("osize",      (int64_t)osize),
        log::KV("nsize",      (int64_t)nsize),
        log::KV("caller_ra",  caller_ra),
        log::KV("is_free",    (int64_t)(nsize == 0 ? 1 : 0)),
        log::KV("kcdx_dummynode", g_kcdx_dummynode));
}
```

### Wiring — WHGame image-range resolution (in `ArmFreallocProbe`, after the kcdx image-range resolve)

```cpp
{
    HMODULE whgame_mod = GetModuleHandleW(L"WHGame.dll");
    MODULEINFO wmi{};
    if (whgame_mod &&
        GetModuleInformation(GetCurrentProcess(), whgame_mod, &wmi, sizeof(wmi))) {
        g_whgame_image_base = reinterpret_cast<uintptr_t>(wmi.lpBaseOfDll);
        g_whgame_image_size = wmi.SizeOfImage;
        LOG_DEBUG_KV("MID_HOOK", "probe_a.whgame_image",
            log::KV("base", (void*)g_whgame_image_base),
            log::KV("size", (int64_t)g_whgame_image_size),
            log::KV("end",  (void*)(g_whgame_image_base + g_whgame_image_size)));
    } else {
        log::Error("PROBE A: GetModuleHandle/Info(WHGame.dll) failed — "
                   "WHGame-image block classification disabled");
    }
}
```
