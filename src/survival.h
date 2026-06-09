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
//   vtable_index_deferred   — a vtable_index row; its survival datum (a slot
//                             target body-hash) is design-defined but population
//                             waits on the runtime-vtable verification path. A
//                             DEFINED, longer-lived deferral, never a 3.2-pending.
//
//   The per-kind STATIC checks (step 3.2) add these fail-loud CannotCheck
//   reasons (each names a specific failure, never a false Unchanged):
//   anchor_changed          — a dependent row whose anchor (derivesFrom) came
//                             back Changed; transitively un-derivable.
//   anchor_unresolved       — the anchor's resolved RVA was not threaded in (a
//                             dependent dispatched WITHOUT the ordered walk).
//   no_aob                  — a callsite / instruction_anchor with an empty aob.
//   no_anchor_string        — a string_anchor / instruction_anchor with no literal.
//   bad_rule                — a data_slot whose rule descriptor could not be parsed.
//   no_slot_count           — a vtable_base with slotCount == 0.
//   derivation_off_data     — a data_slot whose derivation did not land in .data.
//   on_disk_unreadable       — the on-disk module could not be parsed for a section scan.
//
//   And these Changed/Ambiguous verdicts (NOT CannotCheck — a definite result):
//   Changed   — a callsite AOB with zero hits, a string_anchor that is absent, a
//               vtable_base of the wrong shape, an instruction_anchor whose chain
//               broke, a data_slot whose derivation no longer lands consistently.
//   Ambiguous — a callsite AOB matching MORE THAN ONE .text site (no longer a
//               unique locator), or a unique-asserting string_anchor with the
//               wrong xref count.
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
// Step 3.1 built the dispatch + the payload model + the function-kind path + the
// Ambiguous status. Step 3.2 (this step) implements the 5 STATIC non-function
// per-kind checks (callsite / string_anchor / instruction_anchor / data_slot /
// vtable_base) under the dispatch, ALL running against the ON-DISK DLL (D25 —
// the same on-disk read the function-hash kind uses, NOT live memory), plus the
// anchor-dependency ordering (a dependent whose anchor is Changed → transitively
// CannotCheck/"anchor_changed"). vtable_index stays a DEFINED deferral
// (CannotCheck / "vtable_index_deferred" — population waits on the runtime-vtable
// path). NO live/reachability check (that is step 3.3, the only check that reads
// the live image) and NO cross-implementation agreement test (step 3.4) land here.

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

    // The search/derivation datums (populated + consumed by the step-3.2 checks).
    std::vector<uint8_t> aob;            // callsite / instruction_anchor pattern.
    std::vector<uint8_t> aobMask;        // wildcard mask for `aob` (1 = match).
    std::string          anchorString;   // string_anchor / instruction_anchor literal.
    bool                 expectUnique = false;  // string_anchor single-xref assert.
    std::string          rule;           // data_slot derivation descriptor.
    uint32_t             slotCount = 0;   // vtable_base expected slot count.

    // --- The anchor's resolved result, threaded in by the dependency-ordered
    // walk (CheckOrdered). A dependent kind (data_slot through an
    // instruction_anchor; instruction_anchor through a string_anchor;
    // vtable_index through a vtable_base) re-derives THROUGH its anchor row, so
    // it needs the anchor's resolved address AND whether the anchor itself
    // survived. The ordered walk resolves anchors FIRST and fills these before
    // dispatching the dependent. The single-row entry consumes them; an
    // independent (anchor-less) kind leaves them default. ---
    bool     hasAnchor = false;          // this row derives THROUGH an anchor.
    bool     anchorChanged = false;      // the anchor's verdict was Changed/CannotCheck.
    uint32_t anchorResolvedRva = 0;      // the anchor's resolved RVA (where it relocated to).

    // For a data_slot rule of the form "disp32@<kid>": the byte offset of the
    // disp32 field within the anchor instruction + the instruction length.
    // Defaulted to the canonical `48 8B 0D <disp32>` MOV shape (REX.W + opcode +
    // modrm = 3-byte disp offset, 7-byte instruction) — the gEnv_pConsole form.
    // A future non-MOV anchor sets these explicitly. Only read on the
    // disp32-rule data_slot path.
    uint32_t dispOffsetInAnchorInstr = 3;
    uint32_t anchorInstrLen = 7;
};

struct Result {
    Status      status = Status::CannotCheck;
    std::string reason;  // a grep-able token (see header doc) when CannotCheck;
                         // empty for Unchanged/Changed/Ambiguous.
};

// ---------------------------------------------------------------------------
// THE DISPATCH ENTRY POINT — one entry point, dispatched on `payload.kind`.
//
// The single-row entry does NOT resolve the survival-DAG edge itself. The DAG
// edge (`Row::derivesFrom` — a dependent kind re-derives THROUGH an anchor:
// a data_slot through an instruction_anchor, etc.) is resolved by the
// dependency-ordered walk (CheckOrdered, below), which threads the anchor's
// resolved RVA + verdict onto the Payload (anchorResolvedRva / anchorChanged).
// A dependent dispatched directly with anchorChanged set short-circuits to
// CannotCheck/"anchor_changed". `dll` selects the bytes the check runs against
// (the on-disk backing file — the SETTLED on-disk read, D25; the
// live/reachability check is step 3.3). Today every check reads WHGame.dll's
// on-disk file (dll is reserved for the per-module step 3.3 wires); empty dll =
// the default module.
//
// Function kinds → the existing on-disk body-hash check (verdict UNCHANGED from
// the legacy SurvivalCheck below). The 5 static non-function kinds (callsite /
// string_anchor / instruction_anchor / data_slot / vtable_base) → their real
// on-disk check (Unchanged / Changed / Ambiguous / a CannotCheck reason).
// vtable_index → CannotCheck / "vtable_index_deferred" (population deferred).
Result SurvivalCheck(const Payload& payload,
                     uint32_t rva,
                     const std::string& dll);

// ---------------------------------------------------------------------------
// THE DEPENDENCY-ORDERED WALK — checks a SET of rows anchors-first so a
// dependent kind that re-derives THROUGH an anchor (data_slot → instruction_anchor
// → string_anchor; vtable_index → vtable_base) is checked only AFTER its anchor,
// with the anchor's resolved RVA + verdict threaded into the dependent's Payload.
//
// The single-row SurvivalCheck above cannot do a derivation in isolation: a
// data_slot's check ("follow disp32 from the instruction_anchor" / "anchor RVA −
// 0xA8") needs the ANCHOR's resolved address, which only exists after the anchor
// is checked. So the ordering — the survival DAG — lives HERE, where a row set is
// available, NOT in the single-row entry. A row whose anchor (derivesFrom) came
// back Changed/CannotCheck short-circuits to CannotCheck/"anchor_changed" — never
// silently re-derived through a dead anchor, never a silent pass.
//
// The DAG edge is `Row::derivesFrom` — the row IDENTITY (the caller's stable id;
// today the reference-DB address_versions.id) of the anchor this row derives
// through. 0 = no anchor (an independent row). Rows are walked in topological
// order (anchors before dependents); a cycle or a missing anchor id is reported
// as CannotCheck on the affected dependents (fail-loud, never a hang/silent pass).
struct Row {
    uint64_t    id = 0;            // this row's stable identity (the DAG node).
    uint64_t    derivesFrom = 0;   // the anchor row's id (the DAG edge); 0 = none.
    Payload     payload;
    uint32_t    rva = 0;
    std::string dll;               // empty = the default module (WHGame.dll).
};

struct RowResult {
    uint64_t id = 0;
    Result   result;
};

// Check a row set in dependency order. For each row: an anchor-less row is
// checked directly; a dependent row is checked AFTER its anchor with the
// anchor's resolved RVA + Changed-verdict threaded into its Payload (so the
// single-row check can re-run the derivation). Returns one RowResult per input
// row, in input order. Anchors-first means a Changed/CannotCheck anchor
// transitively blocks every dependent with reason "anchor_changed".
std::vector<RowResult> CheckOrdered(const std::vector<Row>& rows);

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
