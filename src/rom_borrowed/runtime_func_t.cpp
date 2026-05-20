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
//   - big::detour_hook -> kcdx::detour_hook (MinHook-backed shim)
//   - dtor's lua-manager cleanup deferred to Phase 5c step 6 (scripting module)
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
    m_detour      = std::make_unique<kcdx::detour_hook>();
    m_return_type = {type_info_t::none_};
}

runtime_func_t::~runtime_func_t() {
    // Phase 5c step 6 will add a scripting-module singleton equivalent of
    // big::g_lua_manager and erase this hook's entry from it here. For
    // now, just disable the hook (kcdx::detour_hook's dtor also does this
    // best-effort, so this is belt-and-suspenders).
    if (m_detour) {
        m_detour->disable();
    }
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
    cc.mov(original_ptr, (uintptr_t)m_detour->get_original_ptr());
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
    // std::vector<uint8_t> + VirtualProtect approach (Phase 5c.7b.1,
    // 2026-05-18).
    //
    // owner=0 here means "engine, not a plugin." Once Phase 5e wires
    // RegisterFunction we'll thread the calling plugin's handle through.
    m_jit_function_buffer = kcdx::trampoline::AllocateBranch(/*owner=*/0, size);
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

class jit_error_handler : public asmjit::ErrorHandler {
public:
    jit_error_handler() = default;

    void handle_error(asmjit::Error /*err*/, const char* message,
                      asmjit::BaseEmitter* /*origin*/) override {
        log::ErrorF("runtime_func_t asmjit error: %s", message);
    }
};

uintptr_t runtime_func_t::make_jit_midfunc(const std::vector<std::string>& param_types,
                                           const std::vector<std::string>& param_captures,
                                           const int stack_restore_offset,
                                           const asmjit::Arch arch,
                                           mid_callback_t mid_callback,
                                           const uintptr_t target_func_ptr) {
    for (const std::string& s : param_types) {
        m_param_types.push_back(get_type_info_from_string(s));
    }

    asmjit::CodeHolder code;
    auto env = asmjit::Environment::host();
    env.set_arch(arch);
    code.init(env);
    jit_error_handler eh;
    code.set_error_handler(&eh);

    // initialize function
    asmjit::x86::Assembler cc(&code);

    asmjit::StringLogger asmLog;
    const auto format_flags =
        asmjit::FormatFlags::kMachineCode | asmjit::FormatFlags::kExplainImms | asmjit::FormatFlags::kRegCasts |
        asmjit::FormatFlags::kHexImms     | asmjit::FormatFlags::kHexOffsets  | asmjit::FormatFlags::kPositions;

    asmLog.add_flags(format_flags);
    code.set_logger(&asmLog);

    asmjit::Label original_invoke_label = cc.new_label();

    // save caller-saved registers
    cc.push(asmjit::x86::qword_ptr((uint64_t)m_detour->get_original_ptr()));
    cc.pushfq();
    cc.push(asmjit::x86::rbp);
    cc.push(asmjit::x86::rax);
    cc.push(asmjit::x86::rcx);
    cc.push(asmjit::x86::rdx);
    cc.push(asmjit::x86::r8);
    cc.push(asmjit::x86::r9);
    cc.push(asmjit::x86::r10);
    cc.push(asmjit::x86::r11);

    // setup the stack structure to hold arguments for user callback
    int32_t stack_size = 16 * (int32_t)param_types.size();

    // allocate space
    cc.sub(asmjit::x86::rsp, stack_size);

    // save capture registers to save change
    std::unordered_map<uint8_t, asmjit::x86::Gp> cap_Gps;
    std::vector<asmjit::x86::Mem> target_address_cache(param_types.size(), asmjit::x86::Mem());

    // capture registers to the stack
    for (uint8_t argIdx = 0; argIdx < param_types.size(); argIdx++) {
        auto argType    = get_type_id(param_types.at(argIdx));
        auto argCapture = param_captures.at(argIdx);
        if (argCapture.at(0) == '[') {
            if (is_general_register(argType)) {
                // caller-saved registers' offset + temp register's offset
                auto target_address = get_addr_from_name(argCapture, stack_size + 8 * 10 + 8);
                if (!target_address.has_value()) {
                    log::Error("runtime_func_t::make_jit_midfunc: can't get address from name (GP)");
                    return 0;
                }
                target_address_cache[argIdx] = *target_address;
                cc.push(asmjit::x86::rbp);
                cc.mov(asmjit::x86::rbp, *target_address);
                cc.mov(asmjit::x86::ptr(asmjit::x86::rsp, 16 * argIdx + 8), asmjit::x86::rbp);
                cc.pop(asmjit::x86::rbp);
            } else if (is_XMM_register(argType)) {
                auto target_address = get_addr_from_name(argCapture, stack_size + 8 * 10 + 16);
                if (!target_address.has_value()) {
                    log::Error("runtime_func_t::make_jit_midfunc: can't get address from name (XMM)");
                    return 0;
                }
                target_address_cache[argIdx] = *target_address;
                cc.sub(asmjit::x86::rsp, 16);
                cc.movq(asmjit::x86::ptr(asmjit::x86::rsp, 16), asmjit::x86::xmm0);
                cc.movq(asmjit::x86::xmm0, *target_address);
                cc.movq(asmjit::x86::ptr(asmjit::x86::rsp, 16 * argIdx + 16), asmjit::x86::xmm0);
                cc.movq(asmjit::x86::xmm0, asmjit::x86::ptr(asmjit::x86::rsp, 16));
                cc.add(asmjit::x86::rsp, 16);
            } else {
                log::Error("runtime_func_t::make_jit_midfunc: unsupported parameter type (likely unknown name in param_types — try 'i32', 'i64', 'ptr', 'f32', 'f64', etc.)");
                return 0;
            }
        } else {
            if (is_general_register(argType)) {
                auto target_reg = get_gp_from_name(argCapture);
                if (!target_reg.has_value()) {
                    auto target_address = get_addr_from_name('[' + argCapture + ']', stack_size + 8 * 10 + 8);
                    if (!target_address.has_value()) {
                        log::Error("runtime_func_t::make_jit_midfunc: can't get register from name (GP fallback)");
                        return 0;
                    }
                    target_address_cache[argIdx] = *target_address;
                    cc.push(asmjit::x86::rbp);
                    cc.lea(asmjit::x86::rbp, *target_address);
                    cc.mov(asmjit::x86::ptr(asmjit::x86::rsp, 16 * argIdx + 8), asmjit::x86::rbp);
                    cc.pop(asmjit::x86::rbp);
                } else {
                    if (*target_reg == asmjit::x86::rsp) {
                        cc.push(asmjit::x86::rbp);
                        cc.mov(asmjit::x86::rbp, asmjit::x86::rsp);
                        cc.add(asmjit::x86::rbp, stack_size + 8 * 10 + 8);
                        cc.mov(asmjit::x86::ptr(asmjit::x86::rsp, 16 * argIdx), *target_reg);
                        cc.pop(asmjit::x86::rbp);
                    } else {
                        cc.mov(asmjit::x86::ptr(asmjit::x86::rsp, 16 * argIdx), *target_reg);
                    }
                    cap_Gps[argIdx] = *target_reg;
                }
            } else if (is_XMM_register(argType)) {
                auto target_reg = get_xmm_from_name(argCapture);
                if (!target_reg.has_value()) {
                    log::Error("runtime_func_t::make_jit_midfunc: can't get register from name (XMM)");
                    return 0;
                }
                cc.movq(asmjit::x86::ptr(asmjit::x86::rsp, 16 * argIdx), *target_reg);
            } else {
                log::Error("runtime_func_t::make_jit_midfunc: unsupported parameter type (likely unknown name in param_types — try 'i32', 'i64', 'ptr', 'f32', 'f64', etc.)");
                return 0;
            }
        }
    }

    // pass arguments to the function
    cc.mov(asmjit::x86::rcx, asmjit::x86::rsp);
    cc.mov(asmjit::x86::rdx, param_types.size());
    cc.mov(asmjit::x86::r8, (uintptr_t)target_func_ptr);

    // save the rsp
    cc.mov(asmjit::x86::rbp, asmjit::x86::rsp);

    // allocate prelogue space, may require a bigger space
    cc.sub(asmjit::x86::rsp, 128);

    // stack alignment
    cc.and_(asmjit::x86::rsp, -16);

    // invoke the mid callback
    cc.mov(asmjit::x86::r9, (uintptr_t)mid_callback);
    cc.call(asmjit::x86::r9);

    // restore rsp
    cc.mov(asmjit::x86::rsp, asmjit::x86::rbp);

    // if the callback return value is zero, skip orig.
    cc.test(asmjit::x86::rax, asmjit::x86::rax);
    cc.jz(original_invoke_label);
    cc.mov(asmjit::x86::ptr(asmjit::x86::rsp, stack_size + 8 * 9), asmjit::x86::rax);
    cc.bind(original_invoke_label);

    // restore caller-saved registers before using again.
    auto restore_register = [&](asmjit::x86::Gp reg, size_t index) {
        for (const auto& pair : cap_Gps) {
            if (pair.second == reg) {
                return;
            }
        }
        cc.mov(reg, asmjit::x86::ptr(asmjit::x86::rsp, stack_size + 8 * (int32_t)index));
    };
    restore_register(asmjit::x86::r11, 0);
    restore_register(asmjit::x86::r10, 1);
    restore_register(asmjit::x86::r9,  2);
    restore_register(asmjit::x86::r8,  3);
    restore_register(asmjit::x86::rdx, 4);
    restore_register(asmjit::x86::rcx, 5);
    restore_register(asmjit::x86::rax, 6);
    restore_register(asmjit::x86::rbp, 7);

    // apply change
    for (uint8_t argIdx = 0; argIdx < param_types.size(); argIdx++) {
        auto argType    = get_type_id(param_types.at(argIdx));
        auto argCapture = param_captures.at(argIdx);
        if (argCapture.at(0) == '[') {
            if (is_general_register(argType)) {
                std::vector<uint32_t> useable_reg_list = get_useable_gp_id_from_name(argCapture);
                if (useable_reg_list.size() > 0) {
                    asmjit::x86::Gp temp_reg = asmjit::x86::gpq(useable_reg_list[0]);
                    cc.push(temp_reg);
                    cc.mov(temp_reg, asmjit::x86::ptr(asmjit::x86::rsp, 16 * argIdx + 8));
                    cc.mov(target_address_cache[argIdx], temp_reg);
                    cc.pop(temp_reg);
                }
            } else if (is_XMM_register(argType)) {
                cc.sub(asmjit::x86::rsp, 16);
                cc.movq(asmjit::x86::ptr(asmjit::x86::rsp, 16), asmjit::x86::xmm0);
                cc.movq(asmjit::x86::xmm0, asmjit::x86::ptr(asmjit::x86::rsp, 16 * argIdx + 16));
                cc.movq(target_address_cache[argIdx], asmjit::x86::xmm0);
                cc.movq(asmjit::x86::xmm0, asmjit::x86::ptr(asmjit::x86::rsp, 16));
                cc.add(asmjit::x86::rsp, 16);
            }
        } else {
            if (is_general_register(argType)) {
                auto target_reg = get_gp_from_name(argCapture);
                // If it is a computed address, we can't restore it.
                if (target_reg.has_value()) {
                    cc.mov(*target_reg, asmjit::x86::ptr(asmjit::x86::rsp, 16 * argIdx));
                }
            } else if (is_XMM_register(argType)) {
                auto target_reg = get_xmm_from_name(argCapture);
                cc.movq(*target_reg, asmjit::x86::ptr(asmjit::x86::rsp, 16 * argIdx));
            }
        }
    }

    // stack cleanup
    cc.add(asmjit::x86::rsp, stack_size + 8 * 8);
    cc.popfq();

    if (stack_restore_offset != 0) {
        cc.sub(asmjit::x86::rsp, stack_restore_offset);
    }

    // jump to the original function
    cc.ret();

    // write to buffer
    cc.finalize();

    // worst case, overestimates for case trampolines needed
    code.flatten();
    size_t size = code.code_size();

    // Same branch_pool allocation as make_jit_func — see Phase 5c.7b.1
    // notes above. owner=0 (engine, not a plugin) until 5e wires it.
    m_jit_function_buffer = kcdx::trampoline::AllocateBranch(/*owner=*/0, size);
    if (!m_jit_function_buffer) {
        log::Error("runtime_func_t::make_jit_midfunc: branch_pool allocation failed");
        return 0;
    }
    m_jit_function_size = size;

    if (code.has_unresolved_fixups()) {
        code.resolve_cross_section_fixups();
    }

    code.relocate_to_base((uintptr_t)m_jit_function_buffer);
    code.copy_flattened_data(m_jit_function_buffer, size);

    log::DebugF("runtime_func_t::make_jit_midfunc: JIT stub at 0x%p (%zu bytes, branch_pool)",
                m_jit_function_buffer, size);
    // Log the disassembly line by line so log.h's 1024-char buffer
    // doesn't truncate it. Each asmjit-emitted instruction is on its
    // own line in the StringLogger output.
    const char* p = asmLog.data();
    while (p && *p) {
        const char* end = strchr(p, '\n');
        std::string line = end ? std::string(p, end - p) : std::string(p);
        if (!line.empty()) log::DebugF("  jit| %s", line.c_str());
        if (!end) break;
        p = end + 1;
    }

    return (uintptr_t)m_jit_function_buffer;
}

void runtime_func_t::create_and_enable_hook(const std::string& hook_name,
                                            uintptr_t target_func_ptr,
                                            uintptr_t jitted_func_ptr,
                                            bool is_follow_call_on_fn_address) {
    m_target_func_ptr = target_func_ptr;

    m_detour->set_instance(hook_name, (void*)target_func_ptr, (void*)jitted_func_ptr);
    m_detour->set_is_follow_call_on_fn_address(is_follow_call_on_fn_address);
    m_detour->enable();
}

}  // namespace kcdx::rom
