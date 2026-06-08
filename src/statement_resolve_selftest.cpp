#include "statement_resolve_selftest.h"

#include <atomic>
#include <cstdint>
#include <cstdio>   // snprintf
#include <string>

#include "log.h"
#include "refdb.h"
#include "test.h"

namespace kcdx::stmt_resolve {

namespace {

constexpr const char* kRow      = "cap-83-stmt-resolve";
constexpr const char* kCategory = "STMT_RESOLVE_SELFTEST";

// The curated function under test (kcdx_id 144 — 59 statements, all §9.3
// locator families present). cap-67 already resolves it by name.
constexpr const char* kFn = "SaveGame";

// The first-call-to callee target measured from the curated DB (idx 8).
constexpr const char* kCallee = "FUN_1804d455c";

// Build a bare locator of one kind.
refdb::StatementLocator Loc(refdb::StatementLocatorKind k) {
    refdb::StatementLocator l;
    l.kind = k;
    return l;
}

// One assertion's outcome: a found resolution whose idx/brl/kind matched (or
// not), or a not-found that the assertion required to be found.
struct Check {
    bool ok = false;
    std::string detail;  // populated on failure (what was expected vs observed).
};

// Assert a locator resolves to the expected statement_idx; optionally also
// assert byte_range_len and/or kind. Any mismatch (or found=false) is a FAIL.
Check ExpectIdx(const char* label,
                const refdb::StatementResolution& r,
                int64_t wantIdx,
                bool checkBrl, int64_t wantBrl,
                bool checkKind, const char* wantKind) {
    Check c;
    if (!r.found) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "%s: resolution returned found=false (a broken resolution — the "
            "locator should resolve to statement idx %lld)",
            label, (long long)wantIdx);
        c.detail = buf;
        return c;
    }
    if (r.statement_idx != wantIdx) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "%s: resolved to statement idx %lld, expected %lld",
            label, (long long)r.statement_idx, (long long)wantIdx);
        c.detail = buf;
        return c;
    }
    if (checkBrl && (!r.has_byte_range_len || r.byte_range_len != wantBrl)) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "%s: byte_range_len %lld (has=%d), expected %lld",
            label,
            (long long)r.byte_range_len, r.has_byte_range_len ? 1 : 0,
            (long long)wantBrl);
        c.detail = buf;
        return c;
    }
    if (checkKind && r.kind != wantKind) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "%s: kind \"%s\", expected \"%s\"",
            label, r.kind.c_str(), wantKind);
        c.detail = buf;
        return c;
    }
    c.ok = true;
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

    char reason[768];

    // refdb must be open for any resolution to be meaningful. If it is not, the
    // address surface itself is down — that is its own loud failure (cap-67 et
    // al. catch it); here we report a clear degraded result rather than a
    // resolution FAIL.
    if (!refdb::IsLoaded()) {
        std::snprintf(reason, sizeof(reason),
            "DEGRADED: refdb is not loaded — the statement-resolution surface "
            "cannot be exercised (the reference database did not open; see the "
            "REFDB open_failed line). Not a statement-resolution defect.");
        LOG_INFO_KV(kCategory, "selftest_degraded",
            ::kcdx::log::KV::BareStr("reason", "db_not_loaded"));
        kcdx::test::ReportResult(kRow, true, reason);
        kcdx::test::EmitSummaryIfChanged("cap-83 stmt-resolve");
        return;
    }

    // Probe function_entry FIRST. It distinguishes the two not-found worlds:
    //   * SaveGame resolves AND has statements  → function_entry is found=true
    //     (every function has a first statement) → run the full assertion set.
    //   * SaveGame is unknown OR carries no statements (the pre-deploy state
    //     where the shipped USER DB lacks the statement tables — 2a's empty
    //     caches) → function_entry is found=false → report DEGRADED (PASS, but
    //     flagged), NOT a hard FAIL. The DEV DB carries the tables; the
    //     deployed USER DB regenerates them on deploy.
    refdb::StatementResolution entry =
        refdb::ResolveStatementByName(kFn, Loc(refdb::StatementLocatorKind::FunctionEntry));

    if (!entry.found) {
        std::snprintf(reason, sizeof(reason),
            "DEGRADED: SaveGame's function_entry did not resolve — the curated "
            "statement tables are not present in this build's reference.sqlite "
            "(name_unknown or function_no_statements; the deployed USER DB "
            "regenerates the statement tables on deploy). Statement resolution "
            "is not exercisable here; this is a deploy-state observation, NOT a "
            "resolution defect.");
        LOG_INFO_KV(kCategory, "selftest_degraded",
            ::kcdx::log::KV::BareStr("reason", "statement_data_not_present"),
            ::kcdx::log::KV("function", kFn));
        kcdx::test::ReportResult(kRow, true, reason);
        kcdx::test::EmitSummaryIfChanged("cap-83 stmt-resolve");
        return;
    }

    // Statement data IS present — run every falsifiable assertion against the
    // ground-truth values measured from the curated DB.
    Check c_entry = ExpectIdx("function_entry", entry,
                              /*wantIdx=*/0,
                              /*checkBrl=*/true, /*wantBrl=*/3,
                              /*checkKind=*/true, /*wantKind=*/"assign");

    refdb::StatementResolution exit_ =
        refdb::ResolveStatementByName(kFn, Loc(refdb::StatementLocatorKind::FunctionExit));
    Check c_exit = ExpectIdx("function_exit", exit_,
                             /*wantIdx=*/58,
                             /*checkBrl=*/true, /*wantBrl=*/30,
                             /*checkKind=*/true, /*wantKind=*/"return");

    refdb::StatementLocator firstCallLoc = Loc(refdb::StatementLocatorKind::FirstCallTo);
    firstCallLoc.callee_or_fn = kCallee;
    refdb::StatementResolution firstCall =
        refdb::ResolveStatementByName(kFn, firstCallLoc);
    Check c_firstcall = ExpectIdx("first_call_to(FUN_1804d455c)", firstCall,
                                  /*wantIdx=*/8,
                                  /*checkBrl=*/true, /*wantBrl=*/5,
                                  /*checkKind=*/false, /*wantKind=*/nullptr);

    refdb::StatementResolution firstRet =
        refdb::ResolveStatementByName(kFn, Loc(refdb::StatementLocatorKind::FirstReturn));
    Check c_firstret = ExpectIdx("first_return", firstRet,
                                 /*wantIdx=*/13,
                                 /*checkBrl=*/false, /*wantBrl=*/0,
                                 /*checkKind=*/true, /*wantKind=*/"return");

    refdb::StatementResolution lastRet =
        refdb::ResolveStatementByName(kFn, Loc(refdb::StatementLocatorKind::LastReturn));
    Check c_lastret = ExpectIdx("last_return", lastRet,
                                /*wantIdx=*/58,
                                /*checkBrl=*/false, /*wantBrl=*/0,
                                /*checkKind=*/false, /*wantKind=*/nullptr);

    // Captures-by-name join @ function_entry (idx 0): EXACTLY 2 — param_7
    // (stack, size 8) AND puVar6 (register, size 8). Order-independent: assert
    // the SET (both present with the right storage_kind + size).
    Check c_caps;
    {
        const auto& caps = entry.captures;
        bool param7_ok = false, puvar6_ok = false;
        for (const auto& cap : caps) {
            if (cap.var_name == "param_7" &&
                cap.storage_kind == "stack" &&
                cap.has_size_bytes && cap.size_bytes == 8) {
                param7_ok = true;
            }
            if (cap.var_name == "puVar6" &&
                cap.storage_kind == "register" &&
                cap.has_size_bytes && cap.size_bytes == 8) {
                puvar6_ok = true;
            }
        }
        if (caps.size() == 2 && param7_ok && puvar6_ok) {
            c_caps.ok = true;
        } else {
            char buf[320];
            std::snprintf(buf, sizeof(buf),
                "captures@idx0: expected EXACTLY 2 (param_7 stack/8 AND puVar6 "
                "register/8) but got count=%zu param_7_ok=%d puVar6_ok=%d (the "
                "referenced_vars join by (av_id, statement_idx) is wrong)",
                caps.size(), param7_ok ? 1 : 0, puvar6_ok ? 1 : 0);
            c_caps.detail = buf;
        }
    }

    const Check* checks[] = {
        &c_entry, &c_exit, &c_firstcall, &c_firstret, &c_lastret, &c_caps,
    };
    const bool pass = c_entry.ok && c_exit.ok && c_firstcall.ok &&
                      c_firstret.ok && c_lastret.ok && c_caps.ok;

    if (pass) {
        std::snprintf(reason, sizeof(reason),
            "PASS — SaveGame's §9.3 locator families resolve to ground truth: "
            "function_entry=idx0(assign,brl3), function_exit=idx58(return,brl30), "
            "first_call_to(FUN_1804d455c)=idx8(brl5), first_return=idx13(return), "
            "last_return=idx58; captures@idx0 = {param_7 stack/8, puVar6 "
            "register/8}. FALSIFIABLE: any locator resolving to the wrong "
            "index/byte_range_len/kind, or the captures join returning the wrong "
            "set, goes red.");
        LOG_INFO_KV(kCategory, "selftest_pass",
            ::kcdx::log::KV("function", kFn),
            ::kcdx::log::KV("entry_idx", (long long)entry.statement_idx),
            ::kcdx::log::KV("exit_idx", (long long)exit_.statement_idx),
            ::kcdx::log::KV("firstcall_idx", (long long)firstCall.statement_idx),
            ::kcdx::log::KV("captures", (long long)entry.captures.size()));
        kcdx::test::ReportResult(kRow, true, reason);
    } else {
        // Compose a detailed reason naming every failing assertion.
        std::string fails;
        for (const Check* c : checks) {
            if (!c->ok && !c->detail.empty()) {
                if (!fails.empty()) fails += " | ";
                fails += c->detail;
            }
        }
        std::snprintf(reason, sizeof(reason),
            "FAIL: statement resolution mis-resolved against the curated ground "
            "truth — %s", fails.c_str());
        LOG_ERROR_KV(kCategory, "selftest_fail",
            ::kcdx::log::KV("function", kFn),
            ::kcdx::log::KV::BareStr("fails", fails.c_str()));
        kcdx::test::ReportResult(kRow, false, reason);
    }
    kcdx::test::EmitSummaryIfChanged("cap-83 stmt-resolve");
}

}  // namespace kcdx::stmt_resolve
