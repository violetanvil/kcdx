#pragma once

// cap-57 self-test — the END-TO-END mod-loader-absorb regression net (closeout
// of the mod-loader-absorb feature). Steps 1-5 each own a focused self-test
// (cap-52 record-synth, cap-53 manifest reader + version gate, cap-54 registry
// + fold + gate, cap-55 enabled-list build + takeover, cap-56 order persist).
// cap-57 is the permanent end-to-end net over the discovery -> registry ->
// load-order behaviors that are QUERYABLE at boot — the contract that ties the
// whole feature together and catches a regression no single step's test would.
//
// Like cap-52..56, this surface is engine-INTERNAL (kcdx::mod_absorb /
// kcdx::load_order), not an author surface, so cap-57 self-reports from ENGINE
// code via kcdx::test::ReportResult.
//
// UNIT-LEVEL assertions (run at the first update tick, after discovery +
// load_order::Resolve + the version gate have already produced the live
// resolved state):
//   1. LIVE-state, robust-to-empty: IF the live Registry() has any fromModsDir
//      pak mod, EACH such mod has a resolvable "mods.<modid>" row — Of() returns
//      a real after_game/priority-0 Effective (NOT the default sentinel) and
//      IsPluginEnabled is true (a compatible mod loads). Vacuously true on an
//      install with an empty mods/, REAL coverage when mods/ is populated.
//   2. The SUPERSET marker-file classification, driven against a SYNTHETIC root
//      (no fragile dependency on a physical plugin in the live mods/): Discover()
//      over a temp dir with one kcdx.toml folder + one mod.manifest-only folder
//      registers the mod.manifest folder as a pak mod and SKIPS the kcdx.toml
//      folder (a kcdx plugin works dropped in mods/ — it is NOT double-registered
//      as a vanilla pak mod).
//   3. Resolved-order sanity: synthetic pak mods at the same after_game block
//      fold to zone=after_game/priority=0 and preserve mod_order.txt relative
//      order via the orderIndex tiebreaker.
//
// Assertions 2 + 3 MUTATE the registry + the global load_order state, so they
// CAPTURE the live state verbatim (Registry deep-copy + load_order::Snapshot),
// run isolated over a synthetic set, then RESTORE it exactly — the cap-54/55
// snapshot/restore idiom. Assertion 1 only READS live state, so it needs no
// snapshot.
//
// CHECKPOINT-LEVEL (NOT asserted here — confirmed by the live boot): the actual
// native MOUNT (each enabled record's <path>/*.pak mounts, in kcdx order, no
// crash) is verified at the verification checkpoint, not unit-level. The
// MOD_ABSORB takeover/discovery log lines are the agent-read checkpoint signal.

namespace kcdx::mod_absorb {

// Run the cap-57 end-to-end self-test exactly once and report via
// kcdx::test::ReportResult. Idempotent (function-static one-shot). No hook-fire
// / "ready" dependency — discovery + Resolve + the version gate have all run by
// the first update tick — so it reports on the first tick. Assertion 1 reads
// live state read-only; assertions 2-3 snapshot/restore the global state.
void RunModAbsorbE2ESelfTestOnce();

}  // namespace kcdx::mod_absorb
