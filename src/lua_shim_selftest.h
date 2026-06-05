#pragma once

// cap-79 self-test for the Lua symbol shim — the forward layer AND the
// kcdx-side stub layer. src/lua_shim.{h,cpp} + lua_shim_stubs.cpp, routing every
// Lua symbol through WHGame's single compiled body.
//
// Why it lives in engine code (like cap-59 blake3 + cap-66): the shim's g_api()
// table is an ENGINE-INTERNAL symbol, not a plugin export — a Lua/C++ plugin
// cannot call g_api().lua_pushlstring directly. So cap-79 self-reports from
// ENGINE code via kcdx::test::ReportResult.
//
// Falsifiable rows:
//
//   * CAP-79-roundtrip — call a FORWARDED shim member (lua_pushlstring, a
//     resolved id-30 forwarder) against the LIVE lua_State, then read the value
//     back through two more forwarded members (lua_tolstring + lua_type) and
//     assert the round-trip value + type byte-match. This proves the forward
//     layer actually reaches WHGame's compiled Lua and the resolved RVA is the
//     right function — not merely that Resolve() returned true. FAILS if the
//     pushed string does not read back identical, or the type is not
//     LUA_TSTRING, or the shim member is null (forward layer not wired). The
//     stack is restored (settop back to the entry depth) so the live VM is
//     untouched.
//
//   * CAP-79-internal-gated — assert the internal-only gating: IsInternalOnly
//     returns true for the four VM-lifecycle functions the plugin surface must
//     never expose (lua_newstate / lua_close / lua_setallocf / lua_atpanic) and
//     false for an ordinary plugin-facing member (lua_pushlstring). This is the
//     negative proof that the gate the kcdxLuaApi rewire reads actually
//     discriminates — a tautology gate (always-true / always-false) FAILS this
//     row. (The required-miss BAIL path of Resolve() cannot be exercised live
//     without breaking the running VM — forcing a real miss would deny the
//     game its Lua. Its falsifiability is the structured LOG_ERROR_KV +
//     return-false in lua_shim.cpp's FORWARD macro, asserted by code review,
//     not a live row; the gating row is the live negative that CAN go red.)
//
//   * CAP-79-stubs — exercise representative seam STUB classes through
//     g_api against the live VM, each falsifiable: (a) lua_pushnil → lua_type ==
//     LUA_TNIL; (b) lua_pushboolean(true) → lua_toboolean true; (c)
//     lua_pushinteger(42) → lua_type == LUA_TNUMBER and lua_tointeger == 42;
//     (d) lua_gettop returns the entry depth + the exact count pushed (the
//     gettop stub's arithmetic); (e) luaL_register installs a named table read
//     back via lua_getfield (the luaI_openlib-reimplementation stub). FAILS if
//     any pushed value reads back wrong, gettop miscounts, or the registered
//     table is absent. The stack is restored to the entry depth so the live VM
//     is untouched.
//
//   * CAP-79-gc-barrier — the GC-barrier-safety invariant: assert
//     GcBarrierBacked() (luaC_barrierf, id 127, resolved non-null by
//     BindStubPrimitives). A live observation of a barrier CALL is not
//     constructible from a self-test (no seam stub writes a GC pointer into a
//     black object — the verified bodies of the seam pushes write only stack
//     slots, which need no barrier; the barrier-callers lua_replace /
//     lua_setupvalue / lua_setmetatable are RESOLVED forwarders, not stubs). So
//     the falsifiable row is the structural one the design names: the barrier
//     primitive is resolved and available to any GC-pointer-writing stub. FAILS
//     if luaC_barrierf did not resolve (a future GC-pointer stub would write
//     unbarriered). (Surfaced limitation: this asserts availability, not a live
//     call — there is no seam stub whose live path invokes it.)
//
//   * CAP-79-layout — the mainthread self-pointer invariant ValidateLayout(L):
//     G(L)->mainthread (g+0xB0) == L, the one cheap check that ties the live
//     struct layout to the verified offsets every stub reads. FAILS if a future
//     game update shifted the lua_State/global_State layout — the falsifiable
//     proof that the stubs' offset reads are still correct, fail-loud not silent.
//
// Test mode: in-game (the live lua_State is captured at the first lua_pcall,
// which fires continuously once the game's Lua is up — reliably non-null by the
// main menu). One-shot guarded + retried each tick until L is captured
// (mirrors cap-47): a tick before capture leaves the row PENDING and retries,
// never a false FAIL.

namespace kcdx::lua_shim {

// Run the cap-79 shim forward-layer self-test and report via
// kcdx::test::ReportResult. Idempotent / one-shot guarded; safe to call every
// tick from the engine per-tick self-report block. Returns early (no report)
// while the live lua_State is not yet captured.
void RunSelfTestOnce();

}  // namespace kcdx::lua_shim
