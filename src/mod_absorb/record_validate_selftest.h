#pragma once

// cap-58 self-test for the synthesized-record validator
// (record_validate.{h,cpp}). ValidateSynthRecord is an engine-INTERNAL symbol
// (kcdx::mod_absorb) — it inspects a record's raw bytes against the native
// I_Mod/CryString invariants, not an author surface — so cap-58 self-reports
// from ENGINE code via kcdx::test::ReportResult, exactly like cap-52..57.
//
// The test is FALSIFIABLE on the load-bearing claim: it builds a WELL-FORMED
// record via record_synth::BuildRecord (correct construction) and asserts the
// validator ACCEPTS it, then constructs DELIBERATELY-MALFORMED records (a
// corrupted CryString nLength header word; a nulled vtable slot) and asserts the
// validator REJECTS each. The reject cases are the proof the guard actually
// guards — record_synth builds correct records, so a test feeding only real
// records would pass even if the validator were a no-op.

namespace kcdx::mod_absorb {

// Run the cap-58 validator self-test exactly once and report via
// kcdx::test::ReportResult. Idempotent (function-static one-shot) — safe to call
// every tick. No hook-fire / "ready" dependency: BuildRecord + the validator
// work as soon as the Address Library resolves (available at boot).
void RunRecordValidateSelfTestOnce();

}  // namespace kcdx::mod_absorb
