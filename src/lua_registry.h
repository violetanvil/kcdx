#pragma once

// kcdx::lua_registry — deferred-apply queue for Lua-side kcdx.* calls.
//
// The deferred-apply contract:
//
//   API calls register intent; engine applies them in one pass after
//   all plugins have registered. When plugin.lua calls kcdx.hook(opts),
//   the engine validates the call (locator format, signature, zone
//   capability) and queues the registration; nothing is written to game
//   memory yet. After all enabled plugins in the unified ordered list
//   have run their plugin.lua / DLL Preload+Load (i.e. after every
//   plugin in the current zone has had its turn to register),
//   conflict_engine runs pre-flight ONCE across all queued entries,
//   classifies conflicts, decides who wins via unified load order,
//   then the apply pass walks the registrations in order and installs
//   them.
//
// This module owns the queue, the handle metatable, and the per-zone
// apply orchestration. Each kcdx.* surface (kcdx.bytes, kcdx.hook,
// kcdx.code, ...) builds its own kind-specific payload and calls
// Append() to add an entry; later the apply pass dispatches per-kind
// install routines through registered handlers.

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

extern "C" {
#include "lua.h"
}

#include "load_order.h"   // Zone

namespace kcdx::lua_registry {

// What kind of intent this entry represents. Each kind has a payload
// type the binder builds and a per-kind apply handler the registry
// invokes during ApplyZone.
enum class Kind {
    Bytes,        // the kcdx.bytes surface (succeeds the legacy patch table)
    Hook,         // the kcdx.hook surface incl. mode=mid + dynamic_hook (succeeds the legacy hook tables)
    Statement,    // the kcdx.statement.* surface (static-bytes modification: resolve a curated statement, emit an op's bytes, write or trampoline)
    // Code, Command, Cosave, Scan ... added by later commits.
};

// Lifecycle status of a queued entry. Read by handle:applied().
//
//   Pending  — queued, apply pass hasn't reached this entry yet.
//   Applied  — apply succeeded; handle is "live".
//   Failed   — apply rejected (locator failed, byte mismatch, conflict
//              lost, etc.); see `reason` for diagnostic.
//   Removed  — Set by hook_chain::Uninstall (and future per-kind Uninstall
//              paths) after the entry's installed-side has been logically
//              removed. An Uninstall on an entry whose status is Pending or
//              Failed is still valid — it logically removes from the
//              registry's perspective. handle:applied() returns false for
//              Removed.
enum class Status : uint8_t {
    Pending = 0,
    Applied = 1,
    Failed  = 2,
    Removed = 3,
};

// One queued registration. Built by binders, stored in the per-zone
// queue, walked by the apply pass.
struct Entry {
    Kind kind = Kind::Bytes;
    // The stable registry handle id assigned to this entry by Append (>= 1).
    // Mirrored onto the entry itself so per-kind install paths (e.g.
    // hook_chain::Add) can carry it onto their own data structures (a
    // ChainEntry) — later Uninstall(handleId) finds the entry by id without
    // a registry round-trip.
    uint64_t handleId = 0;
    // The plugin that owns this registration. Empty when called from
    // ad-hoc Lua with no owning plugin context. Used by the
    // apply pass to attribute log lines and conflict resolution.
    std::string pluginName;
    // Per-author intra-plugin sort key. Author hint, ignored when the
    // owning plugin has a load_order.toml priority override.
    int priority = 50;
    // Author-supplied name, also used for log lines + dedupe.
    std::string name;
    // The kind-specific payload. Owns its own memory. Type-erased so
    // this header doesn't pull in patch_engine.h / hook_engine.h /
    // etc. — each binder casts the payload back to its own type at
    // apply time. Use a union of strongly-typed shared_ptrs in a
    // future refactor if the type erasure proves error-prone.
    std::shared_ptr<void> payload;

    // Filled by the apply pass.
    std::atomic<Status> status{Status::Pending};
    // Diagnostic for status == Failed. Set by the apply pass; read by
    // handle:applied() and handle:reason().
    std::string reason;

    // The call site that produced this registration. Set on append
    // from debug.getinfo (Lua) so failure log lines can attribute
    // back to the source line.
    std::string callSiteFile;
    int callSiteLine = 0;

    // Default ctor + explicit copy/move semantics so std::vector
    // can store these. The atomic<Status> would normally make this
    // non-copyable; we provide a copy that snapshots the status and
    // hand the registry a stable address discipline (append-only;
    // entries never move after Append returns).
    Entry() = default;
    Entry(const Entry& o)
        : kind(o.kind), handleId(o.handleId),
          pluginName(o.pluginName), priority(o.priority),
          name(o.name), payload(o.payload),
          status(o.status.load(std::memory_order_relaxed)),
          reason(o.reason),
          callSiteFile(o.callSiteFile), callSiteLine(o.callSiteLine) {}
    Entry(Entry&& o) noexcept
        : kind(o.kind), handleId(o.handleId),
          pluginName(std::move(o.pluginName)),
          priority(o.priority), name(std::move(o.name)),
          payload(std::move(o.payload)),
          status(o.status.load(std::memory_order_relaxed)),
          reason(std::move(o.reason)),
          callSiteFile(std::move(o.callSiteFile)),
          callSiteLine(o.callSiteLine) {}
    Entry& operator=(const Entry&) = delete;  // append-only by design
    Entry& operator=(Entry&&)      = delete;
};

// Append an entry to the queue. The entry's zone is derived from its
// owning plugin's load_order::Effective state at append time. Returns
// a stable handle id (>= 1) that binders use to wire up the Lua
// handle userdata's :applied() field. Handle id 0 means "append
// failed" (the entry was not enqueued; the err string explains why).
//
// `err_out` is set when the return value is 0; otherwise left
// unmodified. The error string is suitable for direct return through
// the (nil, err) Lua convention.
//
// Per-kind apply handlers must be registered via RegisterApplyHandler
// before any entry of that kind is appended.
uint64_t Append(Entry&& e, std::string* err_out);

// Look up an entry by handle id. Returns nullptr if not found.
// Pointer remains valid for the process lifetime (append-only; no
// reallocation visible to consumers because we use a node-stable
// container — see implementation).
const Entry* Find(uint64_t handleId);
Entry*       FindMut(uint64_t handleId);

// Invoke `fn` once for every queued entry whose kind == `k`, in append
// order. The registry stays PAYLOAD-AGNOSTIC: it hands the consumer the
// const Entry& (kind / name / priority / status / payload as
// shared_ptr<void>) and the consumer casts the payload back to its own
// type — the registry never interprets a payload (a Kind::Bytes payload is
// a kcdx::patch::PatchEntry, owned and cast by the patch engine; the
// registry does not know that type).
//
// Added for the patch engine's GetAppliedBytesPatchesAtTarget (COMP-15),
// which needs to enumerate Kind::Bytes entries without a per-kind index.
// The whole walk runs UNDER the registry's queue mutex, so `fn` must not
// re-enter the registry (no Append / ApplyZone / Find* from inside it) and
// should be a cheap read-only inspection — same constraint that keeps the
// snapshot-then-release pattern in ApplyZone deadlock-free.
void ForEachEntryOfKind(Kind k, const std::function<void(const Entry&)>& fn);

// Set the lifecycle status (and optionally the diagnostic reason) of the
// entry with the given handle id. Intended caller: hook_chain::Uninstall
// (and future per-kind Uninstall paths) flipping an entry to
// Status::Removed after the installed-side has been logically removed.
// The existing apply pipeline writes status atomically itself; standardizing
// via this helper keeps the cross-TU write surface explicit. No-op if the
// handle is unknown.
void SetStatus(uint64_t handleId, Status s, const std::string& reason = "");

// Per-kind apply handler. The handler is invoked once per matching
// entry during ApplyZone, in unified load order. It updates the
// entry's status + reason.
//
// Returning true means "applied successfully"; false means "rejected"
// (the handler should populate entry.reason). Either way the registry
// flips entry.status appropriately afterward — the handler doesn't
// touch atomic status itself.
using ApplyHandler = bool (*)(Entry& entry, std::string& reason_out);
void RegisterApplyHandler(Kind k, ApplyHandler fn);

// Walk the queue for `zone`, run pre-flight + per-kind apply in
// unified load order, update handle statuses. Idempotent — entries
// that have already been applied or failed are skipped. Safe to call
// multiple times per zone (later kcdx.* calls accumulate and the next
// apply pass picks them up).
//
// Returns the number of entries newly transitioned (Pending → Applied
// or Pending → Failed) by this call.
size_t ApplyZone(kcdx::load_order::Zone zone);

// Push a kcdx.registry handle userdata for the entry with the given
// id onto the Lua stack. The userdata's metatable provides
// :applied(), :reason(), :name(), :wait_applied() (the last is
// currently a stub; a future version implements it via coroutines).
//
// If handleId == 0 (failed Append), pushes nil + err onto the stack
// and returns 2 (the standard kcdx-binder error-return idiom).
//
// Caller is responsible for the return-value count semantic in their
// Lua-C function.
int PushHandleOrError(lua_State* L, uint64_t handleId,
                      const std::string& errIfNoHandle);

// Install the handle metatable into the registry. Called once during
// kcdx::lua_bind::RegisterKcdxTable. Idempotent.
void EnsureHandleMetatable(lua_State* L);

// Register that the Lua chunk loaded from `scriptPath` belongs to
// plugin `pluginName`. Called by the Lua plugin loader immediately
// before it runs each [entrypoints].lua file, so any kcdx.* call made
// from that file (or anything it requires synchronously) attributes to
// the right plugin. `scriptPath` is the path as it will appear in
// debug.getinfo's `source` field (normalized; see implementation).
//
// Idempotent: re-registering the same path overwrites the owner.
void RegisterScriptOwner(const std::string& scriptPath,
                         const std::string& pluginName);

// Resolved owner identity of a Lua call. Carries the 2-dot namespace
// components — `author` (from the calling plugin manifest's
// [plugin].author) and `plugin` (from [plugin].name) — both stamped on
// every registered Entry, and threaded into name resolvers
// (address_library::ResolveByName / symbols::Lookup /
// ResolveSignatureByName / FindResolvedAuthorTarget /
// address_library::RegisterAlias) so the self > engine > other
// precedence is keyed on the full <author>.<plugin> identity.
//
// Both fields may be empty:
//   - plugin == "" (and therefore author == "") for an anonymous caller
//     (console / pak Lua / ad-hoc) — those entries apply anonymously at
//     after_game / priority 50.
//   - author == "" while plugin != "" during the in-progress namespace
//     refactor — a plugin whose [plugin].author is not yet declared.
//     The resolver tolerates an empty author by walking the legacy
//     1-dot tier under (plugin, name).
struct OwningPlugin {
    std::string author;
    std::string plugin;
};

// Resolve the owning plugin for the current Lua call. Implementation:
// walk the Lua callstack via debug.getinfo, then look up the source
// file in the plugin-by-script-path index populated by
// RegisterScriptOwner, then cross-reference the resolved plugin name
// against kcdx::plugins::g_manifests to pull out the matching manifest's
// [plugin].author (so the resolver receives the full identity, not just
// the plugin component).
//
// Returns {"", ""} when the call site doesn't map to a known plugin
// script (anonymous caller).
//
// Single struct (rather than parallel functions) so callers do ONE
// stack-walk, ONE manifest lookup, and adding more identity fields here
// is append-only on the struct — no further fanout of the helper.
OwningPlugin OwningPluginForCurrentCall(lua_State* L,
                                        std::string& callSiteFileOut,
                                        int& callSiteLineOut);

// Register a "ready" lifecycle callback for `pluginName`. `callbackRef`
// is a luaL_ref into LUA_REGISTRYINDEX (the kcdx.on binder takes it).
// Stored keyed by owning plugin; a plugin may call kcdx.on("ready", ...)
// more than once, so callbacks accumulate in a list and fire in
// registration order. Anonymous callers (pluginName == "") are supported
// and fire in the after_game zone — matching how ApplyZone defaults
// anonymous ENTRIES.
//
// Each ready callback fires EXACTLY ONCE: after the plugin's zone apply
// pass transitions all of that zone's entries to a final status (so
// handle:applied()/:reason() are final), the callback runs, then its
// registry ref is released (luaL_unref) and it is removed from the
// pending set. A later ApplyZone tick must not re-fire it. Firing
// happens at the end of ApplyZone(zone) for the matching zone — see the
// implementation; hooks.cpp needs no separate dispatch call.
void RegisterReadyCallback(const std::string& pluginName, int callbackRef);

}  // namespace kcdx::lua_registry
