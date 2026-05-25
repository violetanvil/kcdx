// dynamic_call_jit.cpp — BuildNativeCallThunk body.
//
// Pure native pass-through call-thunk. The emitted function's own ABI
// IS the target's typed signature (host x64), and it forwards every
// arg + the return value 1:1 to the target. No Lua stack involvement
// at any point — the C caller invokes the returned pointer as a normal
// native function after casting it to the typed signature.
//
// asmjit pattern (mirrors src/rom_borrowed/runtime_func_t.cpp:122-262
// minus the pre/post callback ceremony):
//
//   1. Build asmjit::FuncSignature from the type strings via
//      kcdx::rom::get_type_id (same vocabulary as BuildLuaCallThunk).
//   2. cc.add_func(sig) — Compiler emits the prologue. Per-arg loop
//      func->set_arg(i, vreg) lands each incoming arg into a vreg of
//      the right register-class (GP for int/ptr/bool, XMM for f32/f64)
//      per host x64 ABI. Slot index selects the register, NOT a per-
//      class counter — an int at arg-0 + float at arg-1 puts the int
//      in RCX and the float in XMM1.
//   3. cc.invoke(node, targetVa, sig) — Compiler places each vreg into
//      the matching call-register and shadow-space slot. set_arg in a
//      loop wires every slot.
//   4. set_ret(0, ret_reg) + cc.ret(ret_reg) — return value rides RAX
//      (integer/ptr/bool) or XMM0 (float/double), picked by the same
//      is_general_register / is_XMM_register dispatch the existing
//      JitTrampoline uses. void return → cc.ret() with no arg.
//   5. Code allocated in kcdx::trampoline::AllocateBranch — owner is
//      kcdxInvalidPluginHandle (engine-owned thunk, no per-plugin
//      attribution; this is built on behalf of a hook's chain).
//
// This is fundamentally simpler than the BuildLuaCallThunk path in
// lua_bind_dynamic_call.cpp because there is no per-slot lua_to* /
// lua_push* marshaling — the host x64 ABI on both sides means asmjit
// does ALL the register placement automatically. The two builders live
// in different TUs because their bodies share no codegen.

#include "dynamic_call_jit.h"

#include <cstdint>
#include <string>
#include <vector>

#include <asmjit/asmjit.h>

#include "kcdx/Interfaces.h"  // kcdxInvalidPluginHandle
#include "log.h"
#include "rom_borrowed/asmjit_helper.h"  // is_general_register / is_XMM_register / get_type_id
#include "trampoline.h"

namespace kcdx::dynamic_call_jit {

namespace {

// asmjit error handler matching the existing JitTrampoline style. Logs
// any asmjit-emitted error to kcdx.log so JIT problems surface.
class NativeJitErrorHandler : public asmjit::ErrorHandler {
public:
    void handle_error(asmjit::Error /*err*/, const char* message,
                      asmjit::BaseEmitter* /*origin*/) override {
        log::ErrorF("BuildNativeCallThunk asmjit error: %s", message);
    }
};

}  // namespace

void* BuildNativeCallThunk(uintptr_t                       targetVa,
                           const std::string&              returnType,
                           const std::vector<std::string>& paramTypes) {
    if (!targetVa) return nullptr;

    // Build the asmjit signature from the type-string vocabulary. Same
    // shape BuildLuaCallThunk uses (lua_bind_dynamic_call.cpp:492-499).
    asmjit::FuncSignature sig(
        asmjit::CallConvId::kCDecl,
        asmjit::FuncSignature::kNoVarArgs,
        kcdx::rom::get_type_id(returnType));

    for (const auto& s : paramTypes) {
        sig.add_arg(kcdx::rom::get_type_id(s));
    }

    asmjit::CodeHolder code;
    auto env = asmjit::Environment::host();
    env.set_arch(asmjit::Arch::kX64);
    code.init(env);

    NativeJitErrorHandler eh;
    code.set_error_handler(&eh);

    asmjit::x86::Compiler cc(&code);

    asmjit::StringLogger asm_log;
    const auto format_flags =
        asmjit::FormatFlags::kMachineCode | asmjit::FormatFlags::kExplainImms |
        asmjit::FormatFlags::kRegCasts    | asmjit::FormatFlags::kHexImms     |
        asmjit::FormatFlags::kHexOffsets  | asmjit::FormatFlags::kPositions;
    asm_log.add_flags(format_flags);
    code.set_logger(&asm_log);

    // The JIT'd function ADOPTS the target's signature — same calling
    // convention, same arg list, same return. asmjit emits the host x64
    // prologue from `sig` and we land each incoming arg into a vreg of
    // the matching class. Shadow space + 16-byte stack alignment +
    // beyond-4 stack args are all handled by the Compiler.
    asmjit::FuncNode* func = cc.add_func(sig);

    // Per-arg loop: assign a vreg of the right register-class per slot.
    // Slot index (NOT a per-class counter) selects the register —
    // asmjit's prologue places each `set_arg(i, vreg)` into RCX/RDX/R8/
    // R9 (integer/ptr) or XMM0/1/2/3 (float/double) at slot i, with
    // beyond-4 slots arriving on the stack.
    std::vector<asmjit::Reg> arg_registers;
    arg_registers.reserve(sig.arg_count());
    for (uint8_t i = 0; i < sig.arg_count(); ++i) {
        const auto arg_type = sig.args()[i];
        asmjit::Reg arg;
        if (kcdx::rom::is_general_register(arg_type)) {
            // GP register: integer (i8/i16/i32/i64), pointer, bool
            // (zero-extended in low byte), string/cstr/wstr (passed as
            // pointer-width in the integer register).
            arg = cc.new_gp_ptr();
        } else if (kcdx::rom::is_XMM_register(arg_type)) {
            // XMM register: float (single) or double.
            arg = cc.new_xmm();
        } else {
            log::ErrorF("BuildNativeCallThunk: unsupported parameter type at "
                        "index=%u (asmjit TypeId=%d) — check the type string "
                        "against asmjit_helper.cpp::get_type_id (try 'i32', "
                        "'i64', 'ptr', 'f32', 'f64', 'bool', 'string')",
                        (unsigned)i, (int)arg_type);
            return nullptr;
        }
        func->set_arg(i, arg);
        arg_registers.push_back(arg);
    }

    // Invoke the target with the forwarded args. asmjit's Compiler
    // places each vreg back into the target's call-register per the
    // same sig — RCX/RDX/R8/R9 (integer/ptr) or XMM0/1/2/3 (float/
    // double) at slot i; remaining args land on the stack. Shadow space
    // is reserved automatically.
    asmjit::InvokeNode* target_invoke;
    cc.invoke(asmjit::Out(target_invoke), targetVa, sig);
    for (uint8_t i = 0; i < sig.arg_count(); ++i) {
        target_invoke->set_arg(i, arg_registers[i]);
    }

    // Return forwarding. RAX (integer/ptr/bool) or XMM0 (float/double)
    // per host x64; cc.ret(reg) emits the right epilogue. Void return
    // (no has_ret) → cc.ret() with no arg, no return register used.
    if (sig.has_ret()) {
        asmjit::Reg ret_reg;
        if (kcdx::rom::is_general_register(sig.ret())) {
            ret_reg = cc.new_gp_ptr();
        } else if (kcdx::rom::is_XMM_register(sig.ret())) {
            ret_reg = cc.new_xmm();
        } else {
            log::ErrorF("BuildNativeCallThunk: unsupported return type "
                        "(asmjit TypeId=%d) — wider-than-64-bit returns not "
                        "supported", (int)sig.ret());
            return nullptr;
        }
        target_invoke->set_ret(0, ret_reg);
        cc.ret(ret_reg);
    } else {
        cc.ret();
    }

    cc.end_func();
    cc.finalize();

    code.flatten();
    const size_t size = code.code_size();

    // Allocate from the branch pool — same +/-2 GB anchored pool the
    // existing JitTrampoline / runtime_func_t use. nearVa = targetVa so
    // the rel32 call from the emitted body reaches the target even when
    // the target lives in a far module (>2 GB from WHGame.dll). Owner =
    // kcdxInvalidPluginHandle: engine-owned, no per-plugin attribution
    // (this thunk is built on behalf of a hook's chain).
    void* jit_buffer = kcdx::trampoline::AllocateBranch(
        kcdxInvalidPluginHandle, size, targetVa);
    if (!jit_buffer) {
        log::Error("BuildNativeCallThunk: branch_pool allocation failed");
        return nullptr;
    }

    if (code.has_unresolved_fixups()) {
        code.resolve_cross_section_fixups();
    }
    code.relocate_to_base((uintptr_t)jit_buffer);
    code.copy_flattened_data(jit_buffer, size);

    log::DebugF("BuildNativeCallThunk: native call-thunk at 0x%p (%zu bytes) "
                "over target 0x%p, asm:\n%s",
                jit_buffer, size, (void*)targetVa, asm_log.data());

    return jit_buffer;
}

}  // namespace kcdx::dynamic_call_jit
