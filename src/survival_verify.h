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
#include <string>
#include <vector>

#include "survival.h"  // kcdx::survival::Status — the static-check raw status the
                       // verdict mapping consumes.

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

// Run the startup verification pass over the curated USER set (every cached
// refdb entity). Returns one RowVerdict per swept row. ONCE at startup, never on
// the hot path. refdb must be Open() (the cache is the input); an unloaded refdb
// yields an empty result with a logged reason — never a silent empty.
std::vector<RowVerdict> RunStartupVerification();

// Decode a Verdict to its stable token (for logs / the later JSON report).
const char* VerdictName(Verdict v);

}  // namespace kcdx::survival_verify
