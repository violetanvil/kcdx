// scripting — kcdx's substitute for ReturnOfModding's big::g_lua_manager.
//
// RoM's lua_manager owns per-Lua-script-module data: each module carries
// vectors of per-target callbacks. kcdx is different (we load C++ DLL
// plugins, not Lua script bundles), so this module is flatter: a single
// global state holding the live lua_State, a non-owning
// target_func_ptr -> runtime_func_t* map, and per-target vectors of
// Lua-registry refs resolved from "module.function" strings declared
// in [[hook]] / [[mid_hook]] kcdx.toml entries. Callbacks are stored as
// `int` registry refs (luaL_ref) and invoked via raw lua_pcall.
//
// Threading: protected by a single recursive_mutex. The pre/post/mid
// callbacks are invoked from inside JIT trampolines on whichever thread
// the target function ran on. Whether KCD2's Lua VM is safe to enter
// from non-main threads is a Phase 5d question (verify before exposing
// to plugins). Until that's confirmed, callbacks dispatched off the main
// thread will be logged-and-skipped rather than executed.
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

extern "C" {
#include "lua.h"
}

#include "rom_borrowed/runtime_func_t.h"

namespace kcdx::scripting {

// === DIAGNOSTIC HELPER (heap-corruption investigation, 2026-05-20) ===
//
// Dump a labeled snapshot of the Lua state's observable properties to
// the dev log under category `MID_HOOK`. Captures:
//   - the L pointer
//   - lua_gettop(L) (Lua stack height)
//   - lua_topointer(L, LUA_REGISTRYINDEX) (registry table identity —
//     two L's with the same registry ptr share a VM)
//   - lua_topointer(L, LUA_GLOBALSINDEX) (globals identity)
//   - GetCurrentThreadId() (OS thread)
//   - tag: a string label naming the call site
//
// Pure read; safe to call anywhere we have a valid L. Used to compare
// snapshots across kcdx call sites to find the moment something diverges.
// Kept until the heap-corruption issue is resolved; see
// docs/known-issues/kcdx lua_newtable corrupts the process heap.md.
void LogLuaStateSnapshot(lua_State* L, const char* tag);

// PROBE N: raw C-struct field dump of lua_State and global_State.
// Reads L->status / L->top / L->l_G / L->ci / L->savedpc / L->errorJmp /
// L->nCcalls / etc. and the global state's frealloc/ud/gcstate/rootgc/
// gray/totalbytes/mainthread. Used to find the discriminator between
// RegisterKcdxTable's (safe) lua_newtable and PROBE M's (crashing)
// lua_newtable when both fire on the same L at the same depth in the
// same update tick. The Lua C API can't see whatever distinguishes
// them — this probe reads the underlying struct directly. Pure read.
//
// See docs/known-issues/kcdx lua_newtable corrupts the process heap.md
// for the investigation trail.
void LogLuaStateRawStruct(lua_State* L, const char* tag);

// Called once from hooks.cpp's first-update-tick when the game's
// lua_State first becomes live. Safe to call multiple times; the last
// pointer wins (KCD2 may recreate the VM across save/load, in which
// case we re-bind).
void set_lua_state(lua_State* L);

// Returns the currently-bound lua_State, or nullptr if none.
// runtime_func_t callbacks read this to know whether the VM is up.
lua_State* lua_state();

// Non-owning registration. The caller (lua_memory.cpp / future [[hook]]
// installer) owns the runtime_func_t lifetime; this map just lets
// dispatchers find the right pre/post/mid descriptors by target_func_ptr.
void register_hook(uintptr_t target_func_ptr, kcdx::rom::runtime_func_t* hook);
void unregister_hook(uintptr_t target_func_ptr);
kcdx::rom::runtime_func_t* get_existing_dynamic_hook(uintptr_t target_func_ptr);

// Per-target Lua callback registration. The caller pushes the callback
// function onto the Lua stack first, then calls register_*_callback_from_top
// which luaL_ref()s it from the top of the stack (consuming it) and
// stores the registry reference. Multiple plugins may register against
// the same target; all fire in registration order.
//
// The Lua state used for the ref is the one currently bound via
// set_lua_state; it's a programming error to call these before that.
void register_pre_callback_from_top (uintptr_t target_func_ptr);
void register_post_callback_from_top(uintptr_t target_func_ptr);
void register_mid_callback_from_top (uintptr_t target_func_ptr);

// Phase 5f: register a callback by dotted name (e.g., "Foo.Bar"). The
// name is stored verbatim and resolved lazily at first dispatch by
// walking _G[Foo][Bar]. If the name fails to resolve when the hook
// fires, the dispatcher logs a warn and lets the original run.
//
// Multiple registrations against the same target stack; all fire on
// each hook invocation. Order matches registration order.
void register_pre_callback_by_name (uintptr_t target_func_ptr, const std::string& name);
void register_post_callback_by_name(uintptr_t target_func_ptr, const std::string& name);
void register_mid_callback_by_name (uintptr_t target_func_ptr, const std::string& name);

// Clear all callbacks for a target (used when a [[hook]] is being
// uninstalled). luaL_unref's each stored ref so Lua can GC the
// functions. Does not unregister the runtime_func_t itself.
void clear_callbacks(uintptr_t target_func_ptr);

// C trampolines installed as runtime_func_t pre/post/mid_callback_t.
// The JIT'd marshaling code calls into these from inside the hooked
// function's call site. They look up the registered Lua callbacks
// for target_func_ptr and dispatch via lua_pcall.
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
