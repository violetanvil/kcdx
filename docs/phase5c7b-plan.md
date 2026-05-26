# Phase 5c.7b plan — `kcdx.memory.dynamic_hook` (pure-Lua runtime hooks)

Authored 2026-05-18 after the Phase 5c.7a-rev2 commit. Surface to be
landed by this phase: pak Lua mods (or kcdx-DLL plugins) can install
a function-entry hook at runtime against any address kcdx can reach,
with pre/post Lua callbacks receiving typed args.

Goal: ship the *minimum* RoM-equivalent `memory.dynamic_hook` surface,
unified through `hook_engine` so the conflict matrix + first-wins
guarantee from Phase 4b.3 still apply.

## Lua surface

```lua
-- Returns: a "hook handle" userdata (kcdx.memory.dynamic_hook_handle).
-- The handle's main job is keeping the runtime_func_t alive for the
-- session; pak Lua doesn't need to call methods on it.
local handle = kcdx.memory.dynamic_hook({
    name          = "outfit_swap_lua_gate",  -- required, unique
    target        = some_pointer,            -- pointer userdata or integer VA
    return_type   = "i32",                   -- type string (see signatures.md)
    param_types   = {"i32", "ptr"},          -- list of type strings
    pre_callback  = function(return_value, arg1, arg2)
        -- arg1 / arg2 wrapped as kcdx.memory.value_wrapper or pointer
        -- userdata, depending on the type. retval is a value_wrapper
        -- the callback can :set() to change the return.
        System.LogAlways("pre-hook: arg1 = " .. tostring(arg1:get()))
        return true  -- false suppresses the original call
    end,
    post_callback = function(return_value, arg1, arg2)
        -- optional. retval reflects what the original returned (or
        -- what the pre-callback set, if it short-circuited).
    end,
    priority      = 100,                     -- optional, defaults 100
})
```

Returns `nil + error_string` if install failed. Common reasons: target
already hooked by an earlier-priority entry; target address invalid
(VirtualProtect couldn't grant execute); JIT-trampoline alloc failed.

## What this phase is NOT

Out of scope here (separate phases):

- `kcdx.memory.dynamic_hook_mid(...)` — uses runtime_func_t::make_jit_midfunc;
  signature differs (named captures table, not return+args). Phase 5g.
- `kcdx.memory.dynamic_call(name, sig, target_addr) → callable` —
  Lua → native invocation. Phase 5c.7c.
- `kcdx.lua.cfunction_address(luaCfun) → integer VA` — pulls the C
  pointer out of a Lua-stored cfunction. Phase 5c.7d.
- `[[hook]] lua_callback = "Mod.fn"` TOML schema — Phase 5f, builds
  on the same `register_pre_callback_from_top` rails this phase wires up.
- `dynamic_hook_enable / _disable` — RoM ships these but real RoM
  mods don't use them; deferred indefinitely.
- `resolve_pointer_to_type` custom-usertype machinery — deferred to
  whenever the first plugin needs it.

## Architecture

### `hook_engine::InstallRuntime` — new entrypoint

`hook_engine.cpp` today owns the TOML hook install path:
`ApplyOneHook(HookEntry&)` reads a parsed `HookEntry`, allocates from
`trampoline::branch_pool`, calls MinHook. Pre-flight + conflict
detection happens in `conflict_engine::RunPreFlight` (called once at
first-update-tick).

A runtime-installed hook can't go through pre-flight (the whole point
is it didn't exist at first-update-tick time). It has to do its own
conflict check inline, then call the same low-level MinHook + branch
pool path.

New function in `hook_engine.h`:

```cpp
struct RuntimeInstallParams {
    std::string name;
    int         priority = 100;
    uintptr_t   target_addr;
    void*       detour_addr;       // JIT'd by runtime_func_t::make_jit_func
};

struct RuntimeInstallResult {
    bool        ok = false;
    std::string reason;            // populated when !ok
};

// Synchronous install. Performs first-wins check against any hooks
// already in g_installed (whether TOML or runtime). Allocates from
// branch_pool. Calls MH_CreateHook + MH_EnableHook. Adds an entry to
// g_installed so subsequent installs and conflict reports see this
// hook. Logs to kcdx.log on success and failure.
RuntimeInstallResult InstallRuntime(const RuntimeInstallParams& params);
```

The first-wins check walks `hook_engine::g_installed` looking for any
hook whose 5-byte rel32 jmp footprint overlaps `target_addr`. On
overlap: log "hook 'X' aborted: target 0x... is already hooked by
'Y'", return `{ok=false, reason="..."}`.

### Lua-side bindings: `kcdx.memory.dynamic_hook`

New file `src/lua_bind_dynamic_hook.cpp` (~250 LOC). Why its own file:
this is dense raw-C-API code (multiple table-field reads, type-list
parsing, callback registration, JIT call, install call), and at 250
LOC fits the per-file size norm.

Layout:

```cpp
namespace kcdx::lua_bind_dynamic_hook {

// One Lua C function exported as kcdx.memory.dynamic_hook.
// Returns 1 result on success (the handle userdata), or 2 results on
// failure (nil + error string).
int Lua_DynamicHook(lua_State* L);

// Metatable: kcdx.memory.dynamic_hook_handle. __gc currently no-op
// (hooks persist for the session, matching RoM v0.1).
void PushMetatable(lua_State* L);  // for lua_bind_helpers RegisterMetatables

}  // namespace
```

The `Lua_DynamicHook` body in pseudocode:

```
1. luaL_checktype(L, 1, LUA_TTABLE)  -- single table arg
2. Pull "name" (string, required)
3. Pull "target": accept pointer userdata OR integer
4. Pull "return_type": string, default "void"
5. Pull "param_types": Lua table, list of strings
6. Pull "priority": optional integer, default 100
7. Pull "pre_callback" / "post_callback": optional functions
   (at least one of the two must be present)

8. Build runtime_func_t (new'd into a userdata so __gc cleans it up)
9. Call rf->make_jit_func(return_type, param_types, asmjit::Arch::kHost,
                          &scripting::dynamic_hook_pre,
                          &scripting::dynamic_hook_post,
                          target_addr)
   - returns the jitted detour address, or 0 on error

10. hook_engine::InstallRuntime({name, priority, target_addr, jitted_addr})
    - returns {ok, reason}
    - on failure: cleanup the runtime_func_t (its dtor frees the JIT
      buffer), push (nil, reason), return 2.

11. scripting::register_hook(target_addr, rf*)  -- non-owning
12. For each provided callback:
      lua_pushvalue(L, the_callback_idx)         -- push the function copy
      scripting::register_pre_callback_from_top(target_addr)
      (luaL_refs it off the top; consumes it)

13. Push the handle userdata (a runtime_func_t* wrapper), return 1.
```

### Type-string parsing reuse

`runtime_func_t::make_jit_func(return_type_str, param_types_vec, ...)`
already exists and accepts the string-list overload. It calls
`get_type_info_from_string` and `get_type_id` per type. No new parsing
required here.

### Callback storage reuse

`kcdx::scripting::register_pre_callback_from_top` and
`register_post_callback_from_top` already exist (landed in Phase
5c.7a-rev2 alongside the sol2 strip). They `luaL_ref` the function on
top of the stack and store the ref in the per-target map.

### Conflict-engine integration

Phase 5c.7b does **not** add a new conflict category. The first-wins
check in `InstallRuntime` reuses the existing `HookOnHook` semantics
from Phase 4b.3 — same logic, same log line shape — just invoked
synchronously rather than from pre-flight. If a TOML hook already
occupies the address: log + abort. If a runtime hook from earlier in
the session already occupies it: same.

Cross-engine collision detection (runtime hook landing on a TOML
patch's bytes) is the trickier case. For Phase 5c.7b we accept the
simpler-but-loud behavior: the runtime install proceeds without that
check, but post-install we log a warning if the 5-byte rel32 footprint
overlaps any entry in `patch_engine::g_patches`'s `writeRange`. That
warning matches the `HookOverlapsEarlierPatch` semantics from Phase
4b.3 (MinHook relocates the prologue, both apply, no action needed).

## File plan

Grouped into two sub-stages so the SKSE-pattern alignment lands as
its own logical change before the Lua-binding work on top.

### Stage 5c.7b.1 — branch_pool / MinHook alignment with SKSE

| Action | File | LOC est |
|---|---|---|
| Edit | `src/trampoline.{h,cpp}` | +30 (bump reservation to 256 KB; add dst→stub dedup map) |
| Edit | `src/rom_borrowed/runtime_func_t.cpp` | +20 (route make_jit_func / make_jit_midfunc through branch_pool; hard-fail on out-of-range) |
| Edit | `vendor/minhook/src/buffer.c` | +20 (route MinHook trampoline allocations through branch_pool) |
| New  | `vendor/minhook/KCDX_PATCH.md` | ~40 (document the patch + rationale) |

Live-validate before moving to 5c.7b.2: regression test Phase 4
examples, save-load, outfit-swap-in-combat all pass.

### Stage 5c.7b.2 — kcdx.memory.dynamic_hook Lua binding

| Action | File | LOC est |
|---|---|---|
| New  | `src/lua_bind_dynamic_hook.cpp` | ~250 |
| Edit | `src/lua_bind_helpers.cpp` | +20 (register handle metatable) |
| Edit | `src/lua_bind_memory.cpp` | +5 (add `dynamic_hook` to kFunctions) |
| Edit | `src/hook_engine.h` | +25 (RuntimeInstallParams + InstallRuntime decl) |
| Edit | `src/hook_engine.cpp` | +80 (InstallRuntime body) |
| Edit | `CMakeLists.txt` | +1 line |
| Edit | `docs/design.md` | +30 (document the new schema in the API surface section) |

Total across both stages: ~520 LOC + docs. Each file stays under
the small-file rule.

## Validation sequence

Same shape as Phase 5c.7a-rev2:

1. **Build clean** — every file compiles, kcdx.dll size sane.
2. **Regression** — install kcdx.dll, launch, confirm Phase 4
   examples still apply identically (no new conflict warnings).
3. **Save-load** — load same save the sol2 work crashed on, confirm
   no crash.
4. **Verify pak** — extend `lua-memory-verify` with a
   `dynamic_hook` test case:
     - hook the outfit-swap target address resolved via
       `kcdx.memory.scan_pattern(...)`
     - log when the pre-callback fires
     - exit cleanly; confirm hook install logged + at least one
       pre-callback fired during gameplay
5. **First-wins** — second verify pak `dynamic_hook` against the
   SAME target should abort with a clear log line naming the first
   installer.
6. **Outfit-swap-in-combat smoke test** — confirm the original
   feature still works end-to-end (no regression from the runtime
   hook landing on the patched site).

## Open questions deferred to implementation

1. **`pre_callback` signature when there's no return value.** RoM
   passes `(return_value, ...args)` regardless; the callback can
   ignore the first arg if its hook is void. We mirror this.
2. **Custom-type feeders.** The Phase 5c.7a-rev2 change updated
   `type_info_feeder_t` to push-value-and-return-void. The Lua
   callback receives whatever the feeder pushes. This is fine for
   v0.1; we exercise it in Phase 5e when `RegisterFunction` lets
   plugins register custom feeders.
3. **Thread safety of the install.** `InstallRuntime` and
   `register_*_callback_from_top` take `g_lock` in the right order.
   Plugin authors calling `kcdx.memory.dynamic_hook` from their pak
   Lua are on the main game thread (Lua VM is single-threaded). DLL
   plugins might call from `kcdxPlugin_Load` — same thread, same
   safety.
4. **What if `lua_callback` is also set on a `[[hook]]` TOML entry
   targeting the same address?** Phase 5c.7b doesn't add TOML
   `lua_callback` support yet (that's 5f). Until 5f lands, the
   "same address from TOML and Lua-runtime" case can only mean
   TOML's `bytes`-style detour conflicting with Lua-runtime
   dynamic_hook — first-wins applies.

## Risks

1. **Lua-runtime hooks landing inside the JIT'd detour of an
   earlier TOML hook.** The TOML hook's MinHook trampoline gets
   placed in branch_pool. If a runtime hook resolves to an address
   inside that trampoline, the second install corrupts the first.
   Mitigation: branch_pool addresses are reserved in a specific
   ±2GB window from WHGame.dll's .text. AOB scans only return
   matches inside WHGame.dll's executable sections, which don't
   overlap that range. So the scenario shouldn't occur in practice,
   but it's worth verifying with a log assertion.

2. **`make_jit_func` heap allocation violates rel32 reach.**
   ~~Per runtime_func_t.cpp's existing comment~~, the JIT buffer is
   currently `new uint8_t[]` + `VirtualProtect`, NOT branch_pool.
   Per SKSE research (2026-05-18; see commit log for the subagent
   report), this isn't a theoretical issue — when allocation lands
   outside the ±2GB window from `target_addr`, the resulting `E9
   xx xx xx xx` write SILENTLY produces an out-of-range displacement
   rather than a clean error. SKSE handles this by `ASSERT`ing on
   out-of-range allocation rather than recovering.

   **Decision (locked in, taking SKSE pattern verbatim):**
   - Route runtime_func_t's JIT buffer through `trampoline::AllocateBranch`
     (the existing pool, ±2GB anchored to WHGame.dll).
   - On allocation failure: hard error with a clear log message
     ("kcdx.memory.dynamic_hook 'X': JIT pool exhausted, increase
     branch_pool reservation"), matching SKSE's hard-fail pattern.
   - Currently `branch_pool` is sized for 5-byte rel32 stubs; we
     bump its reservation to 256KB so a few hundred Lua-marshaling
     trampolines fit. Plugin-author-controlled future-proofing
     (TOML `[kcdx] branch_pool_reservation_kb = 1024` etc.) is
     deferred to whenever the first plugin hits the limit.
   - Steal CommonLibSSE-NG's dst→stub dedup map (`_5branches`,
     `_6branches`) so two `dynamic_hook` calls against the same
     target reuse one trampoline. Small win, cheap to implement
     (~15 LOC).

   This expands Phase 5c.7b scope by ~50 LOC (runtime_func_t.cpp
   route change + branch_pool bump + log message wording) but
   eliminates the footgun. The increment is small enough to keep
   5c.7b as one phase rather than splitting.

3. **MinHook + branch_pool interaction.** SKSE doesn't use MinHook
   (they roll their own `Write5Branch` / `Write6Branch`). We do
   use MinHook. MinHook internally allocates its OWN trampoline
   buffer (where the relocated original-prologue bytes go), via
   its `Buffer.c` heap. By default that's `VirtualAlloc(NULL, ...)`
   with no address hint — same problem. Phase 4b.1 didn't hit it
   because TOML hooks have always been declarative and the heap
   happened to land close. For runtime hooks called from arbitrary
   moments during gameplay, the heap allocator may have wandered.

   **Decision:** also patch MinHook's `Buffer.c` to take its
   trampoline allocations from our branch_pool. ~20 LOC patch
   that we vendor as a kcdx-specific MinHook delta. Document in
   `vendor/minhook/KCDX_PATCH.md`.
