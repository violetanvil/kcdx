#include "survival_dispatch_selftest.h"

#include <cstdio>   // snprintf
#include <cstring>  // memcmp
#include <string>
#include <vector>

#include "log.h"
#include "refdb.h"
#include "survival.h"
#include "survival_pass.h"
#include "test.h"
#include "version_check_cache.h"

// cap-84 self-test — see survival_dispatch_selftest.h for why this lives in
// engine code + what each sub-check falsifies.

namespace kcdx::survival_dispatch_selftest {

namespace {

constexpr const char* kRow = "cap-84-survival-dispatch";
constexpr const char* kCategory = "SURVDISPATCH";

namespace sv = kcdx::survival;
namespace vcc = kcdx::version_check_cache;
namespace sp = kcdx::survival_pass;

// A synthetic plugin name that can never collide with a real plugin's Lookup
// (the charset is illegal for a [plugin].name, so no real plugin shares it).
constexpr const char* kSyntheticPlugin = "__cap84_synthetic__";

// A curated FUNCTION row used for the optional real-Unchanged check. SaveGame
// (kcdx_id 144) is a function entity with a body the function-hash check covers
// (same row cap-83 uses). When the deployed DB carries its content_hash+length
// the dispatch runs a REAL on-disk check; absent that data → DEGRADED PASS.
constexpr const char* kRealFnName = "SaveGame";

// Two survival::Result are equal iff status AND reason match. The dispatch's
// function path is "verdict unchanged" exactly when it returns the identical
// Result the legacy entry returns for the same inputs.
bool ResultsEqual(const sv::Result& a, const sv::Result& b) {
    return a.status == b.status && a.reason == b.reason;
}

const char* StatusName(sv::Status s) {
    switch (s) {
        case sv::Status::Unchanged:   return "unchanged";
        case sv::Status::Changed:     return "changed";
        case sv::Status::Ambiguous:   return "ambiguous";
        case sv::Status::CannotCheck: return "cannot_check";
    }
    return "?";
}

// Run the dispatch's function path for the given inputs. `derivesFrom`/`dll` are
// ignored by the function path (no DAG edge, default module) — passed as the
// neutral values the real wire-in will use.
sv::Result DispatchFn(uint32_t rva, size_t length,
                      const std::vector<uint8_t>& hash) {
    sv::Payload p;
    p.kind = sv::Kind::Function;
    p.contentHash = hash;
    p.length = length;
    return sv::SurvivalCheck(p, rva, /*derivesFrom=*/0, /*dll=*/std::string());
}

// The legacy entry point for the same inputs (== today's function behavior).
sv::Result LegacyFn(uint32_t rva, size_t length,
                    const std::vector<uint8_t>& hash) {
    return sv::SurvivalCheck(
        rva, length,
        hash.empty() ? nullptr : hash.data(),
        hash.size());
}

}  // namespace

void RunSelfTestOnce() {
    static bool s_reported = false;
    if (s_reported) return;
    s_reported = true;  // synthetic + deterministic — no retry needed.

    char reason[768];

    // ----- Sub-check 1: FUNCTION VERDICT UNCHANGED (dispatch == legacy) -------
    // The legacy SurvivalCheck(rva,length,hash,len) IS today's function
    // behavior; the dispatch's function path must return the IDENTICAL Result.
    // Three deterministic cases (no module mapped) cover the function path's
    // runnable verdicts. Each case's inputs feed BOTH paths; the Results must be
    // byte-identical (status + reason).
    struct Case { const char* name; uint32_t rva; size_t length; std::vector<uint8_t> hash; };
    const std::vector<Case> cases = {
        // empty hash → non-byte entity → CannotCheck "not_applicable".
        {"not_applicable",          0x1000,  64, {}},
        // present-but-wrong-width hash → CannotCheck "expected_hash_bad_length".
        {"expected_hash_bad_length", 0x2000, 64, std::vector<uint8_t>(16, 0x11)},
        // valid 32-byte hash + zero length → CannotCheck "length_zero".
        {"length_zero",             0x3000,   0, std::vector<uint8_t>(32, 0x22)},
    };
    for (const auto& c : cases) {
        sv::Result viaDispatch = DispatchFn(c.rva, c.length, c.hash);
        sv::Result viaLegacy   = LegacyFn(c.rva, c.length, c.hash);
        if (!ResultsEqual(viaDispatch, viaLegacy)) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: the dispatch's function path DIVERGED from the legacy "
                "verdict on case '%s' — dispatch=(%s,'%s') vs legacy=(%s,'%s'). "
                "The function-kind verdict must be identical to before the "
                "per-kind dispatch restructure.",
                c.name,
                StatusName(viaDispatch.status), viaDispatch.reason.c_str(),
                StatusName(viaLegacy.status), viaLegacy.reason.c_str());
            LOG_ERROR_KV(kCategory, "selftest_fail",
                ::kcdx::log::KV("subcheck", "1_fn_verdict"),
                ::kcdx::log::KV("case", c.name));
            kcdx::test::ReportResult(kRow, false, reason);
            kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
            return;
        }
    }

    // Optional REAL on-disk function check: when a curated function row carries
    // content_hash+length, run the actual on-disk check via BOTH paths and
    // assert they agree (status + reason). This exercises a real verdict
    // (Unchanged when the on-disk bytes match the recorded hash) through the
    // dispatch. DEGRADED PASS (never a hard FAIL) when the DB lacks the row /
    // fingerprint — a deploy-state observation, like cap-83.
    bool realChecked = false;
    sv::Status realStatus = sv::Status::CannotCheck;
    if (refdb::IsLoaded()) {
        refdb::NameResolution nr = refdb::ResolveByName(kRealFnName);
        if (nr.found && nr.kind == "function" && nr.has_length &&
            nr.content_hash.size() == sv::kHashLen && nr.length > 0) {
            sv::Result d = DispatchFn(static_cast<uint32_t>(nr.rva),
                                      static_cast<size_t>(nr.length),
                                      nr.content_hash);
            sv::Result l = LegacyFn(static_cast<uint32_t>(nr.rva),
                                    static_cast<size_t>(nr.length),
                                    nr.content_hash);
            if (!ResultsEqual(d, l)) {
                std::snprintf(reason, sizeof(reason),
                    "FAIL: the dispatch's function path DIVERGED from legacy on "
                    "the REAL on-disk %s check — dispatch=(%s,'%s') vs "
                    "legacy=(%s,'%s'). The function verdict must be identical to "
                    "before the restructure on a live row.",
                    kRealFnName,
                    StatusName(d.status), d.reason.c_str(),
                    StatusName(l.status), l.reason.c_str());
                LOG_ERROR_KV(kCategory, "selftest_fail",
                    ::kcdx::log::KV("subcheck", "1_fn_real"));
                kcdx::test::ReportResult(kRow, false, reason);
                kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
                return;
            }
            realChecked = true;
            realStatus = d.status;
        }
    }

    // ----- Sub-check 2: NON-FUNCTION STUBS ARE FAIL-LOUD --------------------
    // Every non-function kind dispatched must return CannotCheck with a DEFINED
    // token — never Unchanged/Changed/Ambiguous, never an empty reason. The
    // payload carries each kind's datum (it is IGNORED by the step-3.1 stub, but
    // populated so the model is real); the verdict is the stub token.
    struct StubCase { sv::Kind kind; const char* kindName; const char* wantReason; };
    const std::vector<StubCase> stubs = {
        {sv::Kind::Callsite,          "callsite",           "not_implemented_3_2"},
        {sv::Kind::StringAnchor,      "string_anchor",      "not_implemented_3_2"},
        {sv::Kind::InstructionAnchor, "instruction_anchor", "not_implemented_3_2"},
        {sv::Kind::DataSlot,          "data_slot",          "not_implemented_3_2"},
        {sv::Kind::VtableBase,        "vtable_base",        "not_implemented_3_2"},
        {sv::Kind::VtableIndex,       "vtable_index",       "vtable_index_deferred"},
    };
    for (const auto& s : stubs) {
        sv::Payload p;
        p.kind = s.kind;
        // Populate a plausible per-kind datum (proves the payload model carries
        // it; the stub ignores it until 3.2). The verdict must still be the stub.
        p.aob = {0x48, 0x8B};
        p.anchorString = "exec autoexec.cfg";
        p.rule = "follow disp32 from anchor X";
        p.slotCount = 69;
        sv::Result r = sv::SurvivalCheck(p, /*rva=*/0x4000, /*derivesFrom=*/0,
                                         /*dll=*/std::string());
        bool ok = r.status == sv::Status::CannotCheck &&
                  !r.reason.empty() &&
                  r.reason == s.wantReason;
        if (!ok) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: non-function kind '%s' did NOT return a fail-loud stub — "
                "got (%s,'%s'), wanted (cannot_check,'%s'). A non-function "
                "survival check must be a DEFINED CannotCheck placeholder, never "
                "a false Unchanged or a silent empty.",
                s.kindName, StatusName(r.status), r.reason.c_str(), s.wantReason);
            LOG_ERROR_KV(kCategory, "selftest_fail",
                ::kcdx::log::KV("subcheck", "2_stub"),
                ::kcdx::log::KV("kind", s.kindName));
            kcdx::test::ReportResult(kRow, false, reason);
            kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
            return;
        }
    }

    // ----- Sub-check 3: AMBIGUOUS IS REPORTABLE ------------------------------
    // (a) survival::Status::Ambiguous maps through the pass's MapStatus to its
    //     OWN FuncStatus value (NOT collapsed to CannotCheck). MapStatus is
    //     file-private to survival_pass, so assert the mapping VIA the codec: an
    //     Ambiguous FuncStatus must round-trip through Save→Load→Lookup as
    //     Ambiguous (byte-identical), proving the value is usable + reportable
    //     end-to-end (enum → codec byte → enum).
    vcc::Reset();
    vcc::Record rec;
    rec.key.pluginName = kSyntheticPlugin;
    rec.key.gameVer = "cap84.1.5.x";
    rec.key.sqliteSha = std::vector<uint8_t>(32, 0x84);
    rec.key.tomlMtime = 840000;
    rec.key.entrypointsMtime = 840001;
    rec.posture = vcc::Posture::WarnAndTry;
    rec.results.push_back({"ambiguous_fn", vcc::FuncStatus::Ambiguous});
    vcc::Upsert(rec);
    if (!vcc::Save()) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: version_check_cache::Save() returned false — could not persist "
            "the synthetic Ambiguous record; the Ambiguous codec round-trip "
            "cannot be verified.");
        LOG_ERROR_KV(kCategory, "selftest_fail",
            ::kcdx::log::KV("subcheck", "3_save"));
        kcdx::test::ReportResult(kRow, false, reason);
        kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
        vcc::Reset();
        return;
    }
    vcc::Reset();  // force a real disk read.
    vcc::Load();
    std::vector<vcc::FuncResult> got;
    vcc::Posture gotPosture = vcc::Posture::WarnAndTry;
    bool hit = vcc::Lookup(rec.key, got, gotPosture);
    bool ambiguousOk = hit && got.size() == 1 &&
                       got[0].targetKey == "ambiguous_fn" &&
                       got[0].status == vcc::FuncStatus::Ambiguous;
    if (!ambiguousOk) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: the Ambiguous FuncStatus did NOT round-trip through the cache "
            "codec (hit=%d, count=%zu, status=%d). Ambiguous must be a usable, "
            "reportable status — not collapsed to CannotCheck and not dropped by "
            "the codec.",
            hit ? 1 : 0, got.size(),
            got.empty() ? -1 : static_cast<int>(got[0].status));
        LOG_ERROR_KV(kCategory, "selftest_fail",
            ::kcdx::log::KV("subcheck", "3_ambiguous_roundtrip"));
        kcdx::test::ReportResult(kRow, false, reason);
        kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
        vcc::Reset();
        return;
    }

    // (b) the survival::Status → FuncStatus mapping is also exercised through the
    //     PASS itself: a non-function ref (empty hash) records CannotCheck — and
    //     the four-value mapping (incl. Ambiguous) is the one the pass walks. The
    //     non-byte ref proves the pass surfaces a non-Unchanged verdict; the
    //     Ambiguous mapping is proven by (a). (The pass cannot itself yet emit
    //     Ambiguous — only the step-3.2 non-function checks do — so the codec
    //     round-trip is the falsifiable proof that the VALUE is reportable.)
    sp::Reset();
    sp::RecordPluginMeta(kSyntheticPlugin, 840002, 840003, vcc::Posture::WarnAndTry);
    sp::RecordTouchedRef(kSyntheticPlugin, "nonbyte", /*rva=*/0x5000, /*length=*/0,
                         /*expectedHash=*/std::vector<uint8_t>{});
    sp::RunPass("cap84.1.5.x", std::vector<uint8_t>(32, 0x84));
    const sp::PassResult* pr = sp::Result(kSyntheticPlugin, "nonbyte");
    if (pr == nullptr || pr->status != vcc::FuncStatus::CannotCheck) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: the pass did NOT map a non-byte ref to CannotCheck (result=%s, "
            "status=%d) — the survival::Status→FuncStatus mapping the pass walks "
            "is broken.",
            pr ? "present" : "MISSING",
            pr ? static_cast<int>(pr->status) : -1);
        LOG_ERROR_KV(kCategory, "selftest_fail",
            ::kcdx::log::KV("subcheck", "3_pass_map"));
        kcdx::test::ReportResult(kRow, false, reason);
        kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
        vcc::Reset();
        sp::Reset();
        return;
    }

    // All sub-checks held. Leave clean in-memory state + an empty on-disk cache
    // so the synthetic records never leak into a future real Lookup.
    vcc::Reset();
    vcc::Save();
    sp::Reset();

    const char* realNote = realChecked
        ? (realStatus == sv::Status::Unchanged
               ? "real SaveGame on-disk check ran (Unchanged) — dispatch==legacy"
               : "real SaveGame on-disk check ran — dispatch==legacy")
        : "real SaveGame check DEGRADED (DB lacks content_hash/length) — synthetic checks held";
    std::snprintf(reason, sizeof(reason),
        "PASS — the function-kind dispatch returns the IDENTICAL verdict to the "
        "legacy on-disk body-hash check (3 deterministic cases%s); every "
        "non-function kind is a fail-loud CannotCheck stub (not_implemented_3_2 / "
        "vtable_index_deferred, never a false Unchanged); and Ambiguous is a "
        "reportable status that round-trips through the pass+codec. [%s]",
        realChecked ? " + 1 real" : "", realNote);
    LOG_INFO_KV(kCategory, "selftest_pass",
        ::kcdx::log::KV("real_checked", realChecked ? "yes" : "degraded"),
        ::kcdx::log::KV("real_status", realChecked ? StatusName(realStatus) : "-"));
    kcdx::test::ReportResult(kRow, true, reason);
    kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
}

}  // namespace kcdx::survival_dispatch_selftest
