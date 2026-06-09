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
// a faked `resolves_works` (AP14).

#include <cstdint>
#include <string>
#include <vector>

namespace kcdx::survival_verify {

// The per-row D25 verdict (the four D25 meanings).
enum class Verdict {
    ResolvesWorks,  // on-disk fingerprint matched a candidate AND the resolved
                    // VA lands in live .text.
    WrongTarget,    // the on-disk bytes match NO candidate row's fingerprint
                    // (matched address_version id = none).
    Dead,           // the address does not resolve into live .text
                    // (0 / off-image / non-.text).
    CannotCheck,    // a precondition failed (module not mapped, no content_hash,
                    // refdb not loaded, …) — fail-loud, never a faked verdict.
};

// One curated row's combined verdict.
struct RowVerdict {
    uint64_t    kcdx_id = 0;          // the curated entity's stable id.
    std::string name;                 // the resolved (post-supersession) name.
    std::string resolved_version;     // the running game version the row resolved at.
    Verdict     verdict = Verdict::CannotCheck;

    // D34 attribution: the address_version id whose fingerprint the swept on-disk
    // bytes matched. Attribution is computed from the ON-DISK fingerprint match
    // (the Unchanged branch), INDEPENDENT of reachability — so a `dead` row (the
    // on-disk hash matched, but the live resolve is off-.text) DOES carry a
    // matched id. has_matched_id=false → no candidate matched (wrong_target), OR
    // cannot_check (the on-disk check never produced a match). A 0 id is a real
    // value, so the flag distinguishes "matched id 0" from "none".
    bool        has_matched_id = false;
    uint64_t    matched_address_version_id = 0;

    std::string detail;               // a human-readable + grep-able reason token
                                      // (the survival reason, or the dead/
                                      // wrong_target/cannot_check cause).
};

// Run the startup verification pass over the curated USER set (every cached
// refdb entity). Returns one RowVerdict per swept row. ONCE at startup, never on
// the hot path. refdb must be Open() (the cache is the input); an unloaded refdb
// yields an empty result with a logged reason — never a silent empty.
std::vector<RowVerdict> RunStartupVerification();

// Decode a Verdict to its stable token (for logs / the later JSON report).
const char* VerdictName(Verdict v);

}  // namespace kcdx::survival_verify
