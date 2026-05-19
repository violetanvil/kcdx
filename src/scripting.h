// scripting — kcdx's substitute for ReturnOfModding's big::g_lua_manager.
//
// RoM's lua_manager owns per-Lua-script-module data: each module carries
// vectors of sol::functions keyed by target_func_ptr. kcdx is different
// (we load C++ DLL plugins, not Lua script bundles), so this module is
// flatter: a single global state holding the live lua_State, a non-owning
// target_func_ptr -> runtime_func_t* map, and per-target vectors of
// sol::functions resolved from "module.function" strings declared in
// [[hook]] / [[mid_hook]] kcdx.toml entries.
//
// Phase 5c.6 lands the storage + dispatch surface. The string-to-Lua-fn
// resolution (sol::state_view::globals lookup) happens here; the parsing
// of "module.function" out of TOML happens in hook_engine (Phase 5f).
//
// Threading: protected by a single recursive_mutex. The pre/post/mid
// callbacks are invoked from inside JIT trampolines on whichever thread
// the target function ran on. Whether KCD2's Lua VM is safe to enter
// from non-main threads is a Phase 5d question (verify before exposing
// to plugins). Until that's confirmed, callbacks dispatched off the main
// thread will be logged-and-skipped rather than executed.
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <sol/sol.hpp>

#include "rom_borrowed/runtime_func_t.h"

namespace kcdx::scripting {

// Called once from hooks.cpp's first-update-tick when the game's
// lua_State first becomes live. Safe to call multiple times; the last
// pointer wins (KCD2 may recreate the VM across save/load, in which
// case we re-bind).
void set_lua_state(lua_State* L);

// Returns the currently-bound lua_State, or nullptr if none.
// runtime_func_t callbacks read this to know whether the VM is up.
lua_State* lua_state();

// Non-owning registration. The caller (memory.cpp / future [[hook]]
// installer) owns the runtime_func_t lifetime; this map just lets
// dispatchers find the right pre/post/mid descriptors by target_func_ptr.
void register_hook(uintptr_t target_func_ptr, kcdx::rom::runtime_func_t* hook);
void unregister_hook(uintptr_t target_func_ptr);
kcdx::rom::runtime_func_t* get_existing_dynamic_hook(uintptr_t target_func_ptr);

// Per-target Lua callback registration. A [[hook]] with
// lua_callback = "MyMod.OnOutfitSwap" calls register_pre_callback with
// the resolved sol::function. Multiple plugins may register against the
// same target; all fire in registration order.
void register_pre_callback (uintptr_t target_func_ptr, sol::protected_function fn);
void register_post_callback(uintptr_t target_func_ptr, sol::protected_function fn);
void register_mid_callback (uintptr_t target_func_ptr, sol::protected_function fn);

// Clear all callbacks for a target (used when a [[hook]] is being
// uninstalled, e.g., during plugin unload). Does not unregister the
// runtime_func_t itself.
void clear_callbacks(uintptr_t target_func_ptr);

// C trampolines installed as runtime_func_t pre/post/mid_callback_t.
// The JIT'd marshaling code calls into these from inside the hooked
// function's call site. They look up the registered Lua callbacks
// for target_func_ptr and dispatch via sol2.
//
// Return semantics match runtime_func_t's typedefs:
//   pre  -> bool: false means "skip the original function"
//   post -> void
//   mid  -> uintptr_t: non-zero means "resume execution at this address
//                       instead of the next instruction" (used by
//                       [[mid_hook]] to short-circuit a control flow)
bool      dynamic_hook_pre (const kcdx::rom::runtime_func_t::parameters_t* params,
                            uint8_t                                        param_count,
                            kcdx::rom::runtime_func_t::return_value_t*     return_value,
                            uintptr_t                                      target_func_ptr);

void      dynamic_hook_post(const kcdx::rom::runtime_func_t::parameters_t* params,
                            uint8_t                                        param_count,
                            kcdx::rom::runtime_func_t::return_value_t*     return_value,
                            uintptr_t                                      target_func_ptr);

uintptr_t dynamic_hook_mid (const kcdx::rom::runtime_func_t::parameters_t* params,
                            size_t                                         param_count,
                            uintptr_t                                      target_func_ptr);

}  // namespace kcdx::scripting
