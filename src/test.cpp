// test — see test.h.
//
// Implementation:
//   - std::map<name, Entry> keeps results sorted by name for deterministic
//     summary output.
//   - One mutex around the map; calls are rare (a few per test plugin per
//     session) so contention isn't a concern.
//   - All emit paths gate on dev::IsEnabled() so the suite is invisible
//     in production.

#include "test.h"

#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "kcdx/Interfaces.h"
#include "dev.h"
#include "log.h"

namespace kcdx::test {

namespace {

struct Entry {
    bool        pass = false;
    std::string reason;
};

std::mutex                  g_lock;
std::map<std::string, Entry> g_results;

// Watermark for change detection. Bumped on every successful
// ReportResult. EmitSummaryIfChanged() compares against the last-emitted
// value and only emits if they differ.
uint64_t g_seq             = 0;
uint64_t g_last_emitted_seq = (uint64_t)-1;  // forces first emit always

// Map of expected-test-name -> owning-plugin-name. Populated by
// RegisterExpectedTestName during config.cpp's plugin discovery
// pass. Aggregator uses it to compute PENDING (registered but no
// report yet) entries. Sorted by name for stable PENDING output.
std::map<std::string, std::string> g_expected;

// Count of test_suite_only plugins skipped because dev mode is off.
// Reported once via EmitGatedOffSummary so even production users
// notice that test plugins exist.
int g_gated_off_count = 0;

}  // namespace

void ReportResult(std::string_view name, bool pass, std::string_view reason) {
    if (!dev::IsEnabled()) return;
    if (name.empty())      return;

    std::lock_guard<std::mutex> lk(g_lock);
    auto& e = g_results[std::string(name)];
    e.pass   = pass;
    e.reason = std::string(reason);
    ++g_seq;

    KCDX_DEV("TEST", "REPORT",
        kcdx::dev::KV("name",   std::string(name)),
        kcdx::dev::KV("pass",   pass),
        kcdx::dev::KV("reason", std::string(reason)));
}

void EmitSummary(const char* messageLabel) {
    if (!dev::IsEnabled()) return;

    std::lock_guard<std::mutex> lk(g_lock);

    // Compute PENDING set: expected names that haven't reported yet.
    std::vector<std::pair<std::string, std::string>> pending;  // (test_name, plugin_name)
    for (const auto& [expectedName, ownerPlugin] : g_expected) {
        if (g_results.find(expectedName) == g_results.end()) {
            pending.emplace_back(expectedName, ownerPlugin);
        }
    }

    // Nothing to report — neither results nor pending.
    if (g_results.empty() && pending.empty()) return;

    size_t reported = g_results.size();
    size_t passing  = 0;
    for (const auto& [_, e] : g_results) if (e.pass) ++passing;
    // "Total" = reported + pending. Gives the user "X/total passing"
    // where total includes silent test plugins.
    size_t total = reported + pending.size();

    if (pending.empty()) {
        log::InfoF("Test suite: %zu/%zu passing as of %s",
                   passing, total,
                   messageLabel ? messageLabel : "<unknown>");
    } else {
        log::InfoF("Test suite: %zu/%zu passing as of %s "
                   "(%zu not yet reported)",
                   passing, total,
                   messageLabel ? messageLabel : "<unknown>",
                   pending.size());
    }

    // FAIL lines come first (most actionable).
    if (passing < reported) {
        for (const auto& [name, e] : g_results) {
            if (e.pass) continue;
            log::InfoF("  FAIL %s: %s",
                       name.c_str(),
                       e.reason.empty() ? "(no reason given)" : e.reason.c_str());
        }
    }
    // Then PENDING.
    for (const auto& [name, owner] : pending) {
        log::InfoF("  PENDING %s (registered by '%s', no report yet)",
                   name.c_str(), owner.c_str());
    }

    KCDX_DEV("TEST", "SUMMARY",
        kcdx::dev::KV("message_label", messageLabel ? messageLabel : "<unknown>"),
        kcdx::dev::KV("passing",       (unsigned long long)passing),
        kcdx::dev::KV("reported",      (unsigned long long)reported),
        kcdx::dev::KV("pending",       (unsigned long long)pending.size()),
        kcdx::dev::KV("total",         (unsigned long long)total));

    g_last_emitted_seq = g_seq;
}

void EmitSummaryIfChanged(const char* messageLabel) {
    if (!dev::IsEnabled()) return;
    {
        std::lock_guard<std::mutex> lk(g_lock);
        if (g_seq == g_last_emitted_seq) return;
    }
    EmitSummary(messageLabel);  // takes its own lock + updates watermark
}

const char* MessageLabel(uint32_t messageType) {
    switch (messageType) {
    case kcdxMessage_PostLoad:     return "kPostLoad";
    case kcdxMessage_PostPostLoad: return "kPostPostLoad";
    case kcdxMessage_InputLoaded:  return "kInputLoaded";
    case kcdxMessage_NewGame:      return "kNewGame";
    case kcdxMessage_PreLoadGame:  return "kPreLoadGame";
    case kcdxMessage_PostLoadGame: return "kPostLoadGame";
    case kcdxMessage_SaveGame:     return "kSaveGame";
    case kcdxMessage_DeleteGame:   return "kDeleteGame";
    case kcdxMessage_LuaReady:     return "kLuaReady";
    default:                       return nullptr;
    }
}

void RegisterExpectedTestName(std::string_view testName,
                              std::string_view pluginName) {
    if (testName.empty()) return;
    std::lock_guard<std::mutex> lk(g_lock);
    g_expected[std::string(testName)] = std::string(pluginName);
}

void IncrementGatedOffCount() {
    std::lock_guard<std::mutex> lk(g_lock);
    ++g_gated_off_count;
}

void EmitGatedOffSummary() {
    int count;
    {
        std::lock_guard<std::mutex> lk(g_lock);
        count = g_gated_off_count;
    }
    if (count <= 0) return;
    log::InfoF("Test suite: %d plugin(s) gated off (dev mode disabled; "
               "enable via <plugins>/kcdx-engine.toml)", count);
}

}  // namespace kcdx::test
