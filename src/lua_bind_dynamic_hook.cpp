// kcdx.memory.dynamic_hook — runtime hook installation from Lua.
//
// Lua surface:
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
#include <memory>
#include <new>
#include <string>
#include <vector>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include <asmjit/asmjit.h>

#include "hook_engine.h"
#include "dev.h"
#include "log.h"
#include "lua_bind_helpers.h"
#include "lua_memory.h"
#include "rom_borrowed/runtime_func_t.h"
#include "scripting.h"

namespace kcdx::lua_bind_dynamic_hook {

namespace {

constexpr const char* kHandleMetatable = "kcdx.memory.dynamic_hook_handle";

// Userdata payload. Holds a unique_ptr to the runtime_func_t so the JIT
// state and asmjit code buffer stay alive for as long as the Lua handle
// exists. __gc destroys the runtime_func_t; the MinHook detour itself is
// owned at the install seam (hook_engine::InstallRuntime drives the backend)
// and is never torn down — kcdx hooks live for the session.
//
// Note: in practice plugins keep the handle in a persistent table so it
// never gets GC'd. If they drop it, the hook's MinHook trampoline AND
// the JIT'd detour code stay in branch_pool (alloc-only); the detour also
// stays installed (kcdx never unhooks). That's a small leak per session,
// acceptable.
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
//
// #12 (fail loud, never silent-drop): a NON-STRING entry is an ERROR, not an
// end-of-list marker. param_types DEFINES the native ABI — silently
// truncating at the first non-string entry builds a JIT thunk for the WRONG
// arity and marshals wrong into a native function (a crash risk). The list
// ends at the first NIL (Lua array convention); a present-but-non-string
// entry is reported via `err_out` (with the 1-based bad index) so the caller
// can reject with its (nil, err) idiom. On error, returns an empty vector
// and sets `err_out` non-empty; the Lua stack is left balanced.
std::vector<std::string> GetStringListField(lua_State* L, const char* key,
                                            std::string& err_out) {
    err_out.clear();
    std::vector<std::string> out;
    lua_getfield(L, 1, key);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return out;
    }
    const int n = static_cast<int>(lua_objlen(L, -1));
    for (int idx = 1; idx <= n; idx++) {
        lua_rawgeti(L, -1, idx);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);  // end of array part — stop cleanly
            break;
        }
        if (lua_type(L, -1) != LUA_TSTRING) {
            const char* gotType = lua_typename(L, lua_type(L, -1));
            lua_pop(L, 1);  // the bad entry
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                "%s[%d] is a %s — every entry must be a type-name string "
                "(e.g. \"ptr\", \"i32\"). %s defines the native function's "
                "ABI; a non-string entry would build a thunk for the wrong "
                "arity.",
                key, idx, gotType, key);
            err_out = buf;
            out.clear();
            lua_pop(L, 1);  // pop the field table itself
            return out;
        }
        out.emplace_back(lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    lua_pop(L, 1);  // pop the field table itself
    return out;
}

// --- Unknown-key rejection (fail loud, never silent-drop) ---------------
//
// The recognized option-key set for kcdx.memory.dynamic_hook. A typo'd
// `pre_calback=` / `retrun_type=` would otherwise vanish silently, the
// author's intent lost. Integer keys (the param_types array's elements live
// in a sub-table) are not checked by the shared gate. The iteration is the
// shared kcdx::lua_bind_helpers::FindUnknownKey; this list stays local
// because the key set belongs to this binder.
static const char* kKnown[] = {
    "name", "target", "return_type", "param_types",
    "pre_callback", "post_callback",
};

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
    // Destruct in-place; runtime_func_t::~runtime_func_t frees the JIT
    // state. The MinHook detour is owned at the install seam and is NOT
    // removed here — kcdx hooks live for the session.
    ud->~HandleUd();
    return 0;
}

// Push the handle's stored target VA as a kcdx.memory.pointer userdata.
//
// NOT an integer: on KCD2, CryEngine's Lua 5.1 is LUA_NUMBER=float, so
// pointer-magnitude values lose precision through the Lua stack. See
// docs/lua-number-precision.md.
int Handle_GetTarget(lua_State* L) {
    auto* ud = static_cast<HandleUd*>(
        luaL_checkudata(L, 1, kHandleMetatable));
    kcdx::lua_bind_helpers::PushPointer(
        L, kcdx::lua_memory::pointer(ud->target_addr));
    return 1;
}

}  // namespace

// Public: lua_bind_helpers::RegisterMetatables calls this once at startup
// (via the same mechanism that registers pointer + value_wrapper metatables).
void PushHandleMetatable(lua_State* L) {
    LOG_INFO("LUA_BIND",
        "      dynamic_hook::PushHandleMetatable ENTER (key='%s')",
        kHandleMetatable);
    if (luaL_newmetatable(L, kHandleMetatable) == 0) {
        LOG_INFO("LUA_BIND",
            "      dynamic_hook::PushHandleMetatable EXIT (already registered)");
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
    LOG_INFO("LUA_BIND",
        "      dynamic_hook::PushHandleMetatable EXIT (freshly registered)");
}

// kcdx.memory.dynamic_hook(table) -> handle userdata or (nil, errmsg)
int Lua_DynamicHook(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    // Reject an unrecognized option key before reading anything — a typo'd
    // key would otherwise vanish silently (fail loud, never silent-drop).
    {
        std::string bad = kcdx::lua_bind_helpers::FindUnknownKey(
            L, 1, kKnown, sizeof(kKnown) / sizeof(kKnown[0]));
        if (!bad.empty()) {
            lua_pushnil(L);
            lua_pushfstring(L,
                "kcdx.memory.dynamic_hook: unrecognized option key '%s' — not "
                "a recognized option (check for a typo).",
                bad.c_str());
            return 2;
        }
    }

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

    // return_type: type-name string, default "void".
    //
    // #12-followup (fail loud, never silent-drop — opposite polarity to the
    // param_types reject below): an ABSENT return_type keeps the "void"
    // default (legal, common). A PRESENT-but-non-string value must REJECT.
    // We read it AT THE CALLSITE rather than via the shared GetStringField
    // (which also serves `name` and whose lua_isstring permissiveness would
    // coerce `return_type = 5` to "5" — the LUA_NUMBER=float gotcha).
    // Leaving GetStringField untouched preserves `name`'s
    // existing handling (a missing name still hits its required-field reject
    // above). Use lua_type == LUA_TSTRING, NOT lua_isstring.
    std::string return_type = "void";
    {
        lua_getfield(L, 1, "return_type");
        const int t = lua_type(L, -1);
        if (t == LUA_TNIL) {
            // absent → keep the "void" default, no reject.
        } else if (t == LUA_TSTRING) {
            return_type = lua_tostring(L, -1);
        } else {
            const char* gotType = lua_typename(L, t);
            lua_pop(L, 1);   // the bad return_type value
            lua_pushnil(L);
            lua_pushfstring(L,
                "kcdx.memory.dynamic_hook '%s': return_type is a %s — it must "
                "be a type-name string (e.g. \"void\", \"i32\", \"ptr\"). "
                "return_type defines how the hooked function's return value is "
                "marshaled to the Lua callbacks; a non-string value would "
                "mis-resolve the return type.",
                name.c_str(), gotType);
            return 2;
        }
        lua_pop(L, 1);
    }

    std::string paramErr;
    std::vector<std::string> param_types =
        GetStringListField(L, "param_types", paramErr);
    if (!paramErr.empty()) {
        lua_pushnil(L);
        lua_pushfstring(L, "kcdx.memory.dynamic_hook '%s': %s",
                        name.c_str(), paramErr.c_str());
        return 2;
    }

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

    KCDX_DEV("LUA", "DYNAMIC_HOOK/request",
        kcdx::dev::KV("name",         name),
        kcdx::dev::KV("target",       (void*)target_addr),
        kcdx::dev::KV("return_type",  return_type),
        kcdx::dev::KV("param_count",  (unsigned long long)param_types.size()),
        kcdx::dev::KV("has_pre",      has_pre),
        kcdx::dev::KV("has_post",     has_post));

    // Build the runtime_func_t. Allocated on the heap because we hand
    // its lifetime to the Lua userdata.
    auto rf = std::make_unique<kcdx::rom::runtime_func_t>();

    // JIT a trampoline. make_jit_func now routes through branch_pool
    // so the resulting address is within ±2 GB of
    // target_addr — MinHook's 5-byte rel32 jmp can reach.
    uintptr_t jit_addr = rf->make_jit_func(
        return_type,
        param_types,
        asmjit::Arch::kX64,
        &kcdx::scripting::dynamic_hook_pre,
        &kcdx::scripting::dynamic_hook_post,
        target_addr);
    if (!jit_addr) {
        KCDX_DEV("LUA", "DYNAMIC_HOOK/jit-failed",
            kcdx::dev::KV("name",   name),
            kcdx::dev::KV("target", (void*)target_addr));
        lua_pop(L, 2);  // pre, post
        lua_pushnil(L);
        lua_pushfstring(L, "kcdx.memory.dynamic_hook '%s': make_jit_func "
                           "failed (signature error or branch_pool exhausted; "
                           "see kcdx.log)", name.c_str());
        return 2;
    }
    KCDX_DEV("LUA", "DYNAMIC_HOOK/jit-ok",
        kcdx::dev::KV("name",     name),
        kcdx::dev::KV("target",   (void*)target_addr),
        kcdx::dev::KV("jit_addr", (void*)jit_addr));

    // Install via hook_engine — same conflict matrix + first-wins as TOML hooks.
    // dynamic_hook is a NON-chain caller — it declares its KIND; the engine
    // selects the backend (select_backend maps DynamicHook -> MinHook, design
    // §4.2). The call site cannot name a backend, so it cannot misroute.
    auto install = kcdx::hook_engine::InstallRuntime(
        name, target_addr, (void*)jit_addr, kcdx::hook_engine::InstallKind::DynamicHook);
    if (!install.ok) {
        KCDX_DEV("LUA", "DYNAMIC_HOOK/install-failed",
            kcdx::dev::KV("name",   name),
            kcdx::dev::KV("target", (void*)target_addr),
            kcdx::dev::KV("reason", install.reason));
        lua_pop(L, 2);  // pre, post
        lua_pushnil(L);
        lua_pushfstring(L, "kcdx.memory.dynamic_hook '%s': %s",
                        name.c_str(), install.reason.c_str());
        return 2;
    }
    KCDX_DEV("LUA", "DYNAMIC_HOOK/install-ok",
        kcdx::dev::KV("name",      name),
        kcdx::dev::KV("target",    (void*)target_addr),
        kcdx::dev::KV("pOriginal", install.pOriginal));

    // Wire MinHook's pOriginal into the JIT'd trampoline's
    // call-original slot. See hook_engine.cpp::ApplyOneMidHook for
    // the long explanation; this is the same fix.
    if (void** slot = rf->get_jit_original_slot()) {
        *slot = install.pOriginal;
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
