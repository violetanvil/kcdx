#include "version_check_selftest.h"

#include <cstdio>   // snprintf
#include <string>
#include <vector>

#include "log.h"
#include "survival_pass.h"
#include "test.h"
#include "version_check_cache.h"

// cap-60 self-test — see version_check_selftest.h for why this lives in engine
// code + what each sub-check falsifies.

namespace kcdx::version_check_selftest {

namespace {

constexpr const char* kRow = "cap-60-version-check-cache";
constexpr const char* kCategory = "VERCACHE";

namespace vcc = kcdx::version_check_cache;
namespace sp = kcdx::survival_pass;

// A synthetic plugin name that can never collide with a real plugin's Lookup
// (the charset is illegal for a [plugin].name, so no real plugin shares it).
constexpr const char* kSyntheticPlugin = "__cap60_synthetic__";

}  // namespace

void RunSelfTestOnce() {
    static bool s_reported = false;
    if (s_reported) return;
    s_reported = true;  // synthetic + deterministic — no retry needed.

    char reason[640];

    // Snapshot nothing live: neither module is wired into production yet, so
    // there is no concurrent writer. We Reset both at the end to leave clean
    // in-memory state regardless.

    // ----- Sub-check 1: cache codec round-trip ------------------------------
    vcc::Reset();
    vcc::Record rec;
    rec.key.pluginName = kSyntheticPlugin;
    rec.key.gameVer = "cap60.1.5.x";
    rec.key.sqliteSha = std::vector<uint8_t>(32, 0xAB);  // a deterministic 32-byte sha.
    rec.key.tomlMtime = 111111;
    rec.key.entrypointsMtime = 222222;
    rec.posture = vcc::Posture::RefuseEntry;
    rec.results.push_back({"alpha_fn", vcc::FuncStatus::Unchanged});
    rec.results.push_back({"beta_fn", vcc::FuncStatus::Changed});
    rec.results.push_back({"gamma_nonbyte", vcc::FuncStatus::CannotCheck});
    vcc::Upsert(rec);

    if (!vcc::Save()) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: version_check_cache::Save() returned false — could not persist "
            "the synthetic record (cache dir / write error). The codec round-trip "
            "cannot be verified.");
        LOG_ERROR_KV(kCategory, "selftest_fail",
            ::kcdx::log::KV("subcheck", "1_save"));
        kcdx::test::ReportResult(kRow, false, reason);
        kcdx::test::EmitSummaryIfChanged("cap-60 version-check-cache");
        vcc::Reset();
        sp::Reset();
        return;
    }

    vcc::Reset();  // drop the in-memory record; force a real disk read.
    vcc::Load();

    // Lookup with MATCHING invalidation inputs → HIT, results round-trip.
    vcc::InvalidationKey matchKey = rec.key;
    std::vector<vcc::FuncResult> got;
    vcc::Posture gotPosture = vcc::Posture::WarnAndTry;
    if (!vcc::Lookup(matchKey, got, gotPosture)) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: a matching-key Lookup after Save+Load MISSED — the codec lost "
            "the record or the invalidation comparison rejected an identical key.");
        LOG_ERROR_KV(kCategory, "selftest_fail",
            ::kcdx::log::KV("subcheck", "1_lookup_match"));
        kcdx::test::ReportResult(kRow, false, reason);
        kcdx::test::EmitSummaryIfChanged("cap-60 version-check-cache");
        vcc::Reset();
        sp::Reset();
        return;
    }
    bool roundTripOk =
        gotPosture == vcc::Posture::RefuseEntry &&
        got.size() == 3 &&
        got[0].targetKey == "alpha_fn" && got[0].status == vcc::FuncStatus::Unchanged &&
        got[1].targetKey == "beta_fn"  && got[1].status == vcc::FuncStatus::Changed &&
        got[2].targetKey == "gamma_nonbyte" && got[2].status == vcc::FuncStatus::CannotCheck;
    if (!roundTripOk) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: codec round-trip mismatch — posture/result-count/per-function "
            "status did not survive Save+Load (got %zu results, posture=%d). The "
            "on-disk byte layout drifted from the reader.",
            got.size(), static_cast<int>(gotPosture));
        LOG_ERROR_KV(kCategory, "selftest_fail",
            ::kcdx::log::KV("subcheck", "1_roundtrip"),
            ::kcdx::log::KV("result_count", (unsigned long long)got.size()));
        kcdx::test::ReportResult(kRow, false, reason);
        kcdx::test::EmitSummaryIfChanged("cap-60 version-check-cache");
        vcc::Reset();
        sp::Reset();
        return;
    }

    // ----- Sub-check 2: invalidation forces a miss --------------------------
    vcc::InvalidationKey staleKey = rec.key;
    staleKey.tomlMtime = rec.key.tomlMtime + 1;  // a single changed input.
    std::vector<vcc::FuncResult> got2;
    vcc::Posture gotPosture2 = vcc::Posture::WarnAndTry;
    if (vcc::Lookup(staleKey, got2, gotPosture2)) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: Lookup with a CHANGED toml_mtime returned a HIT — a changed "
            "invalidation input was ignored, so the cache would serve a stale "
            "result for an edited plugin. Invalidation is broken.");
        LOG_ERROR_KV(kCategory, "selftest_fail",
            ::kcdx::log::KV("subcheck", "2_invalidation"));
        kcdx::test::ReportResult(kRow, false, reason);
        kcdx::test::EmitSummaryIfChanged("cap-60 version-check-cache");
        vcc::Reset();
        sp::Reset();
        return;
    }

    // ----- Sub-check 3: the pass over a synthetic non-byte ref --------------
    // An empty expected hash → SurvivalCheck returns CannotCheck (not_applicable),
    // never Changed/Unchanged. Deterministic; no on-disk binary read.
    sp::Reset();
    sp::RecordPluginMeta(kSyntheticPlugin, 333333, 444444,
                         vcc::Posture::RefuseEntry);
    sp::RecordTouchedRef(kSyntheticPlugin, "synthetic_nonbyte",
                         /*rva=*/0x1000, /*length=*/0,
                         /*expectedHash=*/std::vector<uint8_t>{});
    size_t produced = sp::RunPass("cap60.1.5.x", std::vector<uint8_t>(32, 0xCD));
    const sp::PassResult* pr = sp::Result(kSyntheticPlugin, "synthetic_nonbyte");
    bool passOk =
        produced >= 1 &&
        pr != nullptr &&
        pr->status == vcc::FuncStatus::CannotCheck &&
        pr->posture == vcc::Posture::RefuseEntry;
    if (!passOk) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: the pass over an empty-expected-hash ref did NOT yield "
            "CannotCheck+RefuseEntry (produced=%zu, result=%s, status=%d, "
            "posture=%d). The pass either fabricated a Changed/Unchanged for a "
            "ref it cannot check, or lost the plugin's posture.",
            produced, pr ? "present" : "MISSING",
            pr ? static_cast<int>(pr->status) : -1,
            pr ? static_cast<int>(pr->posture) : -1);
        LOG_ERROR_KV(kCategory, "selftest_fail",
            ::kcdx::log::KV("subcheck", "3_pass_nonbyte"),
            ::kcdx::log::KV("produced", (unsigned long long)produced));
        kcdx::test::ReportResult(kRow, false, reason);
        kcdx::test::EmitSummaryIfChanged("cap-60 version-check-cache");
        vcc::Reset();
        sp::Reset();
        return;
    }

    // All three held. Leave clean in-memory state + an empty on-disk cache so the
    // synthetic record never leaks into a future real Lookup.
    vcc::Reset();
    vcc::Save();
    sp::Reset();

    std::snprintf(reason, sizeof(reason),
        "PASS 3/3 — version_check.bin codec round-trips a record (posture + per-"
        "function status survive Save+Load); a changed toml_mtime forces a cache "
        "MISS (no stale result); the survival pass records CannotCheck (never "
        "Changed) for a non-byte ref and surfaces the plugin's on_changed posture.");
    LOG_INFO_KV(kCategory, "selftest_pass",
        ::kcdx::log::KV("result", "3/3"));
    kcdx::test::ReportResult(kRow, true, reason);
    kcdx::test::EmitSummaryIfChanged("cap-60 version-check-cache");
}

}  // namespace kcdx::version_check_selftest
