# Phase 5b: ReturnOfModding source-port plan

Internal-doc, not a user-facing reference. Captures what each upstream
RoM file does, what it depends on, and how it adapts to kcdx so I
don't relearn it mid-port.

## Source

[xiaoxiao921/ReturnOfModdingBase](https://github.com/xiaoxiao921/ReturnOfModdingBase),
`src/lua/bindings/`. Four files form the typed-marshaling machinery:

| File | Size | Purpose |
|---|---|---|
| `type_info_t.{hpp,cpp}` | 1.7 KB | Type-tag system: maps string type names ("i32", "float", "ptr", ...) to an enum + custom-feeder callbacks. Used by `runtime_func_t::make_jit_func` to decide which asmjit register class each argument lands in. |
| `asmjit_helper.{hpp,cpp}` | 15 KB | asmjit utility wrappers: `get_call_convention(string)`, `get_type_id(string)`, `get_gp_from_name(string)` (RAX/RCX/etc.), `get_addr_from_name` (parses `[rsp+0x10]:i32` style memory expressions). All asmjit-facing, no other deps. |
| `runtime_func_t.{hpp,cpp}` | 22 KB | The load-bearing JIT-trampoline generator. `make_jit_func` builds a trampoline that copies CPU registers/stack into a `parameters_t` struct, calls a C callback, optionally calls the original via the detour's saved trampoline, optionally writes back into registers. `make_jit_midfunc` is the mid-function variant for `[[mid_hook]]`. |
| `memory.{hpp,cpp}` | 45 KB | The Lua-API binding layer. Implements `memory.dynamic_hook` and `memory.dynamic_hook_mid` as Lua-callable functions, defines `pointer` and `value_wrapper_t` userdata types, dispatches pre/post Lua callbacks for each registered hook. Sol2-heavy. |

## Adapter surface (kcdx → RoM)

### `kcdx::detour_hook` — shape-compatible MinHook wrapper

RoM's `runtime_func_t.{hpp,cpp}` references `std::unique_ptr<big::detour_hook>`
in 5 places. To minimize the diff against upstream code, we write a thin
wrapper class with the same method signatures. The wrapper internally
uses MinHook.

```cpp
namespace kcdx {  // we'll alias `namespace big = kcdx;` in adapted files
class detour_hook {
public:
    void set_instance(const std::string& hook_name, void* target, void* detour);
    void set_is_follow_call_on_fn_address(bool b) { /* MinHook doesn't follow */ }
    void enable();   // MH_CreateHook + MH_EnableHook (lazy: only creates on first enable)
    void disable();  // MH_DisableHook
    void* get_original_ptr() const { return original_; }  // MinHook's pOriginal slot
private:
    std::string name_;
    void* target_ = nullptr;
    void* detour_ = nullptr;
    void* original_ = nullptr;
    bool installed_ = false;
};
}  // namespace kcdx
```

~50 LOC. Lives at `src/detour_hook.{h,cpp}` (engine-internal, not in the
public include/ tree).

### Sol2: vendored

RoM's `memory.cpp` uses Sol2 (`sol::object`, `sol::table`, `sol::usertype`,
etc.) for ~54 references. Vendoring Sol2 lets us port `memory.cpp` largely
as-is rather than rewriting it against raw Lua C API. Sol2 is MIT-licensed,
header-only (~2 MB), well-maintained.

Vendor at `vendor/sol2/` flat-copy from the [sol2 repo](https://github.com/ThePhD/sol2)
at its latest release tag. See `vendor/sol2/VENDORED.md` when we create it.

### Dependency substitutions

Each adapted RoM file needs the same set of trivial substitutions:

| RoM dep | kcdx substitute |
|---|---|
| `ankerl::unordered_dense::map` | `std::unordered_map` |
| `<AsyncLogger/Logger.hpp>` + `LOG(level) << "..."` | `kcdx::log::Info("...")` etc. |
| `<string/string.hpp>` | drop the include (we don't use the project's string utilities) |
| `<hooks/detour_hook.hpp>` | `"detour_hook.h"` (the kcdx shim above) |
| `<rom/rom.hpp>` (configuration paths etc.) | drop where possible; replace specific symbols inline |
| `big::g_lua_manager` (singleton) | a kcdx equivalent that tracks `target_func_ptr -> dynamic_hook` mapping. Lives in the new `kcdx::scripting` module that owns dynamic_hook state. |
| `big::lua_module::this_from(env)` | for now, return nullptr (we don't have per-plugin Lua isolation in v0.1 — single Lua state). |

### `big::g_lua_manager` substitute

This is the trickiest piece. RoM's `g_lua_manager` is a singleton that
holds, among other things:

```cpp
ankerl::unordered_dense::map<uintptr_t, std::unique_ptr<runtime_func_t>>
    m_target_func_ptr_to_dynamic_hook;
```

When a hook fires, the C trampoline looks up the corresponding
`runtime_func_t` by target address and dispatches Lua callbacks via
`dynamic_hook_pre_callbacks(target_func_ptr, ...)` and the post variant.

In kcdx we replicate this with `kcdx::scripting::g_dynamic_hooks` —
a `std::unordered_map<uintptr_t, std::unique_ptr<runtime_func_t>>` plus
two dispatch functions. The callbacks themselves (the Lua functions
plugin authors register) live in a parallel vector per dynamic_hook
entry. ~150 LOC in a new `src/scripting.cpp`.

## Adaptation order (Phase 5c)

1. Vendor Sol2 (`vendor/sol2/`)
2. Write `kcdx::detour_hook` shim (`src/detour_hook.{h,cpp}`)
3. Port `type_info_t.{hpp,cpp}` (trivial substitutions)
4. Port `asmjit_helper.{hpp,cpp}` (substitute LOG + map)
5. Port `runtime_func_t.{hpp,cpp}` (substitute LOG + map + detour_hook)
6. Write `kcdx::scripting` module (the manager-singleton substitute)
7. Port `memory.{hpp,cpp}` (Sol2 stays; substitute `big::` for `kcdx::scripting`)
8. Add `src/rom_borrowed/` to CMakeLists, link against asmjit + minhook + lua

Estimated ~2 days for steps 1-8 combined.

## Out of scope for Phase 5c (defer to 5d-5f)

- The public-facing `kcdxScriptingInterface::RegisterFunction` API (5e)
- TOML schema additions: `lua_callback` + `signature` on `[[hook]]` (5f)
- TOML schema additions: `[[mid_hook]]` (5g)
- The Lua VM thread-model test (5d)
- Player-visible example plugin (5h)
- VERIFY_PHASE5.md (5i)

## Risks

1. **Sol2 vs Lua 5.1 compatibility.** Sol2 supports Lua 5.1-5.4; kcdx
   ships Lua 5.1 (KCD2's bundled VM). Need to confirm Sol2 has good
   Lua 5.1 support on first build.

2. **asmjit + RoM compatibility.** RoM was built against an older asmjit;
   the API may have shifted. Build errors will tell us.

3. **JIT trampoline + MinHook prologue interaction.** RoM's trampolines
   work because PolyHook2's relocated prologue lives at a known address
   that the trampoline jumps to. MinHook does the same but via
   `pOriginal` — confirm `runtime_func_t.cpp`'s `m_detour->get_original_ptr()`
   calls land on bytes that MinHook actually relocated.

4. **Lua VM thread-model.** Open question from design.md. Phase 5d
   answers it before we expose plugin-author Lua callbacks.
