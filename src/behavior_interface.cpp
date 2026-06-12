// kcdx::behavior_interface — engine-side impl of kcdxBehaviorInterface.
//
// The C++ mirror of the Lua kcdx.behavior.* binder (src/lua_bind_behavior.cpp).
// The four verbs route into the SAME behavior_registry both languages share; a
// behavior declared in C++ is settable/listable from Lua and vice versa. This
// file owns:
//   - the four verb thunks (Declare/Set/Get/List), each validating C-ABI inputs,
//     resolving the owning plugin from the handle, and routing to the registry;
//   - the engine-owned VALUE-HANDLE model — an opaque kcdxBehaviorValue maps to
//     (behavior full name | a kcdx-side built value | a table-child path) + the
//     generation it was minted against. Values live on the ONE VM; this file
//     NEVER marshals a value out — the coercion / table accessors deref the ref
//     ON the VM (main thread) and coerce;
//   - generation-checked staleness (a handle whose behavior's generation has
//     advanced is STALE — a teaching error, never a dangle);
//   - the C++-side value BUILDERS (typed scalars/strings/tables, a callable
//     value), which pin a value on the VM and hand back a handle;
//   - the QUERY thread-wall (load-wave-gated + main-thread post-load; an
//     off-thread post-load query/build is a loud teaching error);
//   - the per-thread error channel (GetLastError).
//
// The VM is hooks::CurrentLuaState() (the ONE kcdx-built+adopted state). During a
// C++ plugin's kcdxPlugin_Load (the worker load wave), BuildAndAdoptVM already
// published it (dllmain orders BuildAndAdoptVM before DiscoverAndLoad), and the
// VM-adoption wave-end gate (lua_vm_build) holds the engine off the VM until the
// C++ wave is done — so a load-wave query reaches the live VM under the gated
// guarantee.
//
// Invoke (calling a callable value) and the off-thread QUEUED Set are the later
// step (P2 s2). NewCallable mints a callable value now (full parity — no Lua-only
// type), but the ACCESSOR that calls it appends after the v1 ABI markers when it
// lands. An off-thread post-load Set returns the queued-path-lands-later teaching
// error.

#include "behavior_interface.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

extern "C" {
#include "lua.h"      // the value model derefs/pins refs on the live VM
#include "lauxlib.h"  // luaL_ref / luaL_unref (registry refs)
}

#include "behavior_registry.h"
#include "hooks.h"             // CurrentLuaState — the ONE kcdx-built VM
#include "init_phase.h"        // init::Current — the early-stop window check
#include "load_order.h"        // IsPluginEnabled / RunsBefore — §6 discrimination
#include "log.h"               // LOG_*_KV, LOG_PLUGIN_ERROR, ::kcdx::log::KV
#include "lua_vm_build.h"      // CppWaveEnded — the wave-end gate order read
#include "plugin_loader.h"     // AuthorForHandle / NameForHandle / g_manifests
#include "zone_gate.h"         // RejectReason — §6 engine-rejected branch

namespace kcdx::behavior_interface {

namespace {

constexpr const char* kCat = "BEHAVIOR_INTERFACE";

// Forward decl — the C-impl trampoline (defined below) is used by Declare /
// NewCallable above its definition.
int BuildCImplTrampoline(lua_State* L, kcdxBehaviorImplFn fn, void* ctx,
                         bool isRevert);

// === The per-thread error channel ===
// Thread-local so a concurrent C++ caller never reads another thread's error.
thread_local std::string g_lastError;

void SetError(kcdxPluginHandle owningPlugin, const char* verb,
              const std::string& msg) {
    g_lastError = msg;
    LOG_ERROR_KV(kCat, "call_failed",
        ::kcdx::log::KV("verb",   verb),
        ::kcdx::log::KV("plugin", kcdx::plugins::NameForHandle(owningPlugin).c_str()),
        ::kcdx::log::KV("reason", msg.c_str()));
    if (owningPlugin != kcdxInvalidPluginHandle) {
        LOG_PLUGIN_ERROR(owningPlugin, kCat,
            "kcdxBehaviorInterface::%s — %s", verb, msg.c_str());
    }
}

void ClearError() { g_lastError.clear(); }

// === Owner identity from the plugin handle (self > engine > other) ===
struct Owner { std::string author; std::string plugin; };
Owner OwnerFromHandle(kcdxPluginHandle h) {
    Owner o;
    o.author = kcdx::plugins::AuthorForHandle(h);
    o.plugin = kcdx::plugins::NameForHandle(h);
    return o;
}

// === The VALUE-HANDLE model ===
//
// An opaque kcdxBehaviorValue indexes this table. Two flavors:
//   - a BEHAVIOR-value handle: points at a behavior's current value via
//     (fullName, generation). The accessor re-reads the registry's generation;
//     if it advanced, the handle is STALE. The ref it reads is the registry's
//     CurrentValueRef (recorded if set, else default) — re-fetched at access time
//     so a not-yet-applied default vs an applied recorded value both resolve
//     truthfully.
//   - a BUILT-value handle: a kcdx-side value pinned via luaL_ref (NewBool/…/
//     NewTable). It carries its OWN ref + a synthetic generation that never goes
//     stale until consumed by Set/Declare (which luaL_unref + invalidate it). A
//     table CHILD handle (Index/Field) is a built-value handle minted by reading
//     the child off the VM into its own ref — it inherits the parent's identity
//     for staleness by being minted against the parent's generation snapshot.
//
// kHandleKind discriminates; generation gates staleness for behavior handles and
// consumption for built handles.
enum class Kind { Behavior, Built };

struct HandleRec {
    Kind        kind = Kind::Behavior;
    // Behavior flavor:
    std::string behaviorFullName;
    uint64_t    mintedGeneration = 0;  // the behavior's valueGeneration at mint
    // Built flavor:
    int         builtRef = LUA_NOREF;  // a luaL_ref this handle owns (Built only)
    bool        consumed = false;      // a built handle moved into Set/Declare/a table
};

std::map<uint64_t, HandleRec>& Handles() {
    static std::map<uint64_t, HandleRec> h;
    return h;
}
uint64_t& NextHandleId() {
    static uint64_t n = 1;  // 0 is the invalid sentinel
    return n;
}

uint64_t MintBehaviorHandle(const std::string& fullName, uint64_t generation) {
    const uint64_t id = NextHandleId()++;
    HandleRec r;
    r.kind = Kind::Behavior;
    r.behaviorFullName = fullName;
    r.mintedGeneration = generation;
    Handles()[id] = std::move(r);
    return id;
}

uint64_t MintBuiltHandle(int ref) {
    const uint64_t id = NextHandleId()++;
    HandleRec r;
    r.kind = Kind::Built;
    r.builtRef = ref;
    Handles()[id] = std::move(r);
    return id;
}

HandleRec* FindHandle(kcdxBehaviorValue v) {
    auto it = Handles().find(static_cast<uint64_t>(v));
    return (it == Handles().end()) ? nullptr : &it->second;
}

// === The QUERY/CONSTRUCT thread-wall (design §8) ===
//
// A query (Get + every accessor) and a value build need the live VM. During the
// load waves (pre-boundary) any thread is legal under the wave-end gate; post-load
// (boundary completed) only the game main thread is legal. Returns true + sets
// `errOut` when the call is OUT-OF-WINDOW (an off-thread post-load query) — the
// caller turns that into the thread teaching error.
bool OffThreadPostLoadQuery() {
    return kcdx::behavior_registry::PostLoad() && !kcdx::log::IsGameMainThread();
}

const char* kThreadPatterns =
    "a behavior query (Get + every value handle accessor) needs the live VM and "
    "is main-thread-only after load. Off-thread post-load, use one of the two "
    "sanctioned patterns: capture the value in your implementation/revert callback "
    "at apply time (the engine invokes it on the main thread with the value "
    "handle), or copy the value out on the main thread (e.g. from a "
    "kcdxPlugin_PostGameLoad or a main-thread task) before reading it off-thread.";

// === Resolve the live VM, or fail loud ===
lua_State* VmOrError(kcdxPluginHandle owningPlugin, const char* verb) {
    lua_State* L = kcdx::hooks::CurrentLuaState();
    if (!L) {
        SetError(owningPlugin, verb,
            "the kcdx Lua VM is not available yet — behavior values live in the "
            "one VM, which kcdx builds at engine init before any plugin loads. If "
            "you hit this from kcdxPlugin_Load, the VM build did not complete this "
            "boot (see the LUA_VM_BUILD ERROR).");
    }
    return L;
}

// === Discriminating resolution error for a Set miss (design §6) ===
// Mirrors the Lua binder's RaiseSetResolution branches (reorder / failed-load /
// disabled / rejected / absent / typo / bare-name). Shared registry → same
// resolution; the C++ surface emits the same teaching text (the wall is keyed on
// the early-vs-main stop, not the language).
std::vector<std::string> SplitDots(const std::string& s) {
    std::vector<std::string> segs; std::string cur;
    for (char c : s) { if (c == '.') { segs.push_back(cur); cur.clear(); } else cur.push_back(c); }
    segs.push_back(cur);
    return segs;
}

bool DeclarerHasAnyBehavior(const std::string& author, const std::string& plugin) {
    std::vector<const kcdx::behavior_registry::Behavior*> rows;
    kcdx::behavior_registry::Enumerate(author + "." + plugin + ".", rows);
    return !rows.empty();
}

std::string ResolutionMissError(const std::string& nameArg, const Owner& caller) {
    const std::vector<std::string> segs = SplitDots(nameArg);
    const bool prefixed = segs.size() == 3 && !segs[0].empty() &&
                          !segs[1].empty() && !segs[2].empty();
    if (prefixed && segs[0] == "kcdx" && segs[1] == "behavior") {
        return "the engine catalog declares no behavior 'kcdx.behavior." +
               segs[2] + "' — browse kcdx.behavior.list(\"kcdx.behavior.\") (it "
               "is empty until the catalog pack ships; a plugin behavior is set "
               "by its full <author>.<plugin>.<bare> name).";
    }
    if (prefixed) {
        const std::string& author = segs[0];
        const std::string& plugin = segs[1];
        const std::string& bare   = segs[2];
        const std::string owner   = author + "." + plugin;
        bool installed = false;
        for (const auto& m : kcdx::plugins::g_manifests) {
            if (m.author == author && m.name == plugin) { installed = true; break; }
        }
        if (!installed) {
            return "'" + bare + "' belongs to '" + owner + "', which is not "
                   "installed. Install that plugin (or check the prefix for a "
                   "typo); your set cannot resolve until its declarer loads.";
        }
        if (!kcdx::load_order::IsPluginEnabled(plugin)) {
            const std::string& reject = kcdx::zone_gate::RejectReason(owner);
            if (!reject.empty()) {
                return "'" + bare + "' belongs to '" + owner + "', which was "
                       "rejected by the engine (" + reject + "). Fix the cause; "
                       "your set cannot resolve until its declarer loads.";
            }
            return "'" + bare + "' belongs to '" + owner + "', which is installed "
                   "but disabled (load_order.toml). Enable that plugin.";
        }
        if (DeclarerHasAnyBehavior(author, plugin)) {
            return "'" + owner + "' is loaded but declares no behavior '" + bare +
                   "' — check the name against kcdx.behavior.list(\"" + owner +
                   ".\") (a typo, or a behavior a new version removed).";
        }
        if (caller.plugin.empty()) {
            return "'" + owner + "' declares no behavior '" + bare + "' so far. "
                   "Set plugin behaviors from your main entry "
                   "(kcdxPlugin_PostGameLoad), not an early stop.";
        }
        if (kcdx::load_order::RunsBefore(caller.plugin, plugin)) {
            return "'" + owner + "' loads after you — move '" + caller.author +
                   "." + caller.plugin + "' below it (in load_order.toml) so its "
                   "declares run before your set.";
        }
        return "'" + owner + "' is loaded but declares no behavior '" + bare +
               "' — check kcdx.behavior.list(\"" + owner + ".\").";
    }
    return "no plugin loaded so far declares '" + nameArg + "'. If it belongs to "
           "another plugin, use its full <author>.<plugin>.<bare> name; browse "
           "kcdx.behavior.list() to see what is declared.";
}

// ====================================================================
// Verb thunks
// ====================================================================

bool Thunk_Declare(const char* name, const char* description,
                   kcdxBehaviorValue defaultValue, kcdxBehaviorImplFn impl,
                   kcdxBehaviorRevertFn revert, void* userCtx,
                   kcdxPluginHandle owningPlugin) {
    ClearError();
    (void)impl; (void)revert; (void)userCtx;
    if (!name || !*name) {
        SetError(owningPlugin, "Declare",
            "name (the BARE behavior name) is required and must be non-empty — "
            "the engine stamps <author>.<plugin>.<name> from your manifest.");
        return false;
    }
    if (!description || !*description) {
        SetError(owningPlugin, "Declare",
            "description is required (one human line, surfaced by List()).");
        return false;
    }
    if (!impl) {
        SetError(owningPlugin, "Declare",
            "implementation (impl) is required — a kcdxBehaviorImplFn invoked once "
            "at the apply boundary with the final settled value.");
        return false;
    }
    // The declare window: declares are a load-time act (design §5.4). Symmetric
    // with the Lua binder's BoundaryCompleted gate.
    if (kcdx::behavior_registry::BoundaryCompleted()) {
        SetError(owningPlugin, "Declare",
            "declares are a load-time act — this call arrived after the plugin "
            "load waves finished. Declare from kcdxPlugin_Load / "
            "kcdxPlugin_PostGameLoad, not a post-load callback.");
        return false;
    }
    const Owner owner = OwnerFromHandle(owningPlugin);
    if (owner.plugin.empty()) {
        SetError(owningPlugin, "Declare",
            "no owning plugin for this call — behaviors are declared by plugins; "
            "pass your own handle (api->GetPluginHandle(\"your.name\")) as "
            "owningPlugin so the engine can stamp <author>.<plugin>.<bare>.");
        return false;
    }
    if (owner.author.empty()) {
        SetError(owningPlugin, "Declare",
            "your plugin has no [plugin].author in its manifest — the engine "
            "stamps every behavior as <author>.<plugin>.<bare>; add "
            "[plugin].author to your kcdx.toml.");
        return false;
    }

    lua_State* L = VmOrError(owningPlugin, "Declare");
    if (!L) return false;

    // The default must be a real, non-consumed built-value handle (nil is the
    // unset sentinel; a behavior handle is not a value to declare). Pin its ref
    // into a fresh declare-owned default ref; consume the built handle.
    HandleRec* dv = FindHandle(defaultValue);
    if (!dv || dv->kind != Kind::Built || dv->consumed ||
        dv->builtRef == LUA_NOREF) {
        SetError(owningPlugin, "Declare",
            "defaultValue must be a value built via NewBool/NewInt64/NewDouble/"
            "NewString/NewTable/NewCallable (and not already consumed). nil is "
            "the unset sentinel, never a value; default is what Get() answers "
            "while the behavior was never set.");
        return false;
    }
    // The default ref the registry owns: re-pin the built value into its own ref
    // (the built handle's ref is released as the handle is consumed). Push the
    // built value, ref it fresh.
    lua_rawgeti(L, LUA_REGISTRYINDEX, dv->builtRef);
    const int defaultRef = luaL_ref(L, LUA_REGISTRYINDEX);
    luaL_unref(L, LUA_REGISTRYINDEX, dv->builtRef);
    dv->builtRef = LUA_NOREF;
    dv->consumed = true;

    // The C++ impl/revert are C function pointers. Wrapping them as Lua refs the
    // registry can pcall is the LATER-STEP bridge (the engine invokes a C
    // impl/revert at the boundary/toggle by routing through a C trampoline). For
    // s1 the registry stores the impl/revert as refs; a C impl is registered as a
    // callable VALUE built off the function pointer (NewCallable's mechanism),
    // pcall'd at the boundary the same way a Lua function is. Build the trampoline
    // refs now.
    //   NOTE: the boundary invokes implementationRef with the value as a Lua
    //   pcall; a C impl reached through a Lua-C-closure trampoline receives the
    //   value and forwards to the kcdxBehaviorImplFn with the value handle. That
    //   trampoline wiring is the engine-side C-impl bridge.
    const int implRef = BuildCImplTrampoline(L, impl, userCtx, /*isRevert=*/false);
    int revertRef = kcdx::behavior_registry::kNoRef;
    if (revert) {
        revertRef = BuildCImplTrampoline(L, reinterpret_cast<kcdxBehaviorImplFn>(revert),
                                         userCtx, /*isRevert=*/true);
    }

    std::string err;
    const bool ok = kcdx::behavior_registry::DeclarePlugin(
        owner.author, owner.plugin, name, description,
        defaultRef, implRef, revertRef, err);
    if (!ok) {
        luaL_unref(L, LUA_REGISTRYINDEX, defaultRef);
        luaL_unref(L, LUA_REGISTRYINDEX, implRef);
        if (revertRef != kcdx::behavior_registry::kNoRef) {
            luaL_unref(L, LUA_REGISTRYINDEX, revertRef);
        }
        SetError(owningPlugin, "Declare", err);
        return false;
    }
    return true;
}

bool Thunk_Set(const char* name, kcdxBehaviorValue value,
               kcdxPluginHandle owningPlugin) {
    ClearError();
    if (!name || !*name) {
        SetError(owningPlugin, "Set", "name is required and must be non-empty.");
        return false;
    }
    const Owner owner = OwnerFromHandle(owningPlugin);
    lua_State* L = VmOrError(owningPlugin, "Set");
    if (!L) return false;

    // The value must be a real, non-consumed built-value handle (nil is the
    // unset sentinel).
    HandleRec* bv = FindHandle(value);
    if (!bv || bv->kind != Kind::Built || bv->consumed ||
        bv->builtRef == LUA_NOREF) {
        SetError(owningPlugin, "Set",
            "value must be a value built via NewBool/NewInt64/NewDouble/NewString/"
            "NewTable/NewCallable (and not already consumed). nil is the unset "
            "sentinel — to leave a behavior unset, don't set it.");
        return false;
    }

    // Resolve. On a miss, the discriminating §6 set-resolution error.
    const kcdx::behavior_registry::Behavior* resolved =
        kcdx::behavior_registry::ResolveForCaller(owner.author, owner.plugin, name);
    if (!resolved) {
        SetError(owningPlugin, "Set", ResolutionMissError(name, owner));
        return false;
    }
    kcdx::behavior_registry::Behavior* b =
        kcdx::behavior_registry::LookupMutable(resolved->fullName);

    // The window law (design §6). A PLUGIN-tier Set from an EARLY stop (a
    // kcdxPlugin_Load on the worker, phase < EngineSubsystemsInit) is
    // OUT-OF-WINDOW — the declarer's plugin-tier behaviors do not exist yet at
    // the early stop. Catalog-tier (kcdx.behavior.*) names are settable from any
    // stop.
    if (resolved->tier == kcdx::behavior_registry::Tier::Plugin &&
        static_cast<int>(kcdx::init::Current()) <
            static_cast<int>(kcdx::init::InitPhase::EngineSubsystemsInit)) {
        SetError(owningPlugin, "Set",
            "plugin behaviors resolve at the main stop — set '" + b->fullName +
            "' from kcdxPlugin_PostGameLoad (the C++ main stop), not an early "
            "stop (kcdxPlugin_Load). The declarer's behaviors do not exist yet "
            "at the early stop; engine catalog names (kcdx.behavior.*) are the "
            "only behaviors settable that early.");
        return false;
    }

    // Pin the value into a fresh set-owned ref, consume the built handle.
    lua_rawgeti(L, LUA_REGISTRYINDEX, bv->builtRef);
    const int newValueRef = luaL_ref(L, LUA_REGISTRYINDEX);
    luaL_unref(L, LUA_REGISTRYINDEX, bv->builtRef);
    bv->builtRef = LUA_NOREF;
    bv->consumed = true;

    // Post-load? (boundary completed, or already-applied mid-drain) → the toggle.
    if (kcdx::behavior_registry::BoundaryCompleted() || b->applied) {
        // The OFF-THREAD post-load queued Set is the LATER step (P2 s2). For s1,
        // an off-thread post-load Set is a loud teaching error naming the
        // queued-path-lands-later status (the MAIN-thread path executes inline).
        if (!kcdx::log::IsGameMainThread()) {
            luaL_unref(L, LUA_REGISTRYINDEX, newValueRef);
            SetError(owningPlugin, "Set",
                "an off-thread post-load Set queues to the game main thread — the "
                "queued command path lands in a later step. For now, issue a "
                "post-load Set from the game main thread (e.g. a "
                "kcdxPlugin_PostGameLoad, a main-thread task, or an "
                "implementation callback).");
            return false;
        }
        std::string toggleErr;
        const bool ok = kcdx::behavior_registry::ApplyPostLoadToggle(
            L, b, newValueRef, toggleErr);
        if (ok) return true;
        if (!toggleErr.empty()) {
            // A teaching-error disposition (a revert-less behavior). The registry
            // released the ref + left the record untouched.
            SetError(owningPlugin, "Set", toggleErr);
            return false;
        }
        // A declarer-code raise: already logged attributed to the declarer; the
        // set itself does not "fail" at the consumer site (mirror of the Lua
        // binder). Report success — the failure is the declarer's, in the log.
        return true;
    }

    // Load-window record (last-wins) — the registry-owned record + generation
    // bump (so an outstanding C++ value handle on this behavior goes stale).
    kcdx::behavior_registry::RecordLoadSet(L, b, newValueRef,
                                           owner.author, owner.plugin);
    // The consumer->declarer set-edge (feeds ordering).
    kcdx::behavior_registry::RecordEdge(owner.author, owner.plugin, b->fullName);
    return true;
}

bool Thunk_Get(const char* name, kcdxBehaviorValue* outValue,
               kcdxPluginHandle owningPlugin) {
    ClearError();
    if (!outValue) {
        SetError(owningPlugin, "Get", "outValue is required (a kcdxBehaviorValue*).");
        return false;
    }
    if (!name || !*name) {
        SetError(owningPlugin, "Get", "name is required and must be non-empty.");
        return false;
    }
    // The QUERY thread-wall: off-thread post-load is out-of-window.
    if (OffThreadPostLoadQuery()) {
        SetError(owningPlugin, "Get", kThreadPatterns);
        return false;
    }
    const Owner owner = OwnerFromHandle(owningPlugin);
    const kcdx::behavior_registry::Behavior* b =
        kcdx::behavior_registry::ResolveForCaller(owner.author, owner.plugin, name);
    if (!b) {
        SetError(owningPlugin, "Get",
            "no declared behavior matches '" + std::string(name) + "' so far. "
            "Browse kcdx.behavior.list(); to reach another plugin's behavior, use "
            "its full <author>.<plugin>.<bare> name.");
        return false;
    }
    // Mint a value handle against the behavior's current generation. The
    // accessors re-read the generation to detect staleness.
    *outValue = MintBehaviorHandle(b->fullName, b->valueGeneration);
    return true;
}

uint32_t Thunk_List(const char* prefix, kcdxBehaviorListCb callback,
                    void* userCtx) {
    ClearError();
    if (!callback) return 0;
    std::vector<const kcdx::behavior_registry::Behavior*> rows;
    kcdx::behavior_registry::Enumerate(prefix ? prefix : "", rows);
    // Stable storage for the per-entry strings + the declarer label across the
    // callback (DeclarerLabel returns a temporary).
    for (const auto* b : rows) {
        const std::string declarer = b->DeclarerLabel();
        kcdxBehaviorListEntry e;
        e.name        = b->fullName.c_str();
        e.description = b->description.c_str();
        e.declarer    = declarer.c_str();
        e.current     = MintBehaviorHandle(b->fullName, b->valueGeneration);
        callback(&e, userCtx);
    }
    return static_cast<uint32_t>(rows.size());
}

// ====================================================================
// Value-handle accessors
// ====================================================================

// Resolve a value handle to a (live, non-stale) Lua ref + push it on the VM at
// the top of stack; returns the access result. On Ok the value is at L's top
// (caller pops). On a non-Ok result the stack is unchanged and g_lastError is
// set. Generation-checked for behavior handles; consumed-checked for built.
kcdxBehaviorAccess PushHandleValue(lua_State* L, kcdxBehaviorValue v) {
    HandleRec* r = FindHandle(v);
    if (!r) {
        g_lastError = "unknown value handle (0 or a never-minted handle).";
        return kcdxBehaviorAccess_BadHandle;
    }
    if (r->kind == Kind::Built) {
        if (r->consumed || r->builtRef == LUA_NOREF) {
            g_lastError = "this built value was already consumed (passed to "
                          "Set/Declare or a table builder).";
            return kcdxBehaviorAccess_BadHandle;
        }
        lua_rawgeti(L, LUA_REGISTRYINDEX, r->builtRef);
        return kcdxBehaviorAccess_Ok;
    }
    // Behavior handle: re-read the behavior + check the generation.
    const kcdx::behavior_registry::Behavior* b =
        kcdx::behavior_registry::Lookup(r->behaviorFullName);
    if (!b) {
        g_lastError = "the behavior this handle named is no longer registered.";
        return kcdxBehaviorAccess_BadHandle;
    }
    if (b->valueGeneration != r->mintedGeneration) {
        g_lastError = "stale value handle — the behavior '" + b->fullName +
                      "'s recorded value was replaced after this handle was "
                      "obtained (a later set/toggle). Re-Get() the behavior for a "
                      "fresh handle; a handle is valid only while its value is the "
                      "behavior's current recorded value.";
        return kcdxBehaviorAccess_Stale;
    }
    const int ref = kcdx::behavior_registry::CurrentValueRef(b);
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    return kcdxBehaviorAccess_Ok;
}

// The shared pre-amble for an accessor: thread-wall + VM + push the value. On a
// non-Ok return, *outAccess carries the result and nothing is on the stack.
// Returns L (with the value pushed) on Ok, else nullptr.
lua_State* AccessorPush(kcdxBehaviorValue v, kcdxBehaviorAccess* outAccess) {
    if (OffThreadPostLoadQuery()) {
        g_lastError = kThreadPatterns;
        *outAccess = kcdxBehaviorAccess_Thread;
        return nullptr;
    }
    lua_State* L = kcdx::hooks::CurrentLuaState();
    if (!L) {
        g_lastError = "the kcdx Lua VM is not available — behavior values live in "
                      "the one VM.";
        *outAccess = kcdxBehaviorAccess_BadHandle;
        return nullptr;
    }
    const kcdxBehaviorAccess a = PushHandleValue(L, v);
    if (a != kcdxBehaviorAccess_Ok) { *outAccess = a; return nullptr; }
    return L;
}

kcdxBehaviorType Thunk_TypeOf(kcdxBehaviorValue v) {
    g_lastError.clear();
    kcdxBehaviorAccess a;
    lua_State* L = AccessorPush(v, &a);
    if (!L) return kcdxBehaviorType_Invalid;
    const int t = lua_type(L, -1);
    lua_pop(L, 1);
    switch (t) {
        case LUA_TNIL:      return kcdxBehaviorType_Nil;
        case LUA_TBOOLEAN:  return kcdxBehaviorType_Bool;
        case LUA_TNUMBER:   return kcdxBehaviorType_Number;
        case LUA_TSTRING:   return kcdxBehaviorType_String;
        case LUA_TTABLE:    return kcdxBehaviorType_Table;
        case LUA_TFUNCTION: return kcdxBehaviorType_Function;
        default:            return kcdxBehaviorType_Invalid;
    }
}

// Name the actual Lua type at the VM top for a coercion-mismatch teaching error.
std::string TypeNameAtTop(lua_State* L) {
    return lua_typename(L, lua_type(L, -1));
}

kcdxBehaviorAccess Thunk_AsBool(kcdxBehaviorValue v, bool* out) {
    g_lastError.clear();
    if (!out) { g_lastError = "out is required."; return kcdxBehaviorAccess_BadHandle; }
    kcdxBehaviorAccess a;
    lua_State* L = AccessorPush(v, &a);
    if (!L) return a;
    if (lua_type(L, -1) != LUA_TBOOLEAN) {
        g_lastError = "AsBool on a " + TypeNameAtTop(L) +
                      " value — the value is not a boolean.";
        lua_pop(L, 1);
        return kcdxBehaviorAccess_TypeError;
    }
    *out = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return kcdxBehaviorAccess_Ok;
}

kcdxBehaviorAccess Thunk_AsInt64(kcdxBehaviorValue v, int64_t* out) {
    g_lastError.clear();
    if (!out) { g_lastError = "out is required."; return kcdxBehaviorAccess_BadHandle; }
    kcdxBehaviorAccess a;
    lua_State* L = AccessorPush(v, &a);
    if (!L) return a;
    if (lua_type(L, -1) != LUA_TNUMBER) {
        g_lastError = "AsInt64 on a " + TypeNameAtTop(L) +
                      " value — the value is not a number.";
        lua_pop(L, 1);
        return kcdxBehaviorAccess_TypeError;
    }
    *out = static_cast<int64_t>(lua_tonumber(L, -1));
    lua_pop(L, 1);
    return kcdxBehaviorAccess_Ok;
}

kcdxBehaviorAccess Thunk_AsDouble(kcdxBehaviorValue v, double* out) {
    g_lastError.clear();
    if (!out) { g_lastError = "out is required."; return kcdxBehaviorAccess_BadHandle; }
    kcdxBehaviorAccess a;
    lua_State* L = AccessorPush(v, &a);
    if (!L) return a;
    if (lua_type(L, -1) != LUA_TNUMBER) {
        g_lastError = "AsDouble on a " + TypeNameAtTop(L) +
                      " value — the value is not a number.";
        lua_pop(L, 1);
        return kcdxBehaviorAccess_TypeError;
    }
    *out = static_cast<double>(lua_tonumber(L, -1));
    lua_pop(L, 1);
    return kcdxBehaviorAccess_Ok;
}

// The engine-owned per-thread string buffer AsString returns (valid until the
// next accessor call on this thread).
thread_local std::string g_strBuf;

kcdxBehaviorAccess Thunk_AsString(kcdxBehaviorValue v, const char** outStr,
                                  size_t* outLen) {
    g_lastError.clear();
    if (!outStr) { g_lastError = "outStr is required."; return kcdxBehaviorAccess_BadHandle; }
    kcdxBehaviorAccess a;
    lua_State* L = AccessorPush(v, &a);
    if (!L) return a;
    if (lua_type(L, -1) != LUA_TSTRING) {
        g_lastError = "AsString on a " + TypeNameAtTop(L) +
                      " value — the value is not a string.";
        lua_pop(L, 1);
        return kcdxBehaviorAccess_TypeError;
    }
    size_t len = 0;
    const char* s = lua_tolstring(L, -1, &len);
    g_strBuf.assign(s, len);  // copy off the VM (the ref leaves scope on pop)
    lua_pop(L, 1);
    *outStr = g_strBuf.c_str();
    if (outLen) *outLen = g_strBuf.size();
    return kcdxBehaviorAccess_Ok;
}

kcdxBehaviorAccess Thunk_Length(kcdxBehaviorValue v, size_t* outLen) {
    g_lastError.clear();
    if (!outLen) { g_lastError = "outLen is required."; return kcdxBehaviorAccess_BadHandle; }
    kcdxBehaviorAccess a;
    lua_State* L = AccessorPush(v, &a);
    if (!L) return a;
    if (lua_type(L, -1) != LUA_TTABLE) {
        g_lastError = "Length on a " + TypeNameAtTop(L) +
                      " value — the value is not a table.";
        lua_pop(L, 1);
        return kcdxBehaviorAccess_TypeError;
    }
    *outLen = lua_objlen(L, -1);
    lua_pop(L, 1);
    return kcdxBehaviorAccess_Ok;
}

// Mint a child handle by reading the table child at the VM top into its own ref.
// The child is a BUILT handle (it owns its own ref) so it survives independent of
// the parent. A child of a stale/mismatched parent never reaches here (the parent
// push is generation-checked). Pushes nothing net (pops the child + parent).
kcdxBehaviorAccess ReadChild(lua_State* L, kcdxBehaviorValue* outChild) {
    // Child value is at L top; ref it into its own slot.
    const int childRef = luaL_ref(L, LUA_REGISTRYINDEX);
    *outChild = MintBuiltHandle(childRef);
    return kcdxBehaviorAccess_Ok;
}

kcdxBehaviorAccess Thunk_Index(kcdxBehaviorValue table, int64_t index,
                               kcdxBehaviorValue* outChild) {
    g_lastError.clear();
    if (!outChild) { g_lastError = "outChild is required."; return kcdxBehaviorAccess_BadHandle; }
    kcdxBehaviorAccess a;
    lua_State* L = AccessorPush(table, &a);  // parent at top
    if (!L) return a;
    if (lua_type(L, -1) != LUA_TTABLE) {
        g_lastError = "Index on a " + TypeNameAtTop(L) +
                      " value — the value is not a table.";
        lua_pop(L, 1);
        return kcdxBehaviorAccess_TypeError;
    }
    lua_rawgeti(L, -1, static_cast<int>(index));  // child at top, parent below
    const kcdxBehaviorAccess r = ReadChild(L, outChild);  // pops child
    lua_pop(L, 1);  // pop the parent
    return r;
}

kcdxBehaviorAccess Thunk_Field(kcdxBehaviorValue table, const char* key,
                               kcdxBehaviorValue* outChild) {
    g_lastError.clear();
    if (!outChild) { g_lastError = "outChild is required."; return kcdxBehaviorAccess_BadHandle; }
    if (!key) { g_lastError = "key is required."; return kcdxBehaviorAccess_BadHandle; }
    kcdxBehaviorAccess a;
    lua_State* L = AccessorPush(table, &a);
    if (!L) return a;
    if (lua_type(L, -1) != LUA_TTABLE) {
        g_lastError = "Field on a " + TypeNameAtTop(L) +
                      " value — the value is not a table.";
        lua_pop(L, 1);
        return kcdxBehaviorAccess_TypeError;
    }
    lua_getfield(L, -1, key);  // child at top, parent below
    const kcdxBehaviorAccess r = ReadChild(L, outChild);  // pops child
    lua_pop(L, 1);  // pop parent
    return r;
}

// ====================================================================
// Value builders — pin a value on the VM, return a built handle.
// ====================================================================

// The build thread-wall: a build needs the live VM on the main thread post-load.
lua_State* BuilderVm() {
    if (OffThreadPostLoadQuery()) {
        g_lastError = std::string("value construction needs the live VM — ") +
                      kThreadPatterns;
        return nullptr;
    }
    lua_State* L = kcdx::hooks::CurrentLuaState();
    if (!L) g_lastError = "the kcdx Lua VM is not available for value construction.";
    return L;
}

kcdxBehaviorValue Thunk_NewBool(bool v) {
    g_lastError.clear();
    lua_State* L = BuilderVm();
    if (!L) return 0;
    lua_pushboolean(L, v ? 1 : 0);
    return MintBuiltHandle(luaL_ref(L, LUA_REGISTRYINDEX));
}

kcdxBehaviorValue Thunk_NewInt64(int64_t v) {
    g_lastError.clear();
    lua_State* L = BuilderVm();
    if (!L) return 0;
    lua_pushnumber(L, static_cast<lua_Number>(v));
    return MintBuiltHandle(luaL_ref(L, LUA_REGISTRYINDEX));
}

kcdxBehaviorValue Thunk_NewDouble(double v) {
    g_lastError.clear();
    lua_State* L = BuilderVm();
    if (!L) return 0;
    lua_pushnumber(L, static_cast<lua_Number>(v));
    return MintBuiltHandle(luaL_ref(L, LUA_REGISTRYINDEX));
}

kcdxBehaviorValue Thunk_NewString(const char* s, size_t len) {
    g_lastError.clear();
    if (!s) { g_lastError = "NewString: s is null."; return 0; }
    lua_State* L = BuilderVm();
    if (!L) return 0;
    if (len == 0) lua_pushstring(L, s);
    else          lua_pushlstring(L, s, len);
    return MintBuiltHandle(luaL_ref(L, LUA_REGISTRYINDEX));
}

kcdxBehaviorValue Thunk_NewTable(void) {
    g_lastError.clear();
    lua_State* L = BuilderVm();
    if (!L) return 0;
    lua_newtable(L);
    return MintBuiltHandle(luaL_ref(L, LUA_REGISTRYINDEX));
}

// Push a built table handle at the VM top (validated), or return false + error.
bool PushBuiltTable(lua_State* L, kcdxBehaviorValue table, const char* who) {
    HandleRec* r = FindHandle(table);
    if (!r || r->kind != Kind::Built || r->consumed || r->builtRef == LUA_NOREF) {
        g_lastError = std::string(who) + ": the table must be a NewTable() handle "
                      "(not consumed).";
        return false;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, r->builtRef);
    if (lua_type(L, -1) != LUA_TTABLE) {
        lua_pop(L, 1);
        g_lastError = std::string(who) + ": the handle is not a table value.";
        return false;
    }
    return true;
}

// Consume a built child handle: push its value at the VM top, then release the
// child's ref + mark it consumed (its value is moved into the table). Returns
// false + error if the child is not a valid built handle.
bool ConsumeChildAtTop(lua_State* L, kcdxBehaviorValue child, const char* who) {
    HandleRec* c = FindHandle(child);
    if (!c || c->kind != Kind::Built || c->consumed || c->builtRef == LUA_NOREF) {
        g_lastError = std::string(who) + ": the child must be a built value "
                      "(NewBool/.../NewTable) and not already consumed.";
        return false;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, c->builtRef);  // child value at top
    luaL_unref(L, LUA_REGISTRYINDEX, c->builtRef);
    c->builtRef = LUA_NOREF;
    c->consumed = true;
    return true;
}

kcdxBehaviorAccess Thunk_SetIndex(kcdxBehaviorValue table, int64_t index,
                                  kcdxBehaviorValue child) {
    g_lastError.clear();
    lua_State* L = BuilderVm();
    if (!L) return kcdxBehaviorAccess_Thread;
    if (!PushBuiltTable(L, table, "SetIndex")) return kcdxBehaviorAccess_TypeError;
    // table at top. Push the child value above it, then rawseti.
    if (!ConsumeChildAtTop(L, child, "SetIndex")) {
        lua_pop(L, 1);  // pop the table
        return kcdxBehaviorAccess_BadHandle;
    }
    // stack: ... table child  -> rawseti(table_idx=-2)
    lua_rawseti(L, -2, static_cast<int>(index));
    lua_pop(L, 1);  // pop the table
    return kcdxBehaviorAccess_Ok;
}

kcdxBehaviorAccess Thunk_SetField(kcdxBehaviorValue table, const char* key,
                                  kcdxBehaviorValue child) {
    g_lastError.clear();
    if (!key) { g_lastError = "SetField: key is null."; return kcdxBehaviorAccess_BadHandle; }
    lua_State* L = BuilderVm();
    if (!L) return kcdxBehaviorAccess_Thread;
    if (!PushBuiltTable(L, table, "SetField")) return kcdxBehaviorAccess_TypeError;
    if (!ConsumeChildAtTop(L, child, "SetField")) {
        lua_pop(L, 1);
        return kcdxBehaviorAccess_BadHandle;
    }
    lua_setfield(L, -2, key);  // pops the child; table stays
    lua_pop(L, 1);             // pop the table
    return kcdxBehaviorAccess_Ok;
}

kcdxBehaviorValue Thunk_NewCallable(kcdxBehaviorImplFn fn, void* userCtx) {
    g_lastError.clear();
    if (!fn) { g_lastError = "NewCallable: fn is null."; return 0; }
    lua_State* L = BuilderVm();
    if (!L) return 0;
    // Register the C function + context as a callable VALUE (a Lua C closure
    // forwarding to fn with the value handle). The ACCESSOR that CALLS it
    // (Invoke) is the later step — minting it now keeps a callable value
    // representable (full parity: no value type is Lua-only).
    const int ref = BuildCImplTrampoline(L, fn, userCtx, /*isRevert=*/false);
    if (ref == kcdx::behavior_registry::kNoRef) {
        g_lastError = "NewCallable: failed to build the callable trampoline.";
        return 0;
    }
    return MintBuiltHandle(ref);
}

const char* Thunk_GetLastError(void) {
    return g_lastError.empty() ? nullptr : g_lastError.c_str();
}

// ====================================================================
// C-impl trampoline — wrap a kcdxBehaviorImplFn + context as a Lua-callable ref
// the registry pcalls at the boundary/toggle. The Lua upvalue carries the fn ptr
// + ctx as light userdata; the closure receives the value (arg 1), wraps it as a
// transient value handle, and forwards to the C fn.
// ====================================================================

// Light-userdata-carried (fn, ctx, isRevert) bundle. Heap-owned process-lifetime
// (a behavior's impl/revert lives for the session; never freed — boot-time
// declares only).
struct CImplBundle {
    kcdxBehaviorImplFn fn = nullptr;
    void*              ctx = nullptr;
    bool               isRevert = false;
};

// The Lua C closure: arg 1 is the value the boundary/toggle passes. Wrap it as a
// transient BUILT value handle (refs the value so the C fn's accessors can read
// it), call the C fn, then release the transient handle.
int CImplClosure(lua_State* L) {
    CImplBundle* b = static_cast<CImplBundle*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    if (!b || !b->fn) return 0;
    // Ref arg 1 (the value) into a transient built handle.
    lua_pushvalue(L, 1);
    const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    const uint64_t handle = MintBuiltHandle(ref);
    if (b->isRevert) {
        reinterpret_cast<kcdxBehaviorRevertFn>(b->fn)(handle, b->ctx);
    } else {
        b->fn(handle, b->ctx);
    }
    // Release the transient handle's ref + drop the handle record.
    HandleRec* r = FindHandle(handle);
    if (r && r->builtRef != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, r->builtRef);
    }
    Handles().erase(static_cast<uint64_t>(handle));
    return 0;
}

int BuildCImplTrampoline(lua_State* L, kcdxBehaviorImplFn fn, void* ctx,
                         bool isRevert) {
    // Process-lifetime bundle (a declare is boot-time; the impl lives the
    // session). Never freed — matches the registry's process-lifetime refs.
    CImplBundle* b = new CImplBundle{fn, ctx, isRevert};
    lua_pushlightuserdata(L, b);
    lua_pushcclosure(L, CImplClosure, 1);
    return luaL_ref(L, LUA_REGISTRYINDEX);
}

// The engine-owned static interface instance.
kcdxBehaviorInterface g_interface = {
    /*Declare=*/      Thunk_Declare,
    /*Set=*/          Thunk_Set,
    /*Get=*/          Thunk_Get,
    /*List=*/         Thunk_List,
    /*TypeOf=*/       Thunk_TypeOf,
    /*AsBool=*/       Thunk_AsBool,
    /*AsInt64=*/      Thunk_AsInt64,
    /*AsDouble=*/     Thunk_AsDouble,
    /*AsString=*/     Thunk_AsString,
    /*Length=*/       Thunk_Length,
    /*Index=*/        Thunk_Index,
    /*Field=*/        Thunk_Field,
    /*NewBool=*/      Thunk_NewBool,
    /*NewInt64=*/     Thunk_NewInt64,
    /*NewDouble=*/    Thunk_NewDouble,
    /*NewString=*/    Thunk_NewString,
    /*NewTable=*/     Thunk_NewTable,
    /*SetIndex=*/     Thunk_SetIndex,
    /*SetField=*/     Thunk_SetField,
    /*NewCallable=*/  Thunk_NewCallable,
    /*GetLastError=*/ Thunk_GetLastError,
};

}  // namespace

const kcdxBehaviorInterface* GetInterface() {
    return &g_interface;
}

}  // namespace kcdx::behavior_interface
