#include "survival_report.h"

#include <cstdio>   // std::FILE, fopen_s, fwrite, fflush, fclose, snprintf
#include <filesystem>
#include <string>

#include "console.h"  // console::PrintLine — the verified per-row overlay stream.
#include "log.h"
#include "paths.h"
#include "survival_verify.h"

// survival_report — see survival_report.h for the report-producer contract (the
// report producer is a distinct responsibility from the verify sweep; the
// per-row JSONL flush is the load-bearing durability mechanism). COLD path — no
// hot-path allocation/IO discipline applies (the sweep runs once per
// kcdx_verify_all trigger, never during gameplay).

namespace kcdx::survival_report {

namespace {

constexpr const char* kCategory = "SURVREPORT";

namespace svv = kcdx::survival_verify;

// The schema_version the v3 report pins (the cross-repo frontend contract — the
// v3 schema's schema_version const). A version constant, NOT a game-binary
// address.
constexpr int kSchemaVersion = 3;

// The matched-id if/then/else (v3 schema rows.items): a verified_working /
// passed_not_verified row carries an integer matched_address_version_id; every
// other verdict carries null. This mirrors the RowVerdict.has_matched_id the
// sweep already computes — a verified-block verdict KEEPS its matched id, every
// other verdict clears it. (survival_verify.cpp clears has_matched_id for any
// verdict that is not PassedNotVerified/VerifiedWorking/Failed; a dead-resolve
// Failed keeps its id internally, but the v3 schema requires null on failed, so
// the report keys off the verdict, not has_matched_id alone.)
bool VerdictIsVerifiedBlock(svv::Verdict v) {
    return v == svv::Verdict::VerifiedWorking ||
           v == svv::Verdict::PassedNotVerified;
}

// The v3 invoke_skip_reason token, mapped to the schema's enum
// (["unsafe_to_call", "uncontainable", "not_a_callable_kind", null]). The
// in-process InvokeSkipReason::None is the OBSERVED / read-attempted case — the
// schema represents it as JSON null (the enum does NOT carry a "none" token).
// Returns nullptr to signal "emit JSON null"; a non-null pointer is the literal
// token to emit as a quoted string.
const char* InvokeSkipReasonJsonToken(svv::InvokeSkipReason r) {
    switch (r) {
        case svv::InvokeSkipReason::None:             return nullptr;  // → JSON null
        case svv::InvokeSkipReason::UnsafeToCall:     return "unsafe_to_call";
        case svv::InvokeSkipReason::Uncontainable:    return "uncontainable";
        case svv::InvokeSkipReason::NotACallableKind: return "not_a_callable_kind";
    }
    return nullptr;
}

}  // namespace

std::string JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    // A control char with no short escape → \u00XX.
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                    out += buf;
                } else {
                    // Pass UTF-8 bytes through verbatim (the source strings are
                    // already valid UTF-8 — names/details from the DB + the
                    // engine; a non-ASCII byte is a continuation byte of a
                    // multi-byte char, emitted as-is).
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    return out;
}

std::string Reporter::SerializeRow(const svv::RowVerdict& rv) const {
    const bool verifiedBlock = VerdictIsVerifiedBlock(rv.verdict);

    // matched_address_version_id: integer on a verified-block row that matched a
    // candidate, null otherwise (the v3 if/then/else). A verified-block verdict
    // without a matched id is a producer defect, but the schema requires an
    // integer there — fall back to 0 (a real value) so the document still
    // validates, and the cap-84 self-test asserts the id is present.
    std::string matchedId;
    if (verifiedBlock) {
        matchedId = std::to_string(
            rv.has_matched_id ? rv.matched_address_version_id : 0ull);
    } else {
        matchedId = "null";
    }

    // invoke_skip_reason: a quoted token, or JSON null (when invoke_attempted is
    // true OR the in-process reason is None).
    const char* skipTok = InvokeSkipReasonJsonToken(rv.invoke_skip_reason);
    std::string skipReason =
        (rv.invoke_attempted || skipTok == nullptr)
            ? std::string("null")
            : (std::string("\"") + skipTok + "\"");

    // version: the resolved version is required minLength:1 by the schema. A row
    // with no resolved version (an unresolved-name cannot_check) falls back to
    // the report's game_version so the field stays non-empty (the running build
    // is the version the check ran against).
    const std::string& ver =
        rv.resolved_version.empty() ? gameVersion_ : rv.resolved_version;

    std::string out;
    out.reserve(256);
    out += "{\"kcdx_id\":";
    out += std::to_string(rv.kcdx_id);
    out += ",\"version\":\"";
    out += JsonEscape(ver);
    out += "\",\"verdict\":\"";
    out += svv::VerdictName(rv.verdict);  // the v3 snake_case verdict token.
    out += "\",\"method_rank\":";
    out += std::to_string(rv.method_rank);
    out += ",\"invoke_attempted\":";
    out += rv.invoke_attempted ? "true" : "false";
    out += ",\"invoke_skip_reason\":";
    out += skipReason;
    out += ",\"detail\":\"";
    out += JsonEscape(rv.detail);
    out += "\",\"matched_address_version_id\":";
    out += matchedId;
    out += "}";
    return out;
}

bool Reporter::Begin(size_t rowsExpected, const std::string& gameVersion) {
    rowsExpected_ = rowsExpected;
    gameVersion_  = gameVersion;

    // The sink lives alongside kcdx-dev.log — the same logs dir, keyed by
    // the session stamp so each run gets its own pair. The JSONL is the durable
    // during-sweep sink; the .json is the finalized v3 document.
    const std::filesystem::path logsDir =
        kcdx::paths::EngineDataDirPath() / "logs";
    const std::string stamp = kcdx::log::SessionStamp();
    const std::filesystem::path jsonl = logsDir / ("kcdx-verify_" + stamp + ".jsonl");
    const std::filesystem::path json  = logsDir / ("kcdx-verify_" + stamp + ".json");
    jsonlPath_  = kcdx::paths::ToUtf8(jsonl);
    reportPath_ = kcdx::paths::ToUtf8(json);

    std::FILE* f = nullptr;
    const errno_t err = ::fopen_s(&f, jsonlPath_.c_str(), "wb");
    if (err != 0 || f == nullptr) {
        // FAIL LOUD: the durability sink could not open. Degrade to in-memory
        // accumulation (Finalize still writes the v3 document from rowsJson_) but
        // the incremental guarantee is forfeited — surfaced by Finalize. Never a
        // silent empty report.
        sinkDegraded_ = true;
        LOG_ERROR_KV(kCategory, "jsonl_open_failed",
            ::kcdx::log::KV("path", jsonlPath_),
            ::kcdx::log::KV("errno", (long long)err),
            ::kcdx::log::KV("note",
                "incremental JSONL sink could not open; report degrades to "
                "in-memory finalize (incremental durability forfeited this run)"));
        return false;
    }
    jsonl_ = f;
    LOG_INFO_KV(kCategory, "report_begin",
        ::kcdx::log::KV("jsonl", jsonlPath_),
        ::kcdx::log::KV("rows_expected", (unsigned long long)rowsExpected_),
        ::kcdx::log::KV("game_version", gameVersion_));
    return true;
}

void Reporter::OnRow(const svv::RowVerdict& rv) {
    ++rowCount_;
    if (VerdictIsVerifiedBlock(rv.verdict)) ++passingCount_;

    const std::string rowJson = SerializeRow(rv);

    // (1) Stream the per-row console line — `[N/total] <name> v<ver> ->
    // <verdict> (rank <r>)`. The console overlay is the in-game progress state;
    // a 157-row sweep never reads as a hang. PrintLine returns false (and WARNs)
    // when the console surface isn't ready — not fatal to the report.
    {
        char line[512];
        std::snprintf(line, sizeof(line), "[%zu/%zu] %s v%s -> %s (rank %d)",
            rowCount_, rowsExpected_,
            rv.name.empty() ? "(unnamed)" : rv.name.c_str(),
            rv.resolved_version.empty() ? gameVersion_.c_str()
                                        : rv.resolved_version.c_str(),
            svv::VerdictName(rv.verdict), rv.method_rank);
        kcdx::console::PrintLine(line);
    }

    // (2) Accumulate the row for the finalize document (the v3 rows[] array).
    if (!rowsJson_.empty()) rowsJson_ += ",";
    rowsJson_ += rowJson;

    // (3) Append + FLUSH the JSONL line the INSTANT the row resolves — the
    // durable per-row write, paired with the console line in the same tick. A
    // serialize/flush failure is LOGGED loud and does NOT abort the sweep (the
    // remaining rows still flush).
    if (jsonl_ != nullptr) {
        const std::string jsonlLine = rowJson + "\n";
        const size_t wrote =
            std::fwrite(jsonlLine.data(), 1, jsonlLine.size(), jsonl_);
        if (wrote != jsonlLine.size() || std::fflush(jsonl_) != 0) {
            LOG_ERROR_KV(kCategory, "jsonl_flush_failed",
                ::kcdx::log::KV("kcdx_id", (unsigned long long)rv.kcdx_id),
                ::kcdx::log::KV("name", rv.name),
                ::kcdx::log::KV("wrote", (unsigned long long)wrote),
                ::kcdx::log::KV("expected", (unsigned long long)jsonlLine.size()),
                ::kcdx::log::KV("note",
                    "per-row JSONL flush failed; this row's durable record is "
                    "incomplete (the sweep continues)"));
        } else {
            // The flush landed on disk — the incremental counter advances.
            // The cap-95 self-test asserts this grew across the sweep (the sink
            // is written per row, not one bulk write at end).
            ++flushedRows_;
        }
    }
}

bool Reporter::Finalize(bool complete) {
    // Build the v3 document from the accumulated rows[] + the summary roll-up.
    // summary.passing = count of verified_working + passed_not_verified (the v3
    // verified block); summary.total = rows[] length.
    std::string doc;
    doc.reserve(rowsJson_.size() + 256);
    doc += "{\"schema_version\":";
    doc += std::to_string(kSchemaVersion);
    doc += ",\"game_version\":\"";
    doc += JsonEscape(gameVersion_.empty() ? std::string("unknown") : gameVersion_);
    doc += "\",\"summary\":{\"passing\":";
    doc += std::to_string(passingCount_);
    doc += ",\"total\":";
    doc += std::to_string(rowCount_);
    doc += "},\"complete\":";
    doc += complete ? "true" : "false";
    doc += ",\"rows_expected\":";
    doc += std::to_string(rowsExpected_);
    doc += ",\"rows\":[";
    doc += rowsJson_;
    doc += "]}";

    // Close the JSONL sink before writing the finalized document (the durable
    // per-row record is complete; the v3 doc is the consumable form).
    if (jsonl_ != nullptr) {
        std::fclose(jsonl_);
        jsonl_ = nullptr;
    }

    // Write the finalized v3 document alongside kcdx-dev.log.
    std::FILE* f = nullptr;
    const errno_t err = ::fopen_s(&f, reportPath_.c_str(), "wb");
    if (err != 0 || f == nullptr) {
        // FAIL LOUD: the finalized report could not be written.
        LOG_ERROR_KV(kCategory, "report_write_failed",
            ::kcdx::log::KV("path", reportPath_),
            ::kcdx::log::KV("errno", (long long)err),
            ::kcdx::log::KV("note",
                "v3 report finalize failed; the durable JSONL sink still holds "
                "the per-row records for recovery"));
        return false;
    }
    const size_t wrote = std::fwrite(doc.data(), 1, doc.size(), f);
    const bool flushOk = (std::fflush(f) == 0);
    std::fclose(f);
    if (wrote != doc.size() || !flushOk) {
        LOG_ERROR_KV(kCategory, "report_write_short",
            ::kcdx::log::KV("path", reportPath_),
            ::kcdx::log::KV("wrote", (unsigned long long)wrote),
            ::kcdx::log::KV("expected", (unsigned long long)doc.size()),
            ::kcdx::log::KV("note", "v3 report write was short/unflushed"));
        return false;
    }

    LOG_INFO_KV(kCategory, "report_complete",
        ::kcdx::log::KV("path", reportPath_),
        ::kcdx::log::KV("rows", (unsigned long long)rowCount_),
        ::kcdx::log::KV("rows_expected", (unsigned long long)rowsExpected_),
        ::kcdx::log::KV("passing", (unsigned long long)passingCount_),
        ::kcdx::log::KV("complete", complete),
        ::kcdx::log::KV("flushed_rows", (unsigned long long)flushedRows_),
        ::kcdx::log::KV("sink_degraded", sinkDegraded_));

    // The canonical acceptance signal (acceptance-signal.md) — a thin adapter
    // over the report verdict, written to the dev log (the known sink alongside
    // kcdx-dev.log). The verify sweep producing a finalized v3 report that
    // validated (written here) AND completing the curated set is the acceptance
    // claim; a short/partial sweep or a degraded sink denies it. The agent greps
    // these fixed tokens — the user never reads the log.
    const bool accepted = complete && !sinkDegraded_;
    if (accepted) {
        LOG_INFO("ACCEPT", "ACCEPT-RESULT: PASS kcdx_verify_all — v3 report "
                           "finalized (%zu/%zu rows, %zu passing) at %s",
                 rowCount_, rowsExpected_, passingCount_, reportPath_.c_str());
    } else {
        LOG_INFO("ACCEPT", "ACCEPT-RESULT: FAIL kcdx_verify_all — %s "
                           "(%zu/%zu rows, complete=%s, sink_degraded=%s)",
                 sinkDegraded_ ? "incremental JSONL sink did not open"
                               : "sweep did not complete the curated set",
                 rowCount_, rowsExpected_, complete ? "true" : "false",
                 sinkDegraded_ ? "true" : "false");
    }
    LOG_INFO("ACCEPT", "ACCEPT-SUITE: %d/%d passing", accepted ? 1 : 0, 1);

    return true;
}

Reporter::~Reporter() {
    // Defensive close — Finalize closes the sink on the normal path; this catches
    // a Reporter destroyed without Finalize (e.g. an early return). The on-disk
    // JSONL still holds every flushed row (a mid-sweep death leaves a
    // complete-up-to-N record).
    if (jsonl_ != nullptr) {
        std::fclose(jsonl_);
        jsonl_ = nullptr;
    }
}

}  // namespace kcdx::survival_report
