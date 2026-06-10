#include "trampoline_selftest.h"

#include <atomic>
#include <cstdint>
#include <cstdio>   // snprintf
#include <cstring>  // strstr

#include "dev.h"
#include "log.h"
#include "test.h"
#include "trampoline.h"

// cap-93 self-test — see trampoline_selftest.h for why this lives in engine
// code (the branch pool's expansion/exhaustion is engine-internal allocator
// plumbing, not a plugin export). Two falsifiable rows: the pool GREW past 80%
// (expansion), and the exhaustion path produces a teaching error naming the
// required tokens (exhaustion).

namespace {

constexpr const char* kRowExpand   = "cap-93-expansion";
constexpr const char* kRowExhaust  = "cap-93-exhaustion";
constexpr const char* kCategory    = "TRAMPOLINE";

// 64 KB reservation; 80% ≈ 52 KB. Allocate well past that (56 KB) so the served
// reservation crosses the 80% threshold and the eager expansion fires. Chunked
// so we exercise the per-chunk bump-and-check path, not one giant allocation.
constexpr size_t kReservationSize = 64 * 1024;
constexpr size_t kChunkSize       = 4 * 1024;   // 4 KB per allocation
constexpr size_t kTotalToAllocate = 56 * 1024;  // > 80% of one reservation

}  // namespace

namespace kcdx::trampoline_selftest {

void RunSelfTestOnce() {
    // Dev-gated: this drives REAL branch-pool allocations into the live process
    // (they are never freed — the pool is alloc-only), so it does not run in
    // production. Latch only AFTER the gate so a production boot that later
    // enables dev mode still runs it once.
    if (!kcdx::dev::IsEnabled()) return;

    static std::atomic<bool> done{false};
    bool expected = false;
    if (!done.compare_exchange_strong(expected, true,
                                      std::memory_order_acq_rel)) {
        return;
    }

    char reason[512];

    // -------- cap-93-expansion: the pool GROWS past 80% --------
    // Drive real WHGame-anchored (nearVa==0) branch allocations and assert a
    // NEW branch reservation came into existence as a result. FALSIFIABLE:
    // FAILS if the branch-reservation count did not increase (the 80% trigger
    // did not fire, or eager expansion silently failed to stage a region).
    const size_t before = kcdx::trampoline::BranchReservationCountForTest();
    size_t allocated = 0;
    size_t failedAllocs = 0;
    while (allocated < kTotalToAllocate) {
        void* p = kcdx::trampoline::AllocateBranch(/*owner=*/0, kChunkSize);
        if (!p) { ++failedAllocs; break; }
        allocated += kChunkSize;
    }
    const size_t after = kcdx::trampoline::BranchReservationCountForTest();

    const bool grew = after > before;
    if (grew) {
        std::snprintf(reason, sizeof(reason),
            "PASS — drove %zu bytes of real WHGame-anchored branch allocations "
            "(in %zu-byte chunks, past 80%% of a %zu-byte reservation); the "
            "branch-reservation count rose from %zu to %zu, so the proactive "
            "80%%-expansion staged a new rel32-reachable region.",
            allocated, kChunkSize, kReservationSize, before, after);
        LOG_INFO_KV(kCategory, "expansion_pass",
            ::kcdx::log::KV("before", (unsigned long long)before),
            ::kcdx::log::KV("after", (unsigned long long)after),
            ::kcdx::log::KV("allocated", (unsigned long long)allocated));
        kcdx::test::ReportResult(kRowExpand, true, reason);
    } else {
        std::snprintf(reason, sizeof(reason),
            "FAIL: branch-reservation count did NOT increase (before=%zu, "
            "after=%zu) after %zu bytes of real branch allocations (%zu alloc "
            "failure(s)). The pool did not expand — either the 80%%-expansion "
            "trigger did not fire after crossing the threshold, or the eager "
            "MakeBranchReservation found no nearby free region. Expected a new "
            "WHGame-anchored reservation to be staged.",
            before, after, allocated, failedAllocs);
        LOG_ERROR_KV(kCategory, "expansion_fail",
            ::kcdx::log::KV("before", (unsigned long long)before),
            ::kcdx::log::KV("after", (unsigned long long)after));
        kcdx::test::ReportResult(kRowExpand, false, reason);
    }

    // -------- cap-93-exhaustion: the teaching error names the tokens --------
    // Drive the exhaustion ERROR FORMATTER (the same one the production
    // exhaustion path calls) with a small synthetic cap/region count, and assert
    // the produced text names the required tokens: the pool ("branch"), a
    // percentage ("%"), the region count word ("regions"), and an actionable
    // next step ("Next step"). FALSIFIABLE: FAILS if the formatter produces no
    // text, or the text omits the pool name / percentage / region count / the
    // next-step guidance.
    char err[768];
    const bool produced = kcdx::trampoline::FormatExhaustionErrorForTest(
        /*syntheticRegionsTried=*/ 2, /*fullestPct=*/ 1.0, err, sizeof(err));

    const bool hasBranch  = produced && std::strstr(err, "branch") != nullptr;
    const bool hasPercent = produced && std::strstr(err, "%") != nullptr;
    const bool hasRegions = produced && std::strstr(err, "regions") != nullptr;
    const bool hasNext    = produced && std::strstr(err, "Next step") != nullptr;
    const bool allTokens  = hasBranch && hasPercent && hasRegions && hasNext;

    if (produced && allTokens) {
        std::snprintf(reason, sizeof(reason),
            "PASS — the branch-pool exhaustion error formatter produced a "
            "teaching message naming the pool (\"branch\"), a percentage, the "
            "region count (\"regions\"), and an actionable next step "
            "(\"Next step\"). Text: \"%.300s\"", err);
        LOG_INFO_KV(kCategory, "exhaustion_pass",
            ::kcdx::log::KV("branch", hasBranch ? 1 : 0),
            ::kcdx::log::KV("percent", hasPercent ? 1 : 0),
            ::kcdx::log::KV("regions", hasRegions ? 1 : 0),
            ::kcdx::log::KV("nextstep", hasNext ? 1 : 0));
        kcdx::test::ReportResult(kRowExhaust, true, reason);
    } else {
        std::snprintf(reason, sizeof(reason),
            "FAIL: the exhaustion error %s the required tokens — "
            "produced=%d branch=%d percent=%d regions=%d nextstep=%d. The "
            "author-facing exhaustion message must name the pool, the percent "
            "used, the region count, and a next step. Text: \"%.250s\"",
            produced ? "omits" : "was not produced; missing",
            produced ? 1 : 0, hasBranch ? 1 : 0, hasPercent ? 1 : 0,
            hasRegions ? 1 : 0, hasNext ? 1 : 0, produced ? err : "(none)");
        LOG_ERROR_KV(kCategory, "exhaustion_fail",
            ::kcdx::log::KV("produced", produced ? 1 : 0),
            ::kcdx::log::KV("branch", hasBranch ? 1 : 0),
            ::kcdx::log::KV("percent", hasPercent ? 1 : 0),
            ::kcdx::log::KV("regions", hasRegions ? 1 : 0),
            ::kcdx::log::KV("nextstep", hasNext ? 1 : 0));
        kcdx::test::ReportResult(kRowExhaust, false, reason);
    }

    kcdx::test::EmitSummaryIfChanged("cap-93 trampoline-multiregion");
}

}  // namespace kcdx::trampoline_selftest
