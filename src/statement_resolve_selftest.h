#pragma once

// cap-83 self-test — the permanent regression guard for the refdb statement-
// resolution API (the §9.3 locator catalog + the captures-by-name join).
//
// The behavior under test is engine-internal: refdb::ResolveStatementByName
// (src/refdb.{h,cpp}) resolves a §9.3 LOCATOR descriptor to a statement INDEX
// within a curated function, exposes that statement's per-statement reads (kind
// / callee / byte_range_len / ...), and joins the statement's captured
// variables (referenced_vars by (address_version_id, statement_idx)) onto the
// result. This self-test exercises that surface against the curated function
// SaveGame (kcdx_id 144 — 59 statements, all the §9.3 locator families present)
// and asserts each resolution against GROUND-TRUTH values measured from the
// shipped reference.sqlite.
//
// Ground truth (measured from the curated DB; do NOT adjust to match a wrong
// resolution — that is gaming the test, and a wrong resolution SHOULD go red):
//   function_entry          → found, statement_idx 0,  kind "assign", brl 3.
//   function_exit           → found, statement_idx 58, kind "return", brl 30.
//   first_call_to(FUN_1804d455c) → found, statement_idx 8,  brl 5.
//   first_return            → found, statement_idx 13, kind "return".
//   last_return             → found, statement_idx 58.
//   captures @ function_entry (idx 0) → EXACTLY 2: param_7 (stack, size 8) AND
//                             puVar6 (register, size 8). Order-independent.
//
// This runs at boot (no save-load gesture, no hook-fire dependency), so the
// regression is caught by the standard launch-to-menu run.
//
// GRACEFUL on absent data: the curated statement tables ship in the DEV
// reference.sqlite but the deployed USER projection regenerates them on deploy.
// If SaveGame does not resolve (name_unknown) or carries no statements (the
// pre-deploy state where the shipped DB lacks the statement tables — 2a handles
// this with empty caches), the self-test reports a CLEAR DEGRADED result, NOT a
// hard FAIL and NOT a crash. A degraded "statement data not present in this
// build" is distinguishable from a real resolution FAIL.
//
// FALSIFIABLE: the row goes red if any locator resolves to the WRONG index /
// byte_range_len / kind / captures, or returns found=false when the statement
// data IS present (a broken resolution). Each assertion names what makes it red
// in the reason string. The row does NOT go red on the absent-data path (that
// is the DEGRADED report, a deploy-state observation, not a resolution defect).

namespace kcdx::stmt_resolve {

// Run the cap-83 statement-resolution self-test exactly once and report via
// kcdx::test::ReportResult. Idempotent (one-shot guarded); safe to call every
// tick from the engine's per-tick self-report block. No hook/"ready"
// dependency — refdb is open by the first suite tick.
void RunSelfTestOnce();

}  // namespace kcdx::stmt_resolve
