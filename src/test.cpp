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
    if (g_results.empty()) return;

    size_t total = g_results.size();
    size_t passing = 0;
    for (const auto& [_, e] : g_results) if (e.pass) ++passing;

    log::InfoF("Test suite: %zu/%zu passing as of %s",
               passing, total,
               messageLabel ? messageLabel : "<unknown>");

    if (passing < total) {
        for (const auto& [name, e] : g_results) {
            if (e.pass) continue;
            log::InfoF("  FAIL %s: %s",
                       name.c_str(),
                       e.reason.empty() ? "(no reason given)" : e.reason.c_str());
        }
    }

    KCDX_DEV("TEST", "SUMMARY",
        kcdx::dev::KV("message_label", messageLabel ? messageLabel : "<unknown>"),
        kcdx::dev::KV("passing",       (unsigned long long)passing),
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
    default:                       return nullptr;
    }
}

}  // namespace kcdx::test
