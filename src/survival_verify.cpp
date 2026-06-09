#include "survival_verify.h"

#include <exception>  // std::exception (ParsePattern throw)
#include <string>
#include <utility>    // std::move
#include <vector>

#include "log.h"
#include "patch_engine.h"   // patch::ParsePattern — decode a stored aob string → bytes+mask
#include "pe_helpers.h"
#include "plugin_loader.h"  // kcdx::plugins::g_runtimeGameVersionString (the running build tag)
#include "refdb.h"
#include "survival.h"

// survival_verify — the D25 startup verification pass. See survival_verify.h for
// the two-check contract (on-disk version-applicability + live-image
// reachability) and why reachability is a range test, NOT a live-body hash.

namespace kcdx::survival_verify {

namespace {

const char* kCategory = "SURVERIFY";

namespace sv = kcdx::survival;

// Decode a refdb kind string (address_versions.kind) → a survival::Kind. An
// unknown kind defaults to Function (its on-disk body-hash path); a row carrying
// no content_hash on the function path resolves to CannotCheck "not_applicable"
// downstream, so the default never fabricates a verdict. Returns false on an
// EMPTY kind (a malformed row) so the caller can cannot_check it loudly.
bool DecodeKind(const std::string& kindStr, sv::Kind& out) {
    if (kindStr.empty()) return false;
    if (kindStr == "function")            { out = sv::Kind::Function;          return true; }
    if (kindStr == "function_no_sig")     { out = sv::Kind::FunctionNoSig;     return true; }
    if (kindStr == "function_variadic")   { out = sv::Kind::FunctionVariadic;  return true; }
    if (kindStr == "callsite")            { out = sv::Kind::Callsite;          return true; }
    if (kindStr == "string_anchor")       { out = sv::Kind::StringAnchor;      return true; }
    if (kindStr == "instruction_anchor")  { out = sv::Kind::InstructionAnchor; return true; }
    if (kindStr == "data_slot")           { out = sv::Kind::DataSlot;          return true; }
    if (kindStr == "vtable_base")         { out = sv::Kind::VtableBase;        return true; }
    if (kindStr == "vtable_index")        { out = sv::Kind::VtableIndex;       return true; }
    // An unrecognized kind string is a malformed/forward-schema row — fail loud
    // (cannot_check), never a guessed verdict (AP14).
    return false;
}

// Decode a stored AOB string ("48 ?? 89 …") into a Payload's bytes + mask
// (1=literal, 0=wildcard). Reuses patch::ParsePattern (the SAME decoder the live
// AOB path uses — same as cap-84's AobToPayload). Returns false on malformed.
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

// Build the kind-discriminated Payload for one resolved row. Mirrors how the
// cap-84 self-test builds each kind's Payload from a NameResolution — function
// kinds carry contentHash+length; the search/derivation kinds carry their folded
// survival columns (aob / anchorString / rule / slotCount). Returns false when a
// REQUIRED datum for the kind is missing/malformed (the caller cannot_checks it
// loudly rather than dispatching a check that would itself CannotCheck — both
// surface a defined verdict, but the explicit cannot_check carries a clearer
// reason token).
bool BuildPayload(const refdb::NameResolution& nr, sv::Kind kind,
                  sv::Payload& p, const char*& reasonOut) {
    p.kind = kind;
    switch (kind) {
        case sv::Kind::Function:
        case sv::Kind::FunctionNoSig:
        case sv::Kind::FunctionVariadic:
            p.contentHash = nr.content_hash;  // raw 32-byte blob (empty if NULL).
            p.length = nr.has_length ? static_cast<size_t>(nr.length) : 0;
            return true;
        case sv::Kind::Callsite:
            if (!AobToPayload(nr.aob, p)) { reasonOut = "no_aob"; return false; }
            return true;
        case sv::Kind::StringAnchor:
            if (nr.anchor_string.empty()) { reasonOut = "no_anchor_string"; return false; }
            p.anchorString = nr.anchor_string;
            p.expectUnique = nr.has_expect_unique && nr.expect_unique != 0;
            return true;
        case sv::Kind::InstructionAnchor:
            // The instruction_anchor re-runs the resolver chain from its string
            // anchor; the optional instruction-shape AOB is matched at the
            // resolved site if present. The string is the required datum.
            if (nr.anchor_string.empty()) { reasonOut = "no_anchor_string"; return false; }
            p.anchorString = nr.anchor_string;
            AobToPayload(nr.aob, p);  // optional shape AOB; empty → no shape match.
            return true;
        case sv::Kind::DataSlot:
            if (nr.rule.empty()) { reasonOut = "bad_rule"; return false; }
            p.rule = nr.rule;
            // NOTE: a data_slot derives THROUGH an anchor; the single-row sweep
            // here cannot thread an anchor's resolved RVA (that is CheckOrdered's
            // job over a SET). A data_slot dispatched without its anchor returns
            // CannotCheck "anchor_unresolved" — a DEFINED cannot_check, surfaced.
            return true;
        case sv::Kind::VtableBase:
            if (!(nr.has_slot_count && nr.slot_count > 0)) { reasonOut = "no_slot_count"; return false; }
            p.slotCount = static_cast<uint32_t>(nr.slot_count);
            return true;
        case sv::Kind::VtableIndex:
            // vtable_index is a DEFINED deferral in the dispatch — let it through;
            // SurvivalCheck returns CannotCheck "vtable_index_deferred".
            return true;
    }
    reasonOut = "kind_unknown";
    return false;
}

}  // namespace

const char* VerdictName(Verdict v) {
    switch (v) {
        case Verdict::ResolvesWorks: return "resolves_works";
        case Verdict::WrongTarget:   return "wrong_target";
        case Verdict::Dead:          return "dead";
        case Verdict::CannotCheck:   return "cannot_check";
    }
    return "cannot_check";  // unreachable; the switch is exhaustive.
}

std::vector<RowVerdict> RunStartupVerification() {
    std::vector<RowVerdict> out;

    // Precondition: refdb must be open (the curated cache is the input set). An
    // unloaded refdb yields an empty result with a logged reason — never silent.
    if (!refdb::IsLoaded()) {
        LOG_ERROR_KV(kCategory, "verify_skipped",
            ::kcdx::log::KV("reason", "db_not_loaded"),
            ::kcdx::log::KV("note", "refdb not Open() — the curated set is unavailable; no rows swept"));
        return out;
    }

    // Open the LIVE loaded module ONCE for the reachability range test. Failure
    // is NOT fatal to the sweep. In practice both checks need WHGame mapped: when
    // it is not, the on-disk read ALSO fails (module_not_mapped → CannotCheck), so
    // a row lands cannot_check from the on-disk side before reachability matters —
    // the off-game DEGRADE path, exactly like the 3.2 static checks.
    pe::ModuleView view;
    bool liveMapped = pe::OpenModule(L"WHGame.dll", view);
    if (!liveMapped) {
        LOG_WARN_KV(kCategory, "live_module_not_mapped",
            ::kcdx::log::KV("reason", "module_not_mapped"),
            ::kcdx::log::KV("module", "WHGame.dll"),
            ::kcdx::log::KV("note", "live image not mapped — every row's reachability degrades to cannot_check"));
    }

    const std::string runningVer = kcdx::plugins::g_runtimeGameVersionString;

    size_t resolvesWorks = 0, wrongTarget = 0, dead = 0, cannotCheck = 0;

    // Sweep every cached curated entity. ForEachCached gives the resolved id +
    // name + the engine-resolved VA (WhgameBase()+rva) + verification state. The
    // VA is the reachability input (AP1 — engine-resolved, never hardcoded).
    refdb::ForEachCached(
        [&](uint64_t kcdx_id, const std::string& name, uintptr_t va,
            refdb::NameResolution::VerificationState /*state*/) -> bool {
            RowVerdict rv;
            rv.kcdx_id = kcdx_id;
            rv.name = name;
            rv.resolved_version = runningVer;

            // Pull the full resolution for the kind + the fingerprint datum + the
            // folded survival columns. ResolveByName runs the same supersession
            // walk the cache used; the same resolved row.
            refdb::NameResolution nr = refdb::ResolveByName(name);
            if (!nr.found) {
                // The name the cache yielded does not re-resolve — a cache/DB
                // inconsistency. Loud cannot_check, never a faked verdict.
                rv.verdict = Verdict::CannotCheck;
                rv.detail = "name_unresolved";
                LOG_WARN_KV(kCategory, "row_cannot_check",
                    ::kcdx::log::KV("kcdx_id", (unsigned long long)kcdx_id),
                    ::kcdx::log::KV("name", name),
                    ::kcdx::log::KV("reason", "name_unresolved"));
                ++cannotCheck;
                out.push_back(std::move(rv));
                return true;
            }

            // --- Decode the kind + build the Payload. -----------------------
            sv::Kind kind;
            if (!DecodeKind(nr.kind, kind)) {
                rv.verdict = Verdict::CannotCheck;
                rv.detail = "kind_unknown";
                LOG_WARN_KV(kCategory, "row_cannot_check",
                    ::kcdx::log::KV("kcdx_id", (unsigned long long)kcdx_id),
                    ::kcdx::log::KV("name", name),
                    ::kcdx::log::KV("kind", nr.kind),
                    ::kcdx::log::KV("reason", "kind_unknown"));
                ++cannotCheck;
                out.push_back(std::move(rv));
                return true;
            }

            sv::Payload payload;
            const char* buildReason = "build_failed";
            if (!BuildPayload(nr, kind, payload, buildReason)) {
                rv.verdict = Verdict::CannotCheck;
                rv.detail = buildReason;
                LOG_WARN_KV(kCategory, "row_cannot_check",
                    ::kcdx::log::KV("kcdx_id", (unsigned long long)kcdx_id),
                    ::kcdx::log::KV("name", name),
                    ::kcdx::log::KV("kind", nr.kind),
                    ::kcdx::log::KV("reason", buildReason));
                ++cannotCheck;
                out.push_back(std::move(rv));
                return true;
            }

            // --- Check 1: version-applicability (ON-DISK) — REUSE the 3.1/3.2
            // dispatch. The dll selector is the default module (WHGame.dll on
            // disk). Verdict: Unchanged → the on-disk bytes match the candidate
            // fingerprint; Changed/Ambiguous → they do NOT; CannotCheck → a
            // precondition failed (no content_hash, module not mapped, etc.).
            sv::Result onDisk = sv::SurvivalCheck(payload, static_cast<uint32_t>(nr.rva),
                                                  /*dll=*/std::string());

            // --- Check 2: reachability (LIVE IMAGE) — does the engine-resolved
            // VA land in live .text? A RANGE TEST, NOT a body hash (Probe 0.4).
            bool reachable = liveMapped && pe::IsVaInLiveText(view, va);

            // --- D34 attribution: when the on-disk bytes matched the candidate
            // (Unchanged), the matched address_version row is the picked row for
            // this entity. Surface its id. (USER projection: one resolved row per
            // entity, so the candidate set is that one row; the matched id is its
            // address_versions.id. A multi-candidate gap-pass — DEV/Phase 5 —
            // would enumerate all of an entity's version rows and match each;
            // the entry-point shape already returns the matched id either way.)
            if (onDisk.status == sv::Status::Unchanged) {
                int64_t avId = 0;
                if (refdb::CachedAddressVersionId(kcdx_id, avId)) {
                    rv.has_matched_id = true;
                    rv.matched_address_version_id = static_cast<uint64_t>(avId);
                }
            }

            // --- Combine into the per-row verdict (the four D25 meanings). ----
            if (onDisk.status == sv::Status::CannotCheck) {
                // A precondition failed (no content_hash, module-not-mapped on the
                // on-disk read, anchor_unresolved for a data_slot, …) — fail-loud
                // cannot_check carrying the survival reason token. NEVER a faked
                // resolves_works (AP14).
                rv.verdict = Verdict::CannotCheck;
                rv.detail = onDisk.reason.empty() ? "cannot_check" : onDisk.reason;
                ++cannotCheck;
            } else if (onDisk.status == sv::Status::Changed ||
                       onDisk.status == sv::Status::Ambiguous) {
                // The on-disk bytes match NO candidate's fingerprint → the bytes
                // at this DB-recorded address are not the recorded entity on this
                // build → wrong_target (matched id = none).
                rv.verdict = Verdict::WrongTarget;
                rv.has_matched_id = false;
                rv.detail = (onDisk.status == sv::Status::Ambiguous)
                                ? "ambiguous_no_unique_match" : "fingerprint_mismatch";
                ++wrongTarget;
            } else {  // sv::Status::Unchanged — the on-disk fingerprint matched.
                if (reachable) {
                    rv.verdict = Verdict::ResolvesWorks;
                    rv.detail = "matched_and_in_live_text";
                    ++resolvesWorks;
                } else {
                    // On-disk hash matches but the live resolve does not land in
                    // .text (0 / off-image / non-.text, or the module is not
                    // mapped). The signal the on-disk check alone cannot produce.
                    rv.verdict = Verdict::Dead;
                    rv.detail = liveMapped ? "resolved_va_not_in_live_text"
                                           : "live_module_not_mapped";
                    ++dead;
                }
            }

            LOG_DEBUG_KV(kCategory, "row_verdict",
                ::kcdx::log::KV("kcdx_id", (unsigned long long)kcdx_id),
                ::kcdx::log::KV("name", name),
                ::kcdx::log::KV("kind", nr.kind),
                ::kcdx::log::KV("verdict", VerdictName(rv.verdict)),
                ::kcdx::log::KV("matched_av_id",
                    rv.has_matched_id ? (long long)rv.matched_address_version_id : (long long)-1),
                ::kcdx::log::KV("detail", rv.detail.empty() ? "-" : rv.detail.c_str()));

            out.push_back(std::move(rv));
            return true;  // continue the sweep.
        });

    // One lifecycle summary line (the teardown rollup — logging.md / the
    // observability summary floor). One info line for the whole sweep.
    LOG_INFO_KV(kCategory, "verify_complete",
        ::kcdx::log::KV("rows", (unsigned long long)out.size()),
        ::kcdx::log::KV("resolves_works", (unsigned long long)resolvesWorks),
        ::kcdx::log::KV("wrong_target", (unsigned long long)wrongTarget),
        ::kcdx::log::KV("dead", (unsigned long long)dead),
        ::kcdx::log::KV("cannot_check", (unsigned long long)cannotCheck),
        ::kcdx::log::KV("live_mapped", liveMapped ? "yes" : "no"));

    return out;
}

}  // namespace kcdx::survival_verify
