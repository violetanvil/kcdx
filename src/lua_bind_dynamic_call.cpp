// kcdx.memory.dynamic_call — Lua → native function invocation.
//
// Lua surface (Phase 5c.7c):
//
//   local memcpy_addr = kcdx.memory.scan_pattern("48 89 5C 24 ...")
//   local memcpy = kcdx.memory.dynamic_call({
//       target      = memcpy_addr,        -- pointer userdata or integer VA
//       return_type = "ptr",              -- string, optional (default "void")
//       param_types = {"ptr", "ptr", "i64"}, -- list of strings
//   })
//   -- memcpy is a userdata with __call. Invoke like a function:
//   local result = memcpy(dst_addr, src_addr, length)
//
// Returns: callable userdata (kcdx.memory.dynamic_call_handle) on success,
//          nil + error string on failure.
//
// Adapted from RoM's lua/bindings/memory.cpp `dynamic_call` and
// `jit_lua_binded_func` at commit d30217b6. Adaptations:
//   - asmjit camelCase → snake_case (matches kcdx Phase 5c.5 work)
//   - JIT buffer routes through kcdx::trampoline::AllocateBranch
//     (matches kcdx Phase 5c.7b.1) instead of new uint8_t[] + VirtualProtect
//   - Returns a userdata with __call rather than poisoning the Lua
//     global namespace with `__dynamic_call_<addr>` per RoM
//   - sol2 removed throughout (kcdx hard rule #15)
//
// What this enables: pak Lua mods can now invoke arbitrary WHGame.dll
// functions (or any DLL function) at runtime. Combined with
// kcdx.memory.scan_pattern + kcdx.memory.allocate, pak Lua has full
// C-side reach through kcdx — limited only by what signatures the
// plugin author knows.

#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include <asmjit/asmjit.h>

#include "log.h"
#include "lua_bind_helpers.h"
#include "lua_memory.h"
#include "rom_borrowed/asmjit_helper.h"
#include "rom_borrowed/type_info_t.h"
#include "trampoline.h"

namespace kcdx::lua_bind_dynamic_call {

namespace {

constexpr const char* kHandleMetatable = "kcdx.memory.dynamic_call_handle";

// Userdata: holds the JIT'd trampoline address. The trampoline lives
// in branch_pool (alloc-only) so we never free it — handle GC is a
// no-op. Userdata payload is just the function pointer cast as
// lua_CFunction.
struct CallHandleUd {
    lua_CFunction fn = nullptr;
};

// asmjit error handler matching the runtime_func_t.cpp style. Logs
// any asmjit-emitted error to kcdx.log so JIT problems surface.
class JitErrorHandler : public asmjit::ErrorHandler {
public:
    void handle_error(asmjit::Error /*err*/, const char* message,
                      asmjit::BaseEmitter* /*origin*/) override {
        log::ErrorF("dynamic_call asmjit error: %s", message);
    }
};

// Build the trampoline. Lua C function signature: int(lua_State*).
// Returns absolute VA of the JIT'd code (in branch_pool) or 0 on failure.
//
// Per-arg marshaling: invokes the appropriate lua_toXXX function with
// the Lua state and stack index (arg_index + 1, Lua is 1-indexed),
// stores the result in a virtual register, then passes it as that arg
// to the target function.
//
// Per-return marshaling: takes the target's return register, converts
// to a Lua-pushable type if needed, invokes lua_pushXXX.
uintptr_t JitTrampoline(uintptr_t                                  target_func_ptr,
                        const asmjit::FuncSignature&               target_sig,
                        asmjit::Arch                               arch,
                        const std::vector<kcdx::rom::type_info_t>& param_types,
                        kcdx::rom::type_info_t                     return_type) {
    asmjit::CodeHolder code;
    auto env = asmjit::Environment::host();
    env.set_arch(arch);
    code.init(env);

    JitErrorHandler eh;
    code.set_error_handler(&eh);

    asmjit::x86::Compiler cc(&code);

    asmjit::StringLogger asm_log;
    const auto format_flags =
        asmjit::FormatFlags::kMachineCode | asmjit::FormatFlags::kExplainImms |
        asmjit::FormatFlags::kRegCasts    | asmjit::FormatFlags::kHexImms     |
        asmjit::FormatFlags::kHexOffsets  | asmjit::FormatFlags::kPositions;
    asm_log.add_flags(format_flags);
    code.set_logger(&asm_log);

    // The trampoline IS a lua_CFunction: int (*)(lua_State*).
    // Single arg: lua_State* (kUIntPtr). Return: int (kInt32).
    asmjit::FuncNode* func = cc.add_func(
        asmjit::FuncSignature(asmjit::CallConvId::kCDecl,
                              asmjit::FuncSignature::kNoVarArgs,
                              asmjit::TypeId::kInt32,
                              asmjit::TypeId::kUIntPtr));

    asmjit::x86::Gp lua_state_reg = cc.new_gp_ptr("L");
    func->set_arg(0, lua_state_reg);

    // For each declared param, invoke a lua_toXXX function to pull
    // it off the Lua stack at index (arg_index + 1).
    std::vector<asmjit::Reg> arg_registers;
    for (uint8_t arg_index = 0; arg_index < target_sig.arg_count(); arg_index++) {
        const auto& arg_type_info = param_types[arg_index];

        asmjit::Reg arg;

        if (arg_type_info.m_val == kcdx::rom::type_info_t::integer_ ||
            arg_type_info.m_val == kcdx::rom::type_info_t::float_   ||
            arg_type_info.m_val == kcdx::rom::type_info_t::double_) {
            // lua_tonumberx(L, idx, isnum) → lua_Number (double)
            asmjit::InvokeNode* lua_tofunc;
            cc.invoke(asmjit::Out(lua_tofunc),
                      (uintptr_t)&lua_tonumber,
                      asmjit::FuncSignature::build<lua_Number, lua_State*, int>());
            lua_tofunc->set_arg(0, lua_state_reg);
            lua_tofunc->set_arg(1, (int)(arg_index + 1));

            auto tmp = cc.new_xmm();
            lua_tofunc->set_ret(0, tmp);

            if (arg_type_info.m_val == kcdx::rom::type_info_t::integer_) {
                // double → integer (truncating convert)
                auto gp = cc.new_gp_ptr();
                cc.cvttsd2si(gp, tmp);
                arg = gp;
            } else if (arg_type_info.m_val == kcdx::rom::type_info_t::float_) {
                // double → float (narrowing convert)
                auto narrowed = cc.new_xmm();
                cc.cvtsd2ss(narrowed, tmp);
                arg = narrowed;
            } else {
                arg = tmp;  // already double
            }
        } else if (arg_type_info.m_val == kcdx::rom::type_info_t::boolean_) {
            // lua_toboolean(L, idx) → int
            asmjit::InvokeNode* lua_tofunc;
            cc.invoke(asmjit::Out(lua_tofunc),
                      (uintptr_t)&lua_toboolean,
                      asmjit::FuncSignature::build<int, lua_State*, int>());
            lua_tofunc->set_arg(0, lua_state_reg);
            lua_tofunc->set_arg(1, (int)(arg_index + 1));

            auto gp = cc.new_gp_ptr();
            lua_tofunc->set_ret(0, gp);
            arg = gp;
        } else if (arg_type_info.m_val == kcdx::rom::type_info_t::string_) {
            // lua_tolstring(L, idx, len) → const char*. We pass NULL
            // for len (caller doesn't need the length here).
            asmjit::InvokeNode* lua_tofunc;
            cc.invoke(asmjit::Out(lua_tofunc),
                      (uintptr_t)&lua_tolstring,
                      asmjit::FuncSignature::build<const char*, lua_State*, int, size_t*>());
            lua_tofunc->set_arg(0, lua_state_reg);
            lua_tofunc->set_arg(1, (int)(arg_index + 1));
            lua_tofunc->set_arg(2, (uintptr_t)0);

            auto gp = cc.new_gp_ptr();
            lua_tofunc->set_ret(0, gp);
            arg = gp;
        } else if (arg_type_info.m_val == kcdx::rom::type_info_t::ptr_ ||
                   arg_type_info.m_custom) {
            // For pointer args, Lua side passes the address as an
            // integer (a Lua number). Pull it as a double then
            // truncating-convert to a 64-bit integer. RoM does the
            // same; it's the right thing because Lua 5.1 has no
            // integer type.
            asmjit::InvokeNode* lua_tofunc;
            cc.invoke(asmjit::Out(lua_tofunc),
                      (uintptr_t)&lua_tonumber,
                      asmjit::FuncSignature::build<lua_Number, lua_State*, int>());
            lua_tofunc->set_arg(0, lua_state_reg);
            lua_tofunc->set_arg(1, (int)(arg_index + 1));

            auto tmp = cc.new_xmm();
            lua_tofunc->set_ret(0, tmp);

            auto gp = cc.new_gp_ptr();
            cc.cvttsd2si(gp, tmp);
            arg = gp;
        } else {
            // Unsupported arg type.
            log::ErrorF("dynamic_call: unsupported arg type at index %u", arg_index);
            return 0;
        }

        arg_registers.push_back(arg);
    }

    // Call the target.
    asmjit::InvokeNode* target_invoke;
    cc.invoke(asmjit::Out(target_invoke), target_func_ptr, target_sig);
    for (uint8_t i = 0; i < target_sig.arg_count(); i++) {
        target_invoke->set_arg(i, arg_registers[i]);
    }

    // If the target returns something, push it onto the Lua stack.
    if (target_sig.has_ret()) {
        asmjit::Reg ret_reg;
        bool ret_in_gp;
        if (kcdx::rom::is_general_register(target_sig.ret())) {
            ret_reg   = cc.new_gp_ptr();
            ret_in_gp = true;
        } else if (kcdx::rom::is_XMM_register(target_sig.ret())) {
            ret_reg   = cc.new_xmm();
            ret_in_gp = false;
        } else {
            log::Error("dynamic_call: return value wider than 64 bits not supported");
            return 0;
        }
        target_invoke->set_ret(0, ret_reg);

        if (return_type.m_val == kcdx::rom::type_info_t::integer_ ||
            return_type.m_val == kcdx::rom::type_info_t::float_   ||
            return_type.m_val == kcdx::rom::type_info_t::double_) {
            // Coerce to lua_Number (double) for lua_pushnumber.
            asmjit::x86::Vec push_reg;
            if (ret_in_gp) {
                // int → double
                auto tmp = cc.new_xmm();
                cc.cvtsi2sd(tmp, ret_reg.as<asmjit::x86::Gp>());
                push_reg = tmp;
            } else if (target_sig.ret() == asmjit::TypeId::kFloat32) {
                // float → double
                cc.cvtss2sd(ret_reg.as<asmjit::x86::Vec>(), ret_reg.as<asmjit::x86::Vec>());
                push_reg = ret_reg.as<asmjit::x86::Vec>();
            } else {
                push_reg = ret_reg.as<asmjit::x86::Vec>();
            }
            asmjit::InvokeNode* lua_pushfunc;
            cc.invoke(asmjit::Out(lua_pushfunc),
                      (uintptr_t)&lua_pushnumber,
                      asmjit::FuncSignature::build<void, lua_State*, lua_Number>());
            lua_pushfunc->set_arg(0, lua_state_reg);
            lua_pushfunc->set_arg(1, push_reg);
        } else if (return_type.m_val == kcdx::rom::type_info_t::boolean_) {
            asmjit::InvokeNode* lua_pushfunc;
            cc.invoke(asmjit::Out(lua_pushfunc),
                      (uintptr_t)&lua_pushboolean,
                      asmjit::FuncSignature::build<void, lua_State*, int>());
            lua_pushfunc->set_arg(0, lua_state_reg);
            lua_pushfunc->set_arg(1, ret_reg.as<asmjit::x86::Gp>());
        } else if (return_type.m_val == kcdx::rom::type_info_t::string_) {
            asmjit::InvokeNode* lua_pushfunc;
            cc.invoke(asmjit::Out(lua_pushfunc),
                      (uintptr_t)&lua_pushstring,
                      asmjit::FuncSignature::build<const char*, lua_State*, const char*>());
            lua_pushfunc->set_arg(0, lua_state_reg);
            lua_pushfunc->set_arg(1, ret_reg.as<asmjit::x86::Gp>());
        } else if (return_type.m_val == kcdx::rom::type_info_t::ptr_) {
            // Pointer: push as integer (lua_Number). cvtsi2sd then lua_pushnumber.
            auto tmp = cc.new_xmm();
            cc.cvtsi2sd(tmp, ret_reg.as<asmjit::x86::Gp>());
            asmjit::InvokeNode* lua_pushfunc;
            cc.invoke(asmjit::Out(lua_pushfunc),
                      (uintptr_t)&lua_pushnumber,
                      asmjit::FuncSignature::build<void, lua_State*, lua_Number>());
            lua_pushfunc->set_arg(0, lua_state_reg);
            lua_pushfunc->set_arg(1, tmp);
        }
    }

    // lua_CFunction returns int = number of Lua values pushed.
    asmjit::x86::Gp ret_count = cc.new_gp_ptr();
    cc.mov(ret_count, target_sig.has_ret() ? 1 : 0);
    cc.ret(ret_count);

    cc.end_func();
    cc.finalize();

    code.flatten();
    size_t size = code.code_size();

    void* jit_buffer = kcdx::trampoline::AllocateBranch(/*owner=*/0, size);
    if (!jit_buffer) {
        log::Error("dynamic_call: branch_pool allocation failed");
        return 0;
    }

    if (code.has_unresolved_fixups()) {
        code.resolve_cross_section_fixups();
    }
    code.relocate_to_base((uintptr_t)jit_buffer);
    code.copy_flattened_data(jit_buffer, size);

    log::DebugF("dynamic_call: JIT stub at 0x%p (%zu bytes), asm:\n%s",
                jit_buffer, size, asm_log.data());

    return (uintptr_t)jit_buffer;
}

// --- handle metatable -----------------------------------------------------

int Handle_Call(lua_State* L) {
    // __call passes the userdata as arg 1 and the args as 2..N. The
    // JIT trampoline expects the user args at indices 1..N-1, so we
    // remove the userdata at index 1 before tail-calling.
    auto* ud = static_cast<CallHandleUd*>(luaL_checkudata(L, 1, kHandleMetatable));
    if (!ud->fn) {
        return luaL_error(L, "kcdx.memory.dynamic_call handle: trampoline is null");
    }
    lua_remove(L, 1);
    return ud->fn(L);
}

int Handle_Gc(lua_State* L) {
    auto* ud = static_cast<CallHandleUd*>(luaL_checkudata(L, 1, kHandleMetatable));
    ud->~CallHandleUd();
    // The JIT buffer stays in branch_pool (alloc-only).
    return 0;
}

}  // namespace

void PushHandleMetatable(lua_State* L) {
    if (luaL_newmetatable(L, kHandleMetatable) == 0) {
        return;
    }
    lua_pushcfunction(L, Handle_Call);
    lua_setfield(L, -2, "__call");
    lua_pushcfunction(L, Handle_Gc);
    lua_setfield(L, -2, "__gc");
    lua_pushstring(L, kHandleMetatable);
    lua_setfield(L, -2, "__metatable");
}

// kcdx.memory.dynamic_call(table) -> callable handle or (nil, errmsg)
int Lua_DynamicCall(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    // target: pointer userdata or integer
    uintptr_t target_addr = 0;
    {
        lua_getfield(L, 1, "target");
        if (lua_isnumber(L, -1)) {
            target_addr = (uintptr_t)lua_tointeger(L, -1);
        } else if (lua_isuserdata(L, -1)) {
            lua_getmetatable(L, -1);
            luaL_getmetatable(L, kcdx::lua_memory::kPointerMetatable);
            if (lua_rawequal(L, -1, -2)) {
                lua_pop(L, 2);
                auto* p = static_cast<kcdx::lua_memory::pointer*>(lua_touserdata(L, -1));
                target_addr = p->get_address();
            } else {
                lua_pop(L, 2);
            }
        }
        lua_pop(L, 1);
    }
    if (!target_addr) {
        lua_pushnil(L);
        lua_pushliteral(L, "kcdx.memory.dynamic_call: 'target' must be a "
                           "pointer userdata or integer VA");
        return 2;
    }

    // return_type: string, default "void"
    std::string return_type_str = "void";
    {
        lua_getfield(L, 1, "return_type");
        if (lua_isstring(L, -1)) return_type_str = lua_tostring(L, -1);
        lua_pop(L, 1);
    }

    // param_types: list-of-strings table, default empty
    std::vector<std::string> param_types_strings;
    {
        lua_getfield(L, 1, "param_types");
        if (lua_istable(L, -1)) {
            int idx = 1;
            while (true) {
                lua_rawgeti(L, -1, idx);
                if (!lua_isstring(L, -1)) { lua_pop(L, 1); break; }
                param_types_strings.emplace_back(lua_tostring(L, -1));
                lua_pop(L, 1);
                idx++;
            }
        }
        lua_pop(L, 1);
    }

    // Build the asmjit signature for the target function. Use the
    // host calling convention; KCD2 is x86_64 Windows so this resolves
    // to MS x64.
    asmjit::FuncSignature target_sig(
        asmjit::CallConvId::kCDecl,
        asmjit::FuncSignature::kNoVarArgs,
        kcdx::rom::get_type_id(return_type_str));

    std::vector<kcdx::rom::type_info_t> param_types;
    for (const auto& s : param_types_strings) {
        target_sig.add_arg(kcdx::rom::get_type_id(s));
        param_types.push_back(kcdx::rom::get_type_info_from_string(s));
    }

    uintptr_t jit_addr = JitTrampoline(
        target_addr, target_sig, asmjit::Arch::kX64,
        param_types, kcdx::rom::get_type_info_from_string(return_type_str));
    if (!jit_addr) {
        lua_pushnil(L);
        lua_pushliteral(L, "kcdx.memory.dynamic_call: trampoline JIT failed "
                           "(see kcdx.log)");
        return 2;
    }

    // Wrap the trampoline as a userdata with __call. The trampoline is
    // a valid lua_CFunction (we asked asmjit to emit one).
    auto* ud = static_cast<CallHandleUd*>(lua_newuserdata(L, sizeof(CallHandleUd)));
    new (ud) CallHandleUd{reinterpret_cast<lua_CFunction>(jit_addr)};
    luaL_getmetatable(L, kHandleMetatable);
    lua_setmetatable(L, -2);

    log::InfoF("kcdx.memory.dynamic_call: jitted trampoline at 0x%p for target 0x%p",
               (void*)jit_addr, (void*)target_addr);
    return 1;
}

}  // namespace kcdx::lua_bind_dynamic_call
