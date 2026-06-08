// Adapted from ReturnOfModding (https://github.com/xiaoxiao921/ReturnOfModdingBase),
// MIT. See runtime_func_t.h for the adaptation rationale.
//
// Key substitutions vs upstream (asmjit HEAD as of 2026-05; RoM wrote against
// the 2024 camelCase API, upstream moved everything to snake_case):
//   - argCount -> arg_count, hasRet -> has_ret, addArg -> add_arg
//   - setArch -> set_arch, addFunc -> add_func, endFunc -> end_func
//   - addFlags -> add_flags, setLogger -> set_logger, setErrorHandler -> set_error_handler
//   - newUIntPtr() -> new_gp_ptr(), newUInt8() -> new_gp8(), newXmm() -> new_xmm()
//   - newStack -> new_stack, newLabel -> new_label
//   - setIndex/setSize (on Mem) -> set_index/set_size
//   - setArg/setRet (on FuncNode and InvokeNode) -> set_arg/set_ret
//   - codeSize -> code_size, hasUnresolvedLinks -> has_unresolved_fixups
//   - resolveUnresolvedLinks -> resolve_cross_section_fixups
//   - relocateToBase -> relocate_to_base, copyFlattenedData -> copy_flattened_data
//   - FuncSignatureT<...>() -> FuncSignature::build<...>()
//   - asmjit::x86::Xmm -> asmjit::x86::Vec (asmjit unified Xmm/Ymm/Zmm)
//   - asmjit::x86::Reg -> asmjit::Reg (Reg lives in asmjit:: now, not asmjit::x86::)
//   - kHost -> kCDecl (kHost enum was removed)
//   - Compiler::invoke now takes asmjit::Out<InvokeNode*>, not InvokeNode**
//   - ErrorHandler::handleError -> handle_error
//   - ankerl::unordered_dense::map -> std::unordered_map
//   - LOG(LEVEL) << msg -> kcdx::log::Level(msg) / log::LevelF for printf-style
//   - big::detour_hook + the JIT call-original slot it owned -> the slot
//     storage moved onto runtime_func_t (m_original_slot); the backend the
//     install drives is now owned at hook_engine::InstallRuntime, which
//     POPULATES this slot. The dissolved adapter held no logic of its own.
//   - dtor's lua-manager cleanup deferred to a later step (scripting module)
#include "runtime_func_t.h"

#include <windows.h>
#include <unordered_map>

#include "../log.h"
#include "../trampoline.h"

namespace kcdx::rom {

char* runtime_func_t::parameters_t::get_arg_ptr(const uint8_t idx) const {
    return ((char*)&m_arguments) + sizeof(uintptr_t) * idx;
}

unsigned char* runtime_func_t::return_value_t::get() const {
    return (unsigned char*)&m_return_value;
}

runtime_func_t::runtime_func_t() {
    m_return_type = {type_info_t::none_};
}

runtime_func_t::~runtime_func_t() {
    // A later step will add a scripting-module singleton equivalent of
    // big::g_lua_manager and erase this hook's entry from it here. The
    // detour itself is owned at the install seam (hook_engine::InstallRuntime
    // drives the backend); kcdx never unhooks (session-lifetime, SKSE model),
    // so there is no MinHook teardown to do here.
}

uint64_t runtime_func_t::fingerprint_jit_buffer() const {
    if (!m_jit_function_buffer || m_jit_function_size == 0) return 0;
    uint64_t h = 0xcbf29ce484222325ULL;
    const uint8_t* b = static_cast<const uint8_t*>(m_jit_function_buffer);
    for (size_t i = 0; i < m_jit_function_size; ++i) {
        h ^= b[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

uint64_t runtime_func_t::fingerprint_self() const {
    uint64_t h = 0xcbf29ce484222325ULL;
    const uint8_t* b = reinterpret_cast<const uint8_t*>(this);
    for (size_t i = 0; i < sizeof(*this); ++i) {
        h ^= b[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

void runtime_func_t::debug_print_args(const asmjit::FuncSignature& sig) {
    for (uint8_t arg_index_debug = 0; arg_index_debug < sig.arg_count(); arg_index_debug++) {
        const auto arg_type_debug = sig.args()[arg_index_debug];
        log::DebugF("runtime_func_t arg[%u] type=%d",
                    (unsigned)arg_index_debug,
                    (int)arg_type_debug);
    }
}

uintptr_t runtime_func_t::make_jit_func(const asmjit::FuncSignature& sig,
                                        const asmjit::Arch arch,
                                        const user_pre_callback_t pre_callback,
                                        const user_post_callback_t post_callback,
                                        const uintptr_t target_func_ptr) {
    asmjit::CodeHolder code;
    auto env = asmjit::Environment::host();
    env.set_arch(arch);
    code.init(env);

    // initialize function
    asmjit::x86::Compiler cc(&code);
    asmjit::FuncNode* func = cc.add_func(sig);

    asmjit::StringLogger asmLog;
    const auto format_flags =
        asmjit::FormatFlags::kMachineCode | asmjit::FormatFlags::kExplainImms | asmjit::FormatFlags::kRegCasts |
        asmjit::FormatFlags::kHexImms     | asmjit::FormatFlags::kHexOffsets  | asmjit::FormatFlags::kPositions;

    asmLog.add_flags(format_flags);
    code.set_logger(&asmLog);

    // map argument slots to registers, following abi.
    std::vector<asmjit::Reg> arg_registers;
    for (uint8_t arg_index = 0; arg_index < sig.arg_count(); arg_index++) {
        const auto arg_type = sig.args()[arg_index];

        asmjit::Reg arg;
        if (is_general_register(arg_type)) {
            arg = cc.new_gp_ptr();
        } else if (is_XMM_register(arg_type)) {
            arg = cc.new_xmm();
        } else {
            log::ErrorF("runtime_func_t::make_jit_func: unsupported parameter type at index=%u (asmjit TypeId=%d; likely unknown type name in param_types — try 'i32', 'i64', 'ptr', 'f32', 'f64', etc., or check spelling against asmjit_helper.cpp::get_type_id)",
                        (unsigned)arg_index, (int)arg_type);
            debug_print_args(sig);
            return 0;
        }

        func->set_arg(arg_index, arg);
        arg_registers.push_back(arg);
    }

    // setup the stack structure to hold arguments for user callback
    uint32_t stack_size = (uint32_t)(sizeof(uintptr_t) * sig.arg_count());
    m_args_stack        = cc.new_stack(stack_size, 16);
    asmjit::x86::Mem args_stack_index(m_args_stack);

    // assigns some register as index reg
    asmjit::x86::Gp i = cc.new_gp_ptr();

    // stack_index <- stack[i].
    args_stack_index.set_index(i);

    // r/w are sizeof(uintptr_t) width now
    args_stack_index.set_size(sizeof(uintptr_t));

    // set i = 0
    cc.mov(i, 0);
    // mov from arguments registers into the stack structure
    for (uint8_t argIdx = 0; argIdx < sig.arg_count(); argIdx++) {
        const auto argType = sig.args()[argIdx];

        // have to cast back to explicit register types to gen right mov type
        if (is_general_register(argType)) {
            cc.mov(args_stack_index, arg_registers.at(argIdx).as<asmjit::x86::Gp>());
        } else if (is_XMM_register(argType)) {
            cc.movq(args_stack_index, arg_registers.at(argIdx).as<asmjit::x86::Vec>());
        } else {
            log::ErrorF("runtime_func_t::make_jit_func: unsupported parameter type at index=%u (asmjit TypeId=%d; likely unknown type name in param_types — try 'i32', 'i64', 'ptr', 'f32', 'f64', etc., or check spelling against asmjit_helper.cpp::get_type_id)",
                        (unsigned)argIdx, (int)argType);
            debug_print_args(sig);
            return 0;
        }

        // next structure slot (+= sizeof(uintptr_t))
        cc.add(i, sizeof(uintptr_t));
    }

    // get pointer to stack structure and pass it to the user pre callback
    asmjit::x86::Gp arg_struct = cc.new_gp_ptr("arg_struct");
    cc.lea(arg_struct, m_args_stack);

    // fill reg to pass struct arg count to callback
    asmjit::x86::Gp arg_param_count = cc.new_gp8();
    cc.mov(arg_param_count, (uint8_t)sig.arg_count());

    // create buffer for ret val
    asmjit::x86::Mem return_stack = cc.new_stack(sizeof(uintptr_t), 16);
    asmjit::x86::Gp return_struct = cc.new_gp_ptr("return_struct");
    cc.lea(return_struct, return_stack);

    // fill reg to pass target function pointer to callback
    asmjit::x86::Gp target_func_ptr_reg = cc.new_gp_ptr();
    cc.mov(target_func_ptr_reg, target_func_ptr);

    asmjit::Label original_invoke_label      = cc.new_label();
    asmjit::Label skip_original_invoke_label = cc.new_label();

    // invoke the user pre callback
    asmjit::InvokeNode* pre_callback_invoke_node;
    cc.invoke(asmjit::Out(pre_callback_invoke_node),
              (uintptr_t)pre_callback,
              asmjit::FuncSignature::build<bool, parameters_t*, uint8_t, return_value_t*, uintptr_t>());

    // call to user provided function (use ABI of host compiler)
    pre_callback_invoke_node->set_arg(0, arg_struct);
    pre_callback_invoke_node->set_arg(1, arg_param_count);
    pre_callback_invoke_node->set_arg(2, return_struct);
    pre_callback_invoke_node->set_arg(3, target_func_ptr_reg);

    // pre callback returns a bool — sized register for the test instruction.
    asmjit::x86::Gp pre_callback_return_val = cc.new_gp8("pre_callback_return_val");
    pre_callback_invoke_node->set_ret(0, pre_callback_return_val);

    // if the callback return value is zero, skip orig.
    cc.test(pre_callback_return_val, pre_callback_return_val);
    cc.jz(skip_original_invoke_label);

    // label to invoke the original function
    cc.bind(original_invoke_label);

    // mov from arguments stack structure into regs
    cc.mov(i, 0); // reset idx
    for (uint8_t arg_idx = 0; arg_idx < sig.arg_count(); arg_idx++) {
        const auto argType = sig.args()[arg_idx];

        if (is_general_register(argType)) {
            cc.mov(arg_registers.at(arg_idx).as<asmjit::x86::Gp>(), args_stack_index);
        } else if (is_XMM_register(argType)) {
            cc.movq(arg_registers.at(arg_idx).as<asmjit::x86::Vec>(), args_stack_index);
        } else {
            log::ErrorF("runtime_func_t::make_jit_func: unsupported parameter type at index=%u (asmjit TypeId=%d; likely unknown type name in param_types — try 'i32', 'i64', 'ptr', 'f32', 'f64', etc., or check spelling against asmjit_helper.cpp::get_type_id)",
                        (unsigned)arg_idx, (int)argType);
            debug_print_args(sig);
            return 0;
        }

        cc.add(i, sizeof(uint64_t));
    }

    // deref the trampoline ptr (holder must live longer, must be concrete reg since push later)
    asmjit::x86::Gp original_ptr = cc.new_gp_ptr();
    cc.mov(original_ptr, (uintptr_t)get_jit_original_slot());
    cc.mov(original_ptr, asmjit::x86::ptr(original_ptr));

    asmjit::InvokeNode* original_invoke_node;
    cc.invoke(asmjit::Out(original_invoke_node), original_ptr, sig);
    for (uint8_t arg_index = 0; arg_index < sig.arg_count(); arg_index++) {
        original_invoke_node->set_arg(arg_index, arg_registers.at(arg_index));
    }

    if (sig.has_ret()) {
        if (is_general_register(sig.ret())) {
            asmjit::x86::Gp tmp = cc.new_gp_ptr();
            original_invoke_node->set_ret(0, tmp);
            cc.mov(return_stack, tmp);
        } else {
            asmjit::x86::Vec tmp = cc.new_xmm();
            original_invoke_node->set_ret(0, tmp);
            cc.movq(return_stack, tmp);
        }
    }

    cc.bind(skip_original_invoke_label);

    asmjit::InvokeNode* post_callback_invoke_node;
    cc.invoke(asmjit::Out(post_callback_invoke_node),
              (uintptr_t)post_callback,
              asmjit::FuncSignature::build<void, parameters_t*, uint8_t, return_value_t*, uintptr_t>());

    // Set arguments for the post callback
    post_callback_invoke_node->set_arg(0, arg_struct);
    post_callback_invoke_node->set_arg(1, arg_param_count);
    post_callback_invoke_node->set_arg(2, return_struct);
    post_callback_invoke_node->set_arg(3, target_func_ptr_reg);

    if (sig.has_ret()) {
        asmjit::x86::Mem return_stack_index(return_stack);
        return_stack_index.set_size(sizeof(uintptr_t));
        if (is_general_register(sig.ret())) {
            asmjit::x86::Gp tmp2 = cc.new_gp_ptr();
            cc.mov(tmp2, return_stack_index);
            cc.ret(tmp2);
        } else {
            asmjit::x86::Vec tmp2 = cc.new_xmm();
            cc.movq(tmp2, return_stack_index);
            cc.ret(tmp2);
        }
    }

    cc.end_func();

    // write to buffer
    cc.finalize();

    // worst case, overestimates for case trampolines needed
    code.flatten();
    size_t size = code.code_size();

    // Allocate executable memory from kcdx's branch_pool — within
    // +/-2 GB of WHGame.dll, so a 5-byte rel32 jmp from any hook
    // target site can reach this trampoline. Replaces upstream RoM's
    // std::vector<uint8_t> + VirtualProtect approach.
    //
    // owner=0 here means "engine, not a plugin." Once a later step wires
    // RegisterFunction we'll thread the calling plugin's handle through.
    //
    // nearVa = target_func_ptr: anchor the buffer near the hook TARGET, not
    // just WHGame.dll. For a function-entry hook target_func_ptr is the
    // function VA; for a callsite chain it is the call-site VA (the rewritten
    // E8 must reach this buffer). When the target lives in a far module
    // (>2 GB from WHGame) this is what keeps the 5-byte rel32 jmp in range
    // (cap-22). nearVa=0 callers are unchanged (WHGame anchor).
    m_jit_function_buffer = kcdx::trampoline::AllocateBranch(/*owner=*/0, size,
                                                             target_func_ptr);
    if (!m_jit_function_buffer) {
        log::Error("runtime_func_t::make_jit_func: branch_pool allocation failed");
        return 0;
    }
    m_jit_function_size = size;

    // if multiple sections, resolve linkage (1 atm)
    if (code.has_unresolved_fixups()) {
        code.resolve_cross_section_fixups();
    }

    // Relocate to the base-address of the allocated memory.
    code.relocate_to_base((uintptr_t)m_jit_function_buffer);
    code.copy_flattened_data(m_jit_function_buffer, size);

    log::DebugF("runtime_func_t::make_jit_func: JIT stub at 0x%p (%zu bytes, branch_pool), asmjit log:\n%s",
                m_jit_function_buffer, size, asmLog.data());

    return (uintptr_t)m_jit_function_buffer;
}

uintptr_t runtime_func_t::make_jit_func(const std::string& return_type,
                                        const std::vector<std::string>& param_types,
                                        const asmjit::Arch arch,
                                        const user_pre_callback_t pre_callback,
                                        const user_post_callback_t post_callback,
                                        const uintptr_t target_func_ptr,
                                        std::string call_convention) {
    m_return_type = get_type_info_from_string(return_type);

    asmjit::FuncSignature sig(get_call_convention(call_convention),
                              asmjit::FuncSignature::kNoVarArgs,
                              get_type_id(return_type));

    for (const std::string& s : param_types) {
        sig.add_arg(get_type_id(s));
        m_param_types.push_back(get_type_info_from_string(s));
    }

    return make_jit_func(sig, arch, pre_callback, post_callback, target_func_ptr);
}

// make_jit_midfunc (the ~370-line hand-rolled asmjit mid-hook codegen) was
// REMOVED — the mid-function path is now a safetyhook::MidHook adapter
// (src/safetyhook_midhook.{cpp,h}) that reads/writes named captures through
// Context64 and routes each fire to hook_chain::MidDispatch. The three
// call-original modes ride safetyhook's ctx.rip (design §5.1); resume is
// targetVa + original_bytes().size() (safetyhook's relocated-region size, NOT
// the captured-instruction length), computed in the adapter. The
// function-entry JIT (make_jit_func, above) + the call-original slot machinery
// are UNCHANGED — only the mid codegen retired.

}  // namespace kcdx::rom
