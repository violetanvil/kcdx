#pragma once
// comp-18 self-test — the permanent regression guard for the foreign-hook
// prologue classifier (foreign_hook_detect, design §6.1; Phase 4 step 7, E13).
//
// Feeds SYNTHETIC prologues through foreign_hook_detect::Classify +
// ::DecodeJump and asserts each verdict against ground truth. Needs NO live
// game target — the classifier reads bytes kcdx already owns the address of, so
// a stack/static byte buffer at a known VA is a faithful prologue. Boot-only,
// one-shot guarded internally; same timing as the other engine selftests.
//
// FALSIFIABLE (each row states what makes it red):
//   - clean game bytes classify Clean (red if classified a jump);
//   - an E9 into a REGISTERED kcdx-owned range classifies KcdxTrampoline
//     (red if classified Foreign — the discriminator failed to recognize
//     kcdx's own range);
//   - a foreign E9 into an UNREGISTERED range classifies Foreign
//     (red if classified Clean or KcdxTrampoline — the core detection claim);
//   - a foreign FF25 into an unregistered range classifies Foreign
//     (red if not Foreign);
//   - an unrecognized jump-family shape classifies Unknown, never Foreign or
//     KcdxTrampoline (red if mis-classified as a chainable verdict — AP14: an
//     unknown prologue must fail conservative + loud, never silently chained).
// Also asserts the E9/FF25 byte DECODE computes the exact target VA.

namespace kcdx::foreign_hook_detect_selftest {

// Run the comp-18 foreign-hook-classifier self-test exactly once and report via
// kcdx::test::ReportResult. Idempotent (one-shot guarded); safe to call every
// tick from the engine's per-tick self-report block. No hook/"ready"/VM
// dependency.
void RunSelfTestOnce();

}  // namespace kcdx::foreign_hook_detect_selftest
