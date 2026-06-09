#pragma once

// cap-85 self-test — the JS↔C++ CROSS-IMPLEMENTATION AGREEMENT pin (TRD D27,
// step 3.4). The conformance gate that proves the C++ engine static survival
// check and the JS browser static check return the SAME verdict on the SAME DLL
// bytes for every in-scope kind — the engine is the authority, the browser its
// faithful mirror.
//
// HOW THE PIN WORKS (the test-of-record pattern, extended from version_resolver):
// the cross-implementation fixture (cross_impl_fixture.py) is the SINGLE
// source-of-truth — it declares each (kcdx_id, kind) → on-disk byte-slice(s) +
// the pinned expected verdict (Unchanged / Changed / Ambiguous / CannotCheck),
// derived from fingerprint-per-kind.md (NOT from any checker's output — the
// ground truth a checker must REPRODUCE). The Python reference checker reproduces
// those verdicts; the JS browser checker pins against the SAME fixture
// (crossImplAgreement.test.ts); THIS self-test pins the C++ ENGINE against it.
//
// The fixture is emitted to a JSON contract (the Python source-of-truth → JSON →
// embedded here as kFixtureJson via survival_agreement_fixture.h, GENERATED). For
// each fixture row's slice, this test:
//   1. parses the embedded JSON (a compact dependency-free reader),
//   2. builds the engine survival Payload (kind + datum) for the row,
//   3. PLANTS the slice's raw bytes in a SYNTHETIC PE at the right section/rva
//      (the C++ analogue of the JS makeAnchorPE/makeFakePE planting) — because the
//      engine's static checks scan PE SECTIONS, but the fixture slices are JUST
//      the kind's bytes (no PE scaffolding),
//   4. runs the REAL engine static check over the planted PE via the buffer
//      seam survival::SurvivalCheckOnBuffer (the IDENTICAL per-kind dispatch the
//      production SurvivalCheck runs — no new check logic),
//   5. asserts engine_verdict == the fixture's pinned verdict.
//
// The agreement is over the ON-DISK version-applicability checks ONLY (D27) — the
// byte/AOB/hash/derivation checks both the engine and the browser compute. The
// loaded-image reachability check (resolve-into-live-.text) is engine-only (the
// browser cannot read the loaded image), so it is OUTSIDE this pin (and is covered
// by cap-84 sub-checks 7-9).
//
// FALSIFIABLE (AP15): the row goes RED if the engine verdict DIVERGES from the
// fixture's pinned verdict for ANY kind/slice. The assertion is NEVER weakened to
// pass — a genuine engine divergence is a 3.1/3.2 engine bug the conformance test
// caught, surfaced as a FAIL naming the kind + the two verdicts, never papered
// over by adjusting the fixture (adjusting the ground-truth fixture to match a
// wrong engine output is AP15 gaming — forbidden). The fixture's BLAKE3
// content_hash is also pinned: a function-row slice the engine hashes must read
// Unchanged, proving the engine's BLAKE3 (vendored) reproduces the same hash the
// Python/JS oracle stamped (a non-canonical engine BLAKE3 turns the function row
// red — the cross-impl hash agreement).
//
// IN-SCOPE KINDS the pin covers (== the fixture's): function (body hash),
// callsite (AOB re-match: unique→Unchanged / zero→Changed / multi→Ambiguous),
// string_anchor (.rdata literal presence), instruction_anchor + data_slot
// (disp32 derivation chains), vtable_base (table-shape), + the vtable_index
// CannotCheck row. Boot-only, deterministic, self-contained — the synthetic PEs
// + embedded fixture do NOT depend on WHGame.dll being mapped or the deployed DB
// (unlike cap-84's real-row checks), so this is a HARD pass/fail, never DEGRADED.
//
// Why it lives in engine code (like cap-84): survival::SurvivalCheckOnBuffer is an
// engine-internal symbol, not a plugin export — so cap-85 self-reports from ENGINE
// code via kcdx::test::ReportResult. One-shot guarded.

namespace kcdx::survival_agreement_selftest {

// Run the cap-85 JS↔C++ agreement self-test exactly once and report via
// kcdx::test::ReportResult. Idempotent (one-shot guarded); safe to call every tick
// from the engine's per-tick self-report block. No hook/"ready" dependency, no
// WHGame.dll dependency (synthetic PEs + an embedded fixture).
void RunSelfTestOnce();

}  // namespace kcdx::survival_agreement_selftest
