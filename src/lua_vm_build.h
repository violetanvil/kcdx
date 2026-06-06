#pragma once

// This header's public API declares lua_State* / lua_Alloc (BuiltState() and the
// .cpp's intercept signature), so it pulls the Lua typedefs itself rather than
// relying on the includer — the same pattern lua_shim.h uses. lua.h carries
// lua_State + lua_Alloc; the declarations below reference nothing deeper.
extern "C" {
#include "lua.h"
}

// === lua_vm_build — kcdx builds the ONE Lua VM on its worker thread, the engine
// ADOPTS it through the lua_newstate-callee intercept (no force-load) ===
//
// THE KEYSTONE of the kcdx-owns-the-VM mechanism. On the worker thread, after
// WHGame is mapped and the Address Library is open, kcdx:
//   1. resolves the Lua symbol shim (lua_shim::Resolve — populates g_api());
//   2. builds the ONE lua_State via the shim's lua_newstate (WHGame's compiled
//      Lua body — never a second VM, never kcdx static Lua);
//   3. validates the single-state mainthread self-pointer invariant
//      ([L->l_G + 0xB0] == L) — a struct-layout shift fails LOUD here, not
//      silently inside the VM;
//   4. PUBLISHES the built state into hooks::g_L with a RELEASE edge (the single
//      authoritative publish — the cross-thread happens-before edge);
//   5. installs the lua_newstate-callee INTERCEPT (an engine-stamped Mode::Replace
//      chain entry on the resolved lua_newstate target) so the engine's
//      CScriptSystem::Init call RETURNS kcdx's pre-built state instead of
//      allocating its own. Init then runs its OWN storedebug=0 / luaL_openlibs /
//      3-extension-registrar sequence ON kcdx's state.
//
// NO force-load: kcdx does NOT LoadLibraryW WHGame (impossible — CREATE_SUSPENDED
// boot + loader lock). The worker's ldr_notify::WaitForGameDll already blocks
// until the game maps WHGame; this unit runs at the post-refdb::Open point.
//
// CROSS-THREAD, NEVER TIMED: kcdx builds the VM on the WORKER
// thread; CScriptSystem::Init (the lua_newstate call this unit intercepts) runs
// on the GAME MAIN thread ~2s later. The worker's RELEASE store of g_L (step 4,
// BEFORE installing the intercept) happens-before the game thread's ACQUIRE load
// inside the replace callback (step 5's intercept body). An explicit
// happens-before edge — never a wall-clock margin.
//
// INTERCEPT MECHANISM: the intercept is an ENGINE-STAMPED Mode::Replace chain
// entry via hook_chain::AddCEngine (pluginName="kcdx", name="engine.lua_newstate")
// — the conflict-engine-mediated engine-bootstrap path, NOT a raw MinHook detour
// outside the conflict engine. Routing through the conflict engine keeps overlap
// detection authoritative for this bootstrap site. An engine-stamped C-kind
// Replace entry runs synchronously on the game main thread (the isEngine C-kind
// off-thread carve-out) and its return becomes lua_newstate's result; the
// original lua_newstate never runs, so no second VM is allocated. lua_newstate is
// resolved BY NAME (refdb id 114) — never a literal RVA, so a new game version
// updates the reference DB, not source.

namespace kcdx::lua_vm_build {

// Build the one Lua VM on the worker thread and install the engine-adoption
// intercept. Call from the worker (ctx B) AFTER refdb::Open() succeeded and
// hooks::Install() ran (MinHook live, the chain up). Idempotent: a second call
// after a successful build is a no-op returning true.
//
// Returns true iff the shim resolved, the VM built, the mainthread invariant
// held, g_L published, and the intercept installed. On ANY failure it logs a
// structured ERROR naming the step and returns false WITHOUT publishing a
// partial state (fail loud, never adopt a half-built VM). On false the worker
// continues — the engine falls back to building its OWN VM (the intercept never
// armed), the static-linked Lua bootstrap (HookedUpdate first-tick) still runs.
bool BuildAndAdoptVM();

// The kcdx-built authoritative lua_State (the one published into hooks::g_L), or
// nullptr if BuildAndAdoptVM never succeeded. The cap-81 single-state self-test
// reads this to assert the engine adopted kcdx's state (CurrentLuaState() == this)
// rather than building a second VM. Acquire-loads the worker's release publish.
lua_State* BuiltState();

// True iff the lua_newstate-callee intercept has FIRED (the engine called
// lua_newstate and adopted kcdx's state). False until CScriptSystem::Init runs on
// the game thread. The cap-81 self-test reads this to confirm the adoption path
// executed, not merely that the build succeeded.
bool InterceptFired();

}  // namespace kcdx::lua_vm_build
