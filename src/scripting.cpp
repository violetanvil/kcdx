// See scripting.h for what this is.
#include "scripting.h"

#include <windows.h>  // for the module-origin check: GetModuleHandleEx + GetModuleFileNameA

#include <mutex>
#include <unordered_map>
#include <vector>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lstate.h"   // for raw lua_State / global_State struct reads
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

// Thread-local re-entrancy guard. If a hook callback calls
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

// Non-owning. Lifetime of runtime_func_t lives in the kcdx.hook
// installer (or, eventually, Lua-side memory.dynamic_hook userdata).
std::unordered_map<uintptr_t, kcdx::rom::runtime_func_t*> g_target_to_hook;

// A callback is either a baked Lua-registry ref OR a dotted name we
// resolve lazily at dispatch. The "register_*_from_top" path produces
// refs; the TOML "lua_callback = 'Foo.Bar'" path produces names. Both
// fire from the same dispatcher loop.
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

// Defined here, declared in scripting.h. JIT codegen takes its address
// via get_mid_skip_flag_address(). See header for the per-invocation
// lifecycle contract.
std::atomic<uint8_t> g_mid_skip_original{0};

uint8_t* get_mid_skip_flag_address() {
    // std::atomic<uint8_t>'s storage is byte-addressable. C++20 made
    // is_always_lock_free a constexpr we can static_assert against;
    // x64 has had lock-free single-byte atomics forever, so this is
    // trivially true. The cast yields the address of the byte the
    // JIT can `mov al, [addr]` from.
    static_assert(std::atomic<uint8_t>::is_always_lock_free,
                  "atomic<uint8_t> must be lock-free for JIT byte read");
    return reinterpret_cast<uint8_t*>(&g_mid_skip_original);
}

// Diagnostic helper: format a byte buffer as a lowercase-hex space-separated
// string. Used to hex-dump fresh Tables for ABI comparison. 16 bytes
// per chunk to keep grep-friendly.
static std::string HexDump(const void* p, size_t n) {
    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(n * 3);
    const uint8_t* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) {
        out.push_back(hex[b[i] >> 4]);
        out.push_back(hex[b[i] & 0xF]);
        if (i + 1 < n) out.push_back(' ');
    }
    return out;
}

// Diagnostic: dump a Table-shaped memory region for ABI comparison.
// Reads up to `bytes` bytes from `p` and logs them under `lstate.raw.tbl`.
// VirtualQuery is used to confirm the page is committed before reading,
// to avoid a probe-induced crash if `p` is garbage.
static void LogTableBytes(const void* p, size_t bytes, const char* tag) {
    if (!p || bytes == 0) return;
    MEMORY_BASIC_INFORMATION mbi{};
    if (::VirtualQuery(p, &mbi, sizeof(mbi)) == 0 ||
        mbi.State != MEM_COMMIT ||
        (mbi.Protect & PAGE_NOACCESS)) {
        LOG_DEBUG_KV("MID_HOOK", "lstate.raw.tbl",
            log::KV("tag",  std::string(tag ? tag : "?")),
            log::KV("p",    p),
            log::KV("note", std::string("memory not readable")));
        return;
    }
    // Clamp `bytes` to the end of the committed region.
    uintptr_t end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    uintptr_t start = reinterpret_cast<uintptr_t>(p);
    size_t avail = (end > start) ? (size_t)(end - start) : 0;
    if (bytes > avail) bytes = avail;
    LOG_DEBUG_KV("MID_HOOK", "lstate.raw.tbl",
        log::KV("tag",   std::string(tag ? tag : "?")),
        log::KV("p",     p),
        log::KV("bytes", (int64_t)bytes),
        log::KV("hex",   HexDump(p, bytes)));
}

void LogLuaStateSnapshot(lua_State* L, const char* tag) {
    if (!L) {
        LOG_DEBUG_KV("MID_HOOK", "lstate.snapshot",
            log::KV("tag", std::string(tag ? tag : "?")),
            log::KV("L",   (void*)nullptr));
        return;
    }
    // lua_topointer is read-only on LUA_REGISTRYINDEX / LUA_GLOBALSINDEX.
    // For Lua 5.1 those return the underlying Table object pointer; two
    // lua_State* sharing one VM share these pointers.
    const void* registry_ptr = lua_topointer(L, LUA_REGISTRYINDEX);
    const void* globals_ptr  = lua_topointer(L, LUA_GLOBALSINDEX);
    LOG_DEBUG_KV("MID_HOOK", "lstate.snapshot",
        log::KV("tag",          std::string(tag ? tag : "?")),
        log::KV("L",            (void*)L),
        log::KV("top",          (int64_t)lua_gettop(L)),
        log::KV("registry_ptr", registry_ptr),
        log::KV("globals_ptr",  globals_ptr),
        log::KV("status",       (int64_t)lua_status(L)),
        log::KV("os_tid",       (int64_t)::GetCurrentThreadId()));
}

void LogLuaStateRawStruct(lua_State* L, const char* tag) {
    // Dump raw C-struct fields of `lua_State` and `global_State`
    // straight off the pointer. The Lua C API summary in
    // LogLuaStateSnapshot returns identical values at the safe vs crashing
    // newtable sites (verified against the binary). The question this answers: do
    // any *lower-level* struct fields differ that would discriminate one
    // call site from the other? If yes, that field is the smoking gun. If
    // no, the trigger is invisible to anything the Lua source code can
    // observe at the call site — meaning it lives inside lua_createtable's
    // own body, or in the static-vs-WHGame Lua ABI mismatch.
    //
    // Pure read; safe to call anywhere we have a valid L.
    if (!L) {
        LOG_DEBUG_KV("MID_HOOK", "lstate.raw",
            log::KV("tag", std::string(tag ? tag : "?")),
            log::KV("L",   (void*)nullptr));
        return;
    }
    // lua_State (from lstate.h)
    global_State* g = L->l_G;
    LOG_DEBUG_KV("MID_HOOK", "lstate.raw.L",
        log::KV("tag",         std::string(tag ? tag : "?")),
        log::KV("L",           (void*)L),
        log::KV("status",      (int64_t)L->status),
        log::KV("top",         (void*)L->top),
        log::KV("base",        (void*)L->base),
        log::KV("l_G",         (void*)g),
        log::KV("ci",          (void*)L->ci),
        log::KV("savedpc",     (void*)L->savedpc),
        log::KV("stack_last",  (void*)L->stack_last),
        log::KV("stack",       (void*)L->stack),
        log::KV("end_ci",      (void*)L->end_ci),
        log::KV("base_ci",     (void*)L->base_ci),
        log::KV("stacksize",   (int64_t)L->stacksize),
        log::KV("size_ci",     (int64_t)L->size_ci),
        log::KV("nCcalls",     (int64_t)L->nCcalls),
        log::KV("hookmask",    (int64_t)L->hookmask),
        log::KV("allowhook",   (int64_t)L->allowhook),
        log::KV("errorJmp",    (void*)L->errorJmp),
        log::KV("errfunc",     (int64_t)L->errfunc),
        log::KV("openupval",   (void*)L->openupval),
        log::KV("gclist",      (void*)L->gclist));
    // global_State — shared across all threads of this VM
    if (g) {
        LOG_DEBUG_KV("MID_HOOK", "lstate.raw.G",
            log::KV("tag",          std::string(tag ? tag : "?")),
            log::KV("L",            (void*)L),
            log::KV("g",            (void*)g),
            log::KV("frealloc",     (void*)g->frealloc),
            log::KV("ud",           (void*)g->ud),
            log::KV("currentwhite", (int64_t)g->currentwhite),
            log::KV("gcstate",      (int64_t)g->gcstate),
            log::KV("sweepstrgc",   (int64_t)g->sweepstrgc),
            log::KV("rootgc",       (void*)g->rootgc),
            log::KV("sweepgc",      (void*)g->sweepgc),
            log::KV("gray",         (void*)g->gray),
            log::KV("grayagain",    (void*)g->grayagain),
            log::KV("weak",         (void*)g->weak),
            log::KV("tmudata",      (void*)g->tmudata),
            log::KV("GCthreshold",  (int64_t)g->GCthreshold),
            log::KV("totalbytes",   (int64_t)g->totalbytes),
            log::KV("estimate",     (int64_t)g->estimate),
            log::KV("gcdept",       (int64_t)g->gcdept),
            log::KV("gcpause",      (int64_t)g->gcpause),
            log::KV("gcstepmul",    (int64_t)g->gcstepmul),
            log::KV("panic",        (void*)g->panic),
            log::KV("mainthread",   (void*)g->mainthread));
    }
    // Static-Lua ABI sanity: log sizes of the structs as our vendored
    // copy sees them. If WHGame's Lua disagrees, these are still useful
    // baselines for later comparison against a memory hexdump.
    LOG_DEBUG_KV("MID_HOOK", "lstate.raw.sizes",
        log::KV("tag",                std::string(tag ? tag : "?")),
        log::KV("sizeof_lua_State",   (int64_t)sizeof(lua_State)),
        log::KV("sizeof_global_State",(int64_t)sizeof(global_State)),
        log::KV("offsetof_l_G",       (int64_t)offsetof(lua_State, l_G)),
        log::KV("offsetof_top",       (int64_t)offsetof(lua_State, top)),
        log::KV("offsetof_status",    (int64_t)offsetof(lua_State, status)),
        log::KV("offsetof_errorJmp",  (int64_t)offsetof(lua_State, errorJmp)),
        log::KV("offsetof_frealloc",  (int64_t)offsetof(global_State, frealloc)));
}

void set_lua_state(lua_State* L) {
    std::scoped_lock guard(g_lock);
    g_lua_state = L;
    if (L) {
        log::Info("scripting: lua_State bound");
        LogLuaStateSnapshot(L, "set_lua_state.enter");
        LogLuaStateSnapshot(L, "set_lua_state.exit");
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
        // DEBUG (not INFO) — hot path fires on every hooked function
        // call. The BreadcrumbScope at function entry already names
        // scripting.dispatch_pre for the process-level filter; these
        // per-pcall lines are only useful for verbose dev-mode trace.
        LOG_DEBUG("SCRIPTING",
            "  before lua_pcall pre target=0x%p cb='%s' nargs=%d",
            (void*)target_func_ptr, cb.name.c_str(), n_args);
        int status = lua_pcall(g_lua_state, n_args, 1, 0);
        LOG_DEBUG("SCRIPTING",
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
        // DEBUG (not INFO) — hot path; BreadcrumbScope already names
        // scripting.dispatch_post for the unhandled-exception filter.
        LOG_DEBUG("SCRIPTING",
            "  before lua_pcall post target=0x%p cb='%s' nargs=%d",
            (void*)target_func_ptr, cb.name.c_str(), n_args);
        int status = lua_pcall(g_lua_state, n_args, 0, 0);
        LOG_DEBUG("SCRIPTING",
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
    LOG_DEBUG_KV("MID_HOOK", "dispatch.enter",
        log::KV("target",      (void*)target_func_ptr),
        log::KV("params_ptr",  (void*)params),
        log::KV("param_count", (int64_t)param_count));
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

    LOG_DEBUG_KV("MID_HOOK", "dispatch.hook_resolved",
        log::KV("target",     (void*)target_func_ptr),
        log::KV("hook",       (void*)hook),
        log::KV("jit_buf",    const_cast<kcdx::rom::runtime_func_t*>(hook)->get_jit_buffer()),
        log::KV("fnv_jit",    hook->fingerprint_jit_buffer()),
        log::KV("fnv_self",   hook->fingerprint_self()));

    // Clear the skip-original flag at the start of each dispatch.
    // The JIT trampoline reads this after we return; we want to
    // start from a known state so a stale "set" from a previous
    // mid-hook can't carry over.
    g_mid_skip_original.store(0, std::memory_order_release);

    // Mid-hooks pass args as a table keyed by 1..N (matches RoM).
    // For call_original="auto", the Lua callback can set
    // `args._skip = true` to signal the JIT to skip the captured
    // instruction. We dup the captures table before pcall so we
    // retain access to read _skip after pcall pops it.
    for (CallbackEntry& cb : it->second) {
        if (!PushCallback(g_lua_state, cb)) {
            log::WarnF("scripting: mid-callback for target 0x%p: '%s' "
                       "unresolved at dispatch time", (void*)target_func_ptr,
                       cb.name.c_str());
            continue;
        }
        // Build the captures table — [func, args]
        lua_createtable(g_lua_state, (int)param_count, 0);
        int table_idx = lua_gettop(g_lua_state);
        for (uint8_t i = 0; i < param_count; i++) {
            kcdx::lua_memory::to_lua(g_lua_state, params, i, hook->m_param_types);
            lua_rawseti(g_lua_state, table_idx, i + 1);
        }
        // We need to (a) call `func(args)` and (b) retain access to
        // `args` after the call so we can read `args._skip`. The
        // sequence below produces the layout pcall expects (func at
        // top-1, arg at top) while leaving one extra `args` ref
        // BELOW that gets left on the stack post-pcall:
        //
        //   [func, args]              after createtable + rawseti loop
        //   lua_insert(L, -2)         -> [args, func]    (move args below func)
        //   lua_pushvalue(L, -2)      -> [args, func, args]
        //   lua_pcall(L, 1, 0, 0)     -> consumes top 2 entries [func, args]
        //                                leaves [args] (the first push)
        //
        // pcall expects function at index top-nargs; with our final
        // pre-pcall stack [args, func, args], top=3, nargs=1, so
        // function is at index 3-1=2 (= func ✓) and arg is at index 3
        // (= args ✓).
        lua_insert(g_lua_state, -2);
        lua_pushvalue(g_lua_state, -2);

        LOG_DEBUG("SCRIPTING",
            "  before lua_pcall mid target=0x%p cb='%s'",
            (void*)target_func_ptr, cb.name.c_str());
        int status = lua_pcall(g_lua_state, 1, 0, 0);
        LOG_DEBUG("SCRIPTING",
            "  after  lua_pcall mid target=0x%p cb='%s' status=%d",
            (void*)target_func_ptr, cb.name.c_str(), status);
        if (status != 0) {
            const char* err = lua_tostring(g_lua_state, -1);
            LOG_ERROR("SCRIPTING",
                "mid-callback for target 0x%p ('%s') threw: %s",
                (void*)target_func_ptr, cb.name.c_str(),
                err ? err : "<no message>");
            // Pop: error message + args_dup (the dup we pushed before
            // pcall; pcall pushed err at -1 and the dup is now at -2).
            // Actually: when pcall fails, it pops nargs+1 (the func +
            // its args) and pushes the error. So stack ends up:
            // [args_dup, err]. Pop both.
            lua_pop(g_lua_state, 2);
            continue;
        }
        // pcall succeeded, nresults=0; stack is [args_dup]. Read
        // args_dup._skip to decide whether to set the skip flag.
        lua_getfield(g_lua_state, -1, "_skip");
        if (lua_toboolean(g_lua_state, -1)) {
            g_mid_skip_original.store(1, std::memory_order_release);
        }
        lua_pop(g_lua_state, 2);  // _skip value + args_dup
    }
    LOG_DEBUG_KV("MID_HOOK", "dispatch.exit",
        log::KV("target",          (void*)target_func_ptr),
        log::KV("skip_original",   (int64_t)g_mid_skip_original.load(std::memory_order_acquire)));
    // Return 0: the old return-value-as-resume-addr semantic is
    // retired. JIT no longer reads rax.
    return 0;
}

}  // namespace kcdx::scripting
