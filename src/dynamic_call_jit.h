#pragma once

// kcdx::dynamic_call_jit — JIT a lua_CFunction that calls a native
// target with a declared signature.
//
// Extracted from lua_bind_dynamic_call.cpp (Phase 2b sub-4) so it can be
// reused by hook_chain's call_original bridge as well as the
// kcdx.memory.dynamic_call surface. The generated stub IS a
// lua_CFunction (int(lua_State*)): it pulls the declared args off the
// Lua stack (indices 1..N), calls the native target with them in the
// host (MS x64) calling convention, and pushes the typed return value.
//
// The target VA is BAKED into the emitted code as an immediate. For
// call_original this is fine: pass MinHook's trampoline-to-original VA
// (read from detour_hook::get_original_ptr() AFTER InstallRuntime has
// populated it). MinHook never relocates a trampoline post-create, so a
// baked VA is stable for the session (kcdx never unhooks — hooks live
// for the session, per .claude/rules/hook-engine.md).
//
// Type strings use the same vocabulary as kcdx::rom::get_type_id /
// get_type_info_from_string ("void", "i32"/"i64"/integer-ish, "float",
// "double", "ptr", "bool", "const char*"/"string"). At the ABI level
// strings and pointers are both pointer-width; the string<->Lua
// conversion only matters for the Lua stack marshaling this stub does.

#include <cstdint>
#include <string>
#include <vector>

extern "C" {
#include "lua.h"
}

namespace kcdx::dynamic_call_jit {

// Build a lua_CFunction stub that calls `targetVa` with the given
// signature. Returns the stub (callable as a lua_CFunction / pushable
// via lua_pushcfunction) or nullptr on failure (logged). The stub's
// code lives in the branch pool (alloc-only; never freed).
lua_CFunction BuildLuaCallThunk(uintptr_t                        targetVa,
                                const std::string&               returnType,
                                const std::vector<std::string>&  paramTypes);

}  // namespace kcdx::dynamic_call_jit
