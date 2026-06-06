#pragma once

// cap-81 self-test — the KEYSTONE: kcdx builds the one Lua VM on its worker
// thread and the engine ADOPTS it through the lua_newstate-callee intercept
// (src/lua_vm_build.{h,cpp} + the hooks.cpp guarded-confirm).
//
// Why it lives in engine code (like cap-79 / cap-80 / cap-66): the VM build +
// adoption is engine-internal boot plumbing — a Lua/C++ plugin cannot observe
// `lua_vm_build::BuiltState()` or the engine's lua_newstate intercept. So cap-81
// self-reports from ENGINE code via kcdx::test::ReportResult. Single-surface
// (engine-internal VM/boot plumbing).
//
// THE FALSIFIABLE OBSERVABLE OF LAST RESORT: the game BOOTING is itself the
// keystone's falsifiable proof — a bad adoption (a wrong state returned, a
// frame-corrupt intercept, a layout mismatch) AVs at boot, so no log is written
// and the suite never reports. A green suite that includes a PASS here is the
// affirmative single-state proof on top of "it didn't crash."
//
// Falsifiable rows (all read the LIVE adopted state, captured at the first
// lua_pcall via kcdx::hooks::CurrentLuaState):
//
//   * CAP-81-single-state — the intercept worked: kcdx built exactly ONE state
//     AND the engine adopted it. Asserts (a) lua_vm_build::BuiltState() is
//     non-null (the worker built + published a state); (b)
//     lua_vm_build::InterceptFired() is true (CScriptSystem::Init called
//     lua_newstate and the intercept returned kcdx's state — the adoption path
//     executed); (c) hooks::CurrentLuaState() (the state the engine's lua_pcall
//     runs on) EQUALS BuiltState() — the engine runs on kcdx's state, not a
//     second VM. FAILS if BuiltState is null (build/publish failed), if the
//     intercept never fired (the engine built its own VM), or if the live state
//     differs from the built one (a second VM exists — the dual-Lua hazard the
//     keystone kills). This is THE single-state assertion.
//
//   * CAP-81-mainthread — the mainthread self-pointer invariant
//     [L->l_G + 0xB0] == L holds on the ADOPTED state (lua_shim::ValidateLayout).
//     FAILS if a game update shifted the struct layout — the falsifiable proof
//     the adopted state's layout is the one the shim's offsets were verified
//     against.
//
//   * CAP-81-kcdx-tables — the kcdx.* tables are present in the adopted state:
//     _G.kcdx is a table with a populated sub-table (kcdx.log). FAILS if _G.kcdx
//     is absent or not a table (the kcdx surface never registered on the adopted
//     state). Proves the kcdx authoring surface lives on the one adopted VM.
//
//   * CAP-81-cryengine-script — a CryEngine script path still runs on the adopted
//     state: _G.System is a table (CryEngine's own script bindings populated the
//     adopted VM — the engine's CScriptSystem::Init ran luaL_openlibs + its
//     registrars ON kcdx's state, exactly as the narrow-intercept design intends).
//     FAILS if _G.System is absent (CryEngine never bound its scripts to kcdx's
//     state — adoption corrupted the boot sequence). The observable that the
//     engine's own Lua scripts coexist on kcdx's adopted VM.
//
// Test mode: in-game (the live adopted state is captured at the first lua_pcall,
// which fires continuously once the game's Lua is up — reliably non-null by the
// main menu; the intercept fires at CScriptSystem::Init, before that). One-shot
// guarded + retried each tick until both the state is captured AND the intercept
// has fired: a tick before either leaves the rows PENDING and retries, never a
// false FAIL.

namespace kcdx::cap81_vm_adopt_selftest {

// Run the cap-81 keystone self-test and report via kcdx::test::ReportResult.
// Idempotent / one-shot guarded; safe to call every tick from the engine
// per-tick self-report block. Returns early (no report) while the live state is
// not yet captured OR the intercept has not yet fired.
void RunSelfTestOnce();

}  // namespace kcdx::cap81_vm_adopt_selftest
