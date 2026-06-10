#include "survival_verify.h"

#include <cstring>    // strcmp / strncmp (engine-stamp identity on a chain entry)
#include <exception>  // std::exception (ParsePattern throw)
#include <mutex>      // the invocation-record store guard
#include <string>
#include <unordered_set>  // the CALLED-by-kcdx invocation record
#include <utility>    // std::move
#include <vector>

#include "cvar.h"                  // GetInt/GetFloat — the rank-2 cvar safe-read
                                  // reuses the existing read-only production
                                  // accessor (config-value read, zero mutation).
#include "hook_chain.h"            // GetAllChainTargets — live engine/plugin chain entries
#include "log.h"
#include "modification_inventory.h"  // FireRecord / LastFires — the detour fire breadcrumb ring
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

// The verification-method ranks this static pass produces (weakest→strongest:
// 5 resolution, 4 on-disk version-applicability hash, 3 loaded-image
// reachability, 2 safe-read, 1 observed execution). This pass runs ranks 4 and
// 3; ranks 1–2 are added by later steps. A row whose strongest method could not
// even attempt (no kind / no payload) carries the rank of the method it was
// going to attempt — the on-disk version-applicability check, rank 4.
constexpr int kRankOnDiskHash    = 4;  // on-disk version-applicability fingerprint.
constexpr int kRankReachability  = 3;  // live-image .text range test.
// Rank 2 — safe-read exercise: read the row's live target with ZERO mutation (a
// cvar value read, a read-only vtable walk). Caps at PassedNotVerified — a sane
// read proves the target yields a plausible value, NOT that its behavior works,
// so it stays below rank-1 observed execution. The vtable_base read-only
// LOADED-IMAGE walk (each entry resolves into live .text) is a rank-3 method
// (§11.6) — a stronger live read than the base-VA reachability range test, but
// still a static-class read capped at PassedNotVerified.
constexpr int kRankSafeRead      = 2;  // cvar live-value read (zero mutation).
// Rank 1 — observed live execution: the function fired in the running process
// AND passed through correctly (the engine's own hook-chain fire breadcrumb, OR
// kcdx's own production call that already ran). The ONLY rank that awards
// verified_working. Produced by the observation tier below, NOT by the static
// MapStaticVerdict (which caps at PassedNotVerified, rank 3).
constexpr int kRankObservedExecution = 1;

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
        case Verdict::VerifiedWorking:   return "verified_working";
        case Verdict::PassedNotVerified: return "passed_not_verified";
        case Verdict::Failed:            return "failed";
        case Verdict::NotApplicable:     return "not_applicable";
        case Verdict::CannotCheck:       return "cannot_check";
        case Verdict::Skipped:           return "skipped";
        case Verdict::Error:             return "error";
    }
    return "cannot_check";  // unreachable; the switch is exhaustive.
}

const char* InvokeSkipReasonName(InvokeSkipReason r) {
    switch (r) {
        case InvokeSkipReason::None:             return "none";
        case InvokeSkipReason::UnsafeToCall:     return "unsafe_to_call";
        case InvokeSkipReason::Uncontainable:    return "uncontainable";
        case InvokeSkipReason::NotACallableKind: return "not_a_callable_kind";
    }
    return "none";  // unreachable; the switch is exhaustive.
}

KindDispatch DispatchForKind(sv::Kind kind) {
    KindDispatch d;
    switch (kind) {
        // function — the FLOOR is the foreign-uncallable case (§11.6 row 4):
        // rank-4 static, invoke_attempted false, unsafe_to_call. The sweep
        // REFINES this at runtime: a rank-1 HOOKED/CALLED observation (rows 1-2:
        // invoke_attempted false, None) or a rank-2 cvar safe-read (row 3:
        // invoke_attempted true, None) overrides the floor when those tiers reach
        // the row. The ceiling rank is the top a function CAN reach (rank 1).
        case sv::Kind::Function:
            d.method = DispatchMethod::ObservedThenStatic;
            d.ceilingRank = kRankObservedExecution;  // a function CAN reach rank-1.
            d.invokeAttempted = false;
            d.invokeSkipReason = InvokeSkipReason::UnsafeToCall;  // foreign-uncallable floor.
            return d;
        // function_no_sig — rank-4 static (no ABI to call) → not_a_callable_kind.
        case sv::Kind::FunctionNoSig:
            d.method = DispatchMethod::StaticOnly;
            d.ceilingRank = kRankOnDiskHash;
            d.invokeAttempted = false;
            d.invokeSkipReason = InvokeSkipReason::NotACallableKind;
            return d;
        // function_variadic — rank-4 static, unsafe_to_call (an ABI, no zero-risk
        // synthetic call shape).
        case sv::Kind::FunctionVariadic:
            d.method = DispatchMethod::StaticOnly;
            d.ceilingRank = kRankOnDiskHash;
            d.invokeAttempted = false;
            d.invokeSkipReason = InvokeSkipReason::UnsafeToCall;
            return d;
        // callsite — rank-3/4 static (AOB uniqueness + reachability).
        case sv::Kind::Callsite:
            d.method = DispatchMethod::StaticOnly;
            d.ceilingRank = kRankReachability;
            d.invokeAttempted = false;
            d.invokeSkipReason = InvokeSkipReason::NotACallableKind;
            return d;
        // string_anchor — rank-4 static (.rdata literal + xref).
        case sv::Kind::StringAnchor:
            d.method = DispatchMethod::StaticOnly;
            d.ceilingRank = kRankOnDiskHash;
            d.invokeAttempted = false;
            d.invokeSkipReason = InvokeSkipReason::NotACallableKind;
            return d;
        // instruction_anchor — rank-3/4 static (resolver-chain derivation).
        case sv::Kind::InstructionAnchor:
            d.method = DispatchMethod::StaticOnly;
            d.ceilingRank = kRankReachability;
            d.invokeAttempted = false;
            d.invokeSkipReason = InvokeSkipReason::NotACallableKind;
            return d;
        // data_slot — rank-5 derivation (no content hash).
        case sv::Kind::DataSlot:
            d.method = DispatchMethod::StaticOnly;
            d.ceilingRank = 5;  // derivation/existence rank.
            d.invokeAttempted = false;
            d.invokeSkipReason = InvokeSkipReason::NotACallableKind;
            return d;
        // vtable_base — rank-3 via the read-only entry-walk (REPLACES the base-VA
        // range test). The walk IS the §11.6 rank-3 reachability.
        case sv::Kind::VtableBase:
            d.method = DispatchMethod::VtableBaseWalk;
            d.ceilingRank = kRankReachability;
            d.invokeAttempted = false;
            d.invokeSkipReason = InvokeSkipReason::NotACallableKind;
            return d;
        // vtable_index — deferred (cannot_check; no live-vtable resolution path).
        case sv::Kind::VtableIndex:
            d.method = DispatchMethod::Deferred;
            d.ceilingRank = 0;  // no method reaches a verdict above cannot_check.
            d.invokeAttempted = false;
            d.invokeSkipReason = InvokeSkipReason::NotACallableKind;
            return d;
    }
    // Unreachable (the switch is exhaustive over the 9 kinds) — the safe default
    // is the not-a-callable static floor.
    d.method = DispatchMethod::StaticOnly;
    d.ceilingRank = kRankOnDiskHash;
    d.invokeAttempted = false;
    d.invokeSkipReason = InvokeSkipReason::NotACallableKind;
    return d;
}

StaticVerdict MapStaticVerdict(bool versionGap, sv::Status onDiskStatus,
                               const std::string& onDiskReason, bool reachable,
                               bool liveMapped) {
    StaticVerdict r;

    // The version-applicability check ran and found the running build is NOT
    // covered by this row — a version gap. Wins first: a row not applicable to
    // this build is not condemned on its bytes. Distinct from cannot_check
    // (inputs missing) and from failed (a divergence on a COVERED version).
    if (versionGap) {
        r.verdict = Verdict::NotApplicable;
        r.method_rank = kRankOnDiskHash;
        r.detail = "version_not_covered";
        return r;
    }

    if (onDiskStatus == sv::Status::CannotCheck) {
        // The attempt ran but the row lacks the inputs the check needs (no
        // content_hash, module-not-mapped on the on-disk read, anchor_unresolved,
        // a deferred kind like vtable_index, …). fail-loud cannot_check carrying
        // the survival reason token. NEVER a faked pass (a check that cannot run
        // must say so, never fabricate a passing verdict).
        r.verdict = Verdict::CannotCheck;
        r.method_rank = kRankOnDiskHash;
        r.detail = onDiskReason.empty() ? "cannot_check" : onDiskReason;
        return r;
    }

    if (onDiskStatus == sv::Status::Changed || onDiskStatus == sv::Status::Ambiguous) {
        // The on-disk bytes match NO candidate's fingerprint on a COVERED version
        // → an attempt the row should pass returned wrong → Failed. The on-disk
        // hash (rank 4) is the method that found the divergence.
        r.verdict = Verdict::Failed;
        r.method_rank = kRankOnDiskHash;
        r.detail = (onDiskStatus == sv::Status::Ambiguous)
                       ? "ambiguous_no_unique_match" : "fingerprint_mismatch";
        return r;
    }

    // sv::Status::Unchanged — the on-disk fingerprint matched.
    if (reachable) {
        // Both static methods ran and passed; the strongest is reachability
        // (rank 3). Caps at PassedNotVerified — a passing hash + reachability is
        // real evidence but NOT proof the code runs, so it cannot claim
        // VerifiedWorking (the ceiling rule).
        r.verdict = Verdict::PassedNotVerified;
        r.method_rank = kRankReachability;
        r.detail = "matched_and_in_live_text";
        return r;
    }

    // On-disk hash matches but the live resolve does not land in .text (0 /
    // off-image / non-.text, or the module is not mapped). The reachability
    // method (rank 3) ran and FAILED → Failed overrides downward; the rank is
    // the failing method (3).
    r.verdict = Verdict::Failed;
    r.method_rank = kRankReachability;
    r.detail = liveMapped ? "resolved_va_not_in_live_text" : "live_module_not_mapped";
    return r;
}

ObservedExecution ObserveHookedExecution(uintptr_t va) {
    ObservedExecution obs;
    if (va == 0) {
        obs.detail = "no_va";
        return obs;  // a row that did not resolve has no observable hook site.
    }

    // (1) Is an ENGINE-stamped chain entry installed on this VA? The engine's
    // own hooks register via AddCEngine (pluginName "kcdx", name "engine.<site>")
    // and are the only chain entries this rank-1 tier observes — a plugin hook on
    // a curated target is not kcdx's own observation. GetAllChainTargets reports
    // the owning entry's (pluginName, hookName) per chain VA; an engine entry is
    // identified by pluginName == "kcdx" with an "engine." name prefix (the
    // AddCEngine convention). The match is by VA — the engine entry's resolved
    // target VA equals this row's engine-resolved VA — never by name string.
    for (const auto& t : kcdx::hook_chain::GetAllChainTargets()) {
        if (t.va != va) continue;
        const bool engineStamped =
            t.pluginName && std::strcmp(t.pluginName, "kcdx") == 0 &&
            t.hookName && std::strncmp(t.hookName, "engine.", 7) == 0;
        if (engineStamped) {
            obs.hasChainEntry = true;
            break;
        }
    }
    if (!obs.hasChainEntry) {
        // No engine hook on this VA — nothing kcdx observes the game firing.
        // NOT a fabricated verdict: the caller falls through to the static
        // ceiling. (A row hooked only by a plugin, or not hooked at all, is
        // observed by some other method, not this one.)
        obs.detail = "no_engine_chain_entry";
        return obs;
    }

    // (2) Did the game actually FIRE that detour this session? Read the live
    // detour fire breadcrumb ring (the same ring cap-47 reads) for a recorded
    // fire AT this VA. A fire is an OBSERVED FACT (RecordFire stores the chain's
    // target VA on each DispatchPre/Post), never an assumption that the hook
    // "should" fire. The newest matching seq is the evidence; seq 0 is an empty
    // slot the dump skips, so any matching record proves a real fire.
    namespace mi = kcdx::modification_inventory;
    mi::FireRecord fires[mi::kFireRingSize];
    const unsigned n = mi::LastFires(fires, mi::kFireRingSize);
    for (unsigned i = 0; i < n; ++i) {
        if (fires[i].targetVa == va && fires[i].seq != 0) {
            obs.fireSeq = fires[i].seq;  // LastFires is newest-first → first hit is newest.
            break;
        }
    }
    if (obs.fireSeq == 0) {
        // The engine hook is installed but has NOT fired this session yet (e.g.
        // a save/load detour before any save loads). NOT observed → the caller
        // keeps the static ceiling; this is the design's "no observed fire ⇒ not
        // rank-1" path, not a failure.
        obs.detail = "engine_hook_not_fired";
        return obs;
    }

    // Engine hook on the VA AND a recorded fire this session → observed live
    // execution (the game ran the function, the detour passed through). Rank-1.
    obs.observed = true;
    obs.detail = "observed_engine_hook_fire";
    return obs;
}

bool ObservedToVerdict(const ObservedExecution& obs, StaticVerdict& out) {
    if (!obs.observed) return false;  // no observation → static ceiling stands.
    // Observed live execution is the ONLY method that awards verified_working —
    // the top rung of the ceiling. It OVERRIDES the static result (a passing
    // static check capped at passed_not_verified) UPWARD to rank-1.
    out.verdict = Verdict::VerifiedWorking;
    out.method_rank = kRankObservedExecution;
    out.detail = obs.detail.empty() ? "observed_engine_hook_fire" : obs.detail;
    return true;
}

// --- CALLED-by-kcdx invocation record ---------------------------------------
// A process-lifetime set of curated-target VAs kcdx invoked + that returned
// this session. The CALLED-by-kcdx rank-1 signal: written by the production
// call sites AFTER their real call returns, read by the sweep. Guarded by a
// mutex — the recorders run on the engine/main thread (cvar reads, console
// registration/exec), the reader runs in the startup sweep; one lock keeps the
// set consistent across both. The set is tiny (the handful of curated targets
// kcdx itself calls — the §11.6 CALLED set) and these sites are NOT hot paths
// (one-shot config reads / console registration / command execution), so the
// lock + insert is free.
namespace {
std::mutex                     g_invokedMutex;
std::unordered_set<uintptr_t>  g_invokedVas;
}  // namespace

void RecordKcdxInvocation(uintptr_t va) {
    if (va == 0) return;  // an unresolved target has no curated-row VA to key on.
    std::lock_guard<std::mutex> lock(g_invokedMutex);
    g_invokedVas.insert(va);
}

bool WasInvokedByKcdx(uintptr_t va) {
    if (va == 0) return false;
    std::lock_guard<std::mutex> lock(g_invokedMutex);
    return g_invokedVas.count(va) != 0;
}

void ResetInvocationRecord() {
    std::lock_guard<std::mutex> lock(g_invokedMutex);
    g_invokedVas.clear();
}

// --- Rank-2/3 SAFE-READ tier -------------------------------------------------
// Reads a row's live target with ZERO mutation; caps at PassedNotVerified (a sane
// read proves a plausible value, not that behavior works — below rank-1 observed
// execution). The cvar read reaches a known game cvar through the getter's
// existing read-only production accessor; the vtable_base walk reads loaded-image
// memory only (no call).

namespace {
// A stable, boot-present game cvar used to exercise a getter row's live read with
// zero mutation. sys_pakPriority is a confirmed boot-present int cvar (the same
// known-good read target cap-71/cap-72 use); reading it asserts the getter yields
// a plausible value without depending on any maintainer-set value. NOT a game
// address (AP1 exception): a cvar NAME string the read resolves through the DB.
constexpr const char* kSafeReadCvar = "sys_pakPriority";

// The curated cvar-getter row names (the §11.6 safe-read getter set). A row whose
// name is one of these is a rank-2 cvar-read target — int via GetIVal, float via
// GetFVal. Matched by the resolved (post-supersession) name the sweep carries.
constexpr const char* kCvarGetterInt   = "ICVar_GetIVal";
constexpr const char* kCvarGetterFloat = "ICVar_GetFVal";
}  // namespace

SafeReadResult SafeReadCvarGetter(const std::string& getterName) {
    SafeReadResult r;
    // Read a known game cvar through the getter's existing production accessor —
    // a config-value read, ZERO mutation. GetInt/GetFloat return false when the
    // cvar surface is not ready yet (cvar::Init not run / console not up at this
    // sweep's timing) → the read did NOT run → attempted=false → the caller keeps
    // the static ceiling (the degrade-safe path: the boot/console sweep may reach
    // this row before the cvar surface comes up).
    if (getterName == kCvarGetterInt) {
        int v = 0;
        if (!kcdx::cvar::GetInt(kSafeReadCvar, &v)) {
            // The accessor returned false — surface unready, or the known cvar
            // did not resolve. The read did not produce a value. attempted stays
            // false (degrade); NOT a Failed (a read that could not run neither
            // passes nor fails the row).
            r.detail = "cvar_read_unavailable";
            return r;
        }
        // The accessor returned true → it ran AND a value came back. A value
        // returning at all is the plausible-return signal for a config getter
        // (the no-garbage-write contract: GetInt returns false without writing on
        // any miss, so a true return means a real read). Zero-mutation: a config
        // read changes nothing.
        r.attempted = true;
        r.sane = true;
        r.detail = "cvar_int_read_sane";
        return r;
    }
    if (getterName == kCvarGetterFloat) {
        float v = 0.0f;
        if (!kcdx::cvar::GetFloat(kSafeReadCvar, &v)) {
            r.detail = "cvar_read_unavailable";
            return r;
        }
        r.attempted = true;
        r.sane = true;
        r.detail = "cvar_float_read_sane";
        return r;
    }
    // Not a cvar-getter row — this method does not apply. attempted=false → the
    // caller keeps the static ceiling.
    r.detail = "not_a_cvar_getter";
    return r;
}

SafeReadResult WalkVtableBaseLive(const pe::ModuleView& view, uintptr_t baseVa,
                                  uint32_t slotCount) {
    SafeReadResult r;
    if (baseVa == 0 || slotCount == 0) {
        // No resolved table base / no expected slot count — the walk cannot run.
        // attempted=false → keep the static ceiling (degrade).
        r.detail = "vtable_walk_no_base_or_count";
        return r;
    }
    // The base VA itself must land in a readable image range before the walk —
    // the table sits in .rdata, not .text, so test the base against the live
    // image bounds (the ModuleView span), NOT IsVaInLiveText (that is for the
    // .text ENTRIES). A base outside the mapped image cannot be walked.
    const uintptr_t imgLo = reinterpret_cast<uintptr_t>(view.baseBytes);
    const uintptr_t imgHi = imgLo + view.size;
    const size_t tableBytes = static_cast<size_t>(slotCount) * 8;
    if (imgLo == 0 || baseVa < imgLo || baseVa + tableBytes > imgHi) {
        // The N-qword span is not within the mapped image — the table moved /
        // shrank in the live image, or the base did not resolve into the module.
        // The walk RAN (we attempted to read) and found the shape broken →
        // attempted=true, sane=false → Failed (the live table shape diverged).
        r.attempted = true;
        r.sane = false;
        r.detail = "vtable_base_span_off_image";
        return r;
    }
    // Read each of the N qwords from the LIVE loaded image and assert each entry
    // resolves into live .text (a plausible relocated code pointer). READ-ONLY —
    // no call. A non-.text / null entry means the live table shape broke.
    const uint8_t* base = reinterpret_cast<const uint8_t*>(baseVa);
    for (uint32_t i = 0; i < slotCount; ++i) {
        uintptr_t entry = 0;
        std::memcpy(&entry, base + static_cast<size_t>(i) * sizeof(uintptr_t),
                    sizeof(uintptr_t));
        if (!pe::IsVaInLiveText(view, entry)) {
            // An entry that is not a live .text pointer — the table shape broke
            // in the live image. The walk ran + found the divergence → Failed.
            r.attempted = true;
            r.sane = false;
            r.detail = "vtable_entry_not_in_live_text";
            return r;
        }
    }
    // All N entries resolved into live .text → a sane read-only walk.
    r.attempted = true;
    r.sane = true;
    r.detail = "vtable_all_entries_in_live_text";
    return r;
}

bool SafeReadToVerdict(const SafeReadResult& sr, int passRank, int failedRank,
                       StaticVerdict& out) {
    if (!sr.attempted) return false;  // read did not run → static ceiling stands.
    if (sr.sane) {
        // A sane read caps at PassedNotVerified — NEVER VerifiedWorking (only
        // rank-1 observed execution earns the top rung; the ceiling rule). The
        // rank is the safe-read method that ran (2 for cvar, 3 for the vtable
        // walk).
        out.verdict = Verdict::PassedNotVerified;
        out.method_rank = passRank;
        out.detail = sr.detail.empty() ? "safe_read_sane" : sr.detail;
        return true;
    }
    // The read ran but faulted / returned implausible — a divergence the row
    // should have passed → Failed at the method that ran (a faulted read is
    // failed, never a pass).
    out.verdict = Verdict::Failed;
    out.method_rank = failedRank;
    out.detail = sr.detail.empty() ? "safe_read_failed" : sr.detail;
    return true;
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

    // Per-verdict tallies for the one teardown summary line (the 7-state enum).
    // verified_working is now reachable via either rank-1 observed sub-path: a
    // curated engine-HOOKED row whose hook fired this session, OR a curated
    // CALLED-by-kcdx row whose production call ran + returned this session — both
    // awarded rank-1 by the observation tier (the rest of the static states are
    // produced by the ceiling rule).
    size_t verifiedWorking = 0, passedNotVerified = 0, failed = 0,
           notApplicable = 0, cannotCheck = 0, errored = 0;

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

            // The static checks attempt the on-disk version-applicability method
            // first (rank 4); a row that cannot even build its payload still
            // carries that attempted rank. The verdict mapping below raises the
            // rank to reachability (3) when that stronger method also ran.
            rv.method_rank = kRankOnDiskHash;

            // The whole per-row check runs under a catch: a fault inside the
            // dispatch / on-disk read / reachability test is the HARNESS faulting
            // on this row, NOT the row being wrong — that is `error`, distinct
            // from `failed`: the ROW may be fine, the TEST blew up.
            try {
            // Pull the full resolution for the kind + the fingerprint datum + the
            // folded survival columns. ResolveByName runs the same supersession
            // walk the cache used; the same resolved row.
            refdb::NameResolution nr = refdb::ResolveByName(name);
            if (!nr.found) {
                // The name the cache yielded does not re-resolve — a cache/DB
                // inconsistency. Loud cannot_check, never a faked verdict. The kind
                // is undeterminable, so the invoke posture is not_a_callable_kind
                // (nothing to call — never a blank posture, D36).
                rv.verdict = Verdict::CannotCheck;
                rv.detail = "name_unresolved";
                rv.invoke_skip_reason = InvokeSkipReason::NotACallableKind;
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
                rv.invoke_skip_reason = InvokeSkipReason::NotACallableKind;
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
                // A cannot_check from a missing input still carries the kind's
                // §11.6 invoke posture (an active attempt was selected; the input
                // was absent) — never a blank posture (D36: every row gets a
                // structured response).
                KindDispatch disp = DispatchForKind(kind);
                rv.invoke_attempted = disp.invokeAttempted;
                rv.invoke_skip_reason = disp.invokeSkipReason;
                LOG_WARN_KV(kCategory, "row_cannot_check",
                    ::kcdx::log::KV("kcdx_id", (unsigned long long)kcdx_id),
                    ::kcdx::log::KV("name", name),
                    ::kcdx::log::KV("kind", nr.kind),
                    ::kcdx::log::KV("reason", buildReason));
                ++cannotCheck;
                out.push_back(std::move(rv));
                return true;
            }

            // --- The §11.6 per-kind DISPATCH descriptor — the strongest method
            // this kind attempts + its DEFAULT invoke posture (the foreign-
            // uncallable floor for the function kind; the observed/safe-read tiers
            // below refine it). Set the default posture now; the per-method
            // refinement below overrides it when a stronger tier reaches the row.
            const KindDispatch disp = DispatchForKind(kind);
            rv.invoke_attempted = disp.invokeAttempted;
            rv.invoke_skip_reason = disp.invokeSkipReason;

            // --- Version-applicability: is the running build's version covered
            // by this row's picked interval at all? interval_covers_version is the
            // PRECISE signal — valid_from <= V <= valid_through on the picked row.
            // A genuine version GAP is the running build falling OUTSIDE that
            // interval (no covering row) → the version-applicability check RAN and
            // found non-coverage → `not_applicable` for this build (distinct from a
            // fingerprint mismatch, a divergence on a COVERED version → `failed`).
            // A row that DOES cover V but was not freshly re-verified at V resolves
            // verification_state Unverified yet interval_covers_version==true — it
            // is NOT a gap: it flows past this into the static checks and lands
            // passed_not_verified/failed per the ceiling rule. Keying on the
            // resolver's coarse Unverified state would wrongly condemn that
            // covered-but-stale row as not_applicable.
            const bool versionGap = !nr.interval_covers_version;

            // --- Check 1: version-applicability fingerprint (ON-DISK, rank 4) —
            // REUSE the dispatch. The dll selector is the default module
            // (WHGame.dll on disk). Status: Unchanged → the on-disk bytes match
            // the candidate fingerprint; Changed/Ambiguous → they do NOT;
            // CannotCheck → a precondition failed (no content_hash, module not
            // mapped, etc.).
            sv::Result onDisk = sv::SurvivalCheck(payload, static_cast<uint32_t>(nr.rva),
                                                  /*dll=*/std::string());

            // --- Check 2: reachability (LIVE IMAGE, rank 3) — does the row's
            // resolve land in live .text? A RANGE TEST, NOT a body hash: the live
            // image is relocated + kcdx-detoured, so a live-body hash reads a
            // false mismatch for a genuinely-good row.
            //
            // The reachability SIGNAL is per-kind (§11.6, the dispatcher's job):
            //   - VtableBaseWalk → the rank-3 reachability is the read-only
            //     ENTRY-WALK (each of the N table entries resolves into live .text),
            //     NOT IsVaInLiveText(base). A vtable base sits in .rdata, so the
            //     base-VA range test reads "not in .text" and would condemn a
            //     genuinely-good table to `failed` (the carried-forward defect).
            //     The walk REPLACES the base test as this kind's rank-3
            //     reachability — so `reachable` is fed TRUE here (the base test is
            //     skipped), and the AUTHORITATIVE rank-3 verdict comes from the walk
            //     run through the safe-read tier below (sane → passed_not_verified;
            //     a broken entry → failed, with the walk's own detail). When the
            //     live module is not mapped, the on-disk check is CannotCheck, so
            //     MapStaticVerdict degrades to cannot_check BEFORE this matters (the
            //     same off-game degrade the function kinds take).
            //   - every other kind → IsVaInLiveText(va) (the engine-resolved VA
            //     lands in live executable .text), the existing range test.
            bool reachable = (disp.method == DispatchMethod::VtableBaseWalk)
                                 ? true  // the walk decides reachability (below), not the base test.
                                 : (liveMapped && pe::IsVaInLiveText(view, va));

            // --- D34 attribution: when the on-disk bytes matched the candidate
            // (Unchanged), the matched address_version row is the picked row for
            // this entity. Surface its id. (USER projection: one resolved row per
            // entity, so the candidate set is that one row; the matched id is its
            // address_versions.id. A multi-candidate gap-pass — DEV — would
            // enumerate all of an entity's version rows and match each; the
            // entry-point shape already returns the matched id either way.)
            if (onDisk.status == sv::Status::Unchanged) {
                int64_t avId = 0;
                if (refdb::CachedAddressVersionId(kcdx_id, avId)) {
                    rv.has_matched_id = true;
                    rv.matched_address_version_id = static_cast<uint64_t>(avId);
                }
            }

            // --- Combine into the per-row verdict (the 7-state enum + the
            // ceiling rule) — the same MapStaticVerdict the self-test exercises
            // per case. A divergence (fingerprint mismatch / dead resolve) is
            // Failed; a clean pass caps at PassedNotVerified (rank 3); a version
            // gap is NotApplicable; a missing input is CannotCheck.
            StaticVerdict sv_result =
                MapStaticVerdict(versionGap, onDisk.status, onDisk.reason,
                                 reachable, liveMapped);

            // --- Rank-1 OBSERVED-EXECUTION tier — the ceiling's top rung, taken
            // ABOVE the static result. TWO observed sub-paths feed the one rank-1
            // verdict, both zero-invoke-risk (kcdx never mints a synthetic call;
            // the game/kcdx already ran it, we read the record):
            //   HOOKED — the engine's own hook on this row's VA FIRED this session
            //     (a read of the live chain + the detour fire ring).
            //   CALLED-by-kcdx — kcdx's own production call to this row's VA already
            //     ran AND returned (a read of the invocation record the cvar/console
            //     call sites stamp after their real call returns).
            // On a positive observation from EITHER, override the static verdict
            // UPWARD to verified_working (rank 1) — the only method that earns the
            // top rung. On NO observation, the static result stands (the design's
            // "no observed fire/call ⇒ falls to the next-strongest method" — the
            // static ceiling). The override is gated on a non-divergent static
            // result (PassedNotVerified): an observed fire/call never whitewashes a
            // real divergence (a fingerprint mismatch / dead resolve / version gap)
            // — that contradiction keeps the static failed/not_applicable verdict,
            // the more honest signal.
            //
            // §11.6 ceiling gate (the dispatcher): rank-1 is reachable ONLY for the
            // plain `function` kind (§11.6 rows 1-2 — HOOKED/CALLED). Every other
            // kind's §11.6 ceiling is BELOW verified_working (function_no_sig /
            // function_variadic cap at rank-4; the anchors/data_slot/vtable_base at
            // rank 3-5; vtable_index at cannot_check), so the observed tier is gated
            // to the function kind — a non-function kind is never lifted above its
            // §11.6 ceiling (a wrong-method route would be a silent mis-verdict).
            if (sv_result.verdict == Verdict::PassedNotVerified &&
                disp.method == DispatchMethod::ObservedThenStatic) {
                ObservedExecution obs = ObserveHookedExecution(va);  // HOOKED.
                bool observedRank1 = ObservedToVerdict(obs, sv_result);
                if (!observedRank1 && WasInvokedByKcdx(va)) {
                    // CALLED-by-kcdx: a recorded production call that returned is
                    // observed execution — lift to rank-1 through the SAME seam a
                    // synthetic positive observation uses (one rank-1 path).
                    ObservedExecution called;
                    called.observed = true;
                    called.fireSeq = 0;  // no fire-ring seq — this is a CALLED record, not a hook fire.
                    called.detail = "observed_kcdx_called";
                    observedRank1 = ObservedToVerdict(called, sv_result);
                }
                if (observedRank1) {
                    // §11.6 rows 1-2: an observed HOOKED/CALLED row is
                    // invoke_attempted false (the game/kcdx called it — kcdx never
                    // mints a synthetic call) with NO skip reason (it WAS observed,
                    // not skipped). REFINE the function floor (UnsafeToCall) to the
                    // observed posture.
                    rv.invoke_attempted = false;
                    rv.invoke_skip_reason = InvokeSkipReason::None;
                }
            }

            // --- Rank-2/3 SAFE-READ tier — taken ABOVE the static ceiling but
            // BELOW rank-1, and only when rank-1 did NOT observe this row. The
            // dispatcher selects the method per §11.6:
            //   ObservedThenStatic (function) → rank-2 cvar safe-read (a getter row
            //     reads a known game cvar through the existing production accessor;
            //     a value returning sane is the pass, invoke_attempted true).
            //   VtableBaseWalk (vtable_base) → rank-3 read-only LOADED-IMAGE walk
            //     (each of the N entries resolves into live .text). This is the
            //     AUTHORITATIVE rank-3 reachability for vtable_base (the dispatcher
            //     fed reachable=true to MapStaticVerdict above precisely so the
            //     base-VA test does not pre-condemn it; the walk produces the real
            //     verdict here — sane → passed_not_verified rank 3, a broken entry →
            //     failed rank 3 with the walk's own detail, NOT the base-test token).
            // Same divergence-gate as rank-1: only override a non-divergent
            // PassedNotVerified — a safe-read never whitewashes a static failed/
            // not_applicable/cannot_check. A read that RAN but faulted flips the row
            // to Failed (a faulted read is failed, never a pass); a read that could
            // not run (cvar surface unready / not a getter / no base) leaves the
            // ceiling (degrade).
            if (sv_result.verdict == Verdict::PassedNotVerified) {
                if (disp.method == DispatchMethod::ObservedThenStatic) {
                    // The function kind; SafeReadCvarGetter applies only to the
                    // getter names (ICVar_GetIVal/GetFVal) and returns
                    // attempted=false for any other function → the ceiling stands.
                    SafeReadResult sr = SafeReadCvarGetter(name);
                    bool readRan = SafeReadToVerdict(sr, /*passRank=*/kRankSafeRead,
                                                     /*failedRank=*/kRankSafeRead,
                                                     sv_result);
                    if (readRan && sr.sane) {
                        // §11.6 row 3: a cvar getter's rank-2 safe-read is
                        // invoke_attempted TRUE (a read IS an attempt that ran) with
                        // NO skip reason — REFINE the function floor (UnsafeToCall)
                        // to the safe-read posture. A faulted read (readRan &&
                        // !sane) flipped the row to Failed; its posture stays the
                        // floor (the row diverged, not the §11.6 happy-path getter).
                        rv.invoke_attempted = true;
                        rv.invoke_skip_reason = InvokeSkipReason::None;
                    }
                } else if (disp.method == DispatchMethod::VtableBaseWalk) {
                    // The vtable_base rank-3 read-only walk — the authoritative
                    // reachability for this kind. Only when the live image is mapped
                    // (else the walk cannot run → attempted=false → keep the ceiling;
                    // but off-game the on-disk check already CannotCheck'd, so
                    // sv_result is not PassedNotVerified here). slotCount came from
                    // the row's payload (no_slot_count CannotCheck'd upstream).
                    if (liveMapped) {
                        SafeReadResult sr =
                            WalkVtableBaseLive(view, va, payload.slotCount);
                        SafeReadToVerdict(sr, /*passRank=*/kRankReachability,
                                          /*failedRank=*/kRankReachability, sv_result);
                        // The vtable_base invoke posture stays the §11.6 default
                        // (invoke_attempted false, not_a_callable_kind — a read-only
                        // table walk is not a callable kind); the dispatch descriptor
                        // already set it. No refinement.
                    }
                }
            }

            rv.verdict = sv_result.verdict;
            rv.method_rank = sv_result.method_rank;
            rv.detail = sv_result.detail;
            // Attribution (above) sets has_matched_id only on an on-disk
            // Unchanged. PassedNotVerified and a dead-resolve Failed both came
            // from Unchanged and KEEP that id (a dead row still carries the
            // matched row). VerifiedWorking is an UPGRADE of a PassedNotVerified
            // (the on-disk hash matched AND the engine hook was observed firing),
            // so it KEEPS the matched id too. NotApplicable wins over a
            // coincidental on-disk match (the row is not for this build), so it
            // must NOT surface that id — clear it for every verdict that is not
            // one of the on-disk-matched states.
            if (rv.verdict != Verdict::PassedNotVerified &&
                rv.verdict != Verdict::VerifiedWorking &&
                rv.verdict != Verdict::Failed) {
                rv.has_matched_id = false;
            }
            switch (rv.verdict) {
                case Verdict::VerifiedWorking:   ++verifiedWorking;   break;
                case Verdict::PassedNotVerified: ++passedNotVerified; break;
                case Verdict::Failed:            ++failed;            break;
                case Verdict::NotApplicable:     ++notApplicable;     break;
                case Verdict::CannotCheck:       ++cannotCheck;       break;
                default:                         break;  // Error counted in catch.
            }

            LOG_DEBUG_KV(kCategory, "row_verdict",
                ::kcdx::log::KV("kcdx_id", (unsigned long long)kcdx_id),
                ::kcdx::log::KV("name", name),
                ::kcdx::log::KV("kind", nr.kind),
                ::kcdx::log::KV("verdict", VerdictName(rv.verdict)),
                ::kcdx::log::KV("method_rank", (long long)rv.method_rank),
                ::kcdx::log::KV("invoke_attempted", rv.invoke_attempted ? "yes" : "no"),
                ::kcdx::log::KV("invoke_skip_reason",
                    InvokeSkipReasonName(rv.invoke_skip_reason)),
                ::kcdx::log::KV("matched_av_id",
                    rv.has_matched_id ? (long long)rv.matched_address_version_id : (long long)-1),
                ::kcdx::log::KV("detail", rv.detail.empty() ? "-" : rv.detail.c_str()));

            out.push_back(std::move(rv));
            return true;  // continue the sweep.
            } catch (const std::exception& e) {
                // The verification harness faulted on this row (a check threw,
                // caught) — `error`, NOT `failed`: the ROW may be fine, the TEST
                // blew up. The on-disk version-applicability check (rank 4) is the
                // method that was running when it faulted.
                rv.verdict = Verdict::Error;
                rv.method_rank = kRankOnDiskHash;
                rv.has_matched_id = false;
                rv.detail = std::string("harness_fault:") + e.what();
                LOG_ERROR_KV(kCategory, "row_error",
                    ::kcdx::log::KV("kcdx_id", (unsigned long long)kcdx_id),
                    ::kcdx::log::KV("name", name),
                    ::kcdx::log::KV("reason", "harness_fault"),
                    ::kcdx::log::KV("what", e.what()));
                ++errored;
                out.push_back(std::move(rv));
                return true;  // a faulted row does not stop the sweep.
            }
        });

    // One lifecycle summary line (the teardown rollup — logging.md / the
    // observability summary floor). One info line for the whole sweep, tallying
    // the 7-state verdicts the pass can now produce: verified_working (rank-1
    // observed execution, the small engine-hooked-and-fired set) + the static
    // ceiling's states. skipped is produced by the precondition gate (a later
    // step), so it stays 0 here.
    LOG_INFO_KV(kCategory, "verify_complete",
        ::kcdx::log::KV("rows", (unsigned long long)out.size()),
        ::kcdx::log::KV("verified_working", (unsigned long long)verifiedWorking),
        ::kcdx::log::KV("passed_not_verified", (unsigned long long)passedNotVerified),
        ::kcdx::log::KV("failed", (unsigned long long)failed),
        ::kcdx::log::KV("not_applicable", (unsigned long long)notApplicable),
        ::kcdx::log::KV("cannot_check", (unsigned long long)cannotCheck),
        ::kcdx::log::KV("error", (unsigned long long)errored),
        ::kcdx::log::KV("live_mapped", liveMapped ? "yes" : "no"));

    return out;
}

}  // namespace kcdx::survival_verify
