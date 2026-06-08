// runtime_func_t — JIT-trampoline generator for typed Lua callback dispatch.
//
// Adapted from ReturnOfModding by xiaoxiao921 et al.
// Source: https://github.com/xiaoxiao921/ReturnOfModdingBase/blob/master/src/lua/bindings/runtime_func_t.hpp
// License: MIT. Modifications for kcdx:
//   - namespace lua::memory -> kcdx::rom
//   - PolyHook2's big::detour_hook removed; the JIT call-original slot it
//     owned now lives directly on runtime_func_t (m_original_slot). The
//     detour install is driven at hook_engine::InstallRuntime (the backend
//     seam), which POPULATES the slot; runtime_func_t owns the storage.
//   - destructor's lua_manager-singleton cleanup gated behind kcdx::scripting
//     existing (a later step); the detour is never torn down here (kcdx
//     hooks live for the session)
//
// Public-facing types (parameters_t, return_value_t, the pre/post/mid
// callback typedefs) are bytes-only — no Sol2 dependency in this file.
// Sol2 enters only in memory.cpp (the Lua binding layer above this).
#pragma once

#include <asmjit/asmjit.h>
#include <cstdint>
#include <string>
#include <vector>

#include "asmjit_helper.h"  // for is_general_register, get_call_convention etc.
#include "type_info_t.h"

namespace kcdx::rom {

class runtime_func_t {
    // Allocated from kcdx::trampoline::AllocateBranch (within +/-2 GB of
    // WHGame.dll, alloc-only, no free) instead of heap-via-vector. The
    // previous std::vector approach didn't guarantee rel32 reachability
    // from the target function — could produce silently-truncated 5-byte
    // E9 displacements when the heap wandered outside the 2GB window.
    // Matches SKSE's BranchTrampoline pattern.
    void*                         m_jit_function_buffer = nullptr;
    size_t                        m_jit_function_size   = 0;
    asmjit::x86::Mem              m_args_stack;
    // The JIT call-original slot. runtime_func_t OWNS this storage at a
    // STABLE address the JIT bakes (get_jit_original_slot()); a producer
    // POPULATES it (InstallRuntime writes the backend's relocated-original
    // for a function-entry/mid chain hook or a dynamic_hook; the callsite
    // path writes the callee VA directly with no backend). The JIT'd asm
    // derefs the CURRENT slot value at runtime — so the slot's ADDRESS is
    // baked once at JIT time and the VALUE is filled later by the producer.
    // The slot lives directly on runtime_func_t (not in a backend object)
    // precisely because the callsite path needs it without ANY install.
    void*                         m_original_slot = nullptr;
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
    // mid_callback_t (the make_jit_midfunc mid-dispatch fn-ptr type) was removed
    // with the mid JIT; the mid path now calls hook_chain::MidDispatch directly
    // from the safetyhook::MidHook adapter (src/safetyhook_midhook.cpp).

    runtime_func_t();
    ~runtime_func_t();

    uintptr_t get_target_func_ptr() const { return m_target_func_ptr; }

    // Pointer-to-the-slot-holding-the-relocated-original-entry.
    //
    // CRITICAL: returns void** (a STABLE address into this object), not
    // void* (the slot value). The JIT bakes the address returned here as an
    // asmjit qword_ptr; the JIT'd instruction reads the CURRENT value of the
    // slot at runtime. The producer writes the value here AFTER the JIT runs:
    // InstallRuntime writes the backend's relocated-original (pOriginal) for
    // a function-entry/mid chain hook or a dynamic_hook; the callsite path
    // writes the callee VA directly with no install. Without this write the
    // trampoline reads null and the closing `ret` jumps to address 0.
    //
    // Always non-null now that runtime_func_t owns the slot member directly
    // (the slot can no longer be missing the way it could when it lived in a
    // separately-allocated backend object).
    void** get_jit_original_slot() { return &m_original_slot; }

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

    // The mid-function JIT variant (make_jit_midfunc) was REMOVED — the mid path
    // is now a safetyhook::MidHook adapter (src/safetyhook_midhook.{cpp,h}) that
    // reads/writes named captures through Context64 and routes each fire to
    // hook_chain::MidDispatch. Only the function-entry JIT (make_jit_func above)
    // + the call-original slot remain on runtime_func_t.
};

}  // namespace kcdx::rom
