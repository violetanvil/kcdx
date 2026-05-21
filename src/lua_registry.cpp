// kcdx::lua_registry — see header for the design contract.

#include "lua_registry.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <unordered_map>

extern "C" {
#include "lauxlib.h"
}

#include "log.h"

namespace kcdx::lua_registry {

namespace {

// std::deque chosen over std::vector so node addresses remain stable
// as the queue grows — Entry has an atomic<Status> that consumers
// (handle userdata) hold pointers to via handle id lookup.
std::deque<Entry> g_entries;
// Reverse index from handle id to deque position. Append-only.
std::unordered_map<uint64_t, size_t> g_byHandleId;
// Next handle id to assign. Starts at 1 so 0 means "no handle".
std::atomic<uint64_t> g_nextHandleId{1};

// Coarse-grain lock around the queue. Append and ApplyZone are not
// hot paths (one append per Lua kcdx.* call, one ApplyZone per
// zone-transition); a single mutex keeps the model simple. The atomic
// Status inside Entry is independent of this lock — handle:applied()
// reads it without taking the lock.
std::mutex g_mu;

// Per-kind apply handlers.
ApplyHandler g_handlers[8] = {nullptr};  // sized to fit a few Kinds

// Per-handle-id "this entry has been applied or failed" sweep counter.
// Apply pass increments g_applyEpoch, then walks entries with
// status==Pending and applies. Handles wait_applied via coroutine in
// Phase 2i will sleep on g_applyEpoch advance.
std::atomic<uint64_t> g_applyEpoch{0};

// --- Handle userdata ----------------------------------------------------
//
// kcdx::lua_registry::Handle is the Lua-facing object kcdx.bytes /
// kcdx.hook / etc. return. Methods:
//   :name()        -> string
//   :applied()     -> true | false | nil   (Applied / Failed / Pending)
//   :reason()      -> string | nil         (Failed only)
//   :wait_applied() -> blocking yield  (Phase 2i; stub in 2a)
//
// We use a Lua userdata of sizeof(uint64_t) carrying the handle id.
// Entry pointers are looked up by id every method invocation; this
// keeps the userdata stable even if g_entries reallocates internally.

constexpr const char* kHandleMetatable = "kcdx.registry.handle";

uint64_t* HandleUd(lua_State* L, int idx) {
    return static_cast<uint64_t*>(
        luaL_checkudata(L, idx, kHandleMetatable));
}

int H_name(lua_State* L) {
    auto* h = HandleUd(L, 1);
    const Entry* e = Find(*h);
    if (!e) { lua_pushnil(L); return 1; }
    lua_pushlstring(L, e->name.data(), e->name.size());
    return 1;
}

int H_applied(lua_State* L) {
    auto* h = HandleUd(L, 1);
    const Entry* e = Find(*h);
    if (!e) { lua_pushnil(L); return 1; }
    Status s = e->status.load(std::memory_order_acquire);
    switch (s) {
        case Status::Applied: lua_pushboolean(L, 1); return 1;
        case Status::Failed:  lua_pushboolean(L, 0); return 1;
        case Status::Pending:
        default:              lua_pushnil(L);         return 1;
    }
}

int H_reason(lua_State* L) {
    auto* h = HandleUd(L, 1);
    const Entry* e = Find(*h);
    if (!e) { lua_pushnil(L); return 1; }
    if (e->reason.empty()) { lua_pushnil(L); return 1; }
    lua_pushlstring(L, e->reason.data(), e->reason.size());
    return 1;
}

int H_wait_applied(lua_State* L) {
    // Phase 2a stub. Phase 2i wires coroutine yield/resume. For now,
    // surface a clear error so authors know what's coming.
    auto* h = HandleUd(L, 1);
    const Entry* e = Find(*h);
    if (e) {
        Status s = e->status.load(std::memory_order_acquire);
        if (s != Status::Pending) {
            // Already resolved — just return self for chaining.
            lua_pushvalue(L, 1);
            return 1;
        }
    }
    return luaL_error(L,
        "handle:wait_applied() requires Phase 2i coroutine support; "
        "use kcdx.on(\"ready\", function() ... handle:applied() ... end) "
        "for now");
}

int H_tostring(lua_State* L) {
    auto* h = HandleUd(L, 1);
    const Entry* e = Find(*h);
    const char* statusStr = "?";
    if (e) {
        switch (e->status.load(std::memory_order_acquire)) {
            case Status::Pending: statusStr = "pending"; break;
            case Status::Applied: statusStr = "applied"; break;
            case Status::Failed:  statusStr = "failed";  break;
        }
    }
    lua_pushfstring(L, "kcdx.handle<id=%d name=%s status=%s>",
        static_cast<int>(*h),
        e ? e->name.c_str() : "<gone>",
        statusStr);
    return 1;
}

}  // namespace

void EnsureHandleMetatable(lua_State* L) {
    if (luaL_newmetatable(L, kHandleMetatable) == 0) {
        lua_pop(L, 1);  // already registered
        return;
    }
    // __index = self (so methods live on the metatable)
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pushstring(L, kHandleMetatable);
    lua_setfield(L, -2, "__metatable");
    lua_pushcfunction(L, H_tostring);
    lua_setfield(L, -2, "__tostring");
    lua_pushcfunction(L, H_name);
    lua_setfield(L, -2, "name");
    lua_pushcfunction(L, H_applied);
    lua_setfield(L, -2, "applied");
    lua_pushcfunction(L, H_reason);
    lua_setfield(L, -2, "reason");
    lua_pushcfunction(L, H_wait_applied);
    lua_setfield(L, -2, "wait_applied");
    lua_pop(L, 1);
}

uint64_t Append(Entry&& e, std::string* err_out) {
    // Per-kind handler must be registered before any append for that
    // kind. Catches the developer mistake of binding a Lua surface
    // without wiring its apply handler.
    int kindIdx = static_cast<int>(e.kind);
    if (kindIdx < 0 || kindIdx >= (int)(sizeof(g_handlers)/sizeof(g_handlers[0]))
        || !g_handlers[kindIdx]) {
        if (err_out) {
            *err_out = "internal error: no apply handler registered for "
                       "this kind (engine misconfiguration; see kcdx.log)";
        }
        log::ErrorF("lua_registry::Append: no handler for Kind=%d "
                    "(entry name='%s')", kindIdx, e.name.c_str());
        return 0;
    }

    std::lock_guard<std::mutex> lock(g_mu);
    uint64_t id = g_nextHandleId.fetch_add(1, std::memory_order_relaxed);
    g_entries.emplace_back(std::move(e));
    g_byHandleId.emplace(id, g_entries.size() - 1);

    Entry& back = g_entries.back();
    log::InfoF("lua_registry: queued kind=%d name='%s' plugin='%s' "
               "site=%s:%d (handle=%llu)",
               kindIdx,
               back.name.c_str(),
               back.pluginName.empty() ? "<anon>" : back.pluginName.c_str(),
               back.callSiteFile.empty() ? "?" : back.callSiteFile.c_str(),
               back.callSiteLine,
               (unsigned long long)id);
    return id;
}

const Entry* Find(uint64_t handleId) {
    std::lock_guard<std::mutex> lock(g_mu);
    auto it = g_byHandleId.find(handleId);
    if (it == g_byHandleId.end()) return nullptr;
    return &g_entries[it->second];
}

Entry* FindMut(uint64_t handleId) {
    std::lock_guard<std::mutex> lock(g_mu);
    auto it = g_byHandleId.find(handleId);
    if (it == g_byHandleId.end()) return nullptr;
    return &g_entries[it->second];
}

void RegisterApplyHandler(Kind k, ApplyHandler fn) {
    int idx = static_cast<int>(k);
    if (idx < 0 || idx >= (int)(sizeof(g_handlers)/sizeof(g_handlers[0]))) {
        log::ErrorF("lua_registry::RegisterApplyHandler: Kind=%d out of "
                    "bounds", idx);
        return;
    }
    if (g_handlers[idx]) {
        log::WarnF("lua_registry::RegisterApplyHandler: Kind=%d already "
                   "registered; overwriting (likely a programming error)",
                   idx);
    }
    g_handlers[idx] = fn;
}

size_t ApplyZone(kcdx::load_order::Zone zone) {
    g_applyEpoch.fetch_add(1, std::memory_order_release);

    // Snapshot the pending-entry ids under the lock so we don't hold
    // it across user-handler invocations (which may take a long time
    // and call back into kcdx::log).
    std::vector<uint64_t> ids;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        ids.reserve(g_entries.size());
        for (const auto& kv : g_byHandleId) {
            const Entry& e = g_entries[kv.second];
            if (e.status.load(std::memory_order_acquire) != Status::Pending) {
                continue;
            }
            // Zone filter: route by the OWNING PLUGIN's load_order
            // resolution. Anonymous entries (no plugin) default to
            // after_game since they came from ad-hoc Lua run by the
            // first-update-tick path.
            kcdx::load_order::Zone entryZone =
                kcdx::load_order::Zone::AfterGame;
            if (!e.pluginName.empty()) {
                const auto& eff = kcdx::load_order::Of(e.pluginName);
                if (!eff.enabled) continue;
                entryZone = eff.zone;
            }
            if (entryZone != zone) continue;
            ids.push_back(kv.first);
        }
    }

    // Sort ids by (priority asc, name asc) — the unified-load-order
    // contract. Anonymous entries get priority 50 fallback.
    std::sort(ids.begin(), ids.end(), [](uint64_t a, uint64_t b) {
        const Entry* ea = Find(a);
        const Entry* eb = Find(b);
        if (!ea || !eb) return a < b;
        int pa = ea->priority;
        int pb = eb->priority;
        if (!ea->pluginName.empty()) {
            const auto& effA = kcdx::load_order::Of(ea->pluginName);
            pa = effA.priority;
        }
        if (!eb->pluginName.empty()) {
            const auto& effB = kcdx::load_order::Of(eb->pluginName);
            pb = effB.priority;
        }
        if (pa != pb) return pa < pb;
        return ea->name < eb->name;
    });

    // Walk the sorted list, dispatch to per-kind handler. Each handler
    // populates entry.reason on rejection; the registry flips the
    // status atomic afterward.
    size_t transitioned = 0;
    for (uint64_t id : ids) {
        Entry* e = FindMut(id);
        if (!e) continue;
        ApplyHandler fn = g_handlers[static_cast<int>(e->kind)];
        if (!fn) {
            e->reason = "no apply handler for this entry kind";
            e->status.store(Status::Failed, std::memory_order_release);
            log::ErrorF("lua_registry: entry '%s' has no apply handler",
                        e->name.c_str());
            ++transitioned;
            continue;
        }
        std::string reason;
        bool ok = false;
        try {
            ok = fn(*e, reason);
        } catch (const std::exception& ex) {
            ok = false;
            reason = std::string("apply handler threw: ") + ex.what();
        } catch (...) {
            ok = false;
            reason = "apply handler threw unknown exception";
        }
        e->reason = std::move(reason);
        e->status.store(ok ? Status::Applied : Status::Failed,
                        std::memory_order_release);
        ++transitioned;
        if (!ok) {
            log::ErrorF("lua_registry: entry '%s' (plugin='%s') failed at "
                        "apply: %s",
                        e->name.c_str(),
                        e->pluginName.empty() ? "<anon>"
                                              : e->pluginName.c_str(),
                        e->reason.c_str());
        }
    }

    // Only emit a log line when we actually moved something —
    // ApplyZone runs every update tick (cheap when queue is empty)
    // and we don't want to flood the log with "0 entries transitioned"
    // on every frame. Silent no-op when queue is empty is the
    // common case.
    if (transitioned > 0) {
        log::InfoF("lua_registry: ApplyZone(zone=%s) — %zu entries "
                   "transitioned",
                   zone == kcdx::load_order::Zone::BeforeGame
                       ? "before_game" : "after_game",
                   transitioned);
    }
    return transitioned;
}

int PushHandleOrError(lua_State* L, uint64_t handleId,
                      const std::string& errIfNoHandle) {
    if (handleId == 0) {
        lua_pushnil(L);
        lua_pushlstring(L, errIfNoHandle.data(), errIfNoHandle.size());
        return 2;
    }
    // Allocate the handle userdata.
    auto* ud = static_cast<uint64_t*>(lua_newuserdata(L, sizeof(uint64_t)));
    *ud = handleId;
    luaL_getmetatable(L, kHandleMetatable);
    lua_setmetatable(L, -2);
    return 1;
}

std::string OwningPluginForCurrentCall(lua_State* L,
                                        std::string& callSiteFileOut,
                                        int& callSiteLineOut) {
    // debug.getinfo(2, "Sl") gives us the source + currentline of
    // the caller of the Lua-C function (which is the Lua code that
    // called kcdx.bytes / kcdx.hook / ...).
    lua_Debug ar;
    if (lua_getstack(L, 1, &ar) && lua_getinfo(L, "Sl", &ar)) {
        if (ar.source && ar.source[0] == '@') {
            // Lua marks file sources with '@'. Strip it.
            callSiteFileOut = ar.source + 1;
        } else if (ar.source) {
            callSiteFileOut = ar.source;
        }
        callSiteLineOut = ar.currentline;
    }

    // Plugin-by-script-path lookup arrives in Phase 2h (when
    // [entrypoints].lua loading happens). Until then, all Lua-side
    // registrations are treated as anonymous — they apply at
    // after_game zone using default priority 50.
    return "";
}

}  // namespace kcdx::lua_registry
