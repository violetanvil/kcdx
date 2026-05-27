// kcdx.memory.dynamic_call — Lua → native function invocation.
//
// Lua surface:
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
// `jit_lua_binded_func`. Adaptations:
//   - asmjit camelCase → snake_case (matches kcdx convention)
//   - JIT buffer routes through kcdx::trampoline::AllocateBranch
//     instead of new uint8_t[] + VirtualProtect
//   - Returns a userdata with __call rather than poisoning the Lua
//     global namespace with `__dynamic_call_<addr>` per RoM
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

#include "dynamic_call_jit.h"
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
}  // namespace  (close anon so JitTrampoline has namespace-scope linkage,
   //              callable from BuildLuaCallThunk later in this TU)

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
            // lua_tonumber(L, idx) → lua_Number. On this build
            // LUA_NUMBER=float (vendor/lua/luaconf.h:504), so the result
            // in `tmp` is a 32-bit FLOAT, NOT a double. Converting it as a
            // double (cvttsd2si / treating tmp as double) reinterprets the
            // float bit-pattern and yields garbage→0 — this was the cause
            // of the call_original arg arriving as 0 in an around-mode hook.
            // Read tmp AS A FLOAT, then convert to the target arg's width.
            asmjit::InvokeNode* lua_tofunc;
            cc.invoke(asmjit::Out(lua_tofunc),
                      (uintptr_t)&lua_tonumber,
                      asmjit::FuncSignature::build<lua_Number, lua_State*, int>());
            lua_tofunc->set_arg(0, lua_state_reg);
            lua_tofunc->set_arg(1, (int)(arg_index + 1));

            auto tmp = cc.new_xmm();   // holds a FLOAT (lua_Number)
            lua_tofunc->set_ret(0, tmp);

            if (arg_type_info.m_val == kcdx::rom::type_info_t::integer_) {
                // float → integer (truncating convert from single)
                auto gp = cc.new_gp_ptr();
                cc.cvttss2si(gp, tmp);
                arg = gp;
            } else if (arg_type_info.m_val == kcdx::rom::type_info_t::float_) {
                // target wants a float; tmp is already a float — use as-is.
                arg = tmp;
            } else {
                // target wants a double; widen the float lua_Number to double.
                auto widened = cc.new_xmm();
                cc.cvtss2sd(widened, tmp);
                arg = widened;
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
            // Produce a lua_Number-WIDTH value for lua_pushnumber. On this
            // build LUA_NUMBER=float (vendor/lua/luaconf.h:504), so
            // lua_pushnumber's FP arg is a 32-bit FLOAT in xmm0 — the vreg
            // we hand set_arg MUST be float-typed, or asmjit fails to wire
            // xmm0 (observed: the converted value sat in xmm1 with no move
            // to xmm0, and lua_pushnumber read garbage → pushed 0.0).
            // NOTE: float lua_Number means int/ptr returns above 2^24 lose
            // precision crossing the Lua boundary (LUA_NUMBER is float); for
            // pointer-magnitude values the PushPointer userdata path is the
            // correct surface, not lua_pushnumber.
            asmjit::x86::Vec push_reg = cc.new_xmm();
            if (ret_in_gp) {
                // int → float (single-precision; matches lua_Number)
                cc.cvtsi2ss(push_reg, ret_reg.as<asmjit::x86::Gp>());
            } else if (target_sig.ret() == asmjit::TypeId::kFloat32) {
                // already a float — use as-is (lua_Number is float)
                push_reg = ret_reg.as<asmjit::x86::Vec>();
            } else {
                // double → float (narrow to lua_Number width)
                cc.cvtsd2ss(push_reg, ret_reg.as<asmjit::x86::Vec>());
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
            // Pointer pushed via lua_pushnumber (lua_Number=float width).
            // cvtsi2ss (int→float) so the vreg matches the float arg type;
            // see the integer branch above. WARNING: a float lua_Number
            // can't represent a 48-bit pointer exactly (LUA_NUMBER is float) —
            // ptr returns through this path are lossy; PushPointer userdata
            // is the correct surface for pointer returns. Kept for parity
            // with the existing dynamic_call behavior.
            auto tmp = cc.new_xmm();
            cc.cvtsi2ss(tmp, ret_reg.as<asmjit::x86::Gp>());
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

namespace {  // reopen anon namespace for the rest of the TU-local helpers

// --- Unknown-key rejection (fail loud, never silent-drop) ---------------
//
// The recognized option-key set for kcdx.memory.dynamic_call. A typo'd
// `retrun_type=` / `param_type=` would otherwise vanish silently, the
// author's intent lost. Integer keys (the param_types array's own elements
// live in a sub-table, not here) are not checked by the shared gate. The
// iteration is the shared kcdx::lua_bind_helpers::FindUnknownKey; this list
// stays local because the key set belongs to this binder.
static const char* kKnown[] = {
    "target", "return_type", "param_types",
};

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
    LOG_INFO("LUA_BIND",
        "      dynamic_call::PushHandleMetatable ENTER (key='%s')",
        kHandleMetatable);
    if (luaL_newmetatable(L, kHandleMetatable) == 0) {
        LOG_INFO("LUA_BIND",
            "      dynamic_call::PushHandleMetatable EXIT (already registered)");
        return;
    }
    lua_pushcfunction(L, Handle_Call);
    lua_setfield(L, -2, "__call");
    lua_pushcfunction(L, Handle_Gc);
    lua_setfield(L, -2, "__gc");
    lua_pushstring(L, kHandleMetatable);
    lua_setfield(L, -2, "__metatable");
    LOG_INFO("LUA_BIND",
        "      dynamic_call::PushHandleMetatable EXIT (freshly registered)");
}

// kcdx.memory.dynamic_call(table) -> callable handle or (nil, errmsg)
int Lua_DynamicCall(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    // Reject an unrecognized option key before reading anything — a typo'd
    // key would otherwise vanish silently (fail loud, never silent-drop).
    {
        std::string bad = kcdx::lua_bind_helpers::FindUnknownKey(
            L, 1, kKnown, sizeof(kKnown) / sizeof(kKnown[0]));
        if (!bad.empty()) {
            lua_pushnil(L);
            lua_pushfstring(L,
                "kcdx.memory.dynamic_call: unrecognized option key '%s' — not "
                "a recognized option (check for a typo).",
                bad.c_str());
            return 2;
        }
    }

    // target: kcdx.memory.pointer userdata, raw lightuserdata (exact VA —
    // e.g. from kcdx.lua.cfunction_address), or integer VA.
    uintptr_t target_addr = 0;
    {
        lua_getfield(L, 1, "target");
        if (lua_islightuserdata(L, -1)) {
            target_addr = reinterpret_cast<uintptr_t>(lua_touserdata(L, -1));
        } else if (lua_isnumber(L, -1)) {
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
                           "pointer userdata, lightuserdata, or integer VA");
        return 2;
    }

    // return_type: type-name string, default "void"
    //
    // #12-followup (fail loud, never silent-drop — opposite polarity to the
    // param_types reject below): an ABSENT return_type keeps the "void"
    // default (legal, common). A PRESENT-but-non-string value must REJECT —
    // it must NOT be coerced. On this build LUA_NUMBER=float and lua_isstring
    // returns TRUE for NUMBERS (LUA_NUMBER is float), so the old
    // `if (lua_isstring) take` silently coerced `return_type = 5` to "5" →
    // get_type_id("5") resolves a garbage/default type → wrong return
    // marshaling, no signal. A non-numeric non-string fell to the silent
    // "void" default — either way the author's intent vanished. Use
    // lua_type == LUA_TSTRING (NOT lua_isstring) for the genuine-string check.
    std::string return_type_str = "void";
    {
        lua_getfield(L, 1, "return_type");
        const int t = lua_type(L, -1);
        if (t == LUA_TNIL) {
            // absent → keep the "void" default, no reject.
        } else if (t == LUA_TSTRING) {
            return_type_str = lua_tostring(L, -1);
        } else {
            const char* gotType = lua_typename(L, t);
            lua_pop(L, 1);   // the bad return_type value
            lua_pushnil(L);
            lua_pushfstring(L,
                "kcdx.memory.dynamic_call: return_type is a %s — it must be a "
                "type-name string (e.g. \"void\", \"i32\", \"ptr\"). "
                "return_type defines how the native function's return value is "
                "marshaled back to Lua; a non-string value would mis-resolve "
                "the return type.",
                gotType);
            return 2;
        }
        lua_pop(L, 1);
    }

    // param_types: list-of-strings table, default empty
    //
    // #12 (fail loud, never silent-drop): a NON-STRING entry is an ERROR, not
    // an end-of-list marker. param_types DEFINES the native ABI — silently
    // truncating at the first non-string entry builds a JIT thunk for the
    // WRONG arity and marshals wrong into a native function (a crash risk).
    // The list ends at the first NIL (Lua array convention); a present-but-
    // non-string entry (e.g. {"ptr", 5}) is rejected naming the bad index.
    std::vector<std::string> param_types_strings;
    {
        lua_getfield(L, 1, "param_types");
        if (lua_istable(L, -1)) {
            const int n = static_cast<int>(lua_objlen(L, -1));
            for (int idx = 1; idx <= n; idx++) {
                lua_rawgeti(L, -1, idx);
                if (lua_isnil(L, -1)) {
                    // End of the array part — stop cleanly.
                    lua_pop(L, 1);
                    break;
                }
                if (lua_type(L, -1) != LUA_TSTRING) {
                    const char* gotType = lua_typename(L, lua_type(L, -1));
                    lua_pop(L, 1);   // the bad entry
                    lua_pop(L, 1);   // the param_types table
                    lua_pushnil(L);
                    lua_pushfstring(L,
                        "kcdx.memory.dynamic_call: param_types[%d] is a %s — "
                        "every param_types entry must be a type-name string "
                        "(e.g. \"ptr\", \"i32\"). param_types defines the "
                        "native function's ABI; a non-string entry would "
                        "build a thunk for the wrong arity.",
                        idx, gotType);
                    return 2;
                }
                param_types_strings.emplace_back(lua_tostring(L, -1));
                lua_pop(L, 1);
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

// --- shared extraction: build a lua_CFunction call-thunk for any target ----
//
// Exposed via dynamic_call_jit.h for reuse by hook_chain's call_original
// bridge. Builds the asmjit signature from the type-string vocabulary
// then delegates to the proven JitTrampoline above (kept in this TU so
// the asmjit body lives in exactly one place — extract-on-second-use
// without duplicating 200+ lines of codegen).
namespace kcdx::dynamic_call_jit {

lua_CFunction BuildLuaCallThunk(uintptr_t                       targetVa,
                                const std::string&              returnType,
                                const std::vector<std::string>& paramTypes) {
    if (!targetVa) return nullptr;

    asmjit::FuncSignature target_sig(
        asmjit::CallConvId::kCDecl,
        asmjit::FuncSignature::kNoVarArgs,
        kcdx::rom::get_type_id(returnType));

    std::vector<kcdx::rom::type_info_t> paramTypeInfos;
    for (const auto& s : paramTypes) {
        target_sig.add_arg(kcdx::rom::get_type_id(s));
        paramTypeInfos.push_back(kcdx::rom::get_type_info_from_string(s));
    }

    uintptr_t jit = kcdx::lua_bind_dynamic_call::JitTrampoline(
        targetVa, target_sig, asmjit::Arch::kX64, paramTypeInfos,
        kcdx::rom::get_type_info_from_string(returnType));
    return reinterpret_cast<lua_CFunction>(jit);
}

}  // namespace kcdx::dynamic_call_jit
