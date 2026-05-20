// See scripting.h for what this is.
#include "scripting.h"

#include <mutex>
#include <unordered_map>
#include <vector>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include "crash_guard.h"
#include "dev.h"
#include "log.h"
#include "lua_memory.h"  // for to_lua / to_lua_return

namespace kcdx::scripting {

namespace {

// All state guarded by a single recursive_mutex. Recursive because a
// dispatched Lua callback may itself trigger another hook, which
// re-enters the dispatch path. Non-recursive lock would deadlock.
std::recursive_mutex g_lock;

lua_State* g_lua_state = nullptr;

// Phase 5g: thread-local re-entrancy guard. If a hook callback calls
// (transitively) into a function we've also hooked — e.g., hooked
// lua_pcall and the callback uses System.LogAlways which internally
// runs through pcall — without this guard we'd recurse infinitely
// and stack-overflow. RAII flag flips for the duration of dispatch.
//
// thread_local so concurrent hook fires on different threads each get
// their own guard (engine update thread vs. e.g. audio worker that
// happened to enter Lua somehow).
thread_local bool t_in_dispatch = false;

struct DispatchGuard {
    bool was_set = false;
    DispatchGuard() {
        was_set = t_in_dispatch;
        t_in_dispatch = true;
    }
    ~DispatchGuard() {
        t_in_dispatch = was_set;
    }
    // true if this guard is the outermost (we're entering dispatch fresh).
    // false if we're recursing — caller should skip the dispatch.
    bool is_outermost() const { return !was_set; }
};

// Non-owning. Lifetime of runtime_func_t lives in the [[hook]]
// installer (or, eventually, Lua-side memory.dynamic_hook userdata).
std::unordered_map<uintptr_t, kcdx::rom::runtime_func_t*> g_target_to_hook;

// A callback is either a baked Lua-registry ref OR a dotted name we
// resolve lazily at dispatch. Phase 5c-style "register_*_from_top"
// produces refs; Phase 5f TOML "lua_callback = 'Foo.Bar'" produces
// names. Both fire from the same dispatcher loop.
struct CallbackEntry {
    int         ref  = LUA_NOREF;   // -1 when name-only
    std::string name;               // empty when ref-only
};

std::unordered_map<uintptr_t, std::vector<CallbackEntry>> g_pre_callbacks;
std::unordered_map<uintptr_t, std::vector<CallbackEntry>> g_post_callbacks;
std::unordered_map<uintptr_t, std::vector<CallbackEntry>> g_mid_callbacks;

// Pop the value at the top of `L`, store it via luaL_ref, append a
// ref-only CallbackEntry to the per-target vector. Stack effect: -1.
void StoreTopRef(lua_State*                                                L,
                 std::unordered_map<uintptr_t,
                                    std::vector<CallbackEntry>>&            tbl,
                 uintptr_t                                                  target_func_ptr) {
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    tbl[target_func_ptr].push_back(CallbackEntry{ref, ""});
}

// Append a name-only CallbackEntry. Resolution deferred to dispatch.
void StoreName(std::unordered_map<uintptr_t,
                                  std::vector<CallbackEntry>>& tbl,
               uintptr_t                                       target_func_ptr,
               const std::string&                              name) {
    tbl[target_func_ptr].push_back(CallbackEntry{LUA_NOREF, name});
}

// Resolve a dotted name like "Foo.Bar.Baz" against _G. Pushes the
// resolved function onto the Lua stack on success (stack effect +1)
// and returns true. On failure (any step is nil or not a table/function),
// leaves the stack unchanged and returns false.
bool PushResolvedDotted(lua_State* L, const std::string& dotted) {
    int start_top = lua_gettop(L);
    lua_pushvalue(L, LUA_GLOBALSINDEX);  // start with _G on top
    size_t pos = 0;
    while (pos <= dotted.size()) {
        size_t dot = dotted.find('.', pos);
        std::string segment = dotted.substr(pos, dot - pos);
        if (segment.empty()) {
            lua_settop(L, start_top);
            return false;
        }
        lua_getfield(L, -1, segment.c_str());
        lua_remove(L, -2);  // remove parent
        if (lua_isnil(L, -1)) {
            lua_settop(L, start_top);
            return false;
        }
        if (dot == std::string::npos) break;
        pos = dot + 1;
    }
    if (!lua_isfunction(L, -1)) {
        lua_settop(L, start_top);
        return false;
    }
    return true;
}

// Push the callback's function onto the Lua stack. For ref-backed
// entries, lua_rawgeti from REGISTRYINDEX. For name-backed entries,
// resolve the dotted path against _G — but cache: once a name resolves
// successfully, luaL_ref it and rewrite the entry to a ref-backed
// form. On failure, return false (stack unchanged).
bool PushCallback(lua_State* L, CallbackEntry& cb) {
    if (cb.ref != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, cb.ref);
        return true;
    }
    // Name lookup
    if (!PushResolvedDotted(L, cb.name)) return false;
    // Cache: ref the resolved function so future dispatches skip the
    // dotted walk. lua_pushvalue keeps a copy on the stack so we can
    // ref one and return the other.
    lua_pushvalue(L, -1);
    int new_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    cb.ref  = new_ref;
    cb.name.clear();  // don't keep stale name once cached
    return true;
}

}  // namespace

void set_lua_state(lua_State* L) {
    std::scoped_lock guard(g_lock);
    g_lua_state = L;
    if (L) {
        log::Info("scripting: lua_State bound");
    } else {
        log::Info("scripting: lua_State cleared");
    }
}

lua_State* lua_state() {
    std::scoped_lock guard(g_lock);
    return g_lua_state;
}

void register_hook(uintptr_t target_func_ptr, kcdx::rom::runtime_func_t* hook) {
    std::scoped_lock guard(g_lock);
    g_target_to_hook[target_func_ptr] = hook;
}

void unregister_hook(uintptr_t target_func_ptr) {
    std::scoped_lock guard(g_lock);
    g_target_to_hook.erase(target_func_ptr);
    clear_callbacks(target_func_ptr);
}

kcdx::rom::runtime_func_t* get_existing_dynamic_hook(uintptr_t target_func_ptr) {
    std::scoped_lock guard(g_lock);
    const auto it = g_target_to_hook.find(target_func_ptr);
    return it == g_target_to_hook.end() ? nullptr : it->second;
}

void register_pre_callback_from_top(uintptr_t target_func_ptr) {
    std::scoped_lock guard(g_lock);
    if (!g_lua_state) {
        log::Warn("scripting: register_pre_callback_from_top before lua_State bound");
        return;
    }
    StoreTopRef(g_lua_state, g_pre_callbacks, target_func_ptr);
}

void register_post_callback_from_top(uintptr_t target_func_ptr) {
    std::scoped_lock guard(g_lock);
    if (!g_lua_state) {
        log::Warn("scripting: register_post_callback_from_top before lua_State bound");
        return;
    }
    StoreTopRef(g_lua_state, g_post_callbacks, target_func_ptr);
}

void register_mid_callback_from_top(uintptr_t target_func_ptr) {
    std::scoped_lock guard(g_lock);
    if (!g_lua_state) {
        log::Warn("scripting: register_mid_callback_from_top before lua_State bound");
        return;
    }
    StoreTopRef(g_lua_state, g_mid_callbacks, target_func_ptr);
}

void register_pre_callback_by_name(uintptr_t target_func_ptr, const std::string& name) {
    std::scoped_lock guard(g_lock);
    StoreName(g_pre_callbacks, target_func_ptr, name);
}

void register_post_callback_by_name(uintptr_t target_func_ptr, const std::string& name) {
    std::scoped_lock guard(g_lock);
    StoreName(g_post_callbacks, target_func_ptr, name);
}

void register_mid_callback_by_name(uintptr_t target_func_ptr, const std::string& name) {
    std::scoped_lock guard(g_lock);
    StoreName(g_mid_callbacks, target_func_ptr, name);
}

void clear_callbacks(uintptr_t target_func_ptr) {
    std::scoped_lock guard(g_lock);
    if (!g_lua_state) {
        // Nothing to unref against; just drop the entries.
        g_pre_callbacks.erase(target_func_ptr);
        g_post_callbacks.erase(target_func_ptr);
        g_mid_callbacks.erase(target_func_ptr);
        return;
    }
    auto unref_all = [&](std::unordered_map<uintptr_t,
                                            std::vector<CallbackEntry>>& tbl) {
        auto it = tbl.find(target_func_ptr);
        if (it == tbl.end()) return;
        for (const CallbackEntry& cb : it->second) {
            if (cb.ref != LUA_NOREF) {
                luaL_unref(g_lua_state, LUA_REGISTRYINDEX, cb.ref);
            }
        }
        tbl.erase(it);
    };
    unref_all(g_pre_callbacks);
    unref_all(g_post_callbacks);
    unref_all(g_mid_callbacks);
}

// --- dispatchers ----------------------------------------------------------
//
// All three dispatchers follow the same shape:
//   1. Take the lock, look up callbacks for this target.
//   2. If none, return the default ("call original" / "no restore addr").
//   3. For each registered ref:
//        a. Push the Lua function via lua_rawgeti(L, LUA_REGISTRYINDEX, ref).
//        b. Push the args (return_value first for pre/post, args as table for mid).
//        c. lua_pcall.
//        d. Inspect the result, update accumulator.
//   4. Return the accumulated decision.
//
// pre/post pass: (return_value, ...args)
// mid pass:      (table of args)

bool dynamic_hook_pre(const kcdx::rom::runtime_func_t::parameters_t* params,
                      uint8_t                                        param_count,
                      kcdx::rom::runtime_func_t::return_value_t*     return_value,
                      uintptr_t                                      target_func_ptr) {
    kcdx::guard::BreadcrumbScope bc("scripting.dispatch_pre", nullptr);
    KCDX_DEV("SCRIPTING", "DISPATCH/pre/enter",
        kcdx::dev::KV("target",      (void*)target_func_ptr),
        kcdx::dev::KV("param_count", (unsigned)param_count));
    DispatchGuard re_entry;
    if (!re_entry.is_outermost()) {
        KCDX_DEV("SCRIPTING", "DISPATCH/pre/reentry-skip",
            kcdx::dev::KV("target", (void*)target_func_ptr));
        return true;
    }
    std::scoped_lock guard(g_lock);
    if (!g_lua_state) {
        KCDX_DEV("SCRIPTING", "DISPATCH/pre/no-state",
            kcdx::dev::KV("target", (void*)target_func_ptr));
        return true;
    }

    auto it = g_pre_callbacks.find(target_func_ptr);
    if (it == g_pre_callbacks.end() || it->second.empty()) {
        KCDX_DEV("SCRIPTING", "DISPATCH/pre/no-callbacks",
            kcdx::dev::KV("target",            (void*)target_func_ptr),
            kcdx::dev::KV("registered_targets", (unsigned long long)g_pre_callbacks.size()));
        return true;
    }
    KCDX_DEV("SCRIPTING", "DISPATCH/pre/found",
        kcdx::dev::KV("target",         (void*)target_func_ptr),
        kcdx::dev::KV("callback_count", (unsigned long long)it->second.size()));

    auto hook_it = g_target_to_hook.find(target_func_ptr);
    const kcdx::rom::runtime_func_t* hook =
        (hook_it == g_target_to_hook.end()) ? nullptr : hook_it->second;
    if (!hook) return true;

    bool call_orig = true;
    for (CallbackEntry& cb : it->second) {
        // [func]  (or skip if name failed to resolve)
        if (!PushCallback(g_lua_state, cb)) {
            log::WarnF("scripting: pre-callback for target 0x%p: '%s' "
                       "unresolved at dispatch time; original runs",
                       (void*)target_func_ptr, cb.name.c_str());
            continue;
        }
        // [func, return_value]
        kcdx::lua_memory::to_lua_return(g_lua_state, return_value, hook->m_return_type);
        // [func, return_value, args...]
        for (uint8_t i = 0; i < param_count; i++) {
            kcdx::lua_memory::to_lua(g_lua_state, params, i, hook->m_param_types);
        }
        // 1 + param_count args, expecting 1 return
        int n_args = 1 + (int)param_count;
        LOG_INFO("SCRIPTING",
            "  before lua_pcall pre target=0x%p cb='%s' nargs=%d",
            (void*)target_func_ptr, cb.name.c_str(), n_args);
        int status = lua_pcall(g_lua_state, n_args, 1, 0);
        LOG_INFO("SCRIPTING",
            "  after  lua_pcall pre target=0x%p cb='%s' status=%d",
            (void*)target_func_ptr, cb.name.c_str(), status);
        if (status != 0) {
            const char* err = lua_tostring(g_lua_state, -1);
            LOG_ERROR("SCRIPTING",
                "pre-callback for target 0x%p ('%s') threw: %s",
                (void*)target_func_ptr, cb.name.c_str(),
                err ? err : "<no message>");
            lua_pop(g_lua_state, 1);
            continue;
        }
        // Any callback returning literal false suppresses the original.
        if (lua_isboolean(g_lua_state, -1) && lua_toboolean(g_lua_state, -1) == 0) {
            call_orig = false;
        }
        lua_pop(g_lua_state, 1);
    }
    return call_orig;
}

void dynamic_hook_post(const kcdx::rom::runtime_func_t::parameters_t* params,
                       uint8_t                                        param_count,
                       kcdx::rom::runtime_func_t::return_value_t*     return_value,
                       uintptr_t                                      target_func_ptr) {
    kcdx::guard::BreadcrumbScope bc("scripting.dispatch_post", nullptr);
    KCDX_DEV("SCRIPTING", "DISPATCH/post/enter",
        kcdx::dev::KV("target", (void*)target_func_ptr));
    DispatchGuard re_entry;
    if (!re_entry.is_outermost()) return;
    std::scoped_lock guard(g_lock);
    if (!g_lua_state) return;

    auto it = g_post_callbacks.find(target_func_ptr);
    if (it == g_post_callbacks.end() || it->second.empty()) return;

    auto hook_it = g_target_to_hook.find(target_func_ptr);
    const kcdx::rom::runtime_func_t* hook =
        (hook_it == g_target_to_hook.end()) ? nullptr : hook_it->second;
    if (!hook) return;

    for (CallbackEntry& cb : it->second) {
        if (!PushCallback(g_lua_state, cb)) {
            log::WarnF("scripting: post-callback for target 0x%p: '%s' "
                       "unresolved at dispatch time", (void*)target_func_ptr,
                       cb.name.c_str());
            continue;
        }
        kcdx::lua_memory::to_lua_return(g_lua_state, return_value, hook->m_return_type);
        for (uint8_t i = 0; i < param_count; i++) {
            kcdx::lua_memory::to_lua(g_lua_state, params, i, hook->m_param_types);
        }
        int n_args = 1 + (int)param_count;
        LOG_INFO("SCRIPTING",
            "  before lua_pcall post target=0x%p cb='%s' nargs=%d",
            (void*)target_func_ptr, cb.name.c_str(), n_args);
        int status = lua_pcall(g_lua_state, n_args, 0, 0);
        LOG_INFO("SCRIPTING",
            "  after  lua_pcall post target=0x%p cb='%s' status=%d",
            (void*)target_func_ptr, cb.name.c_str(), status);
        if (status != 0) {
            const char* err = lua_tostring(g_lua_state, -1);
            LOG_ERROR("SCRIPTING",
                "post-callback for target 0x%p ('%s') threw: %s",
                (void*)target_func_ptr, cb.name.c_str(),
                err ? err : "<no message>");
            lua_pop(g_lua_state, 1);
        }
    }
}

uintptr_t dynamic_hook_mid(const kcdx::rom::runtime_func_t::parameters_t* params,
                           size_t                                         param_count,
                           uintptr_t                                      target_func_ptr) {
    kcdx::guard::BreadcrumbScope bc("scripting.dispatch_mid", nullptr);
    KCDX_DEV("SCRIPTING", "DISPATCH/mid/enter",
        kcdx::dev::KV("target", (void*)target_func_ptr));
    DispatchGuard re_entry;
    if (!re_entry.is_outermost()) return 0;
    std::scoped_lock guard(g_lock);
    if (!g_lua_state) return 0;

    auto it = g_mid_callbacks.find(target_func_ptr);
    if (it == g_mid_callbacks.end() || it->second.empty()) return 0;

    auto hook_it = g_target_to_hook.find(target_func_ptr);
    const kcdx::rom::runtime_func_t* hook =
        (hook_it == g_target_to_hook.end()) ? nullptr : hook_it->second;
    if (!hook) return 0;

    // Mid-hooks pass args as a table keyed by 1..N (matches RoM).
    uintptr_t restore_address = 0;
    for (CallbackEntry& cb : it->second) {
        if (!PushCallback(g_lua_state, cb)) {
            log::WarnF("scripting: mid-callback for target 0x%p: '%s' "
                       "unresolved at dispatch time", (void*)target_func_ptr,
                       cb.name.c_str());
            continue;
        }
        lua_createtable(g_lua_state, (int)param_count, 0);
        int table_idx = lua_gettop(g_lua_state);
        for (uint8_t i = 0; i < param_count; i++) {
            kcdx::lua_memory::to_lua(g_lua_state, params, i, hook->m_param_types);
            lua_rawseti(g_lua_state, table_idx, i + 1);
        }
        LOG_INFO("SCRIPTING",
            "  before lua_pcall mid target=0x%p cb='%s'",
            (void*)target_func_ptr, cb.name.c_str());
        int status = lua_pcall(g_lua_state, 1, 1, 0);
        LOG_INFO("SCRIPTING",
            "  after  lua_pcall mid target=0x%p cb='%s' status=%d",
            (void*)target_func_ptr, cb.name.c_str(), status);
        if (status != 0) {
            const char* err = lua_tostring(g_lua_state, -1);
            LOG_ERROR("SCRIPTING",
                "mid-callback for target 0x%p ('%s') threw: %s",
                (void*)target_func_ptr, cb.name.c_str(),
                err ? err : "<no message>");
            lua_pop(g_lua_state, 1);
            continue;
        }
        // First non-zero numeric return wins (matches RoM).
        if (!restore_address && lua_isnumber(g_lua_state, -1)) {
            restore_address = (uintptr_t)lua_tointeger(g_lua_state, -1);
        }
        lua_pop(g_lua_state, 1);
    }
    return restore_address;
}

}  // namespace kcdx::scripting
