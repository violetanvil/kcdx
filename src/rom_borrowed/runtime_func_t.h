// runtime_func_t — JIT-trampoline generator for typed Lua callback dispatch.
//
// Adapted from ReturnOfModding by xiaoxiao921 et al.
// Source: https://github.com/xiaoxiao921/ReturnOfModdingBase/blob/master/src/lua/bindings/runtime_func_t.hpp
// License: MIT. Modifications for kcdx:
//   - namespace lua::memory -> kcdx::rom
//   - PolyHook2's big::detour_hook -> kcdx::detour_hook shim (MinHook-backed)
//   - destructor's lua_manager-singleton cleanup gated behind kcdx::scripting
//     existing (Phase 5c step 6+); for now the destructor just disables
//     the hook
//
// Public-facing types (parameters_t, return_value_t, the pre/post/mid
// callback typedefs) are bytes-only — no Sol2 dependency in this file.
// Sol2 enters only in memory.cpp (the Lua binding layer above this).
#pragma once

#include <asmjit/asmjit.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../detour_hook.h"
#include "asmjit_helper.h"  // for is_general_register, get_call_convention etc.
#include "type_info_t.h"

namespace kcdx::rom {

class runtime_func_t {
    // Phase 5c.7b.1: allocated from kcdx::trampoline::AllocateBranch
    // (within +/-2 GB of WHGame.dll, alloc-only, no free) instead of
    // heap-via-vector. The previous std::vector approach didn't
    // guarantee rel32 reachability from the target function — could
    // produce silently-truncated 5-byte E9 displacements when the
    // heap wandered outside the 2GB window. Matches SKSE's
    // BranchTrampoline pattern (subagent research 2026-05-18).
    void*                         m_jit_function_buffer = nullptr;
    size_t                        m_jit_function_size   = 0;
    asmjit::x86::Mem              m_args_stack;
    std::unique_ptr<kcdx::detour_hook> m_detour;
    uintptr_t                     m_target_func_ptr{};

public:
    type_info_t              m_return_type;
    std::vector<type_info_t> m_param_types;

    struct parameters_t {
        template <typename T>
        void set(const uint8_t idx, const T val) const {
            *(T*)get_arg_ptr(idx) = val;
        }
        template <typename T>
        T get(const uint8_t idx) const {
            return *(T*)get_arg_ptr(idx);
        }
        // asm depends on this specific type. The runtime_func allocates
        // stack space pointed at here (see asmjit::compiler.newStack calls).
        volatile uintptr_t m_arguments;
        // must be char* for aliasing rules when reading back out
        char* get_arg_ptr(const uint8_t idx) const;
    };

    class return_value_t {
        uintptr_t m_return_value;
    public:
        unsigned char* get() const;
    };

    typedef bool      (*user_pre_callback_t) (const parameters_t* params, const uint8_t parameters_count, return_value_t* return_value, const uintptr_t target_func_ptr);
    typedef void      (*user_post_callback_t)(const parameters_t* params, const uint8_t parameters_count, return_value_t* return_value, const uintptr_t target_func_ptr);
    typedef uintptr_t (*mid_callback_t)      (const parameters_t* params, const size_t param_count, const uintptr_t target_func_ptr);

    runtime_func_t();
    ~runtime_func_t();

    uintptr_t get_target_func_ptr() const { return m_target_func_ptr; }

    void enable_hook()  { if (m_detour) m_detour->enable();  }
    void disable_hook() { if (m_detour) m_detour->disable(); }

    // Phase 5g: when the install path bypasses m_detour (e.g.,
    // hook_engine::InstallRuntime calls MH_CreateHook directly to
    // share its g_installed first-wins map across TOML + runtime
    // hooks), the caller must write MinHook's returned pOriginal
    // here so the JIT'd trampoline's `push qword [&original_]` reads
    // the correct value at runtime. Without this, the trampoline
    // pushes null and the subsequent `ret` jumps to address 0.
    //
    // Returns &original_ — the same pointer `get_original_ptr()` bakes
    // into the JIT'd asm at JIT time.
    void** get_jit_original_slot() {
        return m_detour ? m_detour->get_original_ptr() : nullptr;
    }

    // Diagnostic accessors used by hook_engine + scripting for structured
    // logging of JIT-buffer state. Cheap; safe to call at any time after
    // make_jit_*func has returned non-zero.
    void*  get_jit_buffer() const { return m_jit_function_buffer; }
    size_t get_jit_size()   const { return m_jit_function_size;   }

    // FNV-1a fingerprint of the entire JIT buffer contents. Used to
    // detect post-install overwrites — diagnostic only; not on any hot
    // path. Returns 0 if the buffer was never allocated.
    uint64_t fingerprint_jit_buffer() const;

    // FNV-1a over the bytes of `*this` (the runtime_func_t object on
    // the heap). Diagnostic only. Captures whether anything stomped
    // into the object's own heap allocation between install and a
    // later checkpoint. The captures area (m_jit_function_buffer,
    // m_jit_function_size, m_param_types vector, etc.) is part of
    // the fingerprint.
    uint64_t fingerprint_self() const;

    // FNV-1a over the bytes of the detour_hook on the heap. Returns 0
    // if m_detour was reset.
    uint64_t fingerprint_detour() const;

    void debug_print_args(const asmjit::FuncSignature& sig);

    // Build a JIT trampoline given a raw asmjit signature. Returns the
    // address of the generated code, suitable for MinHook to install as
    // a detour.
    uintptr_t make_jit_func(const asmjit::FuncSignature& sig,
                            const asmjit::Arch arch,
                            const user_pre_callback_t pre_callback,
                            const user_post_callback_t post_callback,
                            const uintptr_t target_func_ptr);

    // String-typed convenience overload. Parses return + param type
    // strings ("i32", "float", "const char*", ...) via get_type_id.
    uintptr_t make_jit_func(const std::string& return_type,
                            const std::vector<std::string>& param_types,
                            const asmjit::Arch arch,
                            const user_pre_callback_t pre_callback,
                            const user_post_callback_t post_callback,
                            const uintptr_t target_func_ptr,
                            std::string call_convention = "");

    // Mid-function hook variant. param_captures are register/memory
    // expressions ("rax", "[rcx+0x10]", etc.); stack_restore_offset is
    // how many bytes the original instruction took (so we resume past it).
    uintptr_t make_jit_midfunc(const std::vector<std::string>& param_types,
                               const std::vector<std::string>& param_captures,
                               const int stack_restore_offset,
                               const asmjit::Arch arch,
                               mid_callback_t mid_callback,
                               const uintptr_t target_func_ptr);

    void create_and_enable_hook(const std::string& hook_name,
                                uintptr_t target_func_ptr,
                                uintptr_t jitted_func_ptr,
                                bool is_follow_call_on_fn_address = true);
};

}  // namespace kcdx::rom
