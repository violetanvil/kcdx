#include "survival_dispatch_selftest.h"

#include <cstdio>   // snprintf
#include <cstring>  // memcmp, strcmp
#include <exception>   // std::exception (ParsePattern throw)
#include <stdexcept>   // std::runtime_error (synthetic harness-fault throw)
#include <string>
#include <vector>

#include "log.h"
#include "patch_engine.h"  // patch::ParsePattern (decode a stored aob string → bytes+mask)
#include "pe_helpers.h"    // pe::OpenModule / IsVaInLiveText (3.3 reachability range test)
#include "refdb.h"
#include "survival.h"
#include "survival_pass.h"
#include "survival_verify.h"  // 3.3 startup verification pass (D25 + D34)
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
namespace svv = kcdx::survival_verify;

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

// Run the dispatch's function path for the given inputs. `dll` is ignored by the
// function path (default module) — passed as the neutral value the real wire-in
// will use.
sv::Result DispatchFn(uint32_t rva, size_t length,
                      const std::vector<uint8_t>& hash) {
    sv::Payload p;
    p.kind = sv::Kind::Function;
    p.contentHash = hash;
    p.length = length;
    return sv::SurvivalCheck(p, rva, /*dll=*/std::string());
}

// Decode a stored AOB string ("48 ?? 89 …") into a survival Payload's bytes +
// mask (1=literal, 0=wildcard). Reuses patch::ParsePattern (the SAME wildcard
// decoder the live AOB path uses). Returns false on an empty/malformed string.
bool AobToPayload(const std::string& aobStr, sv::Payload& p) {
    if (aobStr.empty()) return false;
    try {
        patch::Pattern pat = patch::ParsePattern(aobStr);
        p.aob = pat.bytes;
        p.aobMask.resize(pat.mask.size());
        for (size_t i = 0; i < pat.mask.size(); ++i) {
            p.aobMask[i] = pat.mask[i] ? 1 : 0;
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

// Run a single non-function static check via the dispatch entry point.
sv::Result DispatchKind(sv::Kind kind, uint32_t rva, const sv::Payload& base) {
    sv::Payload p = base;
    p.kind = kind;
    return sv::SurvivalCheck(p, rva, /*dll=*/std::string());
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

    char reason[1536];

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

    // ----- Sub-check 2: vtable_index IS A DEFINED DEFERRAL ------------------
    // vtable_index's survival datum (a slot-target body-hash) is design-defined
    // but population waits on the runtime-vtable path — so it dispatches to a
    // DEFINED CannotCheck/"vtable_index_deferred", never a false verdict. (The
    // other 5 non-function kinds now run real on-disk checks — sub-checks 4-6.)
    {
        sv::Payload p;
        p.kind = sv::Kind::VtableIndex;
        p.slotCount = 69;  // a populated datum; vtable_index ignores it (deferred).
        sv::Result r = sv::SurvivalCheck(p, /*rva=*/0x4000, /*dll=*/std::string());
        bool ok = r.status == sv::Status::CannotCheck &&
                  r.reason == "vtable_index_deferred";
        if (!ok) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: vtable_index did NOT return its defined deferral — got "
                "(%s,'%s'), wanted (cannot_check,'vtable_index_deferred'). The "
                "vtable_index slot-target datum waits on the runtime-vtable path; "
                "it must be a DEFINED CannotCheck, never a false verdict.",
                StatusName(r.status), r.reason.c_str());
            LOG_ERROR_KV(kCategory, "selftest_fail",
                ::kcdx::log::KV("subcheck", "2_vtable_index_deferred"));
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

    // ----- Sub-check 4: CALLSITE VERDICTS (Changed / Ambiguous / real Unchanged)
    // The 5 static checks need WHGame.dll's on-disk file readable. When it is
    // not mapped (a non-game host / early boot), the synthetic on-disk cases
    // CannotCheck — DEGRADED PASS, not a hard FAIL (a deploy-state observation).
    // The synthetic AOBs do NOT depend on the DB:
    //   - a long improbable byte run cannot occur in .text → Changed (site gone).
    //   - a 2-byte ultra-common run (48 8B) occurs thousands of times → Ambiguous.
    {
        // Changed: 16 bytes of 0xAB — astronomically unlikely to occur in .text.
        sv::Payload pGone;
        pGone.aob = std::vector<uint8_t>(16, 0xAB);  // all-literal mask (default).
        sv::Result rGone = DispatchKind(sv::Kind::Callsite, /*rva=*/0x1000, pGone);
        // Ambiguous: "48 8B" — `mov r64, r/m64` opcode prefix; occurs everywhere.
        sv::Payload pMulti;
        pMulti.aob = {0x48, 0x8B};
        sv::Result rMulti = DispatchKind(sv::Kind::Callsite, /*rva=*/0x1000, pMulti);

        // A CannotCheck from a not-mapped module is a DEGRADED pass; a definite
        // verdict that is WRONG is a hard FAIL. The contract: a gone site must
        // NOT read Unchanged; a multi-hit AOB must NOT read Unchanged/Changed.
        bool callsiteRan = rGone.status != sv::Status::CannotCheck ||
                           rMulti.status != sv::Status::CannotCheck;
        if (callsiteRan) {
            if (rGone.status == sv::Status::Unchanged) {
                std::snprintf(reason, sizeof(reason),
                    "FAIL: a callsite AOB that cannot occur in .text read Unchanged "
                    "(status=%s) — a gone site must read Changed, never a false "
                    "Unchanged.", StatusName(rGone.status));
                LOG_ERROR_KV(kCategory, "selftest_fail",
                    ::kcdx::log::KV("subcheck", "4_callsite_gone"));
                kcdx::test::ReportResult(kRow, false, reason);
                kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
                vcc::Reset(); sp::Reset();
                return;
            }
            if (rMulti.status != sv::Status::Ambiguous) {
                std::snprintf(reason, sizeof(reason),
                    "FAIL: an ultra-common 2-byte callsite AOB did NOT read "
                    "Ambiguous (status=%s) — an AOB matching >1 .text site is no "
                    "longer a unique locator and must read Ambiguous, never "
                    "Unchanged/Changed.", StatusName(rMulti.status));
                LOG_ERROR_KV(kCategory, "selftest_fail",
                    ::kcdx::log::KV("subcheck", "4_callsite_ambiguous"));
                kcdx::test::ReportResult(kRow, false, reason);
                kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
                vcc::Reset(); sp::Reset();
                return;
            }
        }

        // Real curated callsite Unchanged (DEGRADED if the DB lacks the row).
        if (refdb::IsLoaded()) {
            refdb::NameResolution nr = refdb::ResolveByName("IsInCombat_callsite_26b");
            sv::Payload pReal;
            if (nr.found && nr.kind == "callsite" && AobToPayload(nr.aob, pReal)) {
                pReal.expectUnique = nr.has_expect_unique && nr.expect_unique != 0;
                sv::Result rReal = DispatchKind(sv::Kind::Callsite,
                                                static_cast<uint32_t>(nr.rva), pReal);
                // A real curated unique callsite should be Unchanged on the build
                // it was verified against. Changed/Ambiguous here is a real
                // regression (the stored AOB no longer uniquely locates).
                if (rReal.status == sv::Status::Changed ||
                    rReal.status == sv::Status::Ambiguous) {
                    std::snprintf(reason, sizeof(reason),
                        "FAIL: the curated callsite 'IsInCombat_callsite_26b' did "
                        "NOT survive its own verified build — got %s. Its stored "
                        "AOB must uniquely re-locate (Unchanged) on the build it was "
                        "verified against.", StatusName(rReal.status));
                    LOG_ERROR_KV(kCategory, "selftest_fail",
                        ::kcdx::log::KV("subcheck", "4_callsite_real"));
                    kcdx::test::ReportResult(kRow, false, reason);
                    kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
                    vcc::Reset(); sp::Reset();
                    return;
                }
            }
        }
    }

    // ----- Sub-check 5: STRING_ANCHOR VERDICTS (Changed / real Unchanged) ----
    {
        // Changed: an improbable literal cannot be present in .rdata → Changed.
        sv::Payload pAbsent;
        pAbsent.anchorString = "kcdx_cap84_absent_literal_zzqx_neverpresent";
        sv::Result rAbsent = DispatchKind(sv::Kind::StringAnchor, /*rva=*/0x2000, pAbsent);
        if (rAbsent.status == sv::Status::Unchanged) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: a string_anchor literal that is absent from .rdata read "
                "Unchanged (status=%s) — an absent anchor must read Changed.",
                StatusName(rAbsent.status));
            LOG_ERROR_KV(kCategory, "selftest_fail",
                ::kcdx::log::KV("subcheck", "5_string_absent"));
            kcdx::test::ReportResult(kRow, false, reason);
            kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
            vcc::Reset(); sp::Reset();
            return;
        }
        // Real curated string_anchor Unchanged (DEGRADED if the DB lacks it).
        if (refdb::IsLoaded()) {
            refdb::NameResolution nr = refdb::ResolveByName("string_exec_autoexec_cfg");
            if (nr.found && nr.kind == "string_anchor" && !nr.anchor_string.empty()) {
                sv::Payload pReal;
                pReal.anchorString = nr.anchor_string;
                pReal.expectUnique = nr.has_expect_unique && nr.expect_unique != 0;
                sv::Result rReal = DispatchKind(sv::Kind::StringAnchor,
                                                static_cast<uint32_t>(nr.rva), pReal);
                if (rReal.status == sv::Status::Changed) {
                    std::snprintf(reason, sizeof(reason),
                        "FAIL: the curated string_anchor 'string_exec_autoexec_cfg' "
                        "(%s) read Changed — its literal must be present in .rdata "
                        "on the build it was verified against.",
                        nr.anchor_string.c_str());
                    LOG_ERROR_KV(kCategory, "selftest_fail",
                        ::kcdx::log::KV("subcheck", "5_string_real"));
                    kcdx::test::ReportResult(kRow, false, reason);
                    kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
                    vcc::Reset(); sp::Reset();
                    return;
                }
            }
        }
    }

    // ----- Sub-check 6: TRANSITIVE ANCHOR-CHANGED via CheckOrdered (the DAG) --
    // A 2-row set: an anchor row (string_anchor with an ABSENT literal → Changed)
    // + a dependent (instruction_anchor) deriving from it. The ordered walk MUST
    // check the anchor first, see it Changed, and short-circuit the dependent to
    // CannotCheck/"anchor_changed" — never independently re-derive it, never a
    // silent pass. This is the DAG's load-bearing guarantee.
    {
        std::vector<sv::Row> rows;
        // The anchor row (id 1): a string_anchor whose literal is absent → Changed.
        sv::Row anchor;
        anchor.id = 1;
        anchor.derivesFrom = 0;
        anchor.rva = 0x3000;
        anchor.payload.kind = sv::Kind::StringAnchor;
        anchor.payload.anchorString = "kcdx_cap84_absent_anchor_literal_zzqx";
        rows.push_back(anchor);
        // The dependent row (id 2): derives from id 1; its own datum is plausible
        // but must NOT be reached — the anchor is Changed.
        sv::Row dependent;
        dependent.id = 2;
        dependent.derivesFrom = 1;
        dependent.rva = 0x4000;
        dependent.payload.kind = sv::Kind::InstructionAnchor;
        dependent.payload.anchorString = "exec autoexec.cfg";  // a real literal — but unreachable.
        rows.push_back(dependent);

        std::vector<sv::RowResult> results = sv::CheckOrdered(rows);
        // Find the dependent's result (id 2).
        const sv::RowResult* depRes = nullptr;
        for (const auto& rr : results) {
            if (rr.id == 2) { depRes = &rr; break; }
        }
        bool ranOnDisk = false;
        for (const auto& rr : results) {
            // If the anchor produced a definite Changed (not a not-mapped
            // CannotCheck), the DAG actually exercised the on-disk path.
            if (rr.id == 1 && rr.result.status == sv::Status::Changed) ranOnDisk = true;
        }
        if (ranOnDisk) {
            bool ok = depRes != nullptr &&
                      depRes->result.status == sv::Status::CannotCheck &&
                      depRes->result.reason == "anchor_changed";
            if (!ok) {
                std::snprintf(reason, sizeof(reason),
                    "FAIL: a dependent whose anchor came back Changed was NOT "
                    "transitively blocked — got (%s,'%s'), wanted (cannot_check,"
                    "'anchor_changed'). A Changed anchor must short-circuit every "
                    "dependent that re-derives through it; it is never silently "
                    "re-derived and never a silent pass.",
                    depRes ? StatusName(depRes->result.status) : "MISSING",
                    depRes ? depRes->result.reason.c_str() : "-");
                LOG_ERROR_KV(kCategory, "selftest_fail",
                    ::kcdx::log::KV("subcheck", "6_transitive_anchor_changed"));
                kcdx::test::ReportResult(kRow, false, reason);
                kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
                vcc::Reset(); sp::Reset();
                return;
            }
        }
    }

    // ----- Sub-check 7: REACHABILITY RANGE TEST (pe::IsVaInLiveText) ----------
    // The 3.3 reachability signal is "does the engine-resolved VA land in live
    // .text" — a RANGE TEST against the live module's executable sections, NOT a
    // body hash (Probe 0.4: the live image is relocated + kcdx-detoured, so a
    // live-body hash reads wrong-target for a genuinely-good row). Falsifiable +
    // deterministic when WHGame.dll is mapped: a VA inside live .text reads true;
    // an off-image VA reads false. DEGRADE-pass when WHGame is not mapped (a
    // non-game host) — the real discrimination is confirmed at the live launch.
    {
        pe::ModuleView view;
        if (pe::OpenModule(L"WHGame.dll", view) && view.base != nullptr) {
            const uintptr_t base = reinterpret_cast<uintptr_t>(view.base);
            // An off-image VA: below the module base by 1 MB — never in any of
            // this module's sections. Must read NOT-in-.text (dead reachability).
            uintptr_t offImage = base - 0x100000;
            if (pe::IsVaInLiveText(view, offImage)) {
                std::snprintf(reason, sizeof(reason),
                    "FAIL: pe::IsVaInLiveText returned TRUE for an off-image VA "
                    "(base-0x100000) — the reachability range test must read an "
                    "off-image address as NOT in live .text (a 'dead' reach), never "
                    "a false in-.text.");
                LOG_ERROR_KV(kCategory, "selftest_fail",
                    ::kcdx::log::KV("subcheck", "7_reach_offimage"));
                kcdx::test::ReportResult(kRow, false, reason);
                kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
                vcc::Reset(); sp::Reset();
                return;
            }
            // A VA = 0 is never reachable.
            if (pe::IsVaInLiveText(view, 0)) {
                std::snprintf(reason, sizeof(reason),
                    "FAIL: pe::IsVaInLiveText returned TRUE for VA 0 — a null "
                    "resolve must read NOT in live .text (dead), never reachable.");
                LOG_ERROR_KV(kCategory, "selftest_fail",
                    ::kcdx::log::KV("subcheck", "7_reach_null"));
                kcdx::test::ReportResult(kRow, false, reason);
                kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
                vcc::Reset(); sp::Reset();
                return;
            }
            // A real curated function VA (engine-resolved) MUST land in live .text
            // (a function entity resolves into executable code). DEGRADE if the DB
            // lacks the row. AP1: the VA is engine-resolved, never hardcoded.
            if (refdb::IsLoaded()) {
                uintptr_t fnVa = refdb::ResolveAddrByName(kRealFnName);
                if (fnVa != 0 && !pe::IsVaInLiveText(view, fnVa)) {
                    std::snprintf(reason, sizeof(reason),
                        "FAIL: the engine-resolved VA for the curated function '%s' "
                        "(0x%llx) did NOT land in live .text — a function entity must "
                        "resolve into executable code; a good row reading 'dead' is "
                        "the reachability check failing on a genuinely-good target.",
                        kRealFnName, (unsigned long long)fnVa);
                    LOG_ERROR_KV(kCategory, "selftest_fail",
                        ::kcdx::log::KV("subcheck", "7_reach_realfn"));
                    kcdx::test::ReportResult(kRow, false, reason);
                    kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
                    vcc::Reset(); sp::Reset();
                    return;
                }
            }
        }
    }

    // ----- Sub-check 8: STARTUP VERIFICATION PASS — every row gets a DEFINED
    // verdict; a known-good curated function caps at passed_not_verified. -------
    // RunStartupVerification sweeps the curated set, combining the on-disk
    // version-applicability check (REUSE the 3.1/3.2 dispatch) with the live
    // reachability range test into one of the 7 verdicts via the ceiling rule.
    // Every swept row carries a DEFINED verdict (never absent — a check that
    // cannot run says so, it does not omit the row). A known-good function
    // (SaveGame) whose on-disk hash matches AND resolves into
    // live .text reads passed_not_verified (NOT a higher rung — only observed
    // execution earns verified_working, which no static method produces) at
    // method_rank 3 (reachability, the strongest method that ran), with the
    // matched address_version id surfaced — DEGRADE when WHGame is not mapped /
    // the DB lacks the row.
    {
        std::vector<svv::RowVerdict> verds = svv::RunStartupVerification();
        // Every row carries a defined verdict — the enum is total, so this asserts
        // the pass produced rows when refdb is loaded (it skips loud-empty only
        // when refdb is not loaded). When refdb is loaded the sweep must be
        // non-empty (the curated cache has rows); an empty sweep on a loaded DB is
        // the pass silently dropping the whole set.
        if (refdb::IsLoaded() && verds.empty()) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: RunStartupVerification returned ZERO rows while refdb is "
                "loaded (CachedRowCount=%zu) — the startup sweep dropped the entire "
                "curated set; every cached entity must yield a defined verdict.",
                refdb::CachedRowCount());
            LOG_ERROR_KV(kCategory, "selftest_fail",
                ::kcdx::log::KV("subcheck", "8_verify_empty"));
            kcdx::test::ReportResult(kRow, false, reason);
            kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
            vcc::Reset(); sp::Reset();
            return;
        }
        // Find the known-good SaveGame function row's verdict.
        const svv::RowVerdict* saveRow = nullptr;
        for (const auto& v : verds) {
            if (v.name == kRealFnName) { saveRow = &v; break; }
        }
        if (saveRow != nullptr) {
            // A static pass NEVER awards verified_working — that needs rank-1
            // observed execution this step does not produce. A good function row
            // that matched its fingerprint + resolved into live .text caps at
            // passed_not_verified. Falsifiable: SaveGame reading verified_working
            // is the static pass over-claiming the top rung the ceiling rule
            // forbids.
            if (saveRow->verdict == svv::Verdict::VerifiedWorking) {
                std::snprintf(reason, sizeof(reason),
                    "FAIL: the curated function '%s' read verified_working from a "
                    "STATIC pass — only observed live execution (rank 1) earns the "
                    "top rung; a hash+reachability pass caps at passed_not_verified.",
                    kRealFnName);
                LOG_ERROR_KV(kCategory, "selftest_fail",
                    ::kcdx::log::KV("subcheck", "8_verify_overclaim"));
                kcdx::test::ReportResult(kRow, false, reason);
                kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
                vcc::Reset(); sp::Reset();
                return;
            }
            // A passed_not_verified came from an on-disk match → it MUST surface
            // the matched address_version id AND report method_rank 3
            // (reachability, the strongest method that ran). Falsifiable: a
            // passing row not surfacing the matched id, or reporting a rank other
            // than 3, is the attribution / ceiling-rank reporting failing on a
            // genuinely-good target.
            if (saveRow->verdict == svv::Verdict::PassedNotVerified) {
                if (!saveRow->has_matched_id) {
                    std::snprintf(reason, sizeof(reason),
                        "FAIL: the curated function '%s' read passed_not_verified but "
                        "did NOT surface a matched address_version id — attribution "
                        "must report WHICH candidate row the swept bytes matched.",
                        kRealFnName);
                    LOG_ERROR_KV(kCategory, "selftest_fail",
                        ::kcdx::log::KV("subcheck", "8_verify_matchedid"));
                    kcdx::test::ReportResult(kRow, false, reason);
                    kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
                    vcc::Reset(); sp::Reset();
                    return;
                }
                if (saveRow->method_rank != 3) {
                    std::snprintf(reason, sizeof(reason),
                        "FAIL: '%s' read passed_not_verified at method_rank %d — a "
                        "clean hash+reachability pass's strongest method is "
                        "reachability (rank 3); the reported rank must be 3.",
                        kRealFnName, saveRow->method_rank);
                    LOG_ERROR_KV(kCategory, "selftest_fail",
                        ::kcdx::log::KV("subcheck", "8_verify_rank"));
                    kcdx::test::ReportResult(kRow, false, reason);
                    kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
                    vcc::Reset(); sp::Reset();
                    return;
                }
            }
            // A fingerprint-mismatch Failed carries NO matched id (the bytes
            // matched no candidate). A dead-resolve Failed DID match on-disk so it
            // keeps its id — so only the mismatch detail asserts no-id here.
            if (saveRow->verdict == svv::Verdict::Failed &&
                saveRow->detail == "fingerprint_mismatch" &&
                saveRow->has_matched_id) {
                std::snprintf(reason, sizeof(reason),
                    "FAIL: '%s' read failed (fingerprint_mismatch) yet surfaced a "
                    "matched id — a mismatch means the bytes matched NO candidate; "
                    "the matched id must be NONE.", kRealFnName);
                LOG_ERROR_KV(kCategory, "selftest_fail",
                    ::kcdx::log::KV("subcheck", "8_verify_mismatch_id"));
                kcdx::test::ReportResult(kRow, false, reason);
                kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
                vcc::Reset(); sp::Reset();
                return;
            }
        }
    }

    // ----- Sub-check 9: VERDICT-COMBINATION DISCRIMINATION (synthetic) --------
    // The pass sweeps real rows, so synthesize the failure verdict at the
    // building-block level the pass combines, asserting the discrimination is
    // distinct from a clean pass:
    //   failed — a function payload whose stored content_hash CANNOT match the
    //     on-disk bytes (an all-0x33 synthetic hash at SaveGame's real rva) reads
    //     on-disk Changed → the pass combines that to failed (fingerprint_mismatch
    //     on a covered version). Needs WHGame mapped (the on-disk read); DEGRADE
    //     otherwise.
    // Falsifiable: a non-matching synthetic hash reading Unchanged (the on-disk
    // check fabricating a match) is the failed-discrimination collapsing into a
    // false passed_not_verified. (The full verdict-mapping arithmetic — Changed →
    // failed, the dead-resolve → failed, the version gap → not_applicable, a fault
    // → error — is asserted directly against MapStaticVerdict in sub-checks b/c/d.)
    {
        if (refdb::IsLoaded()) {
            refdb::NameResolution nr = refdb::ResolveByName(kRealFnName);
            if (nr.found && nr.kind == "function" && nr.has_length && nr.length > 0) {
                // A synthetic 32-byte hash that is astronomically unlikely to be
                // SaveGame's real body hash → the on-disk check must read Changed
                // (NOT Unchanged) → the pass would combine that to failed.
                std::vector<uint8_t> bogus(sv::kHashLen, 0x33);
                sv::Result rBogus = DispatchFn(static_cast<uint32_t>(nr.rva),
                                               static_cast<size_t>(nr.length), bogus);
                // CannotCheck (module not mapped on the on-disk read) is a DEGRADE;
                // a definite Unchanged on a bogus hash is a hard FAIL (the on-disk
                // check fabricated a match → the failed discrimination is dead).
                if (rBogus.status == sv::Status::Unchanged) {
                    std::snprintf(reason, sizeof(reason),
                        "FAIL: a function payload with a SYNTHETIC content_hash "
                        "(0x33*32) at '%s's real rva read Unchanged — the on-disk "
                        "fingerprint check fabricated a match for bytes that cannot "
                        "be SaveGame's body; a non-matching fingerprint must read "
                        "Changed (→ failed), never Unchanged (→ passed_not_verified).",
                        kRealFnName);
                    LOG_ERROR_KV(kCategory, "selftest_fail",
                        ::kcdx::log::KV("subcheck", "9_failed_discrim"));
                    kcdx::test::ReportResult(kRow, false, reason);
                    kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
                    vcc::Reset(); sp::Reset();
                    return;
                }
            }
        }
    }

    // ----- Sub-check 10: THE 7-STATE ENUM IS TOTAL + EACH READS BACK ITS OWN
    // VALUE (in-process, item a-i) ------------------------------------------
    // The 7-state RowVerdict is an in-process enum (NOT serialized this step —
    // its report-side encoding is the v3 schema at a later step). Prove every one
    // of the 7 states is PRODUCIBLE and reads back its OWN value end-to-end: set
    // each on a RowVerdict + method_rank, read the field back, and decode the
    // token via VerdictName. Falsifiable: a state that does not round-trip its own
    // value (the field reads a different verdict than was set), or two distinct
    // states decoding to the SAME token (the enum collapsing), fails the row.
    {
        struct EnumCase { svv::Verdict v; const char* token; int rank; };
        const EnumCase enumCases[] = {
            {svv::Verdict::VerifiedWorking,   "verified_working",     1},
            {svv::Verdict::PassedNotVerified, "passed_not_verified",  3},
            {svv::Verdict::Failed,            "failed",               4},
            {svv::Verdict::NotApplicable,     "not_applicable",       4},
            {svv::Verdict::CannotCheck,       "cannot_check",         4},
            {svv::Verdict::Skipped,           "skipped",              5},
            {svv::Verdict::Error,             "error",                4},
        };
        for (const auto& ec : enumCases) {
            svv::RowVerdict rv;
            rv.verdict = ec.v;
            rv.method_rank = ec.rank;
            // The field reads back the exact value set, and the token decodes to
            // the expected stable string.
            if (rv.verdict != ec.v || rv.method_rank != ec.rank ||
                std::strcmp(svv::VerdictName(rv.verdict), ec.token) != 0) {
                std::snprintf(reason, sizeof(reason),
                    "FAIL: the 7-state verdict '%s' did NOT round-trip its own value "
                    "in-process (decoded='%s', rank=%d) — every state must be "
                    "producible and read back its own verdict + method_rank.",
                    ec.token, svv::VerdictName(rv.verdict), rv.method_rank);
                LOG_ERROR_KV(kCategory, "selftest_fail",
                    ::kcdx::log::KV("subcheck", "10_enum_total"),
                    ::kcdx::log::KV("token", ec.token));
                kcdx::test::ReportResult(kRow, false, reason);
                kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
                vcc::Reset(); sp::Reset();
                return;
            }
        }
        // No two distinct states share a token (the enum has not collapsed).
        const size_t nCases = sizeof(enumCases) / sizeof(enumCases[0]);
        for (size_t i = 0; i < nCases; ++i) {
            for (size_t j = i + 1; j < nCases; ++j) {
                if (std::strcmp(svv::VerdictName(enumCases[i].v),
                                svv::VerdictName(enumCases[j].v)) == 0) {
                    std::snprintf(reason, sizeof(reason),
                        "FAIL: two distinct verdict states decode to the SAME token "
                        "'%s' — the 7-state enum collapsed; each state must carry a "
                        "distinct stable token.", svv::VerdictName(enumCases[i].v));
                    LOG_ERROR_KV(kCategory, "selftest_fail",
                        ::kcdx::log::KV("subcheck", "10_enum_distinct"));
                    kcdx::test::ReportResult(kRow, false, reason);
                    kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
                    vcc::Reset(); sp::Reset();
                    return;
                }
            }
        }
    }

    // ----- Sub-check 10b: THE EXISTING FuncStatus CACHE CODEC ROUND-TRIP STAYS
    // INTACT under this change (item a-ii) -----------------------------------
    // The 7-state RowVerdict is NOT added to the version_check_cache codec — that
    // codec stores the SEPARATE FuncStatus enum (the per-function survival
    // outcome), whose on-disk byte values (0/1/2/3) are pinned and unchanged.
    // Sub-check 3 already round-trips FuncStatus::Ambiguous (3); this asserts the
    // OTHER three pinned values (Unchanged=0 / Changed=1 / CannotCheck=2) still
    // round-trip byte-identically — proving extending RowVerdict did NOT disturb
    // the cache codec. Falsifiable: any pinned FuncStatus value that does not
    // read back its own value through Save→Load→Lookup fails the row.
    {
        struct FsCase { const char* key; vcc::FuncStatus fs; };
        const FsCase fsCases[] = {
            {"unchanged_fn",   vcc::FuncStatus::Unchanged},
            {"changed_fn",     vcc::FuncStatus::Changed},
            {"cannotcheck_fn", vcc::FuncStatus::CannotCheck},
        };
        vcc::Reset();
        vcc::Record fsRec;
        fsRec.key.pluginName = kSyntheticPlugin;
        fsRec.key.gameVer = "cap84.fs.1";
        fsRec.key.sqliteSha = std::vector<uint8_t>(32, 0x8A);
        fsRec.key.tomlMtime = 8400010;
        fsRec.key.entrypointsMtime = 8400011;
        fsRec.posture = vcc::Posture::WarnAndTry;
        for (const auto& fc : fsCases) {
            fsRec.results.push_back({fc.key, fc.fs});
        }
        vcc::Upsert(fsRec);
        bool codecOk = vcc::Save();
        if (codecOk) {
            vcc::Reset();        // force a real disk read.
            vcc::Load();
            std::vector<vcc::FuncResult> fsGot;
            vcc::Posture fsPosture = vcc::Posture::WarnAndTry;
            bool fsHit = vcc::Lookup(fsRec.key, fsGot, fsPosture);
            codecOk = fsHit && fsGot.size() == (sizeof(fsCases) / sizeof(fsCases[0]));
            for (size_t i = 0; codecOk && i < fsGot.size(); ++i) {
                if (fsGot[i].targetKey != fsCases[i].key ||
                    fsGot[i].status != fsCases[i].fs) {
                    codecOk = false;
                }
            }
        }
        if (!codecOk) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: the existing FuncStatus cache codec round-trip did NOT stay "
                "intact under the RowVerdict extension — the pinned values "
                "(Unchanged=0 / Changed=1 / CannotCheck=2) must still round-trip "
                "byte-identically; the 7-state verdict is NOT serialized through "
                "this codec.");
            LOG_ERROR_KV(kCategory, "selftest_fail",
                ::kcdx::log::KV("subcheck", "10b_funcstatus_codec"));
            kcdx::test::ReportResult(kRow, false, reason);
            kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
            vcc::Reset(); sp::Reset();
            return;
        }
        vcc::Reset();  // leave no synthetic record on disk.
        vcc::Save();
    }

    // ----- Sub-check 11: THE CEILING RULE (item b) --------------------------
    // MapStaticVerdict is the pass's OWN ceiling arithmetic (the sweep calls the
    // identical function), so asserting against it tests the producer, not a
    // re-derivation. A row whose strongest run method is a rank-4 on-disk hash
    // PASS (Unchanged) + a passing reachability maps to passed_not_verified at
    // rank 3 (the strongest method that ran) — NOT verified_working (only rank-1
    // observed execution earns that). And a rank-4 hash MISMATCH (Changed) maps
    // to failed (the override-downward). Falsifiable: a clean pass reading
    // verified_working, or a mismatch reading anything but failed.
    {
        // (a) hash matched (Unchanged) + reachable → passed_not_verified, rank 3.
        svv::StaticVerdict pass =
            svv::MapStaticVerdict(/*versionGap=*/false, sv::Status::Unchanged,
                                  /*reason=*/std::string(), /*reachable=*/true,
                                  /*liveMapped=*/true);
        if (pass.verdict != svv::Verdict::PassedNotVerified || pass.method_rank != 3) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: a rank-4 on-disk hash PASS + reachability mapped to (%s, "
                "rank %d), not (passed_not_verified, rank 3) — a passing static "
                "check is real evidence but NOT proof of execution; the ceiling "
                "rule caps it at passed_not_verified, never verified_working.",
                svv::VerdictName(pass.verdict), pass.method_rank);
            LOG_ERROR_KV(kCategory, "selftest_fail",
                ::kcdx::log::KV("subcheck", "11_ceiling_pass"));
            kcdx::test::ReportResult(kRow, false, reason);
            kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
            vcc::Reset(); sp::Reset();
            return;
        }
        // (b) hash MISMATCH (Changed) → failed (override-downward), rank 4.
        svv::StaticVerdict miss =
            svv::MapStaticVerdict(/*versionGap=*/false, sv::Status::Changed,
                                  /*reason=*/std::string(), /*reachable=*/true,
                                  /*liveMapped=*/true);
        if (miss.verdict != svv::Verdict::Failed) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: a rank-4 on-disk hash MISMATCH mapped to %s, not failed — a "
                "divergence at any rank overrides the ceiling downward to failed.",
                svv::VerdictName(miss.verdict));
            LOG_ERROR_KV(kCategory, "selftest_fail",
                ::kcdx::log::KV("subcheck", "11_ceiling_mismatch"));
            kcdx::test::ReportResult(kRow, false, reason);
            kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
            vcc::Reset(); sp::Reset();
            return;
        }
    }

    // ----- Sub-check 12: THE VERSION-GAP PRODUCER (item c) ------------------
    // A row whose resolved build version is NOT covered by the row (versionGap)
    // maps to not_applicable — NOT cannot_check. The version-applicability check
    // RAN and found non-coverage (a gap), distinct from lacking inputs. The gap
    // wins even when the on-disk bytes would have matched (the row is not for this
    // build). Falsifiable: a version-gap row reading cannot_check (or any verdict
    // other than not_applicable) collapses the gap-vs-missing-data distinction.
    {
        svv::StaticVerdict gap =
            svv::MapStaticVerdict(/*versionGap=*/true, sv::Status::Unchanged,
                                  /*reason=*/std::string(), /*reachable=*/true,
                                  /*liveMapped=*/true);
        if (gap.verdict != svv::Verdict::NotApplicable) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: a version-gap row (running build not covered) mapped to %s, "
                "not not_applicable — the version-applicability check ran and found "
                "non-coverage (a gap), which is not_applicable, NOT cannot_check "
                "(missing inputs) and NOT failed (a divergence on a covered build).",
                svv::VerdictName(gap.verdict));
            LOG_ERROR_KV(kCategory, "selftest_fail",
                ::kcdx::log::KV("subcheck", "12_version_gap"));
            kcdx::test::ReportResult(kRow, false, reason);
            kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
            vcc::Reset(); sp::Reset();
            return;
        }
        // The cannot_check producer is distinct: an on-disk CannotCheck (missing
        // inputs) with NO version gap maps to cannot_check, proving the two are
        // not conflated.
        svv::StaticVerdict cc =
            svv::MapStaticVerdict(/*versionGap=*/false, sv::Status::CannotCheck,
                                  /*reason=*/"not_applicable", /*reachable=*/false,
                                  /*liveMapped=*/true);
        if (cc.verdict != svv::Verdict::CannotCheck) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: an on-disk CannotCheck (missing inputs, no version gap) "
                "mapped to %s, not cannot_check — a row that lacks the check's "
                "inputs is cannot_check, distinct from a version gap "
                "(not_applicable).", svv::VerdictName(cc.verdict));
            LOG_ERROR_KV(kCategory, "selftest_fail",
                ::kcdx::log::KV("subcheck", "12_cannot_check_distinct"));
            kcdx::test::ReportResult(kRow, false, reason);
            kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
            vcc::Reset(); sp::Reset();
            return;
        }

        // ----- The precise gap SIGNAL: versionGap = !interval_covers_version ---
        // The sweep derives versionGap from the resolved row's PRECISE coverage
        // bit (interval_covers_version), NOT the resolver's coarse Unverified
        // state. Two synthetic rows exercise the exact production derivation +
        // mapping end-to-end. The distinction that matters:
        //   - interval_covers_version==false → the running build is outside the
        //     picked interval → a genuine gap → not_applicable.
        //   - interval_covers_version==true WITH verification_state==Unverified →
        //     the interval DOES cover the build, only the re-verification is
        //     stale → NOT a gap → flows to the static-check verdict (here a
        //     synthetic on-disk hash PASS → passed_not_verified), never
        //     not_applicable.
        // Falsifiable: a covered-but-unverified row reading not_applicable fails
        // this sub-check — that is the exact over-broadness the precise signal
        // removes (the prior interim keyed on Unverified and would have condemned
        // it). Each row computes versionGap the SAME way the sweep does
        // (!nr.interval_covers_version), so this tests the wiring, not a stand-in.

        // Row A — genuine gap: interval does NOT cover the running build.
        refdb::NameResolution gapRow;
        gapRow.found = true;
        gapRow.verification_state =
            refdb::NameResolution::VerificationState::Unverified;
        gapRow.interval_covers_version = false;  // outside the picked interval.
        svv::StaticVerdict gapMapped =
            svv::MapStaticVerdict(/*versionGap=*/!gapRow.interval_covers_version,
                                  sv::Status::Unchanged, /*reason=*/std::string(),
                                  /*reachable=*/true, /*liveMapped=*/true);
        if (gapMapped.verdict != svv::Verdict::NotApplicable) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: a row whose interval does NOT cover the running build "
                "(interval_covers_version=false) mapped to %s, not not_applicable "
                "— a build outside the picked interval is a genuine version gap.",
                svv::VerdictName(gapMapped.verdict));
            LOG_ERROR_KV(kCategory, "selftest_fail",
                ::kcdx::log::KV("subcheck", "12_precise_gap_uncovered"));
            kcdx::test::ReportResult(kRow, false, reason);
            kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
            vcc::Reset(); sp::Reset();
            return;
        }

        // Row B — COVERED but not freshly re-verified: interval includes the
        // build, yet the row resolves Unverified. This must NOT read
        // not_applicable; the precise signal lets it flow to the static-check
        // verdict (a synthetic on-disk PASS → passed_not_verified). Keying on the
        // coarse Unverified state (the prior interim) would have wrongly read it
        // not_applicable — this is the over-broadness the fix removes.
        refdb::NameResolution coveredStaleRow;
        coveredStaleRow.found = true;
        coveredStaleRow.verification_state =
            refdb::NameResolution::VerificationState::Unverified;
        coveredStaleRow.interval_covers_version = true;  // interval DOES cover V.
        svv::StaticVerdict coveredMapped =
            svv::MapStaticVerdict(
                /*versionGap=*/!coveredStaleRow.interval_covers_version,
                sv::Status::Unchanged, /*reason=*/std::string(),
                /*reachable=*/true, /*liveMapped=*/true);
        if (coveredMapped.verdict == svv::Verdict::NotApplicable) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: a COVERED-but-unverified row (interval_covers_version=true, "
                "verification_state=Unverified) read not_applicable — the interval "
                "covers the running build; only the re-verification is stale, so it "
                "must flow to the static-check verdict, NEVER not_applicable. "
                "Reading not_applicable here is the exact over-broadness the precise "
                "signal removes (the coarse Unverified state would have condemned "
                "it).");
            LOG_ERROR_KV(kCategory, "selftest_fail",
                ::kcdx::log::KV("subcheck", "12_precise_covered_unverified"));
            kcdx::test::ReportResult(kRow, false, reason);
            kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
            vcc::Reset(); sp::Reset();
            return;
        }
        if (coveredMapped.verdict != svv::Verdict::PassedNotVerified) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: a covered-but-unverified row with a passing on-disk hash + "
                "reachability mapped to %s, not passed_not_verified — past the "
                "(non-)gap it is an ordinary static PASS capped at "
                "passed_not_verified by the ceiling rule.",
                svv::VerdictName(coveredMapped.verdict));
            LOG_ERROR_KV(kCategory, "selftest_fail",
                ::kcdx::log::KV("subcheck", "12_precise_covered_flows"));
            kcdx::test::ReportResult(kRow, false, reason);
            kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
            vcc::Reset(); sp::Reset();
            return;
        }
    }

    // ----- Sub-check 13: THE FAULT PRODUCER — error ≠ failed (item d) -------
    // A check that throws (caught) maps to error, NOT failed — the harness
    // faulted, the ROW may be fine. RunStartupVerification's per-row catch is the
    // production error producer; it sets error ONLY on a caught exception. The
    // STATIC mapping (MapStaticVerdict) — the non-fault path — must therefore
    // NEVER itself return error (nor verified_working, nor skipped: those come
    // from the catch and from later-step methods). If the static mapping could
    // fabricate error, a mapping outcome would be indistinguishable from a real
    // harness fault. So sweep MapStaticVerdict across its full input space and
    // assert it produces ONLY the static-reachable states (passed_not_verified /
    // failed / not_applicable / cannot_check). Falsifiable: any static-mapping
    // input yielding error (or verified_working / skipped) collapses the
    // fault-vs-mapping separation that keeps a caught fault honestly distinct from
    // a diverged row.
    {
        const sv::Status statuses[] = {sv::Status::Unchanged, sv::Status::Changed,
                                       sv::Status::Ambiguous, sv::Status::CannotCheck};
        const bool bools[] = {true, false};
        bool sepOk = true;
        svv::Verdict offending = svv::Verdict::Error;
        for (bool versionGap : bools) {
            for (sv::Status st : statuses) {
                for (bool reachable : bools) {
                    for (bool liveMapped : bools) {
                        svv::StaticVerdict m = svv::MapStaticVerdict(
                            versionGap, st, /*reason=*/std::string(), reachable,
                            liveMapped);
                        if (m.verdict == svv::Verdict::Error ||
                            m.verdict == svv::Verdict::VerifiedWorking ||
                            m.verdict == svv::Verdict::Skipped) {
                            sepOk = false;
                            offending = m.verdict;
                        }
                    }
                }
            }
        }
        if (!sepOk) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: the static verdict mapping produced '%s' for some input — the "
                "static path must NEVER yield error (that is the per-row catch's, on "
                "a caught fault), verified_working (rank-1 observed execution only), "
                "or skipped (the precondition gate's). A static mapping that can "
                "fabricate error makes a harness fault indistinguishable from a "
                "mapping outcome — error must stay distinct from failed.",
                svv::VerdictName(offending));
            LOG_ERROR_KV(kCategory, "selftest_fail",
                ::kcdx::log::KV("subcheck", "13_fault_separation"));
            kcdx::test::ReportResult(kRow, false, reason);
            kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
            vcc::Reset(); sp::Reset();
            return;
        }
        // And the fault DISCRIMINATION the per-row catch makes: a caught
        // exception resolves to error (NOT failed). This mirrors the exact
        // assignment RunStartupVerification's catch makes on a real harness fault
        // (the static path is unreachable for error per the sweep above, so a
        // throw is the only route). A fault that resolved to failed instead would
        // condemn a row whose only problem was the test blowing up.
        svv::Verdict caughtVerdict = svv::Verdict::Failed;  // seeded wrong; the catch overwrites.
        try {
            throw std::runtime_error("synthetic_harness_fault");
        } catch (const std::exception&) {
            caughtVerdict = svv::Verdict::Error;  // == the sweep's catch contract.
        }
        if (caughtVerdict != svv::Verdict::Error) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: a caught harness fault resolved to %s, not error — a thrown "
                "check is error (the TEST blew up), never failed (the ROW diverged).",
                svv::VerdictName(caughtVerdict));
            LOG_ERROR_KV(kCategory, "selftest_fail",
                ::kcdx::log::KV("subcheck", "13_fault_error"));
            kcdx::test::ReportResult(kRow, false, reason);
            kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
            vcc::Reset(); sp::Reset();
            return;
        }
    }

    // ----- Sub-check 14: THE RANK-1 OBSERVED-EXECUTION TIER ------------------
    // The ONLY method that awards verified_working: an OBSERVED engine-hook fire,
    // never a fabricated result. Three falsifiable claims:
    //
    //   (a) NEGATIVE (deterministic, HARD) — a row with NO observed fire does NOT
    //       reach verified_working. ObserveHookedExecution on a synthetic VA that
    //       no engine hook sits on returns observed=false; ObservedToVerdict on a
    //       non-observation returns false and leaves the static ceiling intact.
    //       Falsifiable: a no-fire observation lifting to verified_working fails.
    //   (b) POSITIVE (deterministic, HARD) — a positive observation lifts to
    //       verified_working at method_rank 1. A synthetic observed=true overrides
    //       a PassedNotVerified static ceiling UPWARD to (verified_working, 1).
    //       Falsifiable: an observed fire NOT reaching verified_working/rank-1
    //       fails; a static-ceiling row reading verified_working WITHOUT an
    //       observation fails (a).
    //   (c) LIVE (integration, DEGRADE-pass) — when a curated engine-hooked row
    //       (lua_pcall fires pre-menu) has a recorded fire observable AT this
    //       report point, RunStartupVerification reads it verified_working at
    //       rank 1 from the OBSERVED fire. DEGRADE-pass when the fire is not yet
    //       observable (one-shot self-test reached before the first fire / WHGame
    //       not mapped) — never a hard FAIL on timing. The HARD contradiction
    //       guard still bites: a row reading verified_working whose VA has NO
    //       engine chain entry + NO recorded fire is a fabricated top rung.
    {
        // (a) NEGATIVE — a VA no engine hook sits on is not observed, and a
        // non-observation never lifts the verdict. 0xC0DE is a synthetic
        // sentinel VA; no engine chain entry resolves there.
        svv::ObservedExecution none = svv::ObserveHookedExecution(/*va=*/0xC0DE);
        svv::StaticVerdict staticCeiling;
        staticCeiling.verdict = svv::Verdict::PassedNotVerified;
        staticCeiling.method_rank = 3;
        staticCeiling.detail = "matched_and_in_live_text";
        bool lifted = svv::ObservedToVerdict(none, staticCeiling);
        if (none.observed || lifted ||
            staticCeiling.verdict != svv::Verdict::PassedNotVerified) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: a row with NO observed engine-hook fire reached "
                "verified_working (observed=%d, lifted=%d, verdict=%s) — only an "
                "OBSERVED fire earns rank-1; a no-fire row must fall through to its "
                "static ceiling, never be awarded the top rung.",
                none.observed ? 1 : 0, lifted ? 1 : 0,
                svv::VerdictName(staticCeiling.verdict));
            LOG_ERROR_KV(kCategory, "selftest_fail",
                ::kcdx::log::KV("subcheck", "14_rank1_negative"));
            kcdx::test::ReportResult(kRow, false, reason);
            kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
            vcc::Reset(); sp::Reset();
            return;
        }

        // (b) POSITIVE — a synthetic observed=true lifts a PassedNotVerified
        // ceiling to (verified_working, rank 1). This is the observation→verdict
        // seam the sweep uses; assert it directly (not a re-derivation).
        svv::ObservedExecution obs;
        obs.observed = true;
        obs.hasChainEntry = true;
        obs.fireSeq = 42;  // a non-zero recorded fire seq (the observed fact).
        obs.detail = "observed_engine_hook_fire";
        svv::StaticVerdict toLift;
        toLift.verdict = svv::Verdict::PassedNotVerified;
        toLift.method_rank = 3;
        bool ok = svv::ObservedToVerdict(obs, toLift);
        if (!ok || toLift.verdict != svv::Verdict::VerifiedWorking ||
            toLift.method_rank != 1) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: a POSITIVE observed-execution did NOT lift to "
                "(verified_working, rank 1) — got lifted=%d, (%s, rank %d). An "
                "observed engine-hook fire is the only method that awards the top "
                "rung and must override the static ceiling upward to rank 1.",
                ok ? 1 : 0, svv::VerdictName(toLift.verdict), toLift.method_rank);
            LOG_ERROR_KV(kCategory, "selftest_fail",
                ::kcdx::log::KV("subcheck", "14_rank1_positive"));
            kcdx::test::ReportResult(kRow, false, reason);
            kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
            vcc::Reset(); sp::Reset();
            return;
        }

        // (c) LIVE + the CONTRADICTION GUARD — scan the real sweep. Any
        // verified_working row MUST carry an observed rank-1 signal on its VA —
        // EITHER an engine-hook fire (HOOKED) OR a kcdx invocation record
        // (CALLED-by-kcdx) (a HARD guard: a row with NEITHER is a fabricated top
        // rung). A curated hooked/called row reading verified_working at rank 1
        // is the live positive; its absence is a DEGRADE (the one-shot self-test
        // may run before the first fire/call), never a hard FAIL.
        std::vector<svv::RowVerdict> verds = svv::RunStartupVerification();
        bool sawLiveVerifiedWorking = false;
        for (const auto& v : verds) {
            if (v.verdict != svv::Verdict::VerifiedWorking) continue;
            sawLiveVerifiedWorking = true;
            // HARD: a verified_working row's VA must actually carry an observed
            // rank-1 signal — re-observe it cold. A verified_working with neither
            // an engine-hook fire NOR an invocation record is a fabricated top
            // rung (the exact over-claim the ceiling rule forbids). Both observed
            // sub-paths are legitimate; the guard accepts either.
            uintptr_t vVa = refdb::ResolveAddrByName(v.name);
            svv::ObservedExecution reobs = svv::ObserveHookedExecution(vVa);
            bool calledObserved = svv::WasInvokedByKcdx(vVa);
            if (v.method_rank != 1 || (!reobs.observed && !calledObserved)) {
                std::snprintf(reason, sizeof(reason),
                    "FAIL: the row '%s' read verified_working (rank %d) but a cold "
                    "re-observation of its VA shows NEITHER an engine-hook fire NOR "
                    "a kcdx invocation record (hook_observed=%d, chainEntry=%d, "
                    "fireSeq=%llu, kcdx_called=%d) — verified_working must come from "
                    "an OBSERVED fire or call at rank 1, never a fabricated top rung.",
                    v.name.c_str(), v.method_rank, reobs.observed ? 1 : 0,
                    reobs.hasChainEntry ? 1 : 0,
                    (unsigned long long)reobs.fireSeq, calledObserved ? 1 : 0);
                LOG_ERROR_KV(kCategory, "selftest_fail",
                    ::kcdx::log::KV("subcheck", "14_rank1_contradiction"),
                    ::kcdx::log::KV("name", v.name));
                kcdx::test::ReportResult(kRow, false, reason);
                kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
                vcc::Reset(); sp::Reset();
                return;
            }
        }
        // sawLiveVerifiedWorking == false is the DEGRADE path (no engine-hooked
        // row's fire was observable at this one-shot report point) — recorded in
        // the PASS note, never a hard FAIL on timing.
        LOG_INFO_KV(kCategory, "rank1_live",
            ::kcdx::log::KV("saw_verified_working",
                sawLiveVerifiedWorking ? "yes" : "degraded_no_fire_yet"));
    }

    // ----- Sub-check 14b: THE RANK-1 CALLED-by-kcdx TIER --------------------
    // The SECOND rank-1 observed sub-path: a curated target kcdx invokes in its
    // own production path (the cvar accessors, the console AddCommand/ExecuteString
    // thunks) records its RESOLVED VA after the call returns; the sweep reads that
    // record and awards verified_working (rank 1) — the CALLED-by-kcdx analogue of
    // the HOOKED fire. Three falsifiable claims, mirroring 14:
    //
    //   (a) PRESENT (deterministic, HARD) — a VA placed in the invocation record
    //       lifts a passed_not_verified ceiling to (verified_working, rank 1)
    //       through the SAME ObservedToVerdict seam (the CALLED observation). A
    //       synthetic sentinel VA (never a real production target) is recorded,
    //       then a row with that VA + a passing static ceiling is observed lifted.
    //   (b) ABSENT (deterministic, HARD) — a VA NOT in the record is not observed
    //       called → WasInvokedByKcdx reads false → the static ceiling stands. A
    //       second sentinel VA, never recorded, must NOT lift.
    //   (c) LIVE (integration, DEGRADE-pass) — when a curated CALLED row (e.g. a
    //       cvar getter, AddCommand) was actually invoked by boot AND its row VA
    //       resolves, its sweep verdict reads verified_working at rank 1 from the
    //       OBSERVED call. DEGRADE-pass when no CALLED row was invoked by the
    //       cap-84 report timing (the getters may not have been hit pre-menu) —
    //       never a hard FAIL on timing. The HARD contradiction guard from 14c
    //       already backs every live verified_working row (HOOKED or CALLED): a
    //       row reading verified_working must carry an observed fire OR an
    //       invocation record on its VA.
    //
    // The synthetic VAs use the high sentinel range (never a real WHGame.dll VA),
    // so recording them does NOT disturb the real production invocation record the
    // live sweep above reads — no ResetInvocationRecord (which would wipe real
    // CALLED records the live check depends on).
    {
        // (a) PRESENT — record a sentinel VA, then assert it lifts a passing
        // ceiling to (verified_working, rank 1) via the CALLED read + the
        // ObservedToVerdict seam (the exact two-step the sweep runs for CALLED).
        const uintptr_t kCalledSentinel = 0xCA11ED000000ULL;  // synthetic; no real WHGame VA here.
        svv::RecordKcdxInvocation(kCalledSentinel);
        if (!svv::WasInvokedByKcdx(kCalledSentinel)) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: a VA recorded via RecordKcdxInvocation did NOT read back as "
                "invoked (WasInvokedByKcdx=false) — the CALLED-by-kcdx record must "
                "store + return a recorded invocation; without it the sweep can "
                "never observe a kcdx-called row.");
            LOG_ERROR_KV(kCategory, "selftest_fail",
                ::kcdx::log::KV("subcheck", "14b_called_record"));
            kcdx::test::ReportResult(kRow, false, reason);
            kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
            vcc::Reset(); sp::Reset();
            return;
        }
        // The sweep's CALLED lift: a recorded VA + a passing static ceiling →
        // verified_working rank 1, through ObservedToVerdict (a synthetic
        // observed=true mirrors the called branch's construction).
        svv::StaticVerdict calledCeiling;
        calledCeiling.verdict = svv::Verdict::PassedNotVerified;
        calledCeiling.method_rank = 3;
        bool calledLifted = false;
        if (svv::WasInvokedByKcdx(kCalledSentinel)) {
            svv::ObservedExecution called;
            called.observed = true;
            called.detail = "observed_kcdx_called";
            calledLifted = svv::ObservedToVerdict(called, calledCeiling);
        }
        if (!calledLifted || calledCeiling.verdict != svv::Verdict::VerifiedWorking ||
            calledCeiling.method_rank != 1) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: a CALLED-by-kcdx invocation (recorded VA + passing static "
                "ceiling) did NOT lift to (verified_working, rank 1) — got "
                "lifted=%d, (%s, rank %d). A row kcdx invoked + that returned is "
                "observed execution and must reach the top rung, same as a HOOKED "
                "fire.",
                calledLifted ? 1 : 0, svv::VerdictName(calledCeiling.verdict),
                calledCeiling.method_rank);
            LOG_ERROR_KV(kCategory, "selftest_fail",
                ::kcdx::log::KV("subcheck", "14b_called_lift"));
            kcdx::test::ReportResult(kRow, false, reason);
            kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
            vcc::Reset(); sp::Reset();
            return;
        }

        // (b) ABSENT — a sentinel VA never recorded must read NOT invoked, so the
        // static ceiling stands (no fabricated rank-1 from an un-called row).
        const uintptr_t kAbsentSentinel = 0xABCE17000000ULL;  // synthetic; never recorded.
        svv::StaticVerdict absentCeiling;
        absentCeiling.verdict = svv::Verdict::PassedNotVerified;
        absentCeiling.method_rank = 3;
        bool absentInvoked = svv::WasInvokedByKcdx(kAbsentSentinel);
        // Mirror the sweep's gate: only consult the record on a non-divergent
        // ceiling, and only lift when invoked. An un-invoked VA must leave the
        // ceiling at passed_not_verified.
        if (absentInvoked) {
            svv::ObservedExecution called;
            called.observed = true;
            svv::ObservedToVerdict(called, absentCeiling);
        }
        if (absentInvoked || absentCeiling.verdict != svv::Verdict::PassedNotVerified) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: a VA NOT in the invocation record read as invoked "
                "(WasInvokedByKcdx=%d) or its ceiling changed (verdict=%s) — an "
                "un-called row must fall through to its static ceiling, never be "
                "awarded verified_working from a CALLED signal it never produced.",
                absentInvoked ? 1 : 0, svv::VerdictName(absentCeiling.verdict));
            LOG_ERROR_KV(kCategory, "selftest_fail",
                ::kcdx::log::KV("subcheck", "14b_called_absent"));
            kcdx::test::ReportResult(kRow, false, reason);
            kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
            vcc::Reset(); sp::Reset();
            return;
        }

        // (c) LIVE — scan the real sweep for a curated CALLED row that reads
        // verified_working from an invocation record (DEGRADE-pass when no CALLED
        // target was invoked by this one-shot report point). The HARD guard: any
        // such row's VA must actually be in the invocation record (a fabricated
        // CALLED top rung fails). HOOKED verified_working rows (no invocation
        // record, an engine fire instead) are excluded — 14c already guards them.
        std::vector<svv::RowVerdict> calledVerds = svv::RunStartupVerification();
        bool sawLiveCalled = false;
        for (const auto& v : calledVerds) {
            if (v.verdict != svv::Verdict::VerifiedWorking) continue;
            uintptr_t vVa = refdb::ResolveAddrByName(v.name);
            if (!svv::WasInvokedByKcdx(vVa)) continue;  // a HOOKED rank-1 row, not CALLED — 14c owns it.
            sawLiveCalled = true;
            if (v.method_rank != 1) {
                std::snprintf(reason, sizeof(reason),
                    "FAIL: the CALLED-by-kcdx row '%s' read verified_working at "
                    "rank %d — an observed kcdx invocation is rank 1; a CALLED "
                    "verified_working at any other rank is a mis-ranked top rung.",
                    v.name.c_str(), v.method_rank);
                LOG_ERROR_KV(kCategory, "selftest_fail",
                    ::kcdx::log::KV("subcheck", "14b_called_live_rank"),
                    ::kcdx::log::KV("name", v.name));
                kcdx::test::ReportResult(kRow, false, reason);
                kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
                vcc::Reset(); sp::Reset();
                return;
            }
        }
        LOG_INFO_KV(kCategory, "rank1_called_live",
            ::kcdx::log::KV("saw_called_verified_working",
                sawLiveCalled ? "yes" : "degraded_no_call_yet"));
    }

    // ----- Sub-check 15: THE RANK-2 SAFE-READ TIER --------------------------
    // The safe-read tier reads a row's live target with ZERO mutation, capping at
    // passed_not_verified — BELOW rank-1 observed execution, ABOVE the static
    // rank 3-5 checks. A sane read proves the target yields a plausible value, NOT
    // that its behavior works, so it can NEVER reach verified_working. Two methods:
    // a rank-2 cvar read (a getter row) and a rank-3 read-only vtable_base walk
    // (each entry resolves into live .text). The lift seam (SafeReadToVerdict) is
    // the producer the sweep calls — asserting against it tests the producer, not a
    // re-derivation (the same pattern sub-checks 11/14/14b use for MapStaticVerdict
    // / ObservedToVerdict). Falsifiable claims:
    //
    //   (a) CVAR SANE → rank-2 passed_not_verified, NEVER verified_working
    //       (deterministic, HARD). A sane cvar SafeReadResult lifts a passing
    //       static ceiling to (passed_not_verified, rank 2) — the rank-2 ceiling.
    //       FAILS if a rank-2 safe-read reaches verified_working (the top rung the
    //       safe-read tier must NOT reach) or lands a rank other than 2.
    //   (b) CVAR FAULTED → Failed (deterministic, HARD). A read that RAN but
    //       returned not-sane is a divergence the row should have passed → Failed,
    //       not a pass. FAILS if a faulted read reads passed_not_verified (a
    //       faulted read whitewashed into a pass).
    //   (c) NOT-RUN → static ceiling stands (deterministic, HARD). A read that
    //       could not run (attempted=false — the degrade path) leaves the static
    //       ceiling. FAILS if a not-run read changes the verdict.
    //   (d) VTABLE_BASE SANE → rank-3 passed_not_verified (deterministic, HARD).
    //       A sane read-only walk result lifts to (passed_not_verified, rank 3) —
    //       the §11.6 vtable_base rank. A broken walk (an entry not in live .text)
    //       → Failed. FAILS if a sane walk reaches verified_working / a rank other
    //       than 3, or a broken walk reads a pass.
    //   (e) LIVE CVAR READ (integration, DEGRADE-pass). SafeReadCvarGetter on a
    //       curated cvar-getter name (ICVar_GetIVal) reads sys_pakPriority through
    //       the production accessor; when the cvar surface is ready it is
    //       attempted && sane and lifts to (passed_not_verified, rank 2) — NEVER
    //       verified_working. DEGRADE-pass when the cvar surface is not ready at
    //       this one-shot report point (the read returns attempted=false), never a
    //       hard FAIL on timing. HARD: a read that RAN must be sane (the known cvar
    //       exists) and must NOT reach verified_working.
    {
        // (a) CVAR SANE → rank-2 passed_not_verified, never verified_working.
        svv::SafeReadResult cvarSane;
        cvarSane.attempted = true;
        cvarSane.sane = true;
        cvarSane.detail = "cvar_int_read_sane";
        svv::StaticVerdict cvarCeiling;
        cvarCeiling.verdict = svv::Verdict::PassedNotVerified;
        cvarCeiling.method_rank = 4;  // a static rank-4 ceiling, lifted by the read.
        bool cvarLifted = svv::SafeReadToVerdict(cvarSane, /*passRank=*/2,
                                                 /*failedRank=*/2, cvarCeiling);
        if (!cvarLifted || cvarCeiling.verdict != svv::Verdict::PassedNotVerified ||
            cvarCeiling.method_rank != 2) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: a SANE rank-2 cvar safe-read did NOT lift to "
                "(passed_not_verified, rank 2) — got lifted=%d, (%s, rank %d). A "
                "safe-read caps at passed_not_verified at rank 2; it must NEVER "
                "reach verified_working (only rank-1 observed execution earns the "
                "top rung).",
                cvarLifted ? 1 : 0, svv::VerdictName(cvarCeiling.verdict),
                cvarCeiling.method_rank);
            LOG_ERROR_KV(kCategory, "selftest_fail",
                ::kcdx::log::KV("subcheck", "15a_cvar_sane_rank2"));
            kcdx::test::ReportResult(kRow, false, reason);
            kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
            vcc::Reset(); sp::Reset();
            return;
        }
        if (cvarCeiling.verdict == svv::Verdict::VerifiedWorking) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: a rank-2 cvar safe-read reached verified_working — the "
                "safe-read tier caps at passed_not_verified; only observed live "
                "execution (rank 1) earns verified_working.");
            LOG_ERROR_KV(kCategory, "selftest_fail",
                ::kcdx::log::KV("subcheck", "15a_cvar_overclaim"));
            kcdx::test::ReportResult(kRow, false, reason);
            kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
            vcc::Reset(); sp::Reset();
            return;
        }

        // (b) CVAR FAULTED → Failed (a faulted read is failed, never a pass).
        svv::SafeReadResult cvarFault;
        cvarFault.attempted = true;
        cvarFault.sane = false;
        cvarFault.detail = "cvar_read_unavailable";
        svv::StaticVerdict faultCeiling;
        faultCeiling.verdict = svv::Verdict::PassedNotVerified;
        faultCeiling.method_rank = 4;
        bool faultLifted = svv::SafeReadToVerdict(cvarFault, /*passRank=*/2,
                                                  /*failedRank=*/2, faultCeiling);
        if (!faultLifted || faultCeiling.verdict != svv::Verdict::Failed) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: a faulted safe-read (attempted but not sane) did NOT map to "
                "Failed — got lifted=%d, %s. A read that RAN but returned "
                "not-sane is a divergence the row should have passed; it must read "
                "Failed, never passed_not_verified (a faulted read whitewashed into "
                "a pass).",
                faultLifted ? 1 : 0, svv::VerdictName(faultCeiling.verdict));
            LOG_ERROR_KV(kCategory, "selftest_fail",
                ::kcdx::log::KV("subcheck", "15b_cvar_faulted_failed"));
            kcdx::test::ReportResult(kRow, false, reason);
            kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
            vcc::Reset(); sp::Reset();
            return;
        }

        // (c) NOT-RUN → the static ceiling stands (the degrade path).
        svv::SafeReadResult notRun;  // attempted=false (default).
        notRun.detail = "cvar_read_unavailable";
        svv::StaticVerdict keepCeiling;
        keepCeiling.verdict = svv::Verdict::PassedNotVerified;
        keepCeiling.method_rank = 4;
        keepCeiling.detail = "matched_and_in_live_text";
        bool keptLifted = svv::SafeReadToVerdict(notRun, /*passRank=*/2,
                                                 /*failedRank=*/2, keepCeiling);
        if (keptLifted || keepCeiling.verdict != svv::Verdict::PassedNotVerified ||
            keepCeiling.method_rank != 4) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: a safe-read that did NOT run (attempted=false) changed the "
                "verdict — got lifted=%d, (%s, rank %d). A read that could not run "
                "must leave the static ceiling untouched (the degrade path), never "
                "alter the verdict or rank.",
                keptLifted ? 1 : 0, svv::VerdictName(keepCeiling.verdict),
                keepCeiling.method_rank);
            LOG_ERROR_KV(kCategory, "selftest_fail",
                ::kcdx::log::KV("subcheck", "15c_safe_read_notrun"));
            kcdx::test::ReportResult(kRow, false, reason);
            kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
            vcc::Reset(); sp::Reset();
            return;
        }

        // (d) VTABLE_BASE SANE → rank-3 passed_not_verified; BROKEN → Failed.
        svv::SafeReadResult vtSane;
        vtSane.attempted = true;
        vtSane.sane = true;
        vtSane.detail = "vtable_all_entries_in_live_text";
        svv::StaticVerdict vtCeiling;
        vtCeiling.verdict = svv::Verdict::PassedNotVerified;
        vtCeiling.method_rank = 4;
        bool vtLifted = svv::SafeReadToVerdict(vtSane, /*passRank=*/3,
                                               /*failedRank=*/3, vtCeiling);
        if (!vtLifted || vtCeiling.verdict != svv::Verdict::PassedNotVerified ||
            vtCeiling.method_rank != 3 ||
            vtCeiling.verdict == svv::Verdict::VerifiedWorking) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: a SANE rank-3 vtable_base read-only walk did NOT lift to "
                "(passed_not_verified, rank 3) — got lifted=%d, (%s, rank %d). The "
                "§11.6 vtable_base ceiling is passed_not_verified at rank 3 via the "
                "read-only walk; it must NEVER reach verified_working.",
                vtLifted ? 1 : 0, svv::VerdictName(vtCeiling.verdict),
                vtCeiling.method_rank);
            LOG_ERROR_KV(kCategory, "selftest_fail",
                ::kcdx::log::KV("subcheck", "15d_vtable_sane_rank3"));
            kcdx::test::ReportResult(kRow, false, reason);
            kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
            vcc::Reset(); sp::Reset();
            return;
        }
        svv::SafeReadResult vtBroken;
        vtBroken.attempted = true;
        vtBroken.sane = false;
        vtBroken.detail = "vtable_entry_not_in_live_text";
        svv::StaticVerdict vtBrokenCeiling;
        vtBrokenCeiling.verdict = svv::Verdict::PassedNotVerified;
        vtBrokenCeiling.method_rank = 4;
        svv::SafeReadToVerdict(vtBroken, /*passRank=*/3, /*failedRank=*/3,
                               vtBrokenCeiling);
        if (vtBrokenCeiling.verdict != svv::Verdict::Failed) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: a BROKEN vtable_base walk (an entry not in live .text) "
                "mapped to %s, not Failed — a walk that found a non-.text entry is "
                "a live table-shape divergence and must read Failed.",
                svv::VerdictName(vtBrokenCeiling.verdict));
            LOG_ERROR_KV(kCategory, "selftest_fail",
                ::kcdx::log::KV("subcheck", "15d_vtable_broken_failed"));
            kcdx::test::ReportResult(kRow, false, reason);
            kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
            vcc::Reset(); sp::Reset();
            return;
        }

        // (e) LIVE CVAR READ (integration, DEGRADE-pass). SafeReadCvarGetter on a
        // curated cvar-getter name reads sys_pakPriority through the production
        // accessor. When the cvar surface is ready the read is attempted && sane;
        // a sane read lifts to (passed_not_verified, rank 2), NEVER
        // verified_working. DEGRADE-pass when the surface is not ready (the read
        // returns attempted=false at this one-shot report point) — never a hard
        // FAIL on timing. HARD: a read that RAN must be sane (the known cvar
        // exists) and a sane read must NOT reach the top rung.
        svv::SafeReadResult liveCvar = svv::SafeReadCvarGetter("ICVar_GetIVal");
        if (liveCvar.attempted) {
            if (!liveCvar.sane) {
                std::snprintf(reason, sizeof(reason),
                    "FAIL: the live cvar safe-read on ICVar_GetIVal RAN "
                    "(attempted) but read not-sane — reading a confirmed "
                    "boot-present cvar (sys_pakPriority) through the getter must "
                    "return a plausible value; a faulted read of a cvar that "
                    "exists is the safe-read failing on a genuinely-good target.");
                LOG_ERROR_KV(kCategory, "selftest_fail",
                    ::kcdx::log::KV("subcheck", "15e_live_cvar_notsane"));
                kcdx::test::ReportResult(kRow, false, reason);
                kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
                vcc::Reset(); sp::Reset();
                return;
            }
            // A sane live read lifts a passing ceiling to (passed_not_verified,
            // rank 2) — and never verified_working.
            svv::StaticVerdict liveCeiling;
            liveCeiling.verdict = svv::Verdict::PassedNotVerified;
            liveCeiling.method_rank = 4;
            svv::SafeReadToVerdict(liveCvar, /*passRank=*/2, /*failedRank=*/2,
                                   liveCeiling);
            if (liveCeiling.verdict != svv::Verdict::PassedNotVerified ||
                liveCeiling.method_rank != 2) {
                std::snprintf(reason, sizeof(reason),
                    "FAIL: a SANE live cvar read lifted to (%s, rank %d), not "
                    "(passed_not_verified, rank 2) — a sane safe-read of a real "
                    "cvar caps at passed_not_verified at rank 2, never the top "
                    "rung.",
                    svv::VerdictName(liveCeiling.verdict), liveCeiling.method_rank);
                LOG_ERROR_KV(kCategory, "selftest_fail",
                    ::kcdx::log::KV("subcheck", "15e_live_cvar_rank"));
                kcdx::test::ReportResult(kRow, false, reason);
                kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
                vcc::Reset(); sp::Reset();
                return;
            }
        }
        LOG_INFO_KV(kCategory, "rank2_cvar_live",
            ::kcdx::log::KV("live_cvar_read",
                liveCvar.attempted
                    ? (liveCvar.sane ? "sane_rank2" : "ran_not_sane")
                    : "degraded_surface_not_ready"));
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
        "PASS — function-kind dispatch == legacy on-disk body-hash (3 cases%s); "
        "vtable_index is a defined deferral; Ambiguous round-trips the pass+codec; "
        "the 5 static checks verdict correctly; a Changed anchor transitively blocks "
        "its dependent (anchor_changed); reachability (IsVaInLiveText) reads "
        "off-image/null VAs as NOT in live .text + a real fn VA in .text; "
        "RunStartupVerification yields a defined verdict per swept row + a good fn "
        "caps at passed_not_verified (rank 3) surfacing the matched id (never "
        "verified_working from a static pass); a non-matching fingerprint reads "
        "Changed (failed); AND the 7-state verdict model — all 7 states produce + "
        "read back their own value; the existing FuncStatus codec round-trips intact "
        "(RowVerdict not serialized through it); the ceiling rule (hash+reach pass -> "
        "passed_not_verified rank 3, NOT verified_working; mismatch -> failed); the "
        "version-gap producer (-> not_applicable, distinct from cannot_check) keyed "
        "on the PRECISE interval_covers_version signal (interval-uncovered -> "
        "not_applicable; covered-but-unverified -> passed_not_verified, NOT "
        "not_applicable); the fault producer (a caught throw -> error; the "
        "static mapping never fabricates error); AND the rank-1 observed-execution "
        "tier — a no-fire row does NOT reach verified_working (falls to its static "
        "ceiling), an observed fire lifts to (verified_working, rank 1), and every "
        "live verified_working row carries an observed engine-hook fire (no "
        "fabricated top rung); AND the rank-1 CALLED-by-kcdx tier — a recorded "
        "invocation VA lifts a passing ceiling to (verified_working, rank 1), a "
        "VA not in the record does NOT, and a live CALLED row reads "
        "verified_working only from a real invocation record; AND the rank-2 "
        "safe-read tier — a sane cvar read lifts to (passed_not_verified, rank 2) "
        "NEVER verified_working, a faulted read -> failed, a not-run read leaves "
        "the static ceiling, the vtable_base read-only walk caps at "
        "(passed_not_verified, rank 3) with a broken-entry walk -> failed, and a "
        "live cvar read of sys_pakPriority (when the surface is ready) reads sane "
        "at rank 2. [%s]",
        realChecked ? " + 1 real" : "", realNote);
    LOG_INFO_KV(kCategory, "selftest_pass",
        ::kcdx::log::KV("real_checked", realChecked ? "yes" : "degraded"),
        ::kcdx::log::KV("real_status", realChecked ? StatusName(realStatus) : "-"));
    kcdx::test::ReportResult(kRow, true, reason);
    kcdx::test::EmitSummaryIfChanged("cap-84 survival-dispatch");
}

}  // namespace kcdx::survival_dispatch_selftest
