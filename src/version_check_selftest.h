#pragma once

// cap-60 self-test for the per-version survival-verification cache
// (version_check_cache.{h,cpp}) + the unified survival pass
// (survival_pass.{h,cpp}).
//
// This is the falsifiable, SELF-CONTAINED proof that the cache codec round-trips
// and the pass produces correct results + honors cache invalidation — WITHOUT
// any live-resolution dependency (no real plugin, no real game-binary hashing):
//
//   1. Cache codec round-trip — Upsert a synthetic record, Save, Reset, Load,
//      Lookup with MATCHING invalidation inputs → the record + its per-function
//      results + posture come back byte-identical. FAILS if the codec drops a
//      field or mis-orders the layout.
//   2. Invalidation — Lookup the same plugin with a DIFFERING toml_mtime → MISS
//      (recheck forced). FAILS if a changed invalidation input is ignored (a
//      stale wrong result the cache must never serve).
//   3. The pass over a synthetic touched ref with an EMPTY expected hash (a
//      non-byte entity) → records CannotCheck (NEVER Changed), surfaces the
//      plugin's posture. Deterministic — no on-disk binary read. FAILS if the
//      pass fabricates a Changed/Unchanged for a ref it cannot check, or loses
//      the posture.
//
// Why it lives in engine code (like cap-59): version_check_cache + survival_pass
// are engine-internal symbols, not plugin exports — so cap-60 self-reports from
// ENGINE code via kcdx::test::ReportResult. The pass is NOT yet wired into the
// live apply path; this self-test exercises the callable machinery in isolation
// and RESETS both modules' in-memory state afterward so it leaves nothing behind.

namespace kcdx::version_check_selftest {

// Run the cap-60 cache+pass self-test exactly once and report via
// kcdx::test::ReportResult. Idempotent — one-shot guarded with a function-static
// bool; safe to call every tick from the engine's per-tick self-report block. No
// dependency on a hook firing or on "ready" — all three sub-checks work on
// synthetic data at boot.
void RunSelfTestOnce();

}  // namespace kcdx::version_check_selftest
