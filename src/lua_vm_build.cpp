// See lua_vm_build.h. The keystone: kcdx builds the ONE Lua VM on the worker
// thread via the shim, publishes it (release), and installs the
// lua_newstate-callee intercept so the engine's CScriptSystem::Init adopts it.

#include "lua_vm_build.h"

#include <atomic>
#include <cstdint>

extern "C" {
#include "lua.h"  // lua_State, lua_Alloc (the lua_newstate signature)
}

#include "hook_chain.h"
#include "hook_payload.h"
#include "hook_signature.h"
#include "hooks.h"        // PublishLuaState — the authoritative g_L writer
#include "log.h"
#include "lua_shim.h"
#include "modification_inventory.h"
#include "refdb.h"

namespace kcdx::lua_vm_build {

namespace {

constexpr const char* kCategory = "LUA_VM_BUILD";

// The one state kcdx builds. Held here so the intercept's replace callback can
// return it on the game thread; the AUTHORITATIVE copy lives in hooks::g_L
// (published with release below). This static is written ONCE on the worker
// (release) before the intercept is installed, and read by the intercept callback
// on the game thread (acquire) — the same happens-before edge as g_L.
std::atomic<lua_State*> g_builtState{nullptr};

// Idempotence latch — a second BuildAndAdoptVM after a successful build is a
// no-op. Worker-thread only (BuildAndAdoptVM is called once from the worker
// sequence), but atomic for visibility against any future caller.
std::atomic<bool> g_done{false};

// Set true (release) by the intercept callback when CScriptSystem::Init calls
// lua_newstate and adopts kcdx's state. Read (acquire) by InterceptFired() — the
// cap-81 self-test's adoption-path-executed signal. Game-thread write,
// game-thread + test read.
std::atomic<bool> g_interceptFired{false};

// === The lua_newstate-callee INTERCEPT (engine adoption) ===
//
// Installed as an engine-stamped Mode::Replace chain entry on the resolved
// lua_newstate target. When the engine's CScriptSystem::Init calls lua_newstate
// (game main thread, ~2s after the worker build), the chain's exclusive
// dispatcher invokes this thunk INSTEAD of the original — the original
// lua_newstate never runs, so no second VM is allocated. The thunk returns
// kcdx's pre-built state (acquire load — the cross-thread happens-before edge
// paired with the worker's release publish), and CScriptSystem::Init proceeds to
// run its OWN storedebug=0 / luaL_openlibs / 3-registrar sequence ON kcdx's
// state. The state is virgin (storedebug=1) at the worker's build, so Init's
// overwrite is correct (P11 v2 §4.1 — the narrow hook is SAFE).
//
// Replace ABI (the C-kind exclusive thunk): the chain JIT-thunks the signature
// "ptr (ptr f, ptr ud)" — this callback receives the two pointer args (the
// engine passes them zeroed; lua_newstate is PGO-fused and ignores them per seed
// id 114) and returns the kcdx-built lua_State* as the ptr return. The
// BuildCDispatchThunk-emitted trampoline marshals the typed args + the rv.
extern "C" lua_State* Intercept_lua_newstate(lua_Alloc /*f*/, void* /*ud*/) {
    // ACQUIRE: pairs with the worker's RELEASE store of g_builtState (and
    // hooks::PublishLuaState's release). By the time the engine reaches Init on
    // the game thread, the worker has fully built + published the state, so this
    // load observes a complete VM. NEVER a wall-clock assumption — the chain
    // entry only exists because the worker installed it AFTER the release store,
    // and the release/acquire edge orders the state's construction before this
    // read.
    lua_State* L = g_builtState.load(std::memory_order_acquire);
    if (L == nullptr) {
        // Must never happen: the intercept is installed only AFTER g_builtState
        // + g_L are published (BuildAndAdoptVM orders publish-then-install). A
        // null here means the install ordering was violated — fail LOUD; return
        // null so the engine's own error path (a failed lua_newstate) surfaces
        // rather than a silently-wrong state.
        LOG_ERROR_KV(kCategory, "intercept_null_state",
            ::kcdx::log::KV("detail",
                "the lua_newstate intercept fired but kcdx's built state is "
                "null — the intercept was installed before the state was "
                "published (an ordering bug). Returning null so the engine "
                "surfaces a hard lua_newstate failure rather than adopting a "
                "wrong state."));
        return nullptr;
    }
    g_interceptFired.store(true, std::memory_order_release);
    LOG_INFO_KV(kCategory, "engine_adopted_kcdx_state",
        ::kcdx::log::KV("L", reinterpret_cast<const void*>(L)),
        ::kcdx::log::KV("detail",
            "CScriptSystem::Init called lua_newstate; the intercept returned "
            "kcdx's pre-built state (the engine adopts it; the original "
            "lua_newstate never ran — no second VM)."));
    return L;
}

}  // namespace

lua_State* BuiltState() {
    return g_builtState.load(std::memory_order_acquire);
}

bool InterceptFired() {
    return g_interceptFired.load(std::memory_order_acquire);
}

bool BuildAndAdoptVM() {
    bool expected = false;
    if (!g_done.compare_exchange_strong(expected, true,
                                        std::memory_order_acq_rel)) {
        return true;  // already built — idempotent
    }

    // --- 1. Resolve the shim (populates g_api). cap-79 flips to PASS once this
    //         runs at init (g_api() is populated). A required-symbol miss bails
    //         loud inside Resolve(); we must NOT touch the VM with an incomplete
    //         shim, so a false here aborts the build. ---
    if (!kcdx::lua_shim::Resolve()) {
        LOG_ERROR_KV(kCategory, "shim_resolve_failed",
            ::kcdx::log::KV("detail",
                "lua_shim::Resolve() returned false — a required Lua symbol did "
                "not resolve (WHGame not mapped, refdb not open, or a seed miss "
                "on this build). kcdx will NOT build the VM with an incomplete "
                "shim. The engine falls back to building its own VM."));
        g_done.store(false, std::memory_order_release);  // allow a retry
        return false;
    }

    const kcdx::lua_shim::LuaApi& api = kcdx::lua_shim::g_api();
    if (api.lua_newstate == nullptr) {
        LOG_ERROR_KV(kCategory, "lua_newstate_unresolved",
            ::kcdx::log::KV("detail",
                "the shim's lua_newstate member is null after Resolve() returned "
                "true — the internal-only VM-build symbol (refdb id 114) did not "
                "wire. kcdx cannot build the VM."));
        g_done.store(false, std::memory_order_release);
        return false;
    }

    // --- 2. Build the ONE state via the shim's lua_newstate (WHGame's compiled
    //         Lua body). lua_newstate is PGO-fused with luaL_newstate and ignores
    //         its allocator args (seed id 114) — pass nullptr/nullptr; WHGame's
    //         l_alloc is used internally. This is WHGame's body, so it allocates
    //         WHGame's sentinels — one Lua body, one sentinel set (no dual-Lua
    //         hazard introduced by this state). ---
    lua_State* L = api.lua_newstate(nullptr, nullptr);
    if (L == nullptr) {
        LOG_ERROR_KV(kCategory, "lua_newstate_returned_null",
            ::kcdx::log::KV("detail",
                "the shim's lua_newstate returned null — WHGame's Lua allocator "
                "failed to build the state. kcdx has no VM to publish; the engine "
                "falls back to its own VM build."));
        g_done.store(false, std::memory_order_release);
        return false;
    }

    // --- 3. Validate the single-state mainthread self-pointer invariant
    //         ([L->l_G + 0xB0] == L). A struct-layout shift (a game update)
    //         fails LOUD here — kcdx must NOT publish a state whose layout the
    //         shim's stubs would misread. ValidateLayout logs the specific
    //         diverged field on failure. ---
    if (!kcdx::lua_shim::ValidateLayout(L)) {
        LOG_ERROR_KV(kCategory, "mainthread_invariant_broken",
            ::kcdx::log::KV("L", reinterpret_cast<const void*>(L)),
            ::kcdx::log::KV("detail",
                "the mainthread self-pointer invariant ([L->l_G + 0xB0] == L) "
                "FAILED on the freshly-built state — WHGame's lua_State/"
                "global_State layout no longer matches the verified offsets (a "
                "game update shifted a field). kcdx must NOT publish this state "
                "(every shim stub reading these offsets would corrupt the VM). "
                "The engine falls back to its own VM build."));
        // The state was allocated through WHGame's body; the engine, on the
        // fallback path, builds its own state — leaving this orphaned is the
        // safe choice (we hold no validated layout to call lua_close through).
        g_done.store(false, std::memory_order_release);
        return false;
    }

    // --- 4. PUBLISH (release) — the single authoritative g_L write + the
    //         intercept's source. Both stores are RELEASE, ordered BEFORE the
    //         intercept install below; the game thread's intercept (and the
    //         first-tick HookedUpdate latch) ACQUIRE-load them. This is the
    //         cross-thread happens-before edge — never a timing margin. ---
    g_builtState.store(L, std::memory_order_release);
    lua_State* prior = kcdx::hooks::PublishLuaState(L);  // RELEASE inside
    if (prior != nullptr) {
        // A non-null prior means g_L was already set before the worker built the
        // VM — the engine's lua_pcall captured a state before adoption (the
        // race the keystone exists to close). Loud, but do NOT abort: g_L now
        // holds kcdx's authoritative state; the engine.lua_pcall guard will flag
        // any divergent L from here.
        LOG_WARN_KV(kCategory, "g_L_already_set_before_publish",
            ::kcdx::log::KV("prior_g_L", reinterpret_cast<const void*>(prior)),
            ::kcdx::log::KV("kcdx_L", reinterpret_cast<const void*>(L)),
            ::kcdx::log::KV("detail",
                "g_L was non-null before the worker's authoritative publish — an "
                "engine lua_pcall captured a state before kcdx built+published "
                "its own. kcdx's state is now authoritative; the lua_pcall guard "
                "flags any future divergent L."));
    }

    // --- 5. Install the lua_newstate-callee INTERCEPT (engine adoption). An
    //         engine-stamped Mode::Replace chain entry on the resolved
    //         lua_newstate target — the conflict-engine-mediated engine-bootstrap
    //         path, NOT a raw MinHook detour outside the conflict engine (so
    //         overlap detection stays authoritative for this site). The
    //         engine-stamped C-kind Replace entry runs synchronously on the game
    //         main thread (the isEngine off-thread carve-out) and its return is
    //         lua_newstate's result; the original never runs (no second VM). ---
    const uintptr_t newstateAddr = kcdx::refdb::ResolveAddrByName("lua_newstate");
    if (!newstateAddr) {
        LOG_ERROR_KV(kCategory, "lua_newstate_addr_unresolved",
            ::kcdx::log::KV("detail",
                "refdb::ResolveAddrByName(\"lua_newstate\") returned 0 — cannot "
                "install the engine-adoption intercept. kcdx's state is built + "
                "published, but the engine will build its OWN second VM at "
                "CScriptSystem::Init (the intercept never armed). The lua_pcall "
                "guard will flag the divergence loud."));
        return false;  // state published; intercept not armed — surfaced loud
    }

    auto sigParse =
        kcdx::hook_signature::Parse("ptr (ptr f, ptr ud)");
    if (!sigParse.ok) {
        LOG_ERROR_KV(kCategory, "intercept_signature_parse_failed",
            ::kcdx::log::KV("error", sigParse.error.c_str()));
        return false;
    }

    kcdx::hook_payload::HookPayload p;
    p.mode         = kcdx::hook_payload::Mode::Replace;
    p.address      = newstateAddr;
    p.signature    = sigParse.sig;
    p.hasSignature = true;
    p.owningPlugin = "kcdx";
    p.owningAuthor = "kcdx";
    p.name         = "engine.lua_newstate";
    auto add = kcdx::hook_chain::AddCEngine(
        p, reinterpret_cast<void*>(&Intercept_lua_newstate),
        sigParse.sig, /*pluginName=*/"kcdx",
        /*priority=*/0, /*name=*/"engine.lua_newstate",
        /*handleId=*/0);
    if (!add.ok) {
        LOG_ERROR_KV(kCategory, "intercept_install_failed",
            ::kcdx::log::KV("reason", add.reason.c_str()),
            ::kcdx::log::KV("detail",
                "AddCEngine(engine.lua_newstate, Replace) failed — the engine "
                "will build its own second VM at CScriptSystem::Init. kcdx's "
                "state is published; the lua_pcall guard flags the divergence."));
        return false;
    }

    kcdx::modification_inventory::RegisterModification(
        newstateAddr, kcdx::modification_inventory::Category::Engine,
        "engine.lua_newstate");

    LOG_INFO_KV(kCategory, "vm_built_and_intercept_armed",
        ::kcdx::log::KV("L", reinterpret_cast<const void*>(L)),
        ::kcdx::log::KV("lua_newstate", reinterpret_cast<const void*>(newstateAddr)),
        ::kcdx::log::KV("detail",
            "kcdx built the one Lua VM on the worker thread, validated the "
            "mainthread invariant, published g_L (release), and armed the "
            "engine-stamped lua_newstate Replace intercept. CScriptSystem::Init "
            "will adopt kcdx's state on the game main thread (acquire)."));
    return true;
}

}  // namespace kcdx::lua_vm_build
