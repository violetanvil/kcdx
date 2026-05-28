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

#include <cstddef>
#include <cstdint>
#include <string>

namespace kcdx::survival {

// The default BLAKE3 / content_hash length in bytes (256-bit).
constexpr size_t kHashLen = 32;

enum class Status {
    Unchanged,    // the on-disk span's BLAKE3 == expected_hash.
    Changed,      // the on-disk span's BLAKE3 != expected_hash (a byte shift).
    CannotCheck,  // the check could not run; see `reason` for the token.
};

struct Result {
    Status      status = Status::CannotCheck;
    std::string reason;  // a grep-able token (see header doc) when CannotCheck;
                         // empty for Unchanged/Changed.
};

// Check whether WHGame.dll's on-disk bytes at [rva, rva+length) still BLAKE3 to
// expectedHash32 (a 32-byte raw digest; the reference DB's content_hash blob).
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
