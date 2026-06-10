#pragma once

// cap-98 self-test — the permanent regression guard for the refdb dev-DB
// cross-function SEARCH layer (Phase 9.4 step 0 — the engine FOUNDATION the
// kcdx.find / kcdx_dev_inspect binders, steps 1/2, consume).
//
// The behavior under test is engine-internal: refdb::FindFunctions /
// EnumerateStatements / OpenDevDb (src/refdb.{h,cpp}) query a SECOND, dev-gated
// read-only connection to reference-dev.sqlite (the full ~321k-function corpus,
// distinct from the shipped g_db). A Lua/C++ plugin cannot observe the dev-DB
// connection or the search layer directly (the binders that expose it are
// steps 1/2), so this self-reports from ENGINE code via kcdx::test::ReportResult.
// Single-surface (engine-internal dev-tool foundation).
//
// Ground truth (measured from reference-dev.sqlite 2026-06-10; do NOT adjust to
// match a wrong result — a wrong search SHOULD go red):
//   string = "   You have to set r_TexturesStreaming = 1 to see texture
//             information!"  → exactly 1 owning function (av 14595, auto_name
//             FUN_18043ee28, module WHGame.dll, rva 0x43EE28), decompile_quality
//             "clean", 189 statements.
//   callee = "_Init_thread_footer"  → 30,393 owning functions (> the 500 cap) —
//             the deterministic over-cap target.
//
// Falsifiable rows (each names what makes it red in the reason string):
//
//   * cap-98-find-string — FindFunctions({string=<the known string above>})
//     returns >= 1 record whose function/module/rva are all non-empty/non-zero.
//     FAILS if zero records come back for a string known to be owned, or if the
//     owning record's function name / module / rva is empty/zero (a broken
//     header read). This is the "a search returns the right set for a known
//     input" assertion from the step's test bar.
//
//   * cap-98-truncates-loud — FindFunctions({callee="_Init_thread_footer"})
//     truncates LOUDLY: truncated==true, total_matches > 500, records.size()
//     == 500 (the cap, kFindResultCap). FAILS if an over-500 search returns a
//     silent partial (truncated==false), a wrong total (<= 500), or a wrong
//     record count (!= 500). This is the "over-500 truncates loudly, never a
//     silent empty/partial" assertion (AP14).
//
//   * cap-98-gate-discriminates — the dev-DB availability gate distinguishes
//     "no match" from "gate failed". Under the dev-gated suite, OpenDevDb()
//     succeeds (dev mode on + the file present + schema match), so a search for
//     a deliberately-impossible string returns unavailable==false with an EMPTY
//     match set (records empty, total_matches==0) — a genuine zero-match, NOT
//     the unavailable signal. FAILS if a clean empty search reports
//     unavailable==true (the gate cannot tell "no match" from "DB down"), or if
//     OpenDevDb() returns false while dev mode is on and the file is present
//     (the gate spuriously rejects an available DB). The dev-mode-OFF half of
//     the gate ("OpenDevDb does NOT open with dev mode off") cannot be
//     exercised from inside a dev-gated suite (the suite only runs with dev
//     mode on) — it is covered by the OpenDevDb dev_mode_off branch + its
//     reason token, asserted by code review, not by this row.
//
//   * cap-98-applicable-ops — applicable_ops names the REAL kcdx.op.* ops whose
//     required statement-kind matches, restricted to the corpus — NOT a kind
//     echo. EnumerateStatements(FUN_18043ee28) → its first `call`-kind statement
//     must carry applicable_ops that INCLUDE skip_call_void + replace_with_noop,
//     EXCLUDE replace_compare_constant (its kind `compare` is never emitted by
//     the corpus dict — naming it would name a move the statement verb's kind-
//     gate rejects, AP14), and is NOT the placeholder kind-echo (["call"]).
//     FAILS if the call statement's applicable_ops is the kind-echo (lacking
//     skip_call_void), omits the call family, or leaks replace_compare_constant.
//     The kind->op-NAME mapping mirrors src/lua_bind_op.cpp's per-op
//     ContractFor()/RequiredKind declarations; this row exercises it against
//     real corpus data.
//
// This runs at boot (no save-load gesture, no hook-fire dependency), so the
// regression is caught by the standard launch-to-menu run.
//
// GRACEFUL on absent data: reference-dev.sqlite is a maintainer/dev artifact
// (~1.3 GB) deployed alongside the shipped reference.sqlite under
// kcdx-engine/data. If it is absent (OpenDevDb returns dev_db_not_found), the
// self-test reports a CLEAR DEGRADED result, NOT a hard FAIL and NOT a crash —
// distinguishable from a real search defect.

namespace kcdx::find_discovery_selftest {

// Run the cap-98 dev-DB search self-test exactly once and report via
// kcdx::test::ReportResult. Idempotent (one-shot guarded); safe to call every
// tick from the engine's per-tick self-report block. No hook/"ready"/VM
// dependency — the dev DB lazy-opens on the first FindFunctions call.
void RunSelfTestOnce();

}  // namespace kcdx::find_discovery_selftest
