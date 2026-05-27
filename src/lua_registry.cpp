// kcdx::lua_registry — see header for the design contract.

#include "lua_registry.h"

#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

extern "C" {
#include "lauxlib.h"
}

#include "hook_chain.h"  // kcdx::hook_chain::Uninstall — H_uninstall
                         // routes a Lua handle:uninstall() on a Kind::Hook
                         // entry through the per-target chain removal.
#include "log.h"
#include "plugin_loader.h"  // kcdx::plugins::g_manifests — manifest
                            // lookup for the resolved plugin's author
                            // (the 2-dot namespace refactor's step 4).
#include "scripting.h"   // scripting::lua_state() — the live VM the
                         // ready callbacks fire against (same source the
                         // hook binder uses to unref callbacks).

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
// status==Pending and applies. A future coroutine-based wait_applied
// will sleep on g_applyEpoch advance.
std::atomic<uint64_t> g_applyEpoch{0};

// --- "ready" lifecycle callbacks (kcdx.on("ready", fn)) -----------------
//
// Pending ready callbacks, keyed by owning plugin name ("" = anonymous).
// Each is a luaL_ref into LUA_REGISTRYINDEX. A plugin may register more
// than one, so the value is a list fired in registration order.
//
// One-shot: after a callback fires (at the end of ApplyZone for the
// plugin's zone), its ref is luaL_unref'd and erased from this map, so a
// later ApplyZone tick cannot re-fire it. The map + g_mu guard the
// registration/erase; firing copies the to-fire refs out under the lock
// (and erases them) BEFORE invoking, so a ready callback that itself
// calls kcdx.on("ready", ...) — or any reentrant ApplyZone — neither
// double-fires nor deadlocks.
std::unordered_map<std::string, std::vector<int>> g_readyCallbacks;

// --- Handle userdata ----------------------------------------------------
//
// kcdx::lua_registry::Handle is the Lua-facing object kcdx.bytes /
// kcdx.hook / etc. return. Methods:
//   :name()        -> string
//   :applied()     -> true | false | nil   (Applied / Failed / Pending)
//   :reason()      -> string | nil         (Failed only)
//   :wait_applied() -> blocking yield  (future; currently a stub)
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
        // Uninstall flips a previously-Applied (or Pending/Failed) entry to
        // Removed. The author contract is "applied() returns false after
        // uninstall" — explicit boolean false, not nil (nil means Pending
        // and is reserved for "apply pass hasn't reached this yet").
        case Status::Removed: lua_pushboolean(L, 0); return 1;
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
    // Current stub. A future version wires coroutine yield/resume. For
    // now, surface a clear error so authors know what's coming.
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
        "handle:wait_applied() requires coroutine support (not yet "
        "available); use kcdx.on(\"ready\", function() ... "
        "handle:applied() ... end) for now");
}

int H_uninstall(lua_State* L) {
    // Lua method-chaining convention: always return self (the handle
    // userdata at stack index 1). Idempotent at every layer — an already-
    // Removed entry, an unknown handle id, and a Kind whose engine-side
    // uninstall is a no-op all flow through the same "flip to Removed +
    // return self" path.
    auto* h = HandleUd(L, 1);
    Entry* e = FindMut(*h);
    if (!e) {
        // Handle's Entry vanished from the registry — shouldn't happen
        // (the queue is append-only), but match the rest of the H_*
        // handlers' tolerant shape: return self, no error.
        lua_pushvalue(L, 1);
        return 1;
    }
    switch (e->kind) {
        case Kind::Hook:
            // hook_chain::Uninstall is idempotent and returns true even
            // when the handle id is unknown or already removed, so a
            // double-uninstall is a safe no-op at the engine layer.
            kcdx::hook_chain::Uninstall(*h);
            SetStatus(*h, Status::Removed);
            break;
        // Non-Hook kinds: raise a teaching error. We do NOT silently flip
        // status — that would lie to the author (`:applied()==false` while
        // the underlying registration is still applied in memory). The
        // patch engine has no unapply path today (no original-bytes
        // snapshot stored on PatchEntry); when a per-kind uninstall ships
        // (its own feature: snapshot + revert + parity test), extend this
        // switch to dispatch to it BEFORE flipping status. Mirrors the
        // existing H_wait_applied "not available yet" pattern.
        default: {
            const char* kindName = "unknown";
            switch (e->kind) {
                case Kind::Bytes: kindName = "kcdx.bytes"; break;
                default:          break;
            }
            return luaL_error(L,
                "handle:uninstall() is not yet supported for %s handles "
                "(only kcdx.hook handles can be uninstalled today). The "
                "underlying engine has no revert path for this surface "
                "yet; a future feature ships per-kind uninstall.",
                kindName);
        }
    }
    lua_pushvalue(L, 1);
    return 1;
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
            case Status::Removed: statusStr = "removed"; break;
        }
    }
    lua_pushfstring(L, "kcdx.handle<id=%d name=%s status=%s>",
        static_cast<int>(*h),
        e ? e->name.c_str() : "<gone>",
        statusStr);
    return 1;
}

// Resolve the zone a ready-callback owner fires in — the SAME routing
// ApplyZone uses for entries. Anonymous ("") defaults to AfterGame
// (matching how ApplyZone defaults anonymous entries). A disabled plugin
// still resolves to its declared zone; ready never fires for it because
// its entries are skipped, but the routing here is owner→zone only.
kcdx::load_order::Zone ReadyOwnerZone(const std::string& pluginName) {
    if (pluginName.empty()) return kcdx::load_order::Zone::AfterGame;
    return kcdx::load_order::Of(pluginName).zone;
}

// Fire (once) every pending "ready" callback whose owning plugin's zone
// == `zone`. Called at the END of ApplyZone(zone), after every entry in
// the zone has transitioned to a final status — so handle:applied() /
// :reason() are final inside the callback.
//
// One-shot + reentrancy-safe: the to-fire refs are copied out of
// g_readyCallbacks and erased UNDER THE LOCK before any invocation, so a
// callback that re-enters (calls kcdx.on / triggers ApplyZone) can't see
// the same ref again. Each callback runs under lua_pcall; a throw is
// logged with the plugin name and does NOT abort the apply pass or other
// plugins' callbacks. Refs are luaL_unref'd after firing (one-shot).
void FireReadyForZone(kcdx::load_order::Zone zone) {
    // Snapshot + erase the matching (plugin, refs) pairs under the lock.
    std::vector<std::pair<std::string, int>> toFire;  // (plugin, ref)
    {
        std::lock_guard<std::mutex> lock(g_mu);
        for (auto it = g_readyCallbacks.begin();
             it != g_readyCallbacks.end();) {
            if (ReadyOwnerZone(it->first) == zone) {
                for (int ref : it->second) {
                    toFire.emplace_back(it->first, ref);
                }
                it = g_readyCallbacks.erase(it);
            } else {
                ++it;
            }
        }
    }
    if (toFire.empty()) return;

    lua_State* L = kcdx::scripting::lua_state();
    if (!L) {
        // No live VM to fire against — release the refs we pulled so they
        // don't leak, and log loudly (this shouldn't happen: ready fires
        // from the main-thread apply pass, where the VM is up).
        log::Error("lua_registry: ready callbacks pending but no live "
                   "lua_State; dropping (engine misconfiguration)");
        // Cannot luaL_unref without a state; the refs are abandoned.
        return;
    }

    for (const auto& pr : toFire) {
        const std::string& plugin = pr.first;
        int ref = pr.second;
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        if (lua_isfunction(L, -1)) {
            // "ready" takes no args.
            int status = lua_pcall(L, 0, 0, 0);
            if (status != 0) {
                const char* msg = lua_tostring(L, -1);
                log::ErrorF("lua_registry: \"ready\" callback for plugin "
                            "'%s' threw: %s",
                            plugin.empty() ? "<anon>" : plugin.c_str(),
                            msg ? msg : "(no message)");
                lua_pop(L, 1);  // pop the error message
            }
        } else {
            lua_pop(L, 1);  // not a function (shouldn't happen) — discard
        }
        // One-shot: release the ref now that it has fired.
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
    }
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
    lua_pushcfunction(L, H_uninstall);
    lua_setfield(L, -2, "uninstall");
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
    // Stamp the entry's own id onto itself. Per-kind install paths that
    // copy data out of the Entry (hook_chain::Add building a ChainEntry)
    // carry this id so later Uninstall(handleId) finds the right entry
    // without a registry round-trip.
    back.handleId = id;
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

void ForEachEntryOfKind(Kind k, const std::function<void(const Entry&)>& fn) {
    // Walk the append-only queue under the queue mutex. `fn` is a cheap
    // read-only inspection (the COMP-15 patch-engine accessor casts the
    // payload and tests a range); it must not re-enter the registry — that
    // would self-deadlock on g_mu. Entries never move (std::deque) and are
    // never destroyed, so handing out a const Entry& for the duration of the
    // call is safe.
    std::lock_guard<std::mutex> lock(g_mu);
    for (const Entry& e : g_entries) {
        if (e.kind == k) fn(e);
    }
}

void SetStatus(uint64_t handleId, Status s, const std::string& reason) {
    std::lock_guard<std::mutex> lock(g_mu);
    auto it = g_byHandleId.find(handleId);
    if (it == g_byHandleId.end()) return;
    Entry& e = g_entries[it->second];
    // The `reason` field is only written under the queue mutex by the
    // apply pipeline + this helper; readers (H_reason) read it without the
    // lock but only after observing a non-Pending status via acquire. Set
    // reason BEFORE the status release-store so a reader that sees the new
    // status also sees the new reason (release/acquire ordering).
    if (!reason.empty()) e.reason = reason;
    e.status.store(s, std::memory_order_release);
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

// Apply-order kind rank (see the comment in ApplyZone's sort). A TOTAL rank:
// EVERY Kind gets a distinct, deterministic value, compared unconditionally
// before the name tiebreak. The load-bearing relationship is Bytes < Hook (a
// bytes-patch must apply before a hook on the same site). Other kinds get
// distinct ranks too — not because their relative order matters (it doesn't;
// non-overlapping sites never interact), but so the comparator is a valid
// strict weak ordering for ANY number of kinds.
//
// Why TOTAL, not a gated "only when one Bytes + one Hook": a gated tier is
// INTRANSITIVE the moment a third queued kind shares a priority band — e.g.
// Bytes "z", Hook "a", Code "m" at one priority gives Code<Bytes (name),
// Bytes<Hook (kind), Hook<Code (name) → a cycle → std::sort UB. A uniform
// rank compared before name is lexicographic (priority, rank, name) and
// transitive by construction for all N kinds. The Kind enum already plans
// Code/Command/Cosave/Scan (lua_registry.h); ranking every kind here means
// adding one cannot arm that UB. An unknown/future kind not listed below
// sorts LAST (kRankOther) — still distinct and deterministic, still transitive.
// File-local (anonymous namespace) — not part of the public contract.
static int kindRank(Kind k) {
    switch (k) {
        case Kind::Bytes: return 0;  // patch writes pristine bytes first
        case Kind::Hook:  return 1;  // hook detours the (now-patched) prologue
        default:          return 100;  // any future kind: deterministic, sorts last
    }
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
                // Gate through IsPluginEnabled so a zone_gate rejection
                // (engineAccepted = false) is honored alongside the user's
                // enable choice — a direct .userEnabled read would bypass it.
                if (!kcdx::load_order::IsPluginEnabled(e.pluginName)) continue;
                entryZone = kcdx::load_order::Of(e.pluginName).zone;
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
        // orderIndex tiebreak (between priority and kind rank). INT_MAX for any
        // plugin/anonymous entry — and ONLY plugins queue registry entries, so
        // this compare is a uniform no-op here (no pak mod produces a Bytes/Hook
        // entry); it is threaded for sort-key consistency with the other load-
        // order sorts. Finite orderIndex never appears on a registry entry.
        int oa = INT_MAX;
        int ob = INT_MAX;
        if (!ea->pluginName.empty()) {
            const auto& effA = kcdx::load_order::Of(ea->pluginName);
            pa = effA.priority;
            oa = effA.orderIndex;
        }
        if (!eb->pluginName.empty()) {
            const auto& effB = kcdx::load_order::Of(eb->pluginName);
            pb = effB.priority;
            ob = effB.orderIndex;
        }
        if (pa != pb) return pa < pb;
        if (oa != ob) return oa < ob;

        // KIND RANK — at the SAME effective priority, order by kind rank
        // (kindRank above) BEFORE the name tiebreak. The load-bearing rule is
        // Bytes < Hook: a Bytes patch must apply BEFORE a Hook on the same
        // site. WHY: a bytes-patch rewrites the prologue in place; a hook
        // detours it (writes an E9 rel32 over the entry, then relocates the
        // original prologue into its trampoline). If the hook applies first,
        // the patch's byte-verify guard sees the E9 (not the pristine bytes it
        // expects) and correctly ABORTS — they fail to coexist. Patch first →
        // pristine bytes → hook detours the now-patched prologue → both apply.
        // This restores the legacy conflict_engine's patch-before-hook order
        // (legacy used priority 100<200) — the comp-02 coexist invariant.
        //
        // PAYLOAD-AGNOSTIC: Entry.kind ONLY — never a VA, payload cast, or
        // locator resolution (the COMP-14/cap-41 sort-time boundary).
        //
        // UNIFORM (every kind ranked), NOT gated to Bytes+Hook: a gated tier
        // is intransitive once a third queued kind shares a band (see kindRank
        // comment) → std::sort UB. Ranking every kind keeps the comparator a
        // valid strict weak ordering — (priority, rank, name) lexicographic —
        // for any N kinds. The band-level reorder of Bytes/Hook relative to
        // other kinds is engine-internal and harmless: non-overlapping sites
        // never touch each other's bytes, so only a Bytes+Hook pair AT ONE
        // SITE has an observable outcome, and that's the relationship this
        // pins.
        const int ra = kindRank(ea->kind);
        const int rb = kindRank(eb->kind);
        if (ra != rb) return ra < rb;
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

    // Every entry in this zone now has a FINAL status (Applied/Failed),
    // so handle:applied()/:reason() are settled. Fire each plugin's
    // "ready" callback for THIS zone exactly once (one-shot; reentrancy-
    // safe; a throw is logged and isolated). Co-located here so it
    // naturally serves both the first-tick ApplyZone(AfterGame) and any
    // later per-tick drain — hooks.cpp needs no separate dispatch. (A
    // future before_game apply pass fires its own zone's ready cbs the
    // same way.) Fired even when transitioned == 0: a plugin whose hooks
    // all already applied on an earlier tick still gets its ready signal
    // on the tick its kcdx.on registration is first seen.
    FireReadyForZone(zone);

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

namespace {

// Plugin-by-script-path index. Keyed by the NORMALIZED script path
// (forward slashes, lowercased) so a debug.getinfo source matches the
// path the loader registered regardless of slash direction / case
// (Windows paths are case-insensitive). Populated by RegisterScriptOwner
// before each plugin.lua runs; read by OwningPluginForCurrentCall.
std::unordered_map<std::string, std::string> g_scriptOwners;

std::string NormalizeScriptPath(const std::string& p) {
    std::string out = p;
    for (char& c : out) {
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

}  // namespace

void RegisterReadyCallback(const std::string& pluginName, int callbackRef) {
    std::lock_guard<std::mutex> lock(g_mu);
    g_readyCallbacks[pluginName].push_back(callbackRef);
}

void RegisterScriptOwner(const std::string& scriptPath,
                         const std::string& pluginName) {
    std::lock_guard<std::mutex> lock(g_mu);
    g_scriptOwners[NormalizeScriptPath(scriptPath)] = pluginName;
}

OwningPlugin OwningPluginForCurrentCall(lua_State* L,
                                        std::string& callSiteFileOut,
                                        int& callSiteLineOut) {
    // Walk up the Lua stack to the nearest frame that maps to a known
    // plugin script. The immediate caller (level 1) is usually the
    // plugin.lua code that called kcdx.hook/.bytes/..., but it may also
    // be a helper module the plugin require()'d — so we climb until we
    // find an attributed source or run out of frames. The first
    // attributed frame found also supplies the call-site file/line for
    // diagnostics.
    bool haveCallSite = false;
    for (int level = 1; level <= 16; ++level) {
        lua_Debug ar;
        if (!lua_getstack(L, level, &ar)) break;
        if (!lua_getinfo(L, "Sl", &ar)) continue;

        std::string src;
        if (ar.source && ar.source[0] == '@') {
            src = ar.source + 1;   // Lua marks file sources with '@'.
        } else if (ar.source) {
            src = ar.source;
        }
        if (src.empty()) continue;

        // First real frame supplies the diagnostic call-site.
        if (!haveCallSite) {
            callSiteFileOut = src;
            callSiteLineOut = ar.currentline;
            haveCallSite = true;
        }

        std::string owner;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            auto it = g_scriptOwners.find(NormalizeScriptPath(src));
            if (it != g_scriptOwners.end()) owner = it->second;
        }
        if (!owner.empty()) {
            // Resolved the plugin name; cross-reference g_manifests to
            // pull out the matching [plugin].author. The corpus today
            // ships empty [plugin].author for most plugins — that comes
            // through here as author == "" and the resolver treats the
            // row as legacy 1-dot. Step 6 of the refactor populates the
            // manifest field; from then on author is the real value.
            OwningPlugin out;
            out.plugin = owner;
            for (const auto& m : kcdx::plugins::g_manifests) {
                if (m.name == owner) { out.author = m.author; break; }
            }
            return out;
        }
    }

    // No attributed frame — ad-hoc Lua (console, pak scripts, etc.).
    // Anonymous: applies at after_game / priority 50. Both fields stay
    // empty (no plugin → no author).
    return OwningPlugin{};
}

}  // namespace kcdx::lua_registry
