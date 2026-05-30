#include "ki0001_node_classifier_selftest.h"

#include <atomic>
#include <cstdint>
#include <cstdio>   // snprintf
#include <cstdlib>  // malloc / free

#include "log.h"
#include "test.h"

// The crash-guard discriminator + kcdx's own dummynode address, exported from
// the vendored Lua table code (vendor/lua/ltable.c). Returns 1 if a node is a
// real (freeable) heap array, 0 if it is a module-image (.rdata) sentinel.
extern "C" const void* kcdx_test_own_dummynode(void);
extern "C" int         kcdx_test_node_freeable(const void* n);

namespace kcdx::ki0001 {

namespace {

constexpr const char* kRow      = "cap-66-node-classifier";
constexpr const char* kCategory = "KI0001";

// A module-image (.rdata) resident object — the SAME memory class as WHGame's
// static-const dummynode_ (MEM_IMAGE). `static const` with a const initializer
// is placed in the module's read-only image, not on the heap. Its address is a
// stand-in for a foreign Lua copy's sentinel: the guard must NOT free it.
static const std::uint64_t kRdataSentinel[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

}  // namespace

void RunSelfTestOnce() {
    static std::atomic<bool> done{false};
    bool expected = false;
    if (!done.compare_exchange_strong(expected, true,
                                      std::memory_order_acq_rel)) {
        return;
    }

    // Three pointer classes the guard must distinguish.
    const void* ownDummy = kcdx_test_own_dummynode();          // kcdx .rdata sentinel
    const void* rdata    = static_cast<const void*>(kRdataSentinel);  // foreign .rdata stand-in
    void*       heap     = std::malloc(64);                    // real heap node array

    char reason[512];

    if (!ownDummy || !heap) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: self-test setup — own_dummynode=%p heap=%p (a null here is a "
            "harness fault, not the guard).", ownDummy, heap);
        kcdx::test::ReportResult(kRow, false, reason);
        kcdx::test::EmitSummaryIfChanged("cap-66 ki0001-node-classifier");
        if (heap) std::free(heap);
        return;
    }

    const int ownVerdict   = kcdx_test_node_freeable(ownDummy);  // expect 0 (skip)
    const int rdataVerdict  = kcdx_test_node_freeable(rdata);     // expect 0 (skip)  <- the crash class
    const int heapVerdict   = kcdx_test_node_freeable(heap);      // expect 1 (free)

    std::free(heap);

    // The exact crash-prevention contract: a .rdata/module-image sentinel
    // (kcdx's own OR a foreign one) is NEVER freed; a heap node IS freed.
    const bool ownOk   = (ownVerdict  == 0);
    const bool rdataOk = (rdataVerdict == 0);  // the KI-0001 regression hinge
    const bool heapOk  = (heapVerdict  == 1);
    const bool pass    = ownOk && rdataOk && heapOk;

    if (pass) {
        std::snprintf(reason, sizeof(reason),
            "PASS — node classifier: kcdx own dummynode=NOT-freeable, "
            "module-image (.rdata) sentinel=NOT-freeable (the foreign-dummynode "
            "crash class), heap node=freeable. kcdx's GC will not hand a foreign "
            "Lua copy's .rdata sentinel to frealloc (the 0xC0000374 save-load "
            "heap corruption).");
        LOG_INFO_KV(kCategory, "selftest_pass",
            ::kcdx::log::KV("own", (long long)ownVerdict),
            ::kcdx::log::KV("rdata", (long long)rdataVerdict),
            ::kcdx::log::KV("heap", (long long)heapVerdict));
        kcdx::test::ReportResult(kRow, true, reason);
    } else {
        std::snprintf(reason, sizeof(reason),
            "FAIL: node classifier mis-verdict — own_dummynode freeable=%d "
            "(want 0), rdata_sentinel freeable=%d (want 0 — THIS being 1 is the "
            "KI-0001 regression: kcdx's GC would free a foreign .rdata sentinel "
            "and re-trigger the 0xC0000374 save-load crash), heap freeable=%d "
            "(want 1).", ownVerdict, rdataVerdict, heapVerdict);
        LOG_ERROR_KV(kCategory, "selftest_fail",
            ::kcdx::log::KV("own", (long long)ownVerdict),
            ::kcdx::log::KV("rdata", (long long)rdataVerdict),
            ::kcdx::log::KV("heap", (long long)heapVerdict));
        kcdx::test::ReportResult(kRow, false, reason);
    }
    kcdx::test::EmitSummaryIfChanged("cap-66 ki0001-node-classifier");
}

}  // namespace kcdx::ki0001
