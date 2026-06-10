#pragma once

// survival_verify — the engine STARTUP VERIFICATION PASS (D25 / D34 / US-11).
//
// A DISTINCT sweep from survival_pass (the plugin-targeted apply-pass over a
// plugin's touched function refs). This pass runs ONCE at engine startup over
// the CURATED USER SET (every cached refdb entity) and, per row, combines TWO
// checks into one per-row verdict:
//
//   1. Version-applicability (ON-DISK) — does the body at the row's recorded RVA
//      in the game the user is ACTUALLY running still match the DB fingerprint?
//      This is exactly what survival::SurvivalCheck (the 3.1/3.2 dispatch) does
//      on-disk — REUSED here, not reimplemented. Match → the DB entry is valid
//      for this build → safe to apply. Mismatch → the build diverged from the
//      DB's recorded version → avoid.
//      ATTRIBUTION (D34): the swept bytes are matched against the candidate
//      address_version rows; the entry point reports WHICH row matched (its
//      address_version id, or NONE → wrong_target).
//
//   2. Reachability (LOADED IMAGE) — the half the on-disk browser cannot do:
//      resolve the row's address via the engine path and confirm it lands in the
//      LIVE module's executable .text. This catches an entry whose on-disk hash
//      matches but whose live resolve is dead/wrong. CRITICAL (Probe 0.4): this
//      is a RANGE TEST against live .text (pe::IsVaInLiveText), NOT a hash of the
//      live runtime body — the live image is relocated + kcdx-detoured (kcdx
//      hooks lua_pcall every session), so a live-body hash reads wrong-target for
//      a genuinely-good row. The reachability signal is "does the resolve land in
//      live code", not "do the live bytes match".
//
// NOT in scope here: the JSON report (Phase 4 — the batch plugin serializes this
// structured result) and the batch DB mutation (Phase 5). This pass returns the
// per-row result in-process; a test/batch plugin (or the cap-84 self-test) drives
// it and reads the vector.
//
// STARTUP / install-time, NOT the hot path: it sweeps every curated row once,
// reads WHGame.dll's whole on-disk file per row (via the reused on-disk checks)
// and opens the live module once per row. Allocation is acceptable here (cold
// path); it is NEVER run during gameplay.
//
// Fail-loud: a precondition failure (module not mapped, no content_hash, refdb
// not loaded) is a DEFINED `cannot_check` verdict with a grep-able reason — never
// a faked `passed_not_verified` (a check that cannot run must say so, never
// fabricate a passing verdict).

#include <cstdint>
#include <functional>  // the per-row sink callback (the incremental-flush seam).
#include <string>
#include <vector>

#include "survival.h"  // kcdx::survival::Status — the static-check raw status the
                       // verdict mapping consumes.

namespace kcdx::pe { struct ModuleView; }  // fwd-decl: the rank-3 vtable_base
                                           // read-only walk reads the live image
                                           // via a ModuleView, kept out of this
                                           // header so windows.h does not leak to
                                           // every consumer (pe_helpers.h is
                                           // included in the .cpp).

namespace kcdx::survival_verify {

// The per-row verdict — the 7-state enum. A verdict is the CEILING of the
// strongest verification METHOD that actually ran (the rank ladder below); a
// `Failed` outcome at any rank overrides the ceiling downward to `Failed`. The
// static-only checks this pass runs (on-disk version-applicability hash + live
// reachability) are ranks 4 and 3 of the ladder, so they CAP at
// PassedNotVerified — only an OBSERVED-execution method (rank 1, added by a
// later step) can award VerifiedWorking. This is an in-process enum, not
// serialized here.
enum class Verdict {
    VerifiedWorking,    // observed executing correctly this session (rank-1
                        // only — no static check earns this; reserved for the
                        // live-exercise method a later step adds).
    PassedNotVerified,  // the strongest applicable attempt PASSED but cannot
                        // prove execution (only bytes / resolution / wiring).
                        // The static checks' top — a passing on-disk hash +
                        // reachability caps here, never VerifiedWorking.
    Failed,             // an attempt the row should pass returned wrong —
                        // diverged on-disk bytes (fingerprint mismatch / wrong
                        // target) or a dead/off-.text live resolve. Overrides
                        // the ceiling downward at any rank.
    NotApplicable,      // the version-applicability check RAN and found the
                        // running build's version is NOT covered by this row
                        // (a version gap). Distinct from CannotCheck — the
                        // check ran + found non-coverage, vs. lacked inputs.
    CannotCheck,        // the attempt ran but the row lacks the inputs the check
                        // needs (no content_hash / a deferred kind like
                        // vtable_index / a precondition the on-disk read needs).
    Skipped,            // a precondition for THIS run was not met (produced
                        // upstream by the precondition gate — never by this
                        // static pass; present so the enum is total).
    Error,              // the verification harness ITSELF faulted on this row (a
                        // check threw, caught) — distinct from Failed: the ROW
                        // may be fine, the TEST blew up.
};

// The reason a row's strongest applicable method did NOT mint a synthetic call —
// a STRUCTURED PROPERTY of the response, never a verdict that means "gave up".
// The flag pairs with invoke_attempted on every row: a row that was observed
// (game/kcdx already ran it) or safe-read carries None (no skip — it was either
// observed without invoking, or a read was attempted); a row whose kind cannot
// safely be invoked carries the reason WHY no invoke happened.
enum class InvokeSkipReason {
    None,             // no skip to report — observed (not invoked), or a read WAS
                      // attempted (the safe-read getter). invoke_attempted carries
                      // whether an attempt ran; this stays None for both.
    UnsafeToCall,     // a foreign/variadic function with an ABI but no zero-risk
                      // way to invoke it (kcdx never mints a synthetic call to a
                      // foreign function; the corruption class is uncatchable).
    Uncontainable,    // reserved (D36) — an invoke whose side effects cannot be
                      // contained; no curated kind uses it yet.
    NotACallableKind, // the kind is not a function to call at all (an anchor, a
                      // data slot, a vtable, a no-sig function with no ABI).
};

// One curated row's combined verdict.
struct RowVerdict {
    uint64_t    kcdx_id = 0;          // the curated entity's stable id.
    std::string name;                 // the resolved (post-supersession) name.
    std::string resolved_version;     // the running game version the row resolved at.
    Verdict     verdict = Verdict::CannotCheck;

    // The rank (1–5) of the verification METHOD that produced this verdict — the
    // strongest method that actually ran (weakest→strongest: 5 existence/
    // resolution, 4 on-disk version-applicability hash, 3 loaded-image
    // reachability, 2 safe-read exercise, 1 observed live execution). On a
    // `Failed` it is the rank of the method that found the divergence. The static
    // pass produces ranks 3 and 4; ranks 1–2 are added by later steps. A
    // CannotCheck/Skipped that ran no method carries the rank of the method it
    // attempted (4 — the on-disk version-applicability check). The report carries
    // BOTH the verdict and the rank that produced it.
    int         method_rank = 0;

    // Attribution: the address_version id whose fingerprint the swept on-disk
    // bytes matched. Attribution is computed from the ON-DISK fingerprint match
    // (the Unchanged branch), INDEPENDENT of reachability — so a Failed-by-dead-
    // resolve row (the on-disk hash matched, but the live resolve is off-.text)
    // DOES carry a matched id. has_matched_id=false → no candidate matched, OR
    // cannot_check (the on-disk check never produced a match). A 0 id is a real
    // value, so the flag distinguishes "matched id 0" from "none".
    bool        has_matched_id = false;
    uint64_t    matched_address_version_id = 0;

    std::string detail;               // a human-readable + grep-able reason token
                                      // (the survival reason, or the failed/
                                      // not_applicable/cannot_check cause).

    // The §11.6 invoke posture for this row — a structured property of the
    // response (D36's `triple + a flag`). `invoke_attempted` is true ONLY for the
    // safe-read getter path (a read IS an attempt that ran); it is false for an
    // OBSERVED row (the game/kcdx already ran it — kcdx never mints a synthetic
    // call) and for every non-callable kind. `invoke_skip_reason` names WHY no
    // invoke happened: None when observed or a read was attempted; UnsafeToCall
    // for a foreign/variadic function; NotACallableKind for an anchor / data slot
    // / vtable / no-sig function. The per-kind dispatcher sets these from the
    // §11.6 matrix; the function kind refines its posture by which method won.
    bool             invoke_attempted = false;
    InvokeSkipReason invoke_skip_reason = InvokeSkipReason::None;
};

// The outcome of the static-method verdict mapping for ONE row — the ceiling
// arithmetic this pass owns, lifted out of the sweep loop so it is directly
// exercisable per case. Carries the verdict, the rank of the method that
// produced it, and the grep-able detail token.
struct StaticVerdict {
    Verdict     verdict = Verdict::CannotCheck;
    int         method_rank = 0;
    std::string detail;
};

// Map the two STATIC checks' raw outcomes onto the 7-state verdict + method_rank
// — the ceiling rule. Inputs:
//   versionGap   — the version-applicability check ran and found the running
//                  build's version is NOT covered by this row (resolver state
//                  Unverified). Wins first → NotApplicable (rank 4).
//   onDiskStatus — the on-disk fingerprint (rank 4): Unchanged matched /
//                  Changed|Ambiguous diverged / CannotCheck a missing input.
//   reachable    — the engine-resolved VA landed in live .text (rank 3).
// The verdict is the CEILING of the strongest method that ran; a divergence at
// any rank (a fingerprint mismatch, or a dead resolve) is Failed and overrides
// downward. A clean pass (hash matched + reachable) caps at PassedNotVerified
// (rank 3) — never VerifiedWorking (that needs rank-1 observed execution). A
// HARNESS fault is NOT mapped here (it is Error, produced by the sweep's catch);
// Skipped is produced upstream by the precondition gate. This function never
// returns VerifiedWorking, Skipped, or Error — only the static-reachable states.
StaticVerdict MapStaticVerdict(bool versionGap, kcdx::survival::Status onDiskStatus,
                               const std::string& onDiskReason, bool reachable,
                               bool liveMapped);

// =====================================================================
// The §11.6 per-kind verification MATRIX — the dispatcher.
// =====================================================================
//
// The data-side companion to D36's enum + rank ladder: for each of the 9 curated
// kinds, the strongest method the sweep ATTEMPTS for that kind, the verdict
// CEILING that method can reach, and the kind's DEFAULT invoke posture. The
// dispatcher routes each swept row to the strongest applicable method its kind
// permits and lets the ceiling rule (MapStaticVerdict + the observed/safe-read
// tiers) produce the verdict — so every row gets an ACTIVE attempt and a
// structured response (no kind is a passive non-result).
//
// The dispatcher decides which METHOD to attempt per kind; the methods themselves
// report what they observed at runtime (which function is in the rank-1 HOOKED/
// CALLED set is the observation tier's runtime fact, not the dispatcher's). For
// the function kind the dispatcher's posture is the FLOOR (the foreign-uncallable
// default); a row that the observation/safe-read tier reaches REFINES it (an
// observed row → invoke_attempted false + None; a safe-read getter →
// invoke_attempted true + None). For the non-function kinds the posture is fixed.

// The strongest method a kind's dispatch attempts — the §11.6 "strongest
// applicable attempt" column, as a method-class tag. The sweep maps each to its
// concrete method (rank-1 observation → rank-2/3 safe-read → static
// MapStaticVerdict → the vtable_base walk → the vtable_index deferral).
enum class DispatchMethod {
    ObservedThenStatic,  // function: try rank-1 observation; fall to the rank-4
                         // static check (foreign-uncallable floor).
    SafeReadCvar,        // function (cvar getter): rank-2 safe-read; the function
                         // dispatch refines this at runtime by the getter name.
    StaticOnly,          // function_no_sig / function_variadic / callsite /
                         // string_anchor / instruction_anchor / data_slot — the
                         // static check (MapStaticVerdict) is the strongest method.
    VtableBaseWalk,      // vtable_base: rank-3 reachability IS the read-only
                         // entry-walk (WalkVtableBaseLive), NOT the base-VA range
                         // test — REPLACES MapStaticVerdict's base-in-.text test.
    Deferred,            // vtable_index: cannot_check (no live-vtable resolution).
};

// One kind's §11.6 dispatch descriptor: the method to attempt + the kind's
// DEFAULT invoke posture (the floor; the function kind refines it by which method
// won). `ceilingRank` is the strongest rank the method can reach for this kind —
// used only as the documented §11.6 ceiling (the actual rank reported is the rank
// of the method that ran). A function kind's default posture is the
// foreign-uncallable case (invoke_attempted false, UnsafeToCall) — the observed/
// safe-read refinement overrides it when those tiers reach the row.
struct KindDispatch {
    DispatchMethod   method = DispatchMethod::StaticOnly;
    int              ceilingRank = 0;             // the §11.6 ceiling rank for this kind.
    bool             invokeAttempted = false;     // the §11.6 default invoke_attempted.
    InvokeSkipReason invokeSkipReason = InvokeSkipReason::NotACallableKind;
};

// The §11.6 matrix lookup: map a curated kind → its dispatch descriptor. This is
// the per-kind routing table (the dispatcher), built verbatim from §11.6 — every
// kind has a row, no kind is a passive non-result. The function kind returns the
// foreign-uncallable FLOOR (ObservedThenStatic + UnsafeToCall); the sweep refines
// its method + posture at runtime (rank-1 observed / rank-2 cvar getter).
KindDispatch DispatchForKind(kcdx::survival::Kind kind);

// Decode an InvokeSkipReason to its stable token (for logs / the later v3 report).
const char* InvokeSkipReasonName(InvokeSkipReason r);

// =====================================================================
// Rank-1 OBSERVED-EXECUTION tier — the only method that awards
// VerifiedWorking.
// =====================================================================
//
// Rank 1 is the top rung of the proof ladder: the function FIRED in the running
// process this session AND passed through correctly. kcdx NEVER mints a
// synthetic call to a foreign function — the GAME calls it, kcdx OBSERVES. Two
// observed sub-paths, both zero-invoke-risk:
//
//   HOOKED — for a row kcdx hooks via the chain (the engine-stamped chain
//     entries: lua_pcall, lua_newstate, the ccrypak_* file paths, serialization,
//     CGame_Update, SaveGame/LoadGame). The signal is the hook-chain fire
//     breadcrumb: the engine-stamped chain entry sits on this row's resolved VA,
//     AND that VA appears in the detour fire ring with a non-zero sequence (the
//     game executed through the detour this session). A fire is an OBSERVED
//     fact, read from the breadcrumb ring — never an assumption that a hook
//     "should" fire.
//
//   CALLED-by-kcdx — for a row kcdx itself calls in production (the cvar
//     accessors, the console command/exec thunks). Those targets are reached
//     through a resolved function pointer (or a runtime vtable slot), not the
//     hook chain, so they leave no fire-ring breadcrumb and no chain entry. The
//     signal is a kcdx-side INVOCATION RECORD: each production call site records
//     the curated target's RESOLVED VA the instant the real call RETURNS (an
//     observed completed invocation — the game ran kcdx's own call, it came
//     back). The record is a process-lifetime set keyed by resolved VA, written
//     AFTER the call returns (never on "pointer resolved" — resolution is rank
//     4–5, NOT proof of execution). The sweep reads the record for a row's VA;
//     present → the curated target was actually invoked + returned this session
//     → rank-1. The recorded VA is the SAME VA the sweep resolves a row to
//     (WhgameBase()+rva via the cache), so the match is exact.
//
// This is the OBSERVATION PRIMITIVE + the rank-1 verdict path. The per-kind
// dispatch that routes WHICH rows are rank-1-eligible is a later step; this
// gives that step a callable observation it slots above the static ceiling.

// The outcome of the rank-1 HOOKED observation for ONE resolved VA — read from
// the live hook chain + the detour fire breadcrumb ring. `observed` is true
// ONLY when an ENGINE-stamped chain entry sits on `va` AND that VA fired this
// session (fire-ring sequence > 0). `fireSeq` is the newest recorded fire
// sequence for the VA (0 when none) — the observed-fact evidence.
struct ObservedExecution {
    bool        observed = false;   // engine-stamped chain entry on va AND it fired.
    bool        hasChainEntry = false;  // an engine-stamped chain entry sits on va.
    uint64_t    fireSeq = 0;        // newest fire-ring seq for va (0 == no fire).
    std::string detail;             // grep-able evidence token.
};

// Observe whether the engine's own hook on `va` FIRED this session — the rank-1
// HOOKED signal. Reads the live chain (hook_chain::GetAllChainTargets) for an
// ENGINE-stamped entry on `va`, and the detour fire breadcrumb
// (modification_inventory::LastFires) for an actual fire at `va`. Pure read; no
// mutation, no synthetic call. A row with no engine chain entry on its VA, or an
// engine entry that has not fired this session, is NOT observed → the caller
// falls through to the static ceiling. Never AWARDS a verdict — it reports the
// observed fact; ObservedToVerdict turns a positive observation into the rank-1
// verdict.
ObservedExecution ObserveHookedExecution(uintptr_t va);

// Lift a positive rank-1 observation into the verdict + rank — the ONLY path
// that produces VerifiedWorking. Returns true and fills `out` (VerifiedWorking,
// method_rank 1) ONLY when `obs.observed` is true; returns false otherwise (the
// caller keeps the static-ceiling verdict). This is the seam the sweep uses:
// observe first, and on a positive observation OVERRIDE the static result with
// rank-1 VerifiedWorking; on no observation the static result stands.
bool ObservedToVerdict(const ObservedExecution& obs, StaticVerdict& out);

// -----------------------------------------------------------------------------
// CALLED-by-kcdx invocation record — the rank-1 signal for a curated target
// kcdx invokes in its own production path (the cvar accessors, the console
// AddCommand/ExecuteString thunks), which are reached through a resolved
// function pointer / runtime vtable slot, NOT the hook chain, and so leave no
// fire-ring breadcrumb.
// -----------------------------------------------------------------------------

// Record that kcdx INVOKED a curated target at `va` AND the call RETURNED — an
// OBSERVED completed invocation, the CALLED-by-kcdx analogue of the HOOKED
// fire-ring. Call this AFTER the real call returns (so a record means
// "invoked + came back", never "pointer resolved"). `va` is the curated row's
// RESOLVED VA (WhgameBase()+rva via refdb) — the SAME VA the sweep resolves the
// row to, so the read below matches exactly. va == 0 is ignored (an unresolved
// target). The store is a small process-lifetime set; these calls are one-shot
// config reads / console registration / command execution (never a hot path),
// so a record-after-return is free. Thread-safe (a mutex-guarded set).
void RecordKcdxInvocation(uintptr_t va);

// True iff `va` was recorded by RecordKcdxInvocation this session — the curated
// target at `va` was actually invoked by kcdx AND returned. Pure read; no
// mutation, no synthetic call. The sweep's rank-1 CALLED path: a row whose VA
// reads true was observed executing → verified_working (rank 1); a row not in
// the record falls through to its static ceiling. A 0 VA always reads false.
bool WasInvokedByKcdx(uintptr_t va);

// Drop every recorded invocation. Test-only — lets the cap-84 self-test assert
// the present/absent discrimination from a known-clean baseline without a real
// production call having seeded the store. Never called by production code.
void ResetInvocationRecord();

// =====================================================================
// Rank-2 SAFE-READ tier — reads a row's live target with ZERO mutation,
// capping at PassedNotVerified.
// =====================================================================
//
// Rank 2 sits BELOW rank-1 observed execution and ABOVE the rank 3-5 static
// checks: a correct read proves the target yields a sane value, NOT that its
// behavior works — so it caps at PassedNotVerified, never VerifiedWorking (only
// rank-1 observed execution earns the top rung; the ceiling rule, MapStaticVerdict).
//
// The safe-read METHOD reads, it never CALLS a foreign function for the sake of
// the test — a read is not an invoke. The cvar read reaches a known game cvar
// through the getter's existing production accessor (config-value read, zero
// mutation); the value coming back sane is the observed pass. invoke_attempted is
// true for the cvar read (a read IS an attempt that ran), with no invoke_skip_reason.
// The vtable_base walk reads loaded-image memory only (no call) — its
// invoke_attempted stays false (a read-only table walk is not a callable kind).

// The outcome of one rank-2/3 safe-read attempt. `attempted` is true ONLY when
// the read actually RAN (the surface was ready, the target resolved); `sane` is
// true ONLY when the read RAN and returned a value the safe-read asserts is
// plausible. A read that ran but faulted/returned implausible is attempted=true,
// sane=false → the caller maps it to Failed (a faulted read is error/failed, not
// a pass). A read that could not run (surface unready / no target) is
// attempted=false → the caller keeps the static ceiling (the read did not happen,
// so it neither passes nor fails the row). Pure observation — never AWARDS a
// verdict; SafeReadToVerdict turns a positive (attempted && sane) into the rank-2
// verdict.
struct SafeReadResult {
    bool        attempted = false;  // the read actually ran.
    bool        sane = false;       // it ran AND returned a plausible value.
    std::string detail;             // grep-able evidence token.
};

// Rank-2 cvar SAFE-READ: read a known game cvar's live value through the getter's
// existing production accessor (kcdx::cvar — config-value read, ZERO mutation).
// `getterName` is the curated cvar-getter row's name (ICVar_GetIVal / ICVar_GetFVal);
// the read picks the matching int/float accessor. `attempted` is true once the
// cvar surface is ready (a read ran); `sane` is true when the accessor returned
// (a value came back) — a value coming back at all is the plausible-return signal
// for a config getter (a faulted/unresolvable read returns false → attempted but
// not sane → Failed). Reads a stable, always-present game cvar so the read does
// not depend on a maintainer-set value. NOT a hot path (runs once per getter row
// in the boot/console sweep). A read that runs IS a kcdx call that returned, so
// the production accessor also seeds the CALLED-by-kcdx record — but THIS sweep's
// rank-1 check ran BEFORE this read, so this row honestly lands rank-2 here (a
// LATER sweep, seeing the seeded call, reads it rank-1).
SafeReadResult SafeReadCvarGetter(const std::string& getterName);

// Rank-3 vtable_base read-only LOADED-IMAGE walk: for the row's resolved table
// base VA, read `slotCount` qwords from the LIVE loaded image and assert each
// entry resolves into live `.text` (pe::IsVaInLiveText). READ-ONLY — no call, no
// mutation. This is a STRONGER live read than the base-VA reachability range test
// (MapStaticVerdict's rank-3 only checks the base VA lands in .text); the §11.6
// vtable_base rank-3 asserts EACH of the N entries is a live code pointer. `view`
// is the already-opened live module; `baseVa` is the engine-resolved table base
// (WhgameBase()+rva — AP1: never hardcoded). `attempted` is true once the live
// module is mapped + baseVa resolved (the walk ran); `sane` is true when ALL N
// entries land in live `.text` (a non-.text / unreadable entry → attempted but not
// sane → Failed: the table shape broke in the live image). NOT a hot path (once
// per vtable_base row in the sweep).
SafeReadResult WalkVtableBaseLive(const kcdx::pe::ModuleView& view,
                                  uintptr_t baseVa, uint32_t slotCount);

// Lift a positive rank-2/3 safe-read into the verdict + rank — caps at
// PassedNotVerified, NEVER VerifiedWorking (the safe-read ceiling). On an
// attempted-but-not-sane read, set `out` to Failed at `failedRank` (a faulted/
// broken read is a divergence the row should have passed). Returns true and fills
// `out` ONLY when the read was ATTEMPTED (passed or failed it); returns false when
// the read did not run (attempted=false) so the caller keeps the static ceiling.
// `passRank` is the rank a sane read reports (2 for the cvar read, 3 for the
// vtable_base walk); `failedRank` is the rank a faulted read reports (same method
// that ran). The seam the sweep uses: try rank-1 first; on no rank-1, try the
// safe-read and on a positive attempt OVERRIDE the static result with the
// safe-read verdict — but only when the static result is non-divergent (the same
// divergence-gate as rank-1: a safe-read never whitewashes a static failed/
// not_applicable, the more honest signal).
bool SafeReadToVerdict(const SafeReadResult& sr, int passRank, int failedRank,
                       StaticVerdict& out);

// Run the verification pass over the curated USER set (every cached refdb
// entity). Returns one RowVerdict per swept row. ONCE per trigger (the console
// command, or the engine self-test), never on the hot path. refdb must be
// Open() (the cache is the input); an unloaded refdb yields an empty result
// with a logged reason — never a silent empty.
//
// `worldLoaded` is the SAVE-LOAD PRECONDITION for the live-exercise tier: the
// rank-1 observed-execution method (the only method that awards
// VerifiedWorking) needs a loaded world — many curated functions fire only
// during play, so a from-menu run can observe no live fire and could never
// honestly reach the top rung. When `worldLoaded` is false, a live-exercise-
// eligible row (a function-kind row whose strongest applicable method is
// observed live execution, and which was not already observed via a pre-menu
// hook/call) resolves `Skipped` with a precondition reason — a LOUD,
// structured "did not run THIS run, and here's exactly why," never a fabricated
// pass and never a silent omission. Every row still gets a structured response.
// When `worldLoaded` is true (the default; the post-save-load run and the
// engine self-test, which exercise the tiers directly) no row is precondition-
// skipped — the static / safe-read / observed tiers run as before. The static
// ranks (3-5) are unaffected by this gate either way; only the live-exercise
// tier is precondition-gated.
//
// `onRow` is the PER-ROW SINK — invoked as each RowVerdict FINALIZES inside
// the sweep loop (BEFORE the next row's attempt), so the caller can stream +
// durably flush each row's result the instant it resolves, NOT in a bulk pass
// over the returned vector (the bulk shape loses everything on a mid-sweep
// death). The sweep STILL returns the full vector for the suite tally — onRow
// and the return value are complementary, not exclusive. Default `{}` (no sink):
// the existing self-test callers (which post-process the returned vector) are
// unaffected. The report producer (survival_report) passes its OnRow here so the
// console stream + the incremental JSONL flush happen in the same per-row tick.
std::vector<RowVerdict> RunStartupVerification(
    bool worldLoaded = true,
    const std::function<void(const RowVerdict&)>& onRow = {});

// Decode a Verdict to its stable token (for logs / the later JSON report).
const char* VerdictName(Verdict v);

}  // namespace kcdx::survival_verify
