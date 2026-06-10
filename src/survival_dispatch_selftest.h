#pragma once

// cap-84 self-test — the permanent regression guard for the survival per-kind
// DISPATCH + the Ambiguous status + the 5 STATIC non-function checks (step 3.2)
// + the anchor-dependency ordering, AND the step-3.3 STARTUP VERIFICATION PASS
// (on-disk version-applicability + live-image reachability + D34 attribution).
//
// The behavior under test is engine-internal: kcdx::survival::SurvivalCheck has
// a per-kind dispatch entry point (Payload{kind,...} → the per-kind check), with
// the function kinds routed through the EXISTING on-disk body-hash check (verdict
// UNCHANGED) and each non-function STATIC kind (callsite / string_anchor /
// instruction_anchor / data_slot / vtable_base) running its real on-disk check;
// vtable_index stays a DEFINED deferral (CannotCheck/"vtable_index_deferred").
// The dependency-ordered walk (CheckOrdered) checks a row set anchors-first so a
// dependent whose anchor is Changed short-circuits to CannotCheck/"anchor_changed".
//
// The falsifiable sub-checks (deterministic, boot-only, no hook-fire / "ready"
// dependency; the real-row checks DEGRADE-PASS when the deployed DB lacks the
// curated survival datum, never a hard FAIL):
//
//   1. FUNCTION VERDICT UNCHANGED — the dispatch's function path returns the
//      IDENTICAL Result the legacy SurvivalCheck(rva,length,hash,len) entry
//      returns for the same inputs (legacy IS today's behavior). 3 deterministic
//      cases (not_applicable / expected_hash_bad_length / length_zero) + 1 REAL
//      on-disk SaveGame check when the DB carries content_hash+length.
//      FAILS if the dispatch's function path diverges from the legacy verdict.
//
//   2. vtable_index IS A DEFINED DEFERRAL — dispatched vtable_index returns
//      CannotCheck/"vtable_index_deferred", never Unchanged/Changed/empty.
//      FAILS if it fabricates a non-deferred verdict.
//
//   3. AMBIGUOUS IS REPORTABLE — survival::Status::Ambiguous maps through the
//      pass to vcc::FuncStatus::Ambiguous (its own value, not collapsed to
//      CannotCheck) and round-trips through the cache codec byte-identically.
//      FAILS if Ambiguous is not a usable, reportable status.
//
//   4. CALLSITE VERDICTS — a synthetic improbable AOB → Changed (zero .text
//      hits); a synthetic ultra-common 2-byte AOB → Ambiguous (>1 .text hit);
//      and a REAL curated callsite row (its stored AOB) → Unchanged (unique hit)
//      when the DB carries it (DEGRADED otherwise). FAILS if a gone site reads
//      Unchanged, or a multi-hit AOB does not read Ambiguous.
//
//   5. STRING_ANCHOR VERDICTS — a REAL curated string_anchor (its literal
//      present in .rdata) → Unchanged when the DB carries it; a synthetic
//      improbable literal → Changed (absent). FAILS if an absent literal reads
//      Unchanged.
//
//   6. TRANSITIVE ANCHOR-CHANGED (the DAG) — a 2-row set run through CheckOrdered:
//      an anchor row that comes back Changed (a string_anchor with an absent
//      literal) + a dependent deriving from it. The dependent MUST short-circuit
//      to CannotCheck/"anchor_changed", NOT be independently re-derived and NOT
//      silently pass. FAILS if a Changed anchor does not transitively block its
//      dependent.
//
//   --- STEP 3.3 — the startup verification pass (D25 + D34) ---
//
//   7. REACHABILITY RANGE TEST — pe::IsVaInLiveText (the 3.3 reachability half)
//      reads an off-image VA (base-1MB) and VA 0 as NOT in live .text, and a
//      real engine-resolved curated function VA AS in live .text. The signal is
//      a RANGE TEST against live executable sections, NOT a live-body hash
//      (Probe 0.4 — the live image is relocated + kcdx-detoured). DEGRADES when
//      WHGame.dll is not mapped. FAILS if an off-image/null VA reads in-.text, or
//      a genuinely-good function VA reads dead.
//
//   8. STARTUP VERIFICATION PASS — RunStartupVerification sweeps the curated set
//      and yields a DEFINED verdict per row (a loaded refdb produces a non-empty
//      sweep); a known-good curated function (SaveGame) whose on-disk hash
//      matches AND resolves into live .text caps at passed_not_verified (NEVER
//      verified_working from a static pass) at method_rank 3 (reachability), WITH
//      its matched address_version id surfaced; a fingerprint-mismatch failed
//      carries NO matched id. DEGRADES when WHGame is not mapped / the DB lacks
//      the row. FAILS if the sweep drops the whole set, a static pass reads
//      verified_working, a passed_not_verified has no matched id or a rank != 3,
//      or a mismatch-failed carries a matched id.
//
//   9. VERDICT-COMBINATION DISCRIMINATION — a function payload with a synthetic
//      non-matching content_hash (0x33*32) at SaveGame's real rva reads on-disk
//      Changed (→ the pass combines to failed), NOT Unchanged. Proves the failed
//      discrimination does not collapse into a false passed_not_verified.
//      DEGRADES when WHGame is not mapped. FAILS if the bogus fingerprint reads
//      Unchanged (the on-disk check fabricating a match).
//
//   --- The 7-state verdict model + the ceiling arithmetic ---
//
//  10. 7-STATE ENUM TOTAL + IN-PROCESS ROUND-TRIP — every one of the 7 verdict
//      states (verified_working / passed_not_verified / failed / not_applicable /
//      cannot_check / skipped / error) is producible on a RowVerdict, reads back
//      its own verdict + method_rank, and decodes to a DISTINCT stable token via
//      VerdictName. FAILS if a state does not round-trip its value or two states
//      share a token (the enum collapsed). (10b) The SEPARATE FuncStatus cache
//      codec round-trip stays intact — the pinned values (0/1/2) still round-trip
//      byte-identically; the 7-state verdict is NOT serialized through that codec.
//
//  11. THE CEILING RULE — MapStaticVerdict (the pass's own arithmetic): an
//      on-disk hash PASS + reachability maps to passed_not_verified at rank 3
//      (the strongest method that ran), NEVER verified_working; an on-disk hash
//      MISMATCH maps to failed (the override-downward). FAILS if a clean pass
//      reads verified_working or a mismatch reads anything but failed.
//
//  12. THE VERSION-GAP PRODUCER — a row whose running build is NOT covered
//      (versionGap) maps to not_applicable, distinct from cannot_check (a missing
//      input). FAILS if a version gap reads cannot_check (or vice versa).
//
//  13. THE FAULT PRODUCER (error ≠ failed) — the static mapping NEVER produces
//      error (nor verified_working / skipped), so error is exclusively the
//      per-row catch's output on a caught fault; a caught throw resolves to
//      error, never failed. FAILS if the static mapping fabricates error or a
//      caught fault resolves to failed.
//
// Why it lives in engine code (like cap-60): survival + survival_pass +
// version_check_cache are engine-internal symbols, not plugin exports — so
// cap-84 self-reports from ENGINE code via kcdx::test::ReportResult. The pass is
// NOT yet wired into the live apply path; this self-test exercises the callable
// machinery in isolation and RESETS the cache's in-memory + on-disk state
// afterward so it leaves nothing behind.

namespace kcdx::survival_dispatch_selftest {

// Run the cap-84 dispatch+Ambiguous self-test exactly once and report via
// kcdx::test::ReportResult. Idempotent (one-shot guarded); safe to call every
// tick from the engine's per-tick self-report block. No hook/"ready" dependency.
void RunSelfTestOnce();

}  // namespace kcdx::survival_dispatch_selftest
