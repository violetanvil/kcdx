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
#include <cstring>
#include <string>
#include <vector>

#include <asmjit/asmjit.h>

#include "kcdx/Interfaces.h"  // kcdxInvalidPluginHandle / kcdxHookCaptureValue
#include "log.h"
#include "rom_borrowed/asmjit_helper.h"  // is_general_register / is_XMM_register / get_type_id
#include "trampoline.h"

namespace kcdx::dynamic_call_jit {

// Forward declaration — defined at namespace-extern scope below; the
// Mid-mode asmjit thunk takes its address as a baked invoke target.
void MidShimEntry(void*              payload_base,
                  int                count,
                  const char* const* capNames,
                  const char* const* capTypes,
                  void*              cFn);

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

// Translate a hook_signature::Type to the asmjit type-string vocabulary
// (the same strings get_type_id consumes). wstr/cstr → "ptr" (pointer-
// width at the ABI; string<->Lua marshaling does not apply to the C
// path — the C author works in native pointers).
const char* SigTypeToAbiString(kcdx::hook_signature::Type t) {
    using T = kcdx::hook_signature::Type;
    switch (t) {
        case T::Void: return "void";
        case T::Bool: return "bool";
        case T::F32:  return "float";
        case T::F64:  return "double";
        case T::Ptr:  return "ptr";
        case T::Wstr: return "ptr";
        case T::Cstr: return "ptr";
        case T::I8:   return "i8";
        case T::I16:  return "i16";
        case T::I32:  return "i32";
        case T::I64:  return "i64";
        case T::U8:   return "u8";
        case T::U16:  return "u16";
        case T::U32:  return "u32";
        case T::U64:  return "u64";
        default:      return "i64";
    }
}

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

// ===========================================================================
// BuildCDispatchThunk — engine→C-callback marshaling trampoline (per-mode).
// ===========================================================================
//
// Phase 3 sub-1 step 5-main chunks 3+4. Emitted at AddC time with full
// knowledge of cFn + cSig + mode. The trampoline's own ABI varies by
// mode (see dynamic_call_jit.h doc-comment for the per-mode shapes);
// internally each emits an asmjit Compiler function whose body:
//
//   1. unpacks the 8-byte parameters_t slots into vregs of the right
//      register-class per cSig (GP for int/ptr/bool; XMM for f32/f64),
//   2. invokes cFn with the per-mode arg layout (typed return + typed
//      args for After/Around/Replace; args[]/outCount + typed args for
//      Before; (values,count) for Mid),
//   3. writes any cFn return / mutation back to parameters_t / rv /
//      the slot bytes.
//
// Slot stride: parameters_t::get_arg_ptr uses 8-byte stride (sizeof
// uintptr_t per slot — runtime_func_t.cpp:36). Mid uses a DIFFERENT
// stride: the JIT writes the capture payload at 16-byte stride per
// slot (make_jit_midfunc convention; see hook_chain.cpp's
// PushCaptureHandle slot pointer arithmetic). The Mid path indexes
// into payload_base by 16*i; the non-Mid paths use 8*i.

namespace {

// Vreg holding a typed cFn argument unpacked from a parameters_t slot.
// For GP types we issue a typed load (movzx/movsx for narrow widths;
// mov for 64-bit) sized to the slot's effective width but the vreg
// itself is GP-pointer-width; asmjit's Compiler picks the right
// sub-register encoding into the calling register. For XMM types we
// load via movss / movsd into a new_xmm.
struct UnpackedArg {
    asmjit::Reg reg;
    bool        is_xmm = false;
};

// Allocate + load one typed arg vreg from a parameters_t slot at
// (baseGp + offset). Each slot is 8 bytes (parameters_t stride).
UnpackedArg LoadSlotArg(asmjit::x86::Compiler&     cc,
                        asmjit::x86::Gp            baseGp,
                        int32_t                    offset,
                        kcdx::hook_signature::Type t) {
    using T = kcdx::hook_signature::Type;
    UnpackedArg out;
    if (t == T::F32) {
        auto x = cc.new_xmm();
        cc.movss(x, asmjit::x86::dword_ptr(baseGp, offset));
        out.reg = x;
        out.is_xmm = true;
        return out;
    }
    if (t == T::F64) {
        auto x = cc.new_xmm();
        cc.movsd(x, asmjit::x86::qword_ptr(baseGp, offset));
        out.reg = x;
        out.is_xmm = true;
        return out;
    }
    // GP family — issue a width-correct load into a pointer-width vreg.
    auto g = cc.new_gp_ptr();
    switch (t) {
        case T::I8:   cc.movsx(g, asmjit::x86::byte_ptr (baseGp, offset)); break;
        case T::U8:   cc.movzx(g, asmjit::x86::byte_ptr (baseGp, offset)); break;
        case T::Bool: cc.movzx(g, asmjit::x86::byte_ptr (baseGp, offset)); break;
        case T::I16:  cc.movsx(g, asmjit::x86::word_ptr (baseGp, offset)); break;
        case T::U16:  cc.movzx(g, asmjit::x86::word_ptr (baseGp, offset)); break;
        case T::I32:  cc.movsxd(g, asmjit::x86::dword_ptr(baseGp, offset)); break;
        case T::U32:  cc.mov   (g.r32(), asmjit::x86::dword_ptr(baseGp, offset)); break;
        // I64 / U64 / Ptr / Wstr / Cstr / Void(fallthrough) all read full 8 bytes.
        default:      cc.mov(g, asmjit::x86::qword_ptr(baseGp, offset)); break;
    }
    out.reg = g;
    return out;
}

// Write a vreg back into a parameters_t slot at (baseGp + offset). Used
// by Before's args[] mutation-back-channel: after cFn returns, the
// engine reads args[i] (8-byte slot) and stores it back at slot i.
// args[] is a uintptr_t[] (8-byte stride); parameters_t is also 8-byte
// stride — so a straight qword copy works.
void StoreSlotArg(asmjit::x86::Compiler& cc,
                  asmjit::x86::Gp        slotBaseGp, int32_t slotOffset,
                  asmjit::x86::Gp        argsBaseGp, int32_t argsOffset) {
    auto tmp = cc.new_gp_ptr();
    cc.mov(tmp, asmjit::x86::qword_ptr(argsBaseGp, argsOffset));
    cc.mov(asmjit::x86::qword_ptr(slotBaseGp, slotOffset), tmp);
}

// Allocate the asmjit code buffer, finalize, copy to branch_pool. Same
// shape BuildNativeCallThunk uses. nearVa==0 means "anywhere"; the
// codegen here is engine-internal (cFn is in the plugin DLL, near
// WHGame.dll via LoadLibrary), and the rel32 calls inside the thunk go
// through asmjit's invoke pattern which handles far targets via a mov-
// imm64+call when needed. Owner = kcdxInvalidPluginHandle.
void* FinalizeAndAlloc(asmjit::CodeHolder& code, asmjit::StringLogger& asm_log,
                       const char* what) {
    code.flatten();
    const size_t size = code.code_size();
    void* jit_buffer = kcdx::trampoline::AllocateBranch(
        kcdxInvalidPluginHandle, size, 0);
    if (!jit_buffer) {
        log::ErrorF("%s: branch_pool allocation failed", what);
        return nullptr;
    }
    if (code.has_unresolved_fixups()) {
        code.resolve_cross_section_fixups();
    }
    code.relocate_to_base((uintptr_t)jit_buffer);
    code.copy_flattened_data(jit_buffer, size);
    log::DebugF("%s: dispatch thunk at 0x%p (%zu bytes), asm:\n%s",
                what, jit_buffer, size, asm_log.data());
    return jit_buffer;
}

// Build the engine-callable thunk signature for a given mode. Engine-
// side shapes (per dynamic_call_jit.h doc-comment):
//   Before:  void thunk(parameters_t* params)
//   After:   void thunk(parameters_t* params, return_value_t* rv)
//   Replace: void thunk(parameters_t* params, return_value_t* rv)
//   Around:  void thunk(parameters_t* params, return_value_t* rv,
//                       void* callOriginalCThunk)
//   Mid:     void thunk(void* payload_base, int count,
//                       const char* const* capNames,
//                       const char* const* capTypes)
asmjit::FuncSignature OuterSigFor(kcdx::hook_payload::Mode mode) {
    using Mode = kcdx::hook_payload::Mode;
    asmjit::FuncSignature sig(asmjit::CallConvId::kCDecl,
                              asmjit::FuncSignature::kNoVarArgs,
                              asmjit::TypeId::kVoid);
    switch (mode) {
        case Mode::Mid:
            sig.add_arg(asmjit::TypeId::kUIntPtr);   // payload_base
            sig.add_arg(asmjit::TypeId::kInt32);     // count
            sig.add_arg(asmjit::TypeId::kUIntPtr);   // capNames
            sig.add_arg(asmjit::TypeId::kUIntPtr);   // capTypes
            break;
        case Mode::Around:
            sig.add_arg(asmjit::TypeId::kUIntPtr);   // params
            sig.add_arg(asmjit::TypeId::kUIntPtr);   // rv
            sig.add_arg(asmjit::TypeId::kUIntPtr);   // callOriginalCThunk
            break;
        case Mode::After:
        case Mode::Replace:
            sig.add_arg(asmjit::TypeId::kUIntPtr);   // params
            sig.add_arg(asmjit::TypeId::kUIntPtr);   // rv
            break;
        case Mode::Before:
        default:
            sig.add_arg(asmjit::TypeId::kUIntPtr);   // params
            break;
    }
    return sig;
}

// Build the cFn-callable signature from cSig for a given mode. This is
// the C author's typed signature, threaded as the prepended args per
// the per-mode decisions:
//   Before:  void cFn(uintptr_t args[], int* outCount, ...typed args)
//   After void:    void cFn(...typed args)
//   After non-void:<typed_return> cFn(<typed_return> origReturn, ...typed args)
//   Around:        <typed_return> cFn(<typed call_original>, ...typed args)
//                  call_original arrives as pointer-width in RCX; the
//                  asmjit signature carries it as TypeId::kUIntPtr — the
//                  C source-level typedef declares the typed function
//                  pointer (D-c-fn-abi-2 Option B).
//   Replace:       <typed_return> cFn(...typed args)
asmjit::FuncSignature CFnSigFor(const kcdx::hook_signature::Signature& cSig,
                                kcdx::hook_payload::Mode               mode) {
    using Mode = kcdx::hook_payload::Mode;
    asmjit::TypeId returnTypeId = asmjit::TypeId::kVoid;
    if (mode == Mode::After || mode == Mode::Around || mode == Mode::Replace) {
        returnTypeId = kcdx::rom::get_type_id(
            std::string(SigTypeToAbiString(cSig.returnType)));
    }
    asmjit::FuncSignature sig(asmjit::CallConvId::kCDecl,
                              asmjit::FuncSignature::kNoVarArgs,
                              returnTypeId);
    if (mode == Mode::Before) {
        sig.add_arg(asmjit::TypeId::kUIntPtr);  // args[]
        sig.add_arg(asmjit::TypeId::kUIntPtr);  // outCount*
    } else if (mode == Mode::Around) {
        sig.add_arg(asmjit::TypeId::kUIntPtr);  // call_original (typed fnptr)
    } else if (mode == Mode::After &&
               cSig.returnType != kcdx::hook_signature::Type::Void) {
        sig.add_arg(kcdx::rom::get_type_id(
            std::string(SigTypeToAbiString(cSig.returnType))));  // origReturn
    }
    for (const auto& a : cSig.args) {
        sig.add_arg(kcdx::rom::get_type_id(
            std::string(SigTypeToAbiString(a.type))));
    }
    return sig;
}

// Write the cFn return register into rv per the cSig return type. rv
// (kcdx::rom::runtime_func_t::return_value_t) wraps a single uintptr_t
// at offset 0 (return_value_t::get() returns &m_return_value). We
// store the right-width slice at rv+0.
void StoreReturn(asmjit::x86::Compiler& cc,
                 asmjit::x86::Gp        rvGp,
                 asmjit::Reg            retReg,
                 bool                   ret_is_xmm,
                 kcdx::hook_signature::Type t) {
    using T = kcdx::hook_signature::Type;
    if (t == T::Void) return;
    if (ret_is_xmm) {
        if (t == T::F32) {
            cc.movss(asmjit::x86::dword_ptr(rvGp, 0), retReg.as<asmjit::x86::Vec>());
        } else {  // F64
            cc.movsd(asmjit::x86::qword_ptr(rvGp, 0), retReg.as<asmjit::x86::Vec>());
        }
        return;
    }
    auto g = retReg.as<asmjit::x86::Gp>();
    switch (t) {
        case T::I8:
        case T::U8:
        case T::Bool: cc.mov(asmjit::x86::byte_ptr (rvGp, 0), g.r8());  break;
        case T::I16:
        case T::U16:  cc.mov(asmjit::x86::word_ptr (rvGp, 0), g.r16()); break;
        case T::I32:
        case T::U32:  cc.mov(asmjit::x86::dword_ptr(rvGp, 0), g.r32()); break;
        // I64 / U64 / Ptr / Wstr / Cstr — full 8-byte store.
        default:      cc.mov(asmjit::x86::qword_ptr(rvGp, 0), g);       break;
    }
}

}  // namespace

void* BuildCDispatchThunk(void*                                  cFn,
                          const kcdx::hook_signature::Signature& cSig,
                          kcdx::hook_payload::Mode               mode) {
    using Mode = kcdx::hook_payload::Mode;
    if (!cFn) return nullptr;

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

    const asmjit::FuncSignature outerSig = OuterSigFor(mode);
    const asmjit::FuncSignature cFnSig   = CFnSigFor(cSig, mode);

    asmjit::FuncNode* func = cc.add_func(outerSig);

    if (mode == Mode::Mid) {
        // -------------------------------------------------------------
        // Mid: void thunk(void* payload_base, int count,
        //                 const char* const* capNames,
        //                 const char* const* capTypes)
        //
        // Author's cFn: void cFn(kcdxHookCaptureValue* values, int count)
        //
        // Stack-allocate values[count] (sizeof(kcdxHookCaptureValue) per
        // entry; the JIT slot payload uses 16-byte stride per the
        // make_jit_midfunc convention — different from parameters_t's
        // 8-byte stride). The thunk receives count as a runtime value;
        // asmjit's new_stack with a fixed max upper-bounds the
        // allocation (v1 cap matches the kcdxHookCapture count gate the
        // binder already enforces; reuse a conservative 16-entry cap
        // here — every cap-21 fixture has 1-2 captures, every author
        // pattern surveyed has <8).
        //
        // For the v1 path we delegate the per-slot fill + invoke + read-
        // back to a small native C helper (MidShim) — keeping the asmjit
        // emit minimal (just forward the 4 args). This honors the
        // brief's "engine carries marshaling" intent without re-deriving
        // the 16-byte stride + per-type field-read logic in asmjit (the
        // code lives once, in C++, where it is greppable). The thunk is
        // STILL a per-(cFn,cSig) emit because MidShim needs cFn as a
        // baked argument — we mov-imm64 it into RAX inline.
        //
        // Engine-side caller invokes the thunk with payload_base, count,
        // capNames, capTypes; we tack on cFn as a 5th arg to MidShim.
        // See MidShimEntry definition below.
        // arg vregs
        auto vPayload  = cc.new_gp_ptr();
        auto vCount    = cc.new_gp32();
        auto vCapNames = cc.new_gp_ptr();
        auto vCapTypes = cc.new_gp_ptr();
        func->set_arg(0, vPayload);
        func->set_arg(1, vCount);
        func->set_arg(2, vCapNames);
        func->set_arg(3, vCapTypes);

        // Invoke MidShimEntry(payload, count, capNames, capTypes, cFn).
        asmjit::InvokeNode* call;
        asmjit::FuncSignature shimSig(
            asmjit::CallConvId::kCDecl,
            asmjit::FuncSignature::kNoVarArgs,
            asmjit::TypeId::kVoid);
        shimSig.add_arg(asmjit::TypeId::kUIntPtr);
        shimSig.add_arg(asmjit::TypeId::kInt32);
        shimSig.add_arg(asmjit::TypeId::kUIntPtr);
        shimSig.add_arg(asmjit::TypeId::kUIntPtr);
        shimSig.add_arg(asmjit::TypeId::kUIntPtr);
        cc.invoke(asmjit::Out(call), (uintptr_t)&MidShimEntry, shimSig);
        call->set_arg(0, vPayload);
        call->set_arg(1, vCount);
        call->set_arg(2, vCapNames);
        call->set_arg(3, vCapTypes);
        call->set_arg(4, (uintptr_t)cFn);
        cc.ret();
        cc.end_func();
        cc.finalize();
        return FinalizeAndAlloc(code, asm_log, "BuildCDispatchThunk[Mid]");
    }

    // ----------------------------------------------------------------
    // Non-Mid modes share the parameters_t-slot unpack pipeline. They
    // differ on (a) what extra leading args cFn takes + (b) what we do
    // with the return.
    // ----------------------------------------------------------------
    auto vParams = cc.new_gp_ptr();
    func->set_arg(0, vParams);

    // The slot base is parameters_t::m_arguments — first 8-byte slot
    // sits at offsetof(parameters_t, m_arguments). The struct layout
    // (runtime_func_t.h:47-61) has m_arguments as the first volatile
    // uintptr_t field; the prior 8-byte head is sentinel-free in the
    // base struct (no vptr / no other field). The dispatcher passes a
    // parameters_t* through; we add an offset of 0 to land on slot 0.
    // (parameters_t::get_arg_ptr is `(char*)&m_arguments + 8*i`; from
    // parameters_t*'s base that equals 8*i.)
    const int32_t slotStride = static_cast<int32_t>(sizeof(uintptr_t));

    asmjit::x86::Gp vRv;  // only valid for After / Around / Replace
    asmjit::x86::Gp vCallOrigC;  // only valid for Around
    if (mode == Mode::After || mode == Mode::Around || mode == Mode::Replace) {
        vRv = cc.new_gp_ptr();
        func->set_arg(1, vRv);
        if (mode == Mode::Around) {
            vCallOrigC = cc.new_gp_ptr();
            func->set_arg(2, vCallOrigC);
        }
    }

    // For Before: stack-allocate an args[] mutation channel sized to
    // cSig.args.size() * 8 bytes, plus an int outCount slot.
    asmjit::x86::Mem argsMem;
    asmjit::x86::Mem outCountMem;
    asmjit::x86::Gp  argsBaseGp;
    asmjit::x86::Gp  outCountBaseGp;
    if (mode == Mode::Before) {
        const int bytesArgs =
            static_cast<int>(cSig.args.size() * sizeof(uintptr_t));
        if (bytesArgs > 0) {
            argsMem = cc.new_stack(static_cast<uint32_t>(bytesArgs), 8);
        }
        outCountMem = cc.new_stack(sizeof(int32_t), 4);

        // Pre-populate args[] with the current slot values + outCount=0
        // so a cFn that does NOT touch args[]/outCount produces a
        // zero-write-back result (original args flow through).
        for (size_t i = 0; i < cSig.args.size(); ++i) {
            const int32_t off = static_cast<int32_t>(i) * slotStride;
            auto tmp = cc.new_gp_ptr();
            cc.mov(tmp, asmjit::x86::qword_ptr(vParams, off));
            asmjit::x86::Mem slot = argsMem;
            slot.add_offset(static_cast<int32_t>(i) * 8);
            cc.mov(slot, tmp);
        }
        auto zero = cc.new_gp32();
        cc.xor_(zero, zero);
        cc.mov(outCountMem, zero);

        // Take the addresses of the args / outCount slots into vregs so
        // we can pass them as cFn's first two args.
        argsBaseGp = cc.new_gp_ptr();
        outCountBaseGp = cc.new_gp_ptr();
        if (bytesArgs > 0) {
            cc.lea(argsBaseGp, argsMem);
        } else {
            cc.xor_(argsBaseGp.r32(), argsBaseGp.r32());
        }
        cc.lea(outCountBaseGp, outCountMem);
    }

    // Unpack typed cFn args from the params slots. Slot i for arg index
    // i (regardless of mode's leading args — the typed args come after
    // the prepended args[] / origReturn / call_original).
    std::vector<UnpackedArg> argRegs;
    argRegs.reserve(cSig.args.size());
    for (size_t i = 0; i < cSig.args.size(); ++i) {
        argRegs.push_back(
            LoadSlotArg(cc, vParams,
                        static_cast<int32_t>(i) * slotStride,
                        cSig.args[i].type));
    }

    // For After non-void: read origReturn from rv+0 sized to cSig.returnType.
    UnpackedArg origReturnArg;
    bool haveOrigReturn = false;
    if (mode == Mode::After &&
        cSig.returnType != kcdx::hook_signature::Type::Void) {
        origReturnArg = LoadSlotArg(cc, vRv, 0, cSig.returnType);
        haveOrigReturn = true;
    }

    // Invoke cFn with the per-mode arg layout.
    asmjit::InvokeNode* call;
    cc.invoke(asmjit::Out(call), (uintptr_t)cFn, cFnSig);

    uint8_t argSlot = 0;
    if (mode == Mode::Before) {
        call->set_arg(argSlot++, argsBaseGp);
        call->set_arg(argSlot++, outCountBaseGp);
    } else if (mode == Mode::Around) {
        call->set_arg(argSlot++, vCallOrigC);
    } else if (mode == Mode::After && haveOrigReturn) {
        call->set_arg(argSlot++, origReturnArg.reg);
    }
    for (const auto& a : argRegs) {
        call->set_arg(argSlot++, a.reg);
    }

    // Capture cFn's return into the right register-class so we can
    // write it back to rv post-call (After non-void / Around / Replace).
    asmjit::Reg cFnRetReg;
    bool        cFnRet_is_xmm = false;
    const bool wantReturn =
        (mode == Mode::Around) || (mode == Mode::Replace) ||
        (mode == Mode::After &&
         cSig.returnType != kcdx::hook_signature::Type::Void);
    if (wantReturn) {
        if (kcdx::rom::is_XMM_register(cFnSig.ret())) {
            cFnRetReg = cc.new_xmm();
            cFnRet_is_xmm = true;
        } else {
            cFnRetReg = cc.new_gp_ptr();
        }
        call->set_ret(0, cFnRetReg);
    }

    // Per-mode post-call work.
    if (mode == Mode::Before) {
        // Read outCount; copy args[0..outCount-1] back to params slots.
        // We unroll the bounded copy at codegen time: for each slot i
        // in cSig, branch on (i < outCount) and store. Loop here for
        // bounded-N (typical 0-8 args) is cleaner than a runtime loop.
        auto outCountReg = cc.new_gp32();
        cc.mov(outCountReg, outCountMem);
        for (size_t i = 0; i < cSig.args.size(); ++i) {
            // if (outCountReg <= i) skip
            asmjit::Label after = cc.new_label();
            cc.cmp(outCountReg, static_cast<int>(i));
            cc.jle(after);
            // copy args[i] (qword) into params slot i (qword).
            asmjit::x86::Mem argMem = argsMem;
            argMem.add_offset(static_cast<int32_t>(i) * 8);
            auto tmp = cc.new_gp_ptr();
            cc.mov(tmp, argMem);
            cc.mov(asmjit::x86::qword_ptr(vParams,
                static_cast<int32_t>(i) * slotStride), tmp);
            cc.bind(after);
        }
    } else if (wantReturn) {
        StoreReturn(cc, vRv, cFnRetReg, cFnRet_is_xmm, cSig.returnType);
    }

    cc.ret();
    cc.end_func();
    cc.finalize();

    const char* what = "BuildCDispatchThunk[?]";
    switch (mode) {
        case Mode::Before:  what = "BuildCDispatchThunk[Before]";  break;
        case Mode::After:   what = "BuildCDispatchThunk[After]";   break;
        case Mode::Around:  what = "BuildCDispatchThunk[Around]";  break;
        case Mode::Replace: what = "BuildCDispatchThunk[Replace]"; break;
        case Mode::Mid:     what = "BuildCDispatchThunk[Mid]";     break;
        default: break;
    }
    return FinalizeAndAlloc(code, asm_log, what);
}

// ============================================================================
// MidShim — the C++ native dispatcher invoked by the Mid mode's tiny asmjit
// thunk. Doing the capture pack/unpack in C++ instead of asmjit keeps the
// 16-byte stride + per-type field-fill/readback in ONE greppable place
// (instead of 14 per-type asmjit branches). The thunk is still
// per-(cFn,cSig) emitted because cFn is baked as the 5th arg; cSig is not
// needed here because Mid is keyed on the runtime capTypes/capNames arrays.
// ============================================================================

namespace {

constexpr size_t kMidStride = 16;  // make_jit_midfunc capture slot stride

// Fill values[i].value_<type> from the slot at (payload_base + 16*i)
// according to capTypes[i] string.
void FillCaptureValueFromSlot(kcdxHookCaptureValue& v,
                              const void* slot,
                              const char* type) {
    v.value_int64 = 0;
    v.value_double = 0.0;
    v.value_ptr = nullptr;
    if (!type || !slot) return;
    if (std::strcmp(type, "f32") == 0 || std::strcmp(type, "float") == 0) {
        v.value_double = static_cast<double>(*static_cast<const float*>(slot));
    } else if (std::strcmp(type, "f64") == 0 ||
               std::strcmp(type, "double") == 0) {
        v.value_double = *static_cast<const double*>(slot);
    } else if (std::strcmp(type, "ptr") == 0) {
        v.value_ptr = *static_cast<void* const*>(slot);
    } else if (std::strcmp(type, "bool") == 0) {
        v.value_int64 = (*static_cast<const uint64_t*>(slot) != 0) ? 1 : 0;
    } else if (std::strcmp(type, "i8")  == 0) {
        v.value_int64 = *static_cast<const int8_t*>(slot);
    } else if (std::strcmp(type, "u8")  == 0) {
        v.value_int64 = *static_cast<const uint8_t*>(slot);
    } else if (std::strcmp(type, "i16") == 0) {
        v.value_int64 = *static_cast<const int16_t*>(slot);
    } else if (std::strcmp(type, "u16") == 0) {
        v.value_int64 = *static_cast<const uint16_t*>(slot);
    } else if (std::strcmp(type, "i32") == 0) {
        v.value_int64 = *static_cast<const int32_t*>(slot);
    } else if (std::strcmp(type, "u32") == 0) {
        v.value_int64 = *static_cast<const uint32_t*>(slot);
    } else if (std::strcmp(type, "u64") == 0) {
        v.value_int64 = static_cast<int64_t>(
            *static_cast<const uint64_t*>(slot));
    } else {
        // i64 default — same path as cSig fall-through.
        v.value_int64 = *static_cast<const int64_t*>(slot);
    }
}

// Read values[i] back into the slot at (payload_base + 16*i) per the
// matching value_<type> field. Writes the FULL 16-byte slot's leading
// bytes per the C-author's typed write; remaining bytes in the slot
// stay untouched (the JIT's reload reads only the typed width). For
// integer/bool we widen the typed write to 8 bytes (matching the Lua
// WriteCaptureValue behavior in hook_chain.cpp:651-657: "store full 64
// bits so the slot is clean for the JIT reload").
void StoreSlotFromCaptureValue(const kcdxHookCaptureValue& v,
                               void* slot, const char* type) {
    if (!type || !slot) return;
    if (std::strcmp(type, "f32") == 0 || std::strcmp(type, "float") == 0) {
        *static_cast<float*>(slot) = static_cast<float>(v.value_double);
    } else if (std::strcmp(type, "f64") == 0 ||
               std::strcmp(type, "double") == 0) {
        *static_cast<double*>(slot) = v.value_double;
    } else if (std::strcmp(type, "ptr") == 0) {
        *static_cast<void**>(slot) = v.value_ptr;
    } else {
        // Integer / bool — widen to 8 bytes (mirrors lua-side
        // WriteCaptureValue in hook_chain.cpp:651-657).
        *static_cast<uint64_t*>(slot) =
            static_cast<uint64_t>(v.value_int64);
    }
}

}  // namespace

// Defined at namespace-extern scope so the asmjit Mid thunk can take its
// address; declared inside the Mid codegen branch as `extern void
// MidShimEntry(...)`. v1 caps the per-call values[] at 16 entries
// (matches the binder's practical author surface); a larger capture
// count gets clamped + logged.
void MidShimEntry(void*              payload_base,
                  int                count,
                  const char* const* capNames,
                  const char* const* capTypes,
                  void*              cFn) {
    if (!cFn || count <= 0) return;
    constexpr int kMaxValuesStack = 16;
    if (count > kMaxValuesStack) {
        log::WarnF("BuildCDispatchThunk[Mid]: capture count=%d exceeds v1 "
                   "stack cap %d; clamping (extra captures ignored — file "
                   "an issue if a real plugin hits this)",
                   count, kMaxValuesStack);
        count = kMaxValuesStack;
    }
    kcdxHookCaptureValue values[kMaxValuesStack] = {};
    char* base = static_cast<char*>(payload_base);
    for (int i = 0; i < count; ++i) {
        values[i].name = capNames ? capNames[i] : nullptr;
        values[i].type = capTypes ? capTypes[i] : nullptr;
        FillCaptureValueFromSlot(values[i], base + kMidStride * i,
                                 values[i].type);
    }
    using CFnT = void (*)(kcdxHookCaptureValue*, int);
    reinterpret_cast<CFnT>(cFn)(values, count);
    for (int i = 0; i < count; ++i) {
        StoreSlotFromCaptureValue(values[i], base + kMidStride * i,
                                  values[i].type);
    }
}

}  // namespace kcdx::dynamic_call_jit
