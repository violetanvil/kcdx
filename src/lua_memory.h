// lua_memory — pak-Lua-visible memory primitives, raw Lua C API only.
//
// Originally adapted from ReturnOfModding (xiaoxiao921/ReturnOfModdingBase,
// `src/lua/bindings/memory.{hpp,cpp}` @ commit d30217b6). MIT.
//
// **Implementation note (rewrite 2026-05-18):** sol2 is intentionally
// NOT used here. Pinned via bisect in Phase 5c.7a, sol2's
// `new_usertype<>` registration on KCD2's live `lua_State` hard-crashes
// the game during save deserialization. See `../CLAUDE.md` hard rule
// #15 and workspace memory `project-kcd2-sol2-incompatibility` for the
// full bisect record and rationale.
//
// What this exposes to pak Lua under the lowercase `kcdx.*` global:
//   kcdx.memory.pointer(addr)              -- userdata + 22-method API
//   kcdx.memory.scan_pattern(pat)          -- AOB scan in WHGame.dll
//   kcdx.memory.scan_pattern_from_module(mod, pat)
//   kcdx.memory.get_module_base_address([mod])
//   kcdx.memory.allocate(size)             -- heap alloc, returns pointer
//   kcdx.memory.free(ptr)                  -- heap free
//
// Why this matters: KCD2's pak-mod Lua sandbox has `package.loadlib`
// compiled out (workspace memory `project-kcd2-lua-sandbox`). Without
// kcdx, pak Lua cannot touch C++ at all. kcdx's `kcdx.memory.*` is the
// **only bridge** for pak Lua mods to interact with native code, so
// the binding has to be game-compatible (no sol2 metatable
// scaffolding) rather than developer-convenient.
//
// Naming: lowercase `kcdx.memory.*` matches RoM convention; the
// PascalCase Phase-1-era `KCDX.ScanAndWrite` / `KCDX.ReadBytes` /
// `KCDX.GetWHGameBase` in lua_bind.cpp remain for backwards compat.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

extern "C" {
#include "lua.h"
}

#include "rom_borrowed/runtime_func_t.h"
#include "rom_borrowed/type_info_t.h"

namespace kcdx::lua_memory {

// Metatable names registered in LUA_REGISTRYINDEX. Stable identifiers
// the rest of kcdx uses to identify a kcdx.memory.pointer userdata
// or kcdx.memory.value_wrapper userdata pushed by other code paths
// (e.g., scripting dispatchers marshaling args).
constexpr const char* kPointerMetatable      = "kcdx.memory.pointer";
constexpr const char* kValueWrapperMetatable = "kcdx.memory.value_wrapper";

// --- pointer type ----------------------------------------------------------
//
// The userdata payload is a single uintptr_t. Methods read/write at
// that address. Phase 1 of the v0.1 API; future versions may add
// patch_byte/word/dword/qword/etc. (RoM exposes those tied to the
// patch_engine's byte_patch lifecycle, which kcdx doesn't have a Lua
// surface for yet).
struct pointer {
private:
    uintptr_t m_address = 0;

public:
    explicit pointer(uintptr_t address);
    pointer();

    pointer add(uintptr_t offset) const;
    pointer sub(uintptr_t offset) const;
    pointer rip() const;
    pointer rip_cmp() const;

    template <typename T> T  get()       const { return *(T*)m_address; }
    template <typename T> void set(T v) const { *(T*)m_address = v;     }

    std::string get_string() const;
    void        set_string(const std::string& s, int max_length) const;

    bool      is_null()  const;
    bool      is_valid() const;
    pointer   deref()    const;
    uintptr_t get_address() const;
};

// Push a `pointer` value onto the Lua stack as a userdata with the
// kPointerMetatable attached. Used by both kcdx.memory.* Lua bindings
// and by the scripting dispatchers when marshaling ptr_-typed args.
// Stack effect: +1.
void push_pointer(lua_State* L, pointer p);

// --- value_wrapper_t -------------------------------------------------------
//
// Wraps a raw byte slot from a runtime_func_t parameters_t or
// return_value_t alongside its type_info_t so Lua callbacks can
// :get() / :set(newVal) the value. Used by [[hook]] lua_callback and
// [[mid_hook]] arg marshaling.
class value_wrapper_t {
    char*                   m_value = nullptr;
    kcdx::rom::type_info_t  m_type{kcdx::rom::type_info_t::none_};

public:
    value_wrapper_t() = default;
    value_wrapper_t(char* val, kcdx::rom::type_info_t type);

    // Push the wrapped underlying value (NOT the wrapper itself) onto
    // the Lua stack with the appropriate Lua type. Stack effect: +1.
    void push_value(lua_State* L) const;

    // Read a Lua value from the given stack index and write it back
    // into m_value, respecting m_type. Reads, doesn't pop.
    void assign_from(lua_State* L, int stack_index);
};

// Push a `value_wrapper_t` onto the Lua stack as a userdata with the
// kValueWrapperMetatable attached. Stack effect: +1.
void push_value_wrapper(lua_State* L, value_wrapper_t vw);

// --- to_lua marshalers (called from kcdx::scripting dispatchers) ----------
//
// Push one argument or return value onto the Lua stack with the right
// representation per its type_info_t. Stack effect: +1.
//
// For type_info_t::ptr_   → pushes a `pointer` userdata.
// For type_info_t::none_  → pushes nil.
// For custom types (type.m_custom != nullptr) → custom feeder runs;
//                          contract: feeder pushes exactly 1 value.
// For primitives          → pushes a `value_wrapper_t` userdata.
void to_lua(lua_State*                                       L,
            const kcdx::rom::runtime_func_t::parameters_t*   params,
            uint8_t                                          arg_index,
            const std::vector<kcdx::rom::type_info_t>&       param_types);

void to_lua_return(lua_State*                                 L,
                   kcdx::rom::runtime_func_t::return_value_t* return_value,
                   kcdx::rom::type_info_t                     return_type);

// --- bindings --------------------------------------------------------------
//
// Called by lua_bind.cpp::RegisterKcdxTable. The kcdx global table is
// expected to be at the top of the Lua stack when bind() is called.
// bind() creates the `memory` sub-table inside the top table and
// populates it with the surface described in the header comment.
// Stack effect: 0 (the kcdx table on entry is left at the same
// position on exit).
void bind(lua_State* L);

}  // namespace kcdx::lua_memory
