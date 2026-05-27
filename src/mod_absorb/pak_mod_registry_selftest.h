#pragma once

// cap-54 self-test for the pak-mod registry + the load-order fold + the version
// gate (pak_mod_registry.{h,cpp} + the load_order Resolve fold) — STEP 3 of the
// mod-loader absorb.
//
// Like cap-52 / cap-53, these surfaces are engine-INTERNAL (kcdx::mod_absorb /
// kcdx::load_order), not author surfaces, so cap-54 self-reports from ENGINE
// code via kcdx::test::ReportResult — the cap-47 / cap-39 / cap-52 / cap-53
// prior-art pattern.
//
// UNIT-LEVEL assertions (run from literals, no game state, no real mods/ dir):
//   1. ParseModOrderText — '#' comments + blank lines stripped, FILE ORDER
//      preserved as the 0-based index.
//   2. The modOrderIndex -> sort ordering: two pak mods with indices 0,1 sort
//      in that order; a -1 (INT_MAX) sorts AFTER both, then by name.
//   3. A "mods.<modid>" Effective lookup after a synthetic register + Resolve:
//      priority 0, after_game; a load_order.toml "mods.<modid>" row OVERRIDES
//      zone/priority/enabled.
//   4. The version gate: an Incompatible pak mod -> SetEngineAccepted(false)
//      -> IsPluginEnabled("mods.<modid>") == false.
//
// Assertions 3 + 4 drive the GLOBAL load_order state (Read + Resolve), so the
// self-test SNAPSHOTS the live registry + re-reads the real load_order.toml +
// re-Resolves at the end to RESTORE the live load order untouched (the test
// runs at the first update tick, after the engine's own Resolve already ran).
//
// CHECKPOINT-LEVEL (not asserted here — confirmed by the live boot): the
// real-directory end-to-end discovery COUNT (how many pak mods the actual
// <game-root>/mods/ + kcdx-plugins/ scan finds) — that depends on the install's
// contents, which a unit test cannot fabricate. The MOD_ABSORB discovery-funnel
// log line ("N pak mod(s): M from mods/, K from kcdx-plugins/; J version-disabled")
// is the agent-read checkpoint signal.

namespace kcdx::mod_absorb {

// Run the cap-54 self-test exactly once and report via kcdx::test::ReportResult.
// Idempotent (function-static one-shot). No hook-fire / "ready" dependency —
// the parse + fold + gate logic all work at boot — so it reports on the first
// tick. Restores the live load-order state before returning.
void RunPakRegistrySelfTestOnce();

}  // namespace kcdx::mod_absorb
