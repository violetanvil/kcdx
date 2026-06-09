#pragma once

// cap-84 self-test — the permanent regression guard for the survival per-kind
// DISPATCH + the Ambiguous status (the foundation step-3.2's non-function checks
// and step-3.3's live reachability check plug into).
//
// The behavior under test is engine-internal: kcdx::survival::SurvivalCheck now
// has a per-kind dispatch entry point (Payload{kind,...} → the per-kind check),
// with the function kinds routed through the EXISTING on-disk body-hash check
// (verdict UNCHANGED from before the restructure) and every non-function kind
// routed to a DEFINED fail-loud step-3.2 stub (CannotCheck, never a false
// Unchanged). The Status enum gained Ambiguous (the callsite multiple-hit
// verdict), which maps through the survival pass to its own FuncStatus value.
//
// The two falsifiable sub-checks (both deterministic, boot-only, no hook-fire /
// "ready" / live-resolution dependency — and a third real-Unchanged check that
// runs when the curated DB carries a function fingerprint, DEGRADED-PASS when it
// does not):
//
//   1. FUNCTION VERDICT UNCHANGED — the dispatch's function path returns the
//      IDENTICAL Result the legacy SurvivalCheck(rva,length,hash,len) entry
//      returns for the same inputs (the legacy entry IS today's behavior, so
//      dispatch==legacy IS "the function verdict is unchanged"). Asserted across
//      the function path's runnable verdicts: not_applicable (empty hash),
//      expected_hash_bad_length, length_zero — all deterministic, no module
//      mapped. PLUS, when a curated function row carries content_hash+length, a
//      REAL on-disk check via BOTH paths must agree (status + reason identical);
//      absent that data it is a clear DEGRADED PASS, never a hard FAIL.
//      FAILS if the dispatch's function path diverges from the legacy verdict.
//
//   2. NON-FUNCTION STUBS ARE FAIL-LOUD — every non-function kind dispatched
//      returns CannotCheck with a DEFINED token (callsite/string_anchor/
//      instruction_anchor/data_slot/vtable_base → "not_implemented_3_2";
//      vtable_index → "vtable_index_deferred"), NEVER Unchanged/Changed/empty.
//      FAILS if any non-function kind fabricates a non-CannotCheck verdict or a
//      silent empty reason.
//
//   3. AMBIGUOUS IS REPORTABLE — survival::Status::Ambiguous maps through the
//      pass's MapStatus to vcc::FuncStatus::Ambiguous (its own value, not
//      collapsed to CannotCheck), and that value round-trips through the cache
//      codec (Save→Load→Lookup) byte-identically. FAILS if Ambiguous is not a
//      usable, reportable status that survives the pass + the codec.
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
