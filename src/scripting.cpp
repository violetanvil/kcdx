#include "scripting.h"

#include "log.h"

#include <mutex>

namespace kcdx::scripting {

namespace {

// All state guarded by a single recursive_mutex. Recursive because a
// dispatched Lua callback may itself trigger another hook (e.g., a Lua
// function calls a hooked C++ function), which re-enters the dispatch
// path. Non-recursive lock would deadlock that case.
std::recursive_mutex g_lock;

lua_State* g_lua_state = nullptr;

// Non-owning map. Lifetime of runtime_func_t lives in the [[hook]]
// installer (or, eventually, Lua-side memory.dynamic_hook userdata).
std::unordered_map<uintptr_t, kcdx::rom::runtime_func_t*> g_target_to_hook;

// Per-target callback vectors. Keyed by target_func_ptr so a single
// hook installation on a given function can fan-in callbacks from
// multiple plugins.
std::unordered_map<uintptr_t, std::vector<sol::protected_function>> g_pre_callbacks;
std::unordered_map<uintptr_t, std::vector<sol::protected_function>> g_post_callbacks;
std::unordered_map<uintptr_t, std::vector<sol::protected_function>> g_mid_callbacks;

}  // namespace

void set_lua_state(lua_State* L) {
    std::scoped_lock guard(g_lock);
    g_lua_state = L;
    if (L) {
        log::Info("scripting: lua_State bound");
    } else {
        log::Info("scripting: lua_State cleared");
    }
}

lua_State* lua_state() {
    std::scoped_lock guard(g_lock);
    return g_lua_state;
}

void register_hook(uintptr_t target_func_ptr, kcdx::rom::runtime_func_t* hook) {
    std::scoped_lock guard(g_lock);
    g_target_to_hook[target_func_ptr] = hook;
}

void unregister_hook(uintptr_t target_func_ptr) {
    std::scoped_lock guard(g_lock);
    g_target_to_hook.erase(target_func_ptr);
    g_pre_callbacks.erase(target_func_ptr);
    g_post_callbacks.erase(target_func_ptr);
    g_mid_callbacks.erase(target_func_ptr);
}

kcdx::rom::runtime_func_t* get_existing_dynamic_hook(uintptr_t target_func_ptr) {
    std::scoped_lock guard(g_lock);
    const auto it = g_target_to_hook.find(target_func_ptr);
    return it == g_target_to_hook.end() ? nullptr : it->second;
}

void register_pre_callback(uintptr_t target_func_ptr, sol::protected_function fn) {
    std::scoped_lock guard(g_lock);
    g_pre_callbacks[target_func_ptr].push_back(std::move(fn));
}

void register_post_callback(uintptr_t target_func_ptr, sol::protected_function fn) {
    std::scoped_lock guard(g_lock);
    g_post_callbacks[target_func_ptr].push_back(std::move(fn));
}

void register_mid_callback(uintptr_t target_func_ptr, sol::protected_function fn) {
    std::scoped_lock guard(g_lock);
    g_mid_callbacks[target_func_ptr].push_back(std::move(fn));
}

void clear_callbacks(uintptr_t target_func_ptr) {
    std::scoped_lock guard(g_lock);
    g_pre_callbacks.erase(target_func_ptr);
    g_post_callbacks.erase(target_func_ptr);
    g_mid_callbacks.erase(target_func_ptr);
}

// --- dispatchers ------------------------------------------------------------
//
// The JIT trampoline built by runtime_func_t::make_jit_func calls into
// these via runtime_func_t::user_pre_callback_t / user_post_callback_t /
// mid_callback_t. We're inside the original function's prologue here —
// minimal allocation, no exceptions across the C/Lua boundary.
//
// Phase 5c.6 ships these with a placeholder marshaling strategy: the
// Lua callback receives the target_func_ptr (as a Lua integer) and
// param_count, but no actual unpacked args yet. Phase 5c.7 wires
// memory.cpp's `pointer`/`value_wrapper_t` types into the arg list.
//
// The lock is held across the entire dispatch. If a callback re-enters
// (e.g., calls a hooked function), the recursive_mutex permits it; but
// the per-target callback vectors must not be mutated mid-iteration,
// so register_* and unregister_* must NOT be called from inside a
// callback. (Future hardening: snapshot the vector under the lock,
// release, then iterate — but that costs an alloc per dispatch.)

bool dynamic_hook_pre(const kcdx::rom::runtime_func_t::parameters_t* /*params*/,
                      uint8_t                                        param_count,
                      kcdx::rom::runtime_func_t::return_value_t*     /*return_value*/,
                      uintptr_t                                      target_func_ptr) {
    std::scoped_lock guard(g_lock);

    if (!g_lua_state) {
        // VM not ready yet — let the original run.
        return true;
    }

    const auto it = g_pre_callbacks.find(target_func_ptr);
    if (it == g_pre_callbacks.end()) {
        return true;
    }

    bool call_orig = true;
    for (const auto& fn : it->second) {
        sol::protected_function_result r = fn(target_func_ptr, (unsigned)param_count);
        if (!r.valid()) {
            sol::error err = r;
            log::ErrorF("scripting: pre-callback for target 0x%p threw: %s",
                        (void*)target_func_ptr, err.what());
            continue;
        }
        // Lua may return false to suppress the original. Any non-false
        // (nil, no return, true) keeps call_orig at its current value;
        // once any callback says "false", we stop calling the original.
        if (r.get_type(0) == sol::type::boolean && r.get<bool>(0) == false) {
            call_orig = false;
        }
    }
    return call_orig;
}

void dynamic_hook_post(const kcdx::rom::runtime_func_t::parameters_t* /*params*/,
                       uint8_t                                        param_count,
                       kcdx::rom::runtime_func_t::return_value_t*     /*return_value*/,
                       uintptr_t                                      target_func_ptr) {
    std::scoped_lock guard(g_lock);

    if (!g_lua_state) return;

    const auto it = g_post_callbacks.find(target_func_ptr);
    if (it == g_post_callbacks.end()) return;

    for (const auto& fn : it->second) {
        sol::protected_function_result r = fn(target_func_ptr, (unsigned)param_count);
        if (!r.valid()) {
            sol::error err = r;
            log::ErrorF("scripting: post-callback for target 0x%p threw: %s",
                        (void*)target_func_ptr, err.what());
        }
    }
}

uintptr_t dynamic_hook_mid(const kcdx::rom::runtime_func_t::parameters_t* /*params*/,
                           size_t                                         param_count,
                           uintptr_t                                      target_func_ptr) {
    std::scoped_lock guard(g_lock);

    if (!g_lua_state) return 0;

    const auto it = g_mid_callbacks.find(target_func_ptr);
    if (it == g_mid_callbacks.end()) return 0;

    uintptr_t restore_address = 0;
    for (const auto& fn : it->second) {
        sol::protected_function_result r = fn(target_func_ptr, (unsigned)param_count);
        if (!r.valid()) {
            sol::error err = r;
            log::ErrorF("scripting: mid-callback for target 0x%p threw: %s",
                        (void*)target_func_ptr, err.what());
            continue;
        }
        // First non-zero integer return wins (matches RoM semantics —
        // first plugin to specify a restore address gets it, rest are
        // advisory).
        if (!restore_address && r.get_type(0) == sol::type::number) {
            restore_address = (uintptr_t)r.get<int64_t>(0);
        }
    }
    return restore_address;
}

}  // namespace kcdx::scripting
