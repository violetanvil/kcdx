#pragma once

// survival — the game-binary content-survival check.
//
// Answers one question for a reference-database entity: do the on-disk bytes of
// the running game binary, at the entity's [rva, rva+length) span, still BLAKE3
// to the content_hash the database recorded for this version? It is how the
// engine detects that a patched game binary (or a wrong version) has shifted the
// bytes a plugin's resolution depends on — BEFORE an apply pass detours stale
// code.
//
// The crux: it hashes the module's ON-DISK BACKING FILE, not live process
// memory. The reference database's content_hash was produced from the on-disk
// bytes (no ASLR relocations applied), so reading the on-disk file makes the
// recorded hash and the check-time hash trivially comparable with zero
// normalization. Live memory would carry applied relocations and diverge.
//
// This module is STANDALONE. It is NOT wired into the apply pass here; that
// wire-in lands in a later step. It depends only on pe_helpers (raw on-disk PE
// parsing) and blake3 (the vector-proven hash).
//
// Fail-loud contract: a check that cannot run never returns Unchanged or
// Changed — it returns CannotCheck with a distinct, grep-able reason token, each
// emitting a structured SURVIVAL log line naming WHAT could not be checked and
// WHY. An empty/NULL expected hash is a NON-BYTE entity (a vtable slot, a data
// offset) and is `not_applicable` — never "Changed".
//
//   not_applicable          — expected hash is empty (non-byte entity); no check.
//   length_zero             — length == 0; nothing to hash.
//   expected_hash_bad_length— expected hash is non-empty but not 32 bytes.
//   module_not_mapped       — WHGame.dll is not loaded in the process.
//   file_not_found          — the module's on-disk path could not be resolved.
//   file_open_error         — the on-disk file could not be opened/read.
//   rva_out_of_range        — [rva,rva+length) maps to no on-disk section.
//   read_error              — the mapped file span could not be read.
//   not_implemented_3_2     — a non-function kind whose check lands in step 3.2;
//                             a DEFINED fail-loud placeholder, never a silent
//                             empty/false-Unchanged. See `Kind` + the
//                             per-kind stubs below.
//
// ---------------------------------------------------------------------------
// PER-KIND DISPATCH (the survival-fingerprint-per-kind model)
//
// A single uniform body hash cannot be the fingerprint for every kind: a
// function resolves by a stored RVA (identity == body bytes), but a callsite
// resolves by re-matching an AOB, a string_anchor by literal presence, a
// data_slot by re-running a derivation, a vtable_base by a table-shape check.
// The survival datum mirrors the resolution mechanism, so the datum's SHAPE is
// per-kind. The check is therefore ONE entry point dispatched on `Kind`, with a
// KIND-DISCRIMINATED PAYLOAD carrying that kind's datum.
//
// This step (3.1) builds the dispatch + the payload model + the function-kind
// path (the EXISTING on-disk body-hash check, verdict UNCHANGED) + the
// Ambiguous status. Every NON-function kind routes to a DEFINED fail-loud stub
// (CannotCheck / "not_implemented_3_2") until step 3.2 implements its real
// check. NO non-function check logic and NO live/reachability check land here.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace kcdx::survival {

// The default BLAKE3 / content_hash length in bytes (256-bit).
constexpr size_t kHashLen = 32;

enum class Status {
    Unchanged,    // the on-disk span's BLAKE3 == expected_hash.
    Changed,      // the on-disk span's BLAKE3 != expected_hash (a byte shift).
    Ambiguous,    // the locator no longer resolves UNIQUELY — e.g. a callsite
                  // AOB that matches MORE THAN ONE site in .text. Not Changed
                  // (the bytes may still be present) and not Unchanged (the
                  // pattern is no longer a unique locator) — genuinely
                  // ambiguous; the maintainer must extend the pattern. (The
                  // callsite multiple-hit verdict; produced by the step-3.2
                  // non-function checks, NOT by the function body-hash path.)
    CannotCheck,  // the check could not run; see `reason` for the token.
};

// The 9 address kinds the engine resolves by. `kind` gates which payload datum a
// row carries (the function kinds carry content_hash+length; the others carry an
// aob / anchor_string / rule / slot_count / etc.). Pinned values are NOT relied
// on (this enum is engine-internal, never serialized) — it discriminates the
// dispatch below.
enum class Kind {
    Function,           // body-hash (the EXISTING check; on-disk).
    FunctionNoSig,      // body-hash, same path as Function.
    FunctionVariadic,   // body-hash, same path as Function.
    Callsite,           // AOB re-match (→ step-3.2 stub; multi-hit → Ambiguous).
    StringAnchor,       // .rdata literal presence (→ step-3.2 stub).
    InstructionAnchor,  // resolver-chain re-derivation (→ step-3.2 stub).
    DataSlot,           // structural derivation, NOT a byte hash (→ step-3.2 stub).
    VtableBase,         // table-shape check (→ step-3.2 stub).
    VtableIndex,        // slot-bound method identity — population deferred
                        // (→ CannotCheck, "vtable_index_deferred").
};

// The kind-discriminated survival datum for ONE row. A plain struct carrying the
// kind tag + every per-kind field, populated per kind (the cells a kind does not
// use stay default/empty) — the same flat "kind gates which cells" shape the
// reference-DB schema uses (one typed column per fact, NULL where unused), not a
// tagged union. The function kinds populate {contentHash, length}; the search/
// derivation kinds populate their own datum, consumed by the step-3.2 checks.
//
// FLAG (payload struct form): the brief left the exact C++ payload shape to the
// simplest choice matching the existing engine's plain-struct style. This is a
// plain struct mirroring the D22 flat-row model (every fact its own field,
// `kind` gates which are relevant) — chosen over a tagged union/variant because
// the codebase uses plain structs throughout (survival::Result, vcc::Record).
struct Payload {
    Kind kind = Kind::Function;

    // function / function_no_sig / function_variadic — the body fingerprint.
    std::vector<uint8_t> contentHash;  // raw 32-byte BLAKE3; empty = none.
    size_t               length = 0;   // span the hash covers.

    // The search/derivation datums (populated + consumed by step 3.2; carried
    // here so the model is complete and 3.2 plugs in without a signature churn).
    std::vector<uint8_t> aob;            // callsite / instruction_anchor pattern.
    std::vector<uint8_t> aobMask;        // wildcard mask for `aob` (1 = match).
    std::string          anchorString;   // string_anchor literal.
    bool                 expectUnique = false;  // string_anchor single-xref assert.
    std::string          rule;           // data_slot derivation descriptor.
    uint32_t             slotCount = 0;   // vtable_base expected slot count.
};

struct Result {
    Status      status = Status::CannotCheck;
    std::string reason;  // a grep-able token (see header doc) when CannotCheck;
                         // empty for Unchanged/Changed/Ambiguous.
};

// ---------------------------------------------------------------------------
// THE DISPATCH ENTRY POINT — one entry point, dispatched on `payload.kind`.
//
// `derivesFrom` is the survival-DAG edge (the row a dependent kind re-derives
// THROUGH — a data_slot through an instruction_anchor, etc.). It is carried
// through the signature so step 3.2 can check in dependency order; the
// function-kind path ignores it. `dll` selects the bytes the check runs against
// (the on-disk backing file for the on-disk version-applicability hash — the
// SETTLED on-disk read; the live/reachability check is a separate step). Today
// the function path always reads WHGame.dll's on-disk file (dll is reserved for
// the per-module check step 3.2/3.3 wires); an empty dll = the default module.
//
// Function kinds → the existing on-disk body-hash check (verdict UNCHANGED from
// the legacy SurvivalCheck below). Every other kind → its DEFINED step-3.2 stub
// (CannotCheck / "not_implemented_3_2"), or vtable_index → CannotCheck /
// "vtable_index_deferred". A multi-hit callsite, once 3.2 lands, returns
// Ambiguous.
Result SurvivalCheck(const Payload& payload,
                     uint32_t rva,
                     uint32_t derivesFrom,
                     const std::string& dll);

// Check whether WHGame.dll's on-disk bytes at [rva, rva+length) still BLAKE3 to
// expectedHash32 (a 32-byte raw digest; the reference DB's content_hash blob).
//
// This is the function-kind body-hash check — KEPT as a thin entry point so
// existing callers (survival_pass) are untouched; the dispatch above routes the
// function kinds through it. Its verdict is UNCHANGED from before this step.
//
// expectedHash32/expectedLen carry the raw 32-byte blob. An EMPTY expected hash
// (expectedHash32 == nullptr OR expectedLen == 0) is a non-byte entity → status
// CannotCheck, reason "not_applicable" (NEVER Changed). A non-empty expected of
// any length other than 32 → CannotCheck, "expected_hash_bad_length".
//
// On a runnable check: Unchanged iff the computed digest byte-equals
// expectedHash32, else Changed. On any precondition failure the result is
// CannotCheck with the matching reason token (already logged).
Result SurvivalCheck(uint32_t rva, size_t length,
                     const uint8_t* expectedHash32, size_t expectedLen);

}  // namespace kcdx::survival
