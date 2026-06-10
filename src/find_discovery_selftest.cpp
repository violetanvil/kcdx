#include "find_discovery_selftest.h"

#include <algorithm>  // std::find
#include <atomic>
#include <cstdint>
#include <cstdio>   // snprintf
#include <string>
#include <vector>

#include "log.h"
#include "refdb.h"
#include "test.h"

namespace kcdx::find_discovery_selftest {

namespace {

constexpr const char* kRowString = "cap-98-find-string";
constexpr const char* kRowTrunc  = "cap-98-truncates-loud";
constexpr const char* kRowGate   = "cap-98-gate-discriminates";
constexpr const char* kRowOps    = "cap-98-applicable-ops";
constexpr const char* kCategory  = "FIND_DISCOVERY_SELFTEST";

// A string_ref owned by EXACTLY one function in the dev corpus (av 14595 /
// FUN_18043ee28 — a sys_spec / texture-streaming diagnostic). Distinctive +
// stable across extractor runs; the owning function is decompile_quality
// "clean" with 189 statements. Ground truth measured 2026-06-10.
constexpr const char* kKnownString =
    "   You have to set r_TexturesStreaming = 1 to see texture information!";

// The auto_name of the same known function (av 14595) — the EnumerateStatements
// handle for the applicable_ops row. A clean 189-statement function carrying
// call statements, so the call→ops mapping is exercised against real corpus
// data. Ground truth measured 2026-06-10.
constexpr const char* kKnownAutoName = "FUN_18043ee28";

// True iff `ops` contains `name`.
bool OpsContain(const std::vector<std::string>& ops, const char* name) {
    return std::find(ops.begin(), ops.end(), std::string(name)) != ops.end();
}

// A callee referenced by 30,393 distinct functions (CRT thread-footer init) —
// far over the 500 cap, so the truncation path is deterministically exercised.
constexpr const char* kOverCapCallee = "_Init_thread_footer";

// A string no statement carries — for the genuine-zero-match (available, empty)
// discriminator. Includes a sentinel no real string_ref would hold.
constexpr const char* kImpossibleString =
    "\x01kcdx_no_such_string_ref_sentinel_\x02_cap94";

refdb::FindCriteria StringCriteria(const char* s) {
    refdb::FindCriteria c;
    c.has_string = true;
    c.string = s;
    return c;
}

refdb::FindCriteria CalleeCriteria(const char* s) {
    refdb::FindCriteria c;
    c.has_callee = true;
    c.callee = s;
    return c;
}

}  // namespace

void RunSelfTestOnce() {
    static std::atomic<bool> done{false};
    bool expected = false;
    if (!done.compare_exchange_strong(expected, true,
                                      std::memory_order_acq_rel)) {
        return;
    }

    char reason[896];

    // The dev DB is a maintainer artifact — if it is absent, the search surface
    // cannot be exercised. Report a CLEAR DEGRADED result on all three rows
    // (not a hard FAIL, not a crash): a deploy-state observation, not a defect.
    // OpenDevDb logs the precise reason token (dev_mode_off / dev_db_not_found /
    // dev_schema_mismatch). Probe its availability first via a genuine-zero
    // search: unavailable==true means the gate could not open it.
    refdb::FindResult probe = refdb::FindFunctions(StringCriteria(kImpossibleString));
    if (probe.unavailable) {
        std::snprintf(reason, sizeof(reason),
            "DEGRADED: the dev discovery DB (reference-dev.sqlite) is not "
            "available — OpenDevDb could not open it (dev mode off, the file is "
            "absent under kcdx-engine/data, or a schema mismatch; see the DEVDB "
            "dev_db_open_* line for which gate failed). The search surface "
            "cannot be exercised this run. Not a search defect.");
        LOG_INFO_KV(kCategory, "selftest_degraded",
            ::kcdx::log::KV::BareStr("reason", "dev_db_unavailable"));
        kcdx::test::ReportResult(kRowString, true, reason);
        kcdx::test::ReportResult(kRowTrunc, true, reason);
        kcdx::test::ReportResult(kRowGate, true, reason);
        kcdx::test::ReportResult(kRowOps, true, reason);
        kcdx::test::EmitSummaryIfChanged("cap-98 find-discovery");
        return;
    }

    // --- cap-98-gate-discriminates -----------------------------------------
    // The `probe` search above is dev-DB-available (unavailable==false) and
    // searches for an impossible string. It MUST be a genuine zero-match:
    // records empty, total_matches 0, unavailable false. This proves the gate
    // (and FindFunctions) distinguishes "no match" from "DB down" — the AP14
    // loud-vs-silent contract.
    {
        const bool genuineEmpty =
            !probe.unavailable && probe.records.empty() &&
            probe.total_matches == 0 && !probe.truncated;
        if (genuineEmpty) {
            std::snprintf(reason, sizeof(reason),
                "PASS: the dev-DB gate discriminates available-empty from "
                "unavailable. With dev mode on and reference-dev.sqlite present, "
                "a search for an impossible string returned a GENUINE zero-match "
                "(unavailable=false, 0 records, total_matches=0) — not the "
                "dev-tool-unavailable signal. OpenDevDb opened the second "
                "read-only connection and the schema gate passed.");
            kcdx::test::ReportResult(kRowGate, true, reason);
        } else {
            std::snprintf(reason, sizeof(reason),
                "FAIL: the dev-DB gate did NOT cleanly discriminate. A search "
                "for an impossible string with the DB available should be a "
                "genuine zero-match, but observed unavailable=%d, records=%zu, "
                "total_matches=%lld, truncated=%d (want unavailable=0, "
                "records=0, total=0, truncated=0). Either the gate cannot tell "
                "'no match' from 'DB down', or an impossible string matched.",
                probe.unavailable ? 1 : 0, probe.records.size(),
                (long long)probe.total_matches, probe.truncated ? 1 : 0);
            LOG_ERROR_KV(kCategory, "gate_discriminate_fail",
                ::kcdx::log::KV("unavailable", probe.unavailable ? 1 : 0),
                ::kcdx::log::KV("records", (long long)probe.records.size()),
                ::kcdx::log::KV("total_matches", (long long)probe.total_matches));
            kcdx::test::ReportResult(kRowGate, false, reason);
        }
    }

    // --- cap-98-find-string ------------------------------------------------
    // A string known to be owned must return >= 1 record whose header
    // (function / module / rva) is fully populated. Falsifiable: zero records
    // for an owned string, or an empty function name / module / zero rva, is a
    // broken search or a broken header read.
    {
        refdb::FindResult r = refdb::FindFunctions(StringCriteria(kKnownString));
        if (r.unavailable) {
            // The probe above said available; an unavailable here is a real
            // regression (the connection dropped mid-run) — report it red.
            std::snprintf(reason, sizeof(reason),
                "FAIL: FindFunctions({string}) returned unavailable=true for a "
                "known-owned string, though the gate probe found the dev DB "
                "available moments earlier. The dev connection is unstable.");
            LOG_ERROR_KV(kCategory, "find_string_unavailable",
                ::kcdx::log::KV::BareStr("reason", "dev_db_unavailable"));
            kcdx::test::ReportResult(kRowString, false, reason);
        } else if (r.records.empty()) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: FindFunctions({string=<r_TexturesStreaming diagnostic>}) "
                "returned 0 records, but the dev corpus has exactly 1 owning "
                "function (av 14595 / FUN_18043ee28). The string_ref query "
                "missed an owner it should have found — a broken search.");
            LOG_ERROR_KV(kCategory, "find_string_empty",
                ::kcdx::log::KV("total_matches", (long long)r.total_matches));
            kcdx::test::ReportResult(kRowString, false, reason);
        } else {
            const refdb::FindRecord& rec = r.records.front();
            const bool headerOk = !rec.function.empty() && !rec.module.empty() &&
                                  rec.rva != 0;
            if (headerOk) {
                std::snprintf(reason, sizeof(reason),
                    "PASS: FindFunctions({string}) returned %zu record(s) "
                    "(total_matches=%lld) for a known-owned string. The top "
                    "record's header is populated: function=\"%s\", "
                    "module=\"%s\", rva=0x%llX, decompile_quality=\"%s\". The "
                    "string_ref->owning-function query + the header read both "
                    "work against the dev corpus.",
                    r.records.size(), (long long)r.total_matches,
                    rec.function.c_str(), rec.module.c_str(),
                    (unsigned long long)rec.rva,
                    rec.decompile_quality_label.c_str());
                kcdx::test::ReportResult(kRowString, true, reason);
            } else {
                std::snprintf(reason, sizeof(reason),
                    "FAIL: FindFunctions({string}) returned %zu record(s) but "
                    "the top record's header is incomplete: function=\"%s\" "
                    "(empty=%d), module=\"%s\" (empty=%d), rva=0x%llX (zero=%d). "
                    "A matched function must carry a non-empty name + module + "
                    "non-zero rva (the LoadFindRecordHeader read is broken).",
                    r.records.size(), rec.function.c_str(),
                    rec.function.empty() ? 1 : 0, rec.module.c_str(),
                    rec.module.empty() ? 1 : 0, (unsigned long long)rec.rva,
                    rec.rva == 0 ? 1 : 0);
                LOG_ERROR_KV(kCategory, "find_string_header_incomplete",
                    ::kcdx::log::KV("function", rec.function.c_str()),
                    ::kcdx::log::KV("module", rec.module.c_str()),
                    ::kcdx::log::KV("rva", (long long)rec.rva));
                kcdx::test::ReportResult(kRowString, false, reason);
            }
        }
    }

    // --- cap-98-truncates-loud ---------------------------------------------
    // A callee owned by > 500 functions must truncate LOUDLY: truncated==true,
    // total_matches > 500, records.size() == kFindResultCap (500). Falsifiable:
    // a silent partial (truncated==false), a wrong total (<= 500), or a wrong
    // record count (!= 500) is a broken cap — the silent-failure shape (AP14).
    {
        refdb::FindResult r = refdb::FindFunctions(CalleeCriteria(kOverCapCallee));
        if (r.unavailable) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: FindFunctions({callee}) returned unavailable=true for an "
                "over-cap callee, though the dev DB was available moments "
                "earlier. The dev connection is unstable.");
            LOG_ERROR_KV(kCategory, "truncate_unavailable",
                ::kcdx::log::KV::BareStr("reason", "dev_db_unavailable"));
            kcdx::test::ReportResult(kRowTrunc, false, reason);
        } else {
            const bool capOk =
                r.truncated && r.total_matches > refdb::kFindResultCap &&
                (int)r.records.size() == refdb::kFindResultCap;
            if (capOk) {
                std::snprintf(reason, sizeof(reason),
                    "PASS: FindFunctions({callee=_Init_thread_footer}) "
                    "truncated LOUDLY — truncated=true, total_matches=%lld (> "
                    "the cap %d), records.size()=%zu (== the cap). The over-cap "
                    "set returns the top-ranked cap + the full count, never a "
                    "silent partial.",
                    (long long)r.total_matches, refdb::kFindResultCap,
                    r.records.size());
                kcdx::test::ReportResult(kRowTrunc, true, reason);
            } else {
                std::snprintf(reason, sizeof(reason),
                    "FAIL: FindFunctions({callee}) did NOT truncate loudly. "
                    "Observed truncated=%d, total_matches=%lld, "
                    "records.size()=%zu (want truncated=1, total>%d, "
                    "records==%d). An over-cap search must set truncated, carry "
                    "the full total, and return exactly the cap — a silent "
                    "partial is the AP14 silent-failure shape.",
                    r.truncated ? 1 : 0, (long long)r.total_matches,
                    r.records.size(), refdb::kFindResultCap,
                    refdb::kFindResultCap);
                LOG_ERROR_KV(kCategory, "truncate_not_loud",
                    ::kcdx::log::KV("truncated", r.truncated ? 1 : 0),
                    ::kcdx::log::KV("total_matches", (long long)r.total_matches),
                    ::kcdx::log::KV("records", (long long)r.records.size()));
                kcdx::test::ReportResult(kRowTrunc, false, reason);
            }
        }
    }

    // --- cap-98-applicable-ops ---------------------------------------------
    // applicable_ops names the REAL kcdx.op.* ops whose required statement-kind
    // matches, restricted to the corpus — NOT a kind echo. Enumerate a known
    // clean function, find its FIRST `call`-kind statement, and assert its
    // applicable_ops contains "skip_call_void" (the call family) AND does NOT
    // contain "replace_compare_constant" (the corpus emits no `compare`, so the
    // compare op must never leak in — naming a move the statement verb rejects
    // is AP14). Falsifiable: FAILS if the kind->ops mapping is the placeholder
    // kind-echo (a `call` statement would carry ["call"], lacking skip_call_void),
    // if the call family is wrong, or if replace_compare_constant leaks in.
    {
        refdb::EnumerateResult e = refdb::EnumerateStatements(kKnownAutoName);
        if (e.unavailable) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: EnumerateStatements(%s) returned unavailable=true, though "
                "the dev DB was available moments earlier. The dev connection is "
                "unstable.", kKnownAutoName);
            LOG_ERROR_KV(kCategory, "ops_unavailable",
                ::kcdx::log::KV::BareStr("reason", "dev_db_unavailable"));
            kcdx::test::ReportResult(kRowOps, false, reason);
        } else if (!e.found) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: EnumerateStatements(%s) returned found=false for a "
                "known-present clean function (av 14595). The name/auto_name "
                "resolve missed an owner it should have found.", kKnownAutoName);
            LOG_ERROR_KV(kCategory, "ops_enumerate_not_found",
                ::kcdx::log::KV("fn", kKnownAutoName));
            kcdx::test::ReportResult(kRowOps, false, reason);
        } else {
            const refdb::FindStatement* callStmt = nullptr;
            // EnumerateResult carries the full statement DETAIL (find does not —
            // KI-0015; dev_inspect's ONE-function path still does).
            for (const refdb::FindStatement& s : e.statements) {
                if (s.kind == "call") { callStmt = &s; break; }
            }
            if (!callStmt) {
                std::snprintf(reason, sizeof(reason),
                    "FAIL: EnumerateStatements(%s) returned %zu statement(s) but "
                    "NONE of kind `call`. The applicable_ops call->ops mapping "
                    "could not be exercised — the known clean function should "
                    "carry at least one call statement.",
                    kKnownAutoName, e.statements.size());
                LOG_ERROR_KV(kCategory, "ops_no_call_stmt",
                    ::kcdx::log::KV("statements",
                        (long long)e.statements.size()));
                kcdx::test::ReportResult(kRowOps, false, reason);
            } else {
                const bool hasSkipCall =
                    OpsContain(callStmt->applicable_ops, "skip_call_void");
                const bool hasNoop =
                    OpsContain(callStmt->applicable_ops, "replace_with_noop");
                const bool leaksCompare =
                    OpsContain(callStmt->applicable_ops,
                               "replace_compare_constant");
                // A call statement also rejects a kind-echo: the placeholder
                // emitted ["call"], which is not a real op name at all.
                const bool isKindEcho =
                    OpsContain(callStmt->applicable_ops, "call");
                if (hasSkipCall && hasNoop && !leaksCompare && !isKindEcho) {
                    std::snprintf(reason, sizeof(reason),
                        "PASS: a `call` statement (idx %lld) in %s carries the "
                        "real call-family ops — applicable_ops includes "
                        "skip_call_void + replace_with_noop, excludes "
                        "replace_compare_constant (its kind `compare` is never "
                        "emitted), and is not a kind-echo. %zu op(s) total. The "
                        "kind->op-NAME mapping (mirroring lua_bind_op.cpp) is "
                        "wired against real corpus data.",
                        (long long)callStmt->idx, kKnownAutoName,
                        callStmt->applicable_ops.size());
                    kcdx::test::ReportResult(kRowOps, true, reason);
                } else {
                    std::snprintf(reason, sizeof(reason),
                        "FAIL: a `call` statement (idx %lld) in %s has wrong "
                        "applicable_ops: has skip_call_void=%d, has "
                        "replace_with_noop=%d, leaks replace_compare_constant=%d, "
                        "is kind-echo(\"call\")=%d, count=%zu (want "
                        "skip_call_void=1, replace_with_noop=1, "
                        "replace_compare_constant=0, kind-echo=0). The kind->ops "
                        "mapping is wrong, the compare op leaked in, or the "
                        "placeholder kind-echo is still in place.",
                        (long long)callStmt->idx, kKnownAutoName,
                        hasSkipCall ? 1 : 0, hasNoop ? 1 : 0, leaksCompare ? 1 : 0,
                        isKindEcho ? 1 : 0, callStmt->applicable_ops.size());
                    LOG_ERROR_KV(kCategory, "ops_wrong",
                        ::kcdx::log::KV("has_skip_call_void", hasSkipCall ? 1 : 0),
                        ::kcdx::log::KV("has_noop", hasNoop ? 1 : 0),
                        ::kcdx::log::KV("leaks_compare", leaksCompare ? 1 : 0),
                        ::kcdx::log::KV("is_kind_echo", isKindEcho ? 1 : 0));
                    kcdx::test::ReportResult(kRowOps, false, reason);
                }
            }
        }
    }

    kcdx::test::EmitSummaryIfChanged("cap-98 find-discovery");
}

}  // namespace kcdx::find_discovery_selftest
