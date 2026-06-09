#pragma once
// comp-19 self-test — the permanent regression guard for FOREIGN-HOOK CHAINING
// (foreign_hook_detect §6.2/§6.3, the chaining half of foreign-hook coexistence;
// Phase 4 step 8, E14/E20). comp-18 proved DETECTION; this proves CHAINING.
//
// The claim: when kcdx installs a function-entry hook on a target ANOTHER mod
// already hooked (a foreign E9 in the prologue), BOTH detours fire, in the order
// game -> kcdx -> foreign -> original (kcdx-first load order, §6.3). The chaining
// is NOT a hand-rolled jump-follower — it falls out of safetyhook's normal
// install: InlineHook::create RELOCATES the foreign prologue jump into kcdx's
// trampoline IP-fixed, so kcdx's call-original runs the foreign detour, which
// runs the real function (design §6.2). SOURCE: vendor/safetyhook/src/
// inline_hook.cpp e9_hook (a relative prologue instruction is relocated with a
// recomputed new_disp), read this session.
//
// The fixture owns BOTH sides (AP1 — no game VA): a kcdx-controlled stub is the
// "real original"; a synthetic FOREIGN detour is installed on it first (a second
// safetyhook InlineHook standing in for the other mod — safetyhook writes a
// standard 5-byte E9 rel32, exactly the foreign prologue jump the relocation
// chains onto); then kcdx's own detour installs over it. Each detour appends a
// marker to a shared order-log; the assertion checks the order-log AND the
// original sentinel came through.
//
// FALSIFIABLE (AP15 — not a tautology):
//   - the order-log reads exactly "KFO" (kcdx appended 'K' first; then the foreign
//     detour appended 'F' when kcdx's call-original delegated to it via the
//     relocated foreign jump; then the real stub appended 'O' when the foreign
//     detour's own call-original reached the original function) — FAILS if ANY
//     marker is absent (a detour or the original did not fire) OR they are out of
//     order (e.g. "FK" / "F" / "K" / "KO");
//   - the final return is the original stub's sentinel (the chain reached the
//     real function through the relocated foreign jump) — FAILS if the original
//     never ran.
// Dev-gated (installs real safetyhook detours); boot-only, one-shot guarded.

namespace kcdx::foreign_hook_chain_selftest {

// Run the comp-19 foreign-hook-chaining self-test exactly once and report via
// kcdx::test::ReportResult. Idempotent (one-shot guarded); safe to call every
// tick from the engine's per-tick self-report block. No hook-fire / "ready" / VM
// dependency — it builds its own target + detours and calls them in-process.
void RunSelfTestOnce();

}  // namespace kcdx::foreign_hook_chain_selftest
