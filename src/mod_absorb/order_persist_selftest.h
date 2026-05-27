#pragma once

// cap-56 self-test for order persistence (order_persist.{h,cpp}) — STEP 5 of
// the mod-loader absorb.
//
// Like cap-52/53/54/55, this surface is engine-INTERNAL (kcdx::mod_absorb::
// order_persist), not an author surface, so cap-56 self-reports from ENGINE
// code via kcdx::test::ReportResult — the cap-47 / cap-39 / cap-52 / cap-53 /
// cap-54 / cap-55 prior-art pattern.
//
// UNIT-LEVEL assertions (run from LITERAL strings — no file I/O, no real game
// files; the pure string serializers are factored out for exactly this):
//   1. load_order.toml row serialization: merging a resolved set into an EMPTY
//      base adds a "[[plugin]]" row keyed "mods.<modid>" carrying the human
//      mod name as a trailing '#' comment.
//   2. Idempotence: merge -> merge-again (same rows) yields BYTE-IDENTICAL text
//      (the second merge adds nothing because every name already has a row).
//   3. mod_order.txt: SerializeModOrderText emits one bare modid per line in
//      the given order; ParseModOrderText round-trips it back to the same
//      sequence with comments stripped.
//   4. Merge-preserves: given an existing row set (a hand-edited plugin row +
//      one pak-mod row) + a newly-discovered mod, the existing rows survive
//      VERBATIM and ONLY the new mod's row is appended.
//
// All assertions are PURE (literal in, string out) — NO global state is
// touched, so unlike cap-54/55 there is nothing to snapshot/restore.
//
// CHECKPOINT-LEVEL (not asserted here — confirmed by the live boot): the
// actual on-disk write to load_order.toml + <game-root>/mods/mod_order.txt,
// the write-if-changed skip on a steady-state boot, and the fail-loud path on
// an unwritable file. The MOD_ABSORB "load_order_persist_*" / "mod_order_persist_*"
// log lines are the agent-read checkpoint signal.

namespace kcdx::mod_absorb {

// Run the cap-56 self-test exactly once and report via kcdx::test::ReportResult.
// Idempotent (function-static one-shot). No hook-fire / "ready" dependency —
// the serializers all work at boot, on literals — so it reports on the first
// tick. Touches NO global state (pure-string assertions).
void RunOrderPersistSelfTestOnce();

}  // namespace kcdx::mod_absorb
