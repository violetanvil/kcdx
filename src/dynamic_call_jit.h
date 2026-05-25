#pragma once

// kcdx::dynamic_call_jit — JIT call-thunks over a native target with a
// declared signature. Two builders, two ABI shapes:
//
//   BuildLuaCallThunk    — Lua-stack-coupled. Emits a lua_CFunction
//                          (int(lua_State*)) that pulls typed args from
//                          the Lua stack at indices 1..N, calls the
//                          target in the host x64 calling convention,
//                          then pushes the typed return back onto the
//                          Lua stack. Body lives in
//                          lua_bind_dynamic_call.cpp (delegates to the
//                          per-slot lua_to* / lua_push* JitTrampoline).
//
//   BuildNativeCallThunk — Pure native pass-through. Emits a function
//                          whose own ABI IS the target's typed
//                          signature: the C caller invokes it as a
//                          normal native function and asmjit's
//                          Compiler-managed prologue lands the typed
//                          args into vregs per host x64, then `invoke`
//                          forwards them to the target. The return
//                          value rides back in the host ABI's return
//                          register (RAX for integer/ptr/bool, XMM0 for
//                          float/double). No Lua stack involvement at
//                          any point. Body lives in dynamic_call_jit.cpp.
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
// strings and pointers are both pointer-width and ride in integer
// registers; the string<->Lua conversion only matters for the
// BuildLuaCallThunk marshaling path.

#include <cstdint>
#include <string>
#include <vector>

extern "C" {
#include "lua.h"
}

#include "hook_payload.h"   // Mode (per-mode codegen branch)
#include "hook_signature.h" // Signature (typed slot marshaling)

namespace kcdx::dynamic_call_jit {

// Build a lua_CFunction stub that calls `targetVa` with the given
// signature. Returns the stub (callable as a lua_CFunction / pushable
// via lua_pushcfunction) or nullptr on failure (logged). The stub's
// code lives in the branch pool (alloc-only; never freed).
lua_CFunction BuildLuaCallThunk(uintptr_t                        targetVa,
                                const std::string&               returnType,
                                const std::vector<std::string>&  paramTypes);

// Build a NATIVE pass-through call-thunk over `targetVa`. The returned
// pointer's calling convention IS the typed signature described by
// `returnType` + `paramTypes` — host x64 end-to-end, NO Lua stack:
//
//   void* thunk = kcdx::dynamic_call_jit::BuildNativeCallThunk(
//                     pOriginalVa, "i32", {"ptr"});
//   auto orig = reinterpret_cast<int(*)(void*)>(thunk);
//   int r = orig(self);   // host x64 call; self in RCX, result in RAX
//
// The C caller casts the returned pointer to its typed signature and
// invokes it normally. asmjit's Compiler places each declared arg into
// the right register-class per host x64 (RCX/RDX/R8/R9 for the first 4
// integer/ptr/bool args; XMM0-3 for the first 4 float/double args;
// remaining args on the stack with shadow space + 16-byte alignment);
// the return value rides back in RAX (integer/ptr/bool) or XMM0
// (float/double). Slot index — NOT a per-class counter — selects the
// register: an int at arg-0 + float at arg-1 puts int in RCX and float
// in XMM1, not XMM0.
//
// Returns the raw thunk pointer (cast to its typed signature at the
// call site) or nullptr on failure (logged). The stub's code lives in
// the branch pool (alloc-only; never freed). The owner-handle for the
// allocation is kcdxInvalidPluginHandle — engine-owned, since this
// thunk is built on behalf of a hook's chain, not for any single
// plugin's own use.
//
// Use case: kcdx.hook chain's C around `call_original` primitive — the
// C author already holds the typed args and wants to invoke the
// original with them, getting the typed return back. The lua_CFunction
// shape is wrong here (Lua-stack-coupled, lossy per .claude/rules/lua-
// precision.md for pointer-magnitude values).
void* BuildNativeCallThunk(uintptr_t                        targetVa,
                           const std::string&               returnType,
                           const std::vector<std::string>&  paramTypes);

// Build an engine→C-callback dispatch trampoline. Emitted at AddC time
// with full knowledge of cFn + cSig + mode + (for Around)
// callOriginalCThunk. The trampoline's own ABI varies by mode (see
// hook_chain.cpp DispatchPre/Post/MidDispatch call sites for the exact
// shapes). Author-facing cFn ABI is per the locked decisions (Phase 3
// sub-1 step 5-main chunks 3+4):
//
//   Mode::Before  — `void cFn(uintptr_t args[], int* outCount,
//                              /* typed args from cSig */)`
//                   Engine-callable shape:
//                       `void thunk(parameters_t* params)`
//                   Thunk unpacks the params into typed cFn args + a
//                   stack-allocated args[N] mutation channel; on
//                   return writes args[0..outCount-1] back to params.
//
//   Mode::After   — non-void: `<typed_return> cFn(<typed_return>
//                                                  origReturn,
//                                                  /* typed args */)`
//                   void:    `void cFn(/* typed args */)`
//                   Engine-callable shape:
//                       `void thunk(parameters_t* params,
//                                   return_value_t* rv)`
//                   Non-void path writes cFn's typed return into rv;
//                   void path ignores rv.
//
//   Mode::Around  — `<typed_return> cFn(<typed_return>(*call_original)
//                                          (/* typed args */),
//                                       /* typed args */)`
//                   Engine-callable shape:
//                       `void thunk(parameters_t* params,
//                                   return_value_t* rv,
//                                   void* callOriginalCThunk)`
//                   Thunk passes callOriginalCThunk as the first typed
//                   cFn arg (pointer-width in RCX); author's C source
//                   casts it to the typed function pointer.
//
//   Mode::Replace — `<typed_return> cFn(/* typed args */)`
//                   Engine-callable shape:
//                       `void thunk(parameters_t* params,
//                                   return_value_t* rv)`
//                   Original never runs; cFn's typed return is written
//                   to rv.
//
//   Mode::Mid     — `void cFn(kcdxHookCaptureValue* values, int count)`
//                   Engine-callable shape:
//                       `void thunk(void* payload_base, int count,
//                                   const char* const* capNames,
//                                   const char* const* capTypes)`
//                   Thunk stack-allocates a kcdxHookCaptureValue[count]
//                   from the live capture metadata + slot payload (16-
//                   byte stride per the JIT), invokes cFn, reads the
//                   typed value field back per capTypes and writes the
//                   slot bytes back.
//
// Returns the emitted trampoline pointer (cast to its call-site ABI at
// the dispatch site) or nullptr on failure (logged). Code lives in the
// branch pool (alloc-only; engine-owned via kcdxInvalidPluginHandle).
void* BuildCDispatchThunk(void*                                  cFn,
                          const kcdx::hook_signature::Signature& cSig,
                          kcdx::hook_payload::Mode               mode);

}  // namespace kcdx::dynamic_call_jit
