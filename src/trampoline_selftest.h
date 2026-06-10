#pragma once

// cap-93 self-test for the branch trampoline pool's multi-region growth —
// proactive 80% expansion, the per-anchor region cap, and the teaching
// exhaustion error (src/trampoline.cpp). These are engine-INTERNAL allocator
// behaviors: the branch pool is the rel32-constrained memory the hook engine
// hands detour bodies, with no plugin export and no author surface — so cap-93
// self-reports from ENGINE code via kcdx::test::ReportResult, exactly like the
// prior-art engine self-tests cap-80 (early-hook) / cap-66 (node classifier).
//
// Two falsifiable rows:
//   - cap-93-expansion: drives real AllocateBranch calls past 80% of one branch
//     reservation and asserts a SECOND branch reservation came into existence
//     (the pool GREW) — proving the proactive expansion actually fired.
//   - cap-93-exhaustion: asserts the teaching exhaustion error is produced and
//     names the required tokens (the pool, a percentage, the region count, and
//     an actionable next step) — using the SAME formatter the production
//     exhaustion path calls, so the test pins the author-facing text without
//     physically exhausting the live pool.
//
// No VM needed, no player input, no game-state dependency — boot-only, mirroring
// cap-80 (the allocator works as soon as the engine is mapped).

namespace kcdx::trampoline_selftest {

// Run the cap-93 trampoline-multiregion self-test exactly once and report via
// kcdx::test::ReportResult. Idempotent (function-static one-shot guard); safe to
// call every tick from the engine's per-tick self-report block. Boot-only — no
// hook-fire / "ready" dependency.
void RunSelfTestOnce();

}  // namespace kcdx::trampoline_selftest
