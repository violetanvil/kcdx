// kcdx.memory.dynamic_hook — runtime hook installation from Lua.
//
// Lua surface (Phase 5c.7b.2):
//
//   local handle = kcdx.memory.dynamic_hook({
//       name          = "my_hook",            -- string, required (logs + first-wins)
//       target        = some_pointer_or_int,  -- VA of target function
//       return_type   = "i32",                -- string, optional (default "void")
//       param_types   = {"i32", "ptr"},       -- list of strings, optional (default {})
//       pre_callback  = function(retval, ...) ... end,  -- optional
//       post_callback = function(retval, ...) ... end,  -- optional
//   })
//
// Returns: handle userdata (kcdx.memory.dynamic_hook_handle) on success,
//          nil + error string on failure.
//
// The handle's job is to keep the underlying runtime_func_t alive for the
// session. Hooks installed via this API do NOT auto-uninstall on handle
// __gc (matches RoM / SKSE: alloc-only, hooks live for the session). To
// disable a hook the plugin should leave it in place — the cost is zero.

#include <cstdint>
#include <cstdio>
#include <new>
#include <string>
#include <vector>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include <asmjit/asmjit.h>

#include "hook_engine.h"
#include "log.h"
#include "lua_bind_helpers.h"
#include "lua_memory.h"
#include "rom_borrowed/runtime_func_t.h"
#include "scripting.h"

namespace kcdx::lua_bind_dynamic_hook {

namespace {

constexpr const char* kHandleMetatable = "kcdx.memory.dynamic_hook_handle";

// Userdata payload. Holds a unique_ptr to the runtime_func_t so the JIT
// state, asmjit code buffer, and MinHook bookkeeping stay alive for as
// long as the Lua handle exists. __gc destroys the runtime_func_t which
// triggers its dtor (disables the hook via detour_hook::disable).
//
// Note: in practice plugins keep the handle in a persistent table so it
// never gets GC'd. If they drop it, the hook's MinHook trampoline AND
// the JIT'd detour code stay in branch_pool (alloc-only) even though
// the hook gets disabled. That's a small leak per session, acceptable.
struct HandleUd {
    std::unique_ptr<kcdx::rom::runtime_func_t> rf;
    uintptr_t target_addr = 0;
};

// Pull a string field from arg-table at index 1. Returns default_value
// (which may be empty string) when the field is missing/wrong type.
std::string GetStringField(lua_State* L, const char* key, const char* default_value) {
    lua_getfield(L, 1, key);
    std::string out = default_value ? default_value : "";
    if (lua_isstring(L, -1)) out = lua_tostring(L, -1);
    lua_pop(L, 1);
    return out;
}

// Pull a list-of-strings table field. Returns empty vector when missing.
std::vector<std::string> GetStringListField(lua_State* L, const char* key) {
    std::vector<std::string> out;
    lua_getfield(L, 1, key);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return out;
    }
    // Walk 1..N (Lua-style array). Stop at first non-string entry.
    int idx = 1;
    while (true) {
        lua_rawgeti(L, -1, idx);
        if (!lua_isstring(L, -1)) {
            lua_pop(L, 1);
            break;
        }
        out.emplace_back(lua_tostring(L, -1));
        lua_pop(L, 1);
        idx++;
    }
    lua_pop(L, 1);  // pop the field table itself
    return out;
}

// Pull target as either a pointer userdata or an integer VA.
// Returns 0 if neither / invalid.
uintptr_t GetTargetField(lua_State* L) {
    lua_getfield(L, 1, "target");
    uintptr_t addr = 0;
    if (lua_isnumber(L, -1)) {
        addr = (uintptr_t)lua_tointeger(L, -1);
    } else if (lua_isuserdata(L, -1)) {
        // Check it's a kcdx.memory.pointer
        lua_getmetatable(L, -1);
        luaL_getmetatable(L, kcdx::lua_memory::kPointerMetatable);
        if (lua_rawequal(L, -1, -2)) {
            // Pop the two metatables, leave the userdata so we can read it.
            lua_pop(L, 2);
            auto* p = static_cast<kcdx::lua_memory::pointer*>(lua_touserdata(L, -1));
            addr = p->get_address();
        } else {
            lua_pop(L, 2);
        }
    }
    lua_pop(L, 1);
    return addr;
}

int Handle_Gc(lua_State* L) {
    auto* ud = static_cast<HandleUd*>(
        luaL_checkudata(L, 1, kHandleMetatable));
    // Unregister callbacks (frees Lua-registry refs back to GC).
    if (ud->target_addr) {
        kcdx::scripting::clear_callbacks(ud->target_addr);
        kcdx::scripting::unregister_hook(ud->target_addr);
    }
    // Destruct in-place; runtime_func_t::~runtime_func_t calls
    // m_detour->disable() to MH_RemoveHook the MinHook entry.
    ud->~HandleUd();
    return 0;
}

// Push the handle's stored target VA — handy for diagnostics.
int Handle_GetTarget(lua_State* L) {
    auto* ud = static_cast<HandleUd*>(
        luaL_checkudata(L, 1, kHandleMetatable));
    lua_pushinteger(L, (lua_Integer)ud->target_addr);
    return 1;
}

}  // namespace

// Public: lua_bind_helpers::RegisterMetatables calls this once at startup
// (via the same mechanism that registers pointer + value_wrapper metatables).
void PushHandleMetatable(lua_State* L) {
    if (luaL_newmetatable(L, kHandleMetatable) == 0) {
        return;  // already registered; mt left on stack
    }
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, Handle_Gc);
    lua_setfield(L, -2, "__gc");
    lua_pushstring(L, kHandleMetatable);
    lua_setfield(L, -2, "__metatable");
    lua_pushcfunction(L, Handle_GetTarget);
    lua_setfield(L, -2, "get_target");
}

// kcdx.memory.dynamic_hook(table) -> handle userdata or (nil, errmsg)
int Lua_DynamicHook(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    std::string name = GetStringField(L, "name", "");
    if (name.empty()) {
        lua_pushnil(L);
        lua_pushliteral(L, "kcdx.memory.dynamic_hook: 'name' is required");
        return 2;
    }

    uintptr_t target_addr = GetTargetField(L);
    if (!target_addr) {
        lua_pushnil(L);
        lua_pushliteral(L, "kcdx.memory.dynamic_hook: 'target' must be a "
                           "pointer userdata or integer VA");
        return 2;
    }

    std::string return_type = GetStringField(L, "return_type", "void");
    std::vector<std::string> param_types = GetStringListField(L, "param_types");

    // At least one callback must be present.
    lua_getfield(L, 1, "pre_callback");
    bool has_pre  = lua_isfunction(L, -1);
    int pre_idx   = lua_gettop(L);
    lua_getfield(L, 1, "post_callback");
    bool has_post = lua_isfunction(L, -1);
    int post_idx  = lua_gettop(L);
    if (!has_pre && !has_post) {
        lua_pop(L, 2);
        lua_pushnil(L);
        lua_pushliteral(L, "kcdx.memory.dynamic_hook: at least one of "
                           "'pre_callback' / 'post_callback' must be a function");
        return 2;
    }

    // Build the runtime_func_t. Allocated on the heap because we hand
    // its lifetime to the Lua userdata.
    auto rf = std::make_unique<kcdx::rom::runtime_func_t>();

    // JIT a trampoline. make_jit_func now routes through branch_pool
    // (Phase 5c.7b.1) so the resulting address is within ±2 GB of
    // target_addr — MinHook's 5-byte rel32 jmp can reach.
    uintptr_t jit_addr = rf->make_jit_func(
        return_type,
        param_types,
        asmjit::Arch::kX64,
        &kcdx::scripting::dynamic_hook_pre,
        &kcdx::scripting::dynamic_hook_post,
        target_addr);
    if (!jit_addr) {
        lua_pop(L, 2);  // pre, post
        lua_pushnil(L);
        lua_pushfstring(L, "kcdx.memory.dynamic_hook '%s': make_jit_func "
                           "failed (signature error or branch_pool exhausted; "
                           "see kcdx.log)", name.c_str());
        return 2;
    }

    // Install via hook_engine — same conflict matrix + first-wins as TOML hooks.
    auto install = kcdx::hook_engine::InstallRuntime(name, target_addr, (void*)jit_addr);
    if (!install.ok) {
        lua_pop(L, 2);  // pre, post
        lua_pushnil(L);
        lua_pushfstring(L, "kcdx.memory.dynamic_hook '%s': %s",
                        name.c_str(), install.reason.c_str());
        return 2;
    }

    // Register with scripting so dispatchers can find this hook by target_addr.
    kcdx::rom::runtime_func_t* rf_raw = rf.get();
    kcdx::scripting::register_hook(target_addr, rf_raw);

    // Register the Lua callbacks. Each register_*_from_top consumes the
    // function from the stack top. We push a *copy* (via lua_pushvalue)
    // and let register consume it; the original at pre_idx/post_idx
    // stays parked on the stack and gets popped at the end.
    if (has_pre) {
        lua_pushvalue(L, pre_idx);
        kcdx::scripting::register_pre_callback_from_top(target_addr);
    }
    if (has_post) {
        lua_pushvalue(L, post_idx);
        kcdx::scripting::register_post_callback_from_top(target_addr);
    }
    lua_pop(L, 2);  // pre, post originals

    // Build the handle userdata. Transfers ownership of rf.
    auto* ud = static_cast<HandleUd*>(lua_newuserdata(L, sizeof(HandleUd)));
    new (ud) HandleUd{std::move(rf), target_addr};
    luaL_getmetatable(L, kHandleMetatable);
    lua_setmetatable(L, -2);

    log::InfoF("kcdx.memory.dynamic_hook '%s': installed at 0x%p (JIT detour 0x%p)",
               name.c_str(), (void*)target_addr, (void*)jit_addr);
    return 1;
}

}  // namespace kcdx::lua_bind_dynamic_hook
