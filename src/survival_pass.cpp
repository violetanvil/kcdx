// survival_pass — implementation. See survival_pass.h for the collector-seam
// rationale + the pass contract.

#include "survival_pass.h"

#include <map>
#include <unordered_map>
#include <utility>

#include "log.h"
#include "survival.h"
#include "version_check_cache.h"

namespace kcdx::survival_pass {

namespace {

const char* kCategory = "SURVIVAL_PASS";

namespace vcc = kcdx::version_check_cache;

// One collected touched ref (what a binder reports at resolve time).
struct TouchedRef {
    std::string          targetKey;
    uint64_t             rva = 0;
    uint64_t             length = 0;
    std::vector<uint8_t> expectedHash;
};

// Per-plugin collected state: the touched refs (keyed by targetKey for the
// per-pair-idempotent replace) + the plugin metadata RecordPluginMeta supplies.
struct PluginCollected {
    // std::map keeps a deterministic targetKey order → a deterministic cache
    // file + deterministic result enumeration (no hash-order churn).
    std::map<std::string, TouchedRef> refs;

    bool          haveMeta = false;
    uint64_t      tomlMtime = 0;
    uint64_t      entrypointsMtime = 0;
    vcc::Posture  posture = vcc::Posture::WarnAndTry;
};

// Collector state. std::map by pluginName → deterministic plugin order.
std::map<std::string, PluginCollected> g_collected;

// Produced results, indexed by (pluginName, targetKey) for Result() and by
// pluginName for ResultsForPlugin(). Populated by RunPass.
std::map<std::pair<std::string, std::string>, PassResult> g_results;

// Map kcdx::survival::Status → the cache/pass FuncStatus.
vcc::FuncStatus MapStatus(kcdx::survival::Status s) {
    switch (s) {
        case kcdx::survival::Status::Unchanged: return vcc::FuncStatus::Unchanged;
        case kcdx::survival::Status::Changed:   return vcc::FuncStatus::Changed;
        default:                                return vcc::FuncStatus::CannotCheck;
    }
}

const char* FuncStatusName(vcc::FuncStatus s) {
    switch (s) {
        case vcc::FuncStatus::Unchanged:   return "unchanged";
        case vcc::FuncStatus::Changed:     return "changed";
        default:                           return "cannot_check";
    }
}

}  // namespace

void RecordTouchedRef(const std::string& owningPlugin,
                      const std::string& targetKey,
                      uint64_t rva,
                      uint64_t length,
                      const std::vector<uint8_t>& expectedHash) {
    PluginCollected& pc = g_collected[owningPlugin];
    TouchedRef ref;
    ref.targetKey = targetKey;
    ref.rva = rva;
    ref.length = length;
    ref.expectedHash = expectedHash;
    pc.refs[targetKey] = std::move(ref);  // per-(plugin,target) idempotent replace.

    LOG_DEBUG_KV(kCategory, "ref_recorded",
        ::kcdx::log::KV("plugin", owningPlugin.empty() ? "(anonymous)" : owningPlugin.c_str()),
        ::kcdx::log::KV("target", targetKey),
        ::kcdx::log::KV("rva", (unsigned long long)rva),
        ::kcdx::log::KV("length", (unsigned long long)length),
        ::kcdx::log::KV("expected_hash_len", (unsigned long long)expectedHash.size()));
}

void RecordPluginMeta(const std::string& pluginName,
                      uint64_t tomlMtime,
                      uint64_t entrypointsMtime,
                      vcc::Posture posture) {
    PluginCollected& pc = g_collected[pluginName];
    pc.haveMeta = true;
    pc.tomlMtime = tomlMtime;
    pc.entrypointsMtime = entrypointsMtime;
    pc.posture = posture;
}

size_t RunPass(const std::string& gameVer,
               const std::vector<uint8_t>& sqliteSha) {
    g_results.clear();

    // The cache must be loaded before the pass so a valid record can short the
    // hashing. Load is fail-loud-to-empty internally — a corrupt/absent cache
    // simply means every plugin rechecks (never a stale wrong result).
    vcc::Load();

    size_t produced = 0;

    for (auto& [pluginName, pc] : g_collected) {
        // Build this plugin's invalidation key for THIS launch.
        vcc::InvalidationKey key;
        key.pluginName = pluginName;
        key.gameVer = gameVer;
        key.sqliteSha = sqliteSha;
        key.tomlMtime = pc.tomlMtime;
        key.entrypointsMtime = pc.entrypointsMtime;

        // CACHE HIT: a still-valid record reuses the per-function results and
        // SKIPS hashing for this plugin (the perf win). The hit's results are
        // taken as authoritative; the posture is taken from the CURRENT meta if
        // available (the posture is the plugin's live intent, not a cached
        // outcome), else from the cached record.
        std::vector<vcc::FuncResult> cachedResults;
        vcc::Posture cachedPosture = vcc::Posture::WarnAndTry;
        bool hit = vcc::Lookup(key, cachedResults, cachedPosture);

        vcc::Posture posture = pc.haveMeta ? pc.posture : cachedPosture;

        if (hit) {
            // Index the cached results by targetKey so refs map onto them.
            std::unordered_map<std::string, vcc::FuncStatus> byKey;
            byKey.reserve(cachedResults.size());
            for (const auto& fr : cachedResults) byKey[fr.targetKey] = fr.status;

            for (const auto& [tk, ref] : pc.refs) {
                PassResult pr;
                pr.pluginName = pluginName;
                pr.targetKey = tk;
                pr.posture = posture;
                pr.fromCache = true;
                auto fit = byKey.find(tk);
                if (fit != byKey.end()) {
                    pr.status = fit->second;
                } else {
                    // A touched ref present this launch but absent from the
                    // cached record (e.g. a new binding on an unchanged plugin
                    // file set — rare). Don't fabricate a result: recheck just
                    // this ref so we never serve a "result" the cache never
                    // computed (a stale wrong result the cache must never serve).
                    kcdx::survival::Result res = kcdx::survival::SurvivalCheck(
                        static_cast<uint32_t>(ref.rva), ref.length,
                        ref.expectedHash.empty() ? nullptr : ref.expectedHash.data(),
                        ref.expectedHash.size());
                    pr.status = MapStatus(res.status);
                    pr.fromCache = false;
                }
                g_results[{pluginName, tk}] = pr;
                ++produced;
            }
            continue;
        }

        // CACHE MISS: run the survival check for every touched ref + rebuild the
        // plugin's cache record.
        vcc::Record rec;
        rec.key = key;
        rec.posture = posture;

        for (const auto& [tk, ref] : pc.refs) {
            kcdx::survival::Result res = kcdx::survival::SurvivalCheck(
                static_cast<uint32_t>(ref.rva), ref.length,
                ref.expectedHash.empty() ? nullptr : ref.expectedHash.data(),
                ref.expectedHash.size());
            vcc::FuncStatus st = MapStatus(res.status);

            PassResult pr;
            pr.pluginName = pluginName;
            pr.targetKey = tk;
            pr.status = st;
            pr.posture = posture;
            pr.fromCache = false;
            g_results[{pluginName, tk}] = pr;
            ++produced;

            vcc::FuncResult fr;
            fr.targetKey = tk;
            fr.status = st;
            rec.results.push_back(std::move(fr));

            LOG_DEBUG_KV(kCategory, "ref_checked",
                ::kcdx::log::KV("plugin", pluginName.empty() ? "(anonymous)" : pluginName.c_str()),
                ::kcdx::log::KV("target", tk),
                ::kcdx::log::KV("status", FuncStatusName(st)),
                ::kcdx::log::KV("survival_reason", res.reason.empty() ? "-" : res.reason.c_str()));
        }

        // Persist the record into the in-memory cache. An anonymous plugin
        // (empty name) is still upserted under the empty-name key; its record is
        // simply never a useful reuse (anonymous callers don't recur with stable
        // identity), but recording it keeps the codec path uniform.
        vcc::Upsert(rec);
    }

    // Write the whole updated cache once. Fail-loud-to-skip internally — a
    // failed write just means the next launch rechecks (never fatal).
    vcc::Save();

    LOG_INFO_KV(kCategory, "pass_complete",
        ::kcdx::log::KV("plugins", (unsigned long long)g_collected.size()),
        ::kcdx::log::KV("results", (unsigned long long)produced));
    return produced;
}

const PassResult* Result(const std::string& pluginName,
                         const std::string& targetKey) {
    auto it = g_results.find({pluginName, targetKey});
    return it == g_results.end() ? nullptr : &it->second;
}

std::vector<PassResult> ResultsForPlugin(const std::string& pluginName) {
    std::vector<PassResult> out;
    for (const auto& [pk, pr] : g_results) {
        if (pk.first == pluginName) out.push_back(pr);
    }
    return out;
}

void Reset() {
    g_collected.clear();
    g_results.clear();
}

}  // namespace kcdx::survival_pass
