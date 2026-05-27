#include "record_synth.h"

#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <vector>

#include "../address_library.h"
#include "../log.h"

// Record synthesis — see record_synth.h for the surface + ownership contract,
// docs/mod-loader-absorb.md for the I_Mod field map + the probe provenance.

namespace kcdx::mod_absorb {

namespace {

constexpr const char* kCat = "MOD_ABSORB";

// Address Library ids for the single I_Mod concrete-class vtable pair
// (PROBE U.9, ASLR-stable; seed rows 3105/3106). Resolved at BuildRecord time,
// NEVER hardcoded as an RVA/VA (AP1).
constexpr uint64_t kImodVtablePrimaryId   = 3105;  // ImodVtable_primary   -> +0x00
constexpr uint64_t kImodVtableSubObjectId = 3106;  // ImodVtable_subobject -> +0x18

// I_Mod record size + field offsets (PROBE U.6.3 — docs/mod-loader-absorb.md).
constexpr size_t kRecordSize = 0x70;

constexpr size_t kOffVtablePrimary   = 0x00;
constexpr size_t kOffRootPathSlash   = 0x08;
constexpr size_t kOffId              = 0x10;
constexpr size_t kOffVtableSubObject = 0x18;
constexpr size_t kOffRootPathNoSlash = 0x20;
constexpr size_t kOffDisplayName     = 0x28;
constexpr size_t kOffDescription     = 0x30;
constexpr size_t kOffAuthor          = 0x38;
constexpr size_t kOffVersion         = 0x40;
constexpr size_t kOffCreatedDate     = 0x48;
// +0x50..0x6F is the zeroed scalar/flag tail.

// One fixed-size record buffer. Boxed in a std::unique_ptr (see g_records)
// rather than stored inline in a std::vector<RecordBuffer>, so its address is
// stable for process lifetime even as more records are built — a vector of
// inline buffers would REALLOCATE on growth and move every prior record (and
// the engine holds the OLD pointer in its enabled list). alignas(16) gives the
// record the natural pointer alignment a vtable'd object expects.
struct alignas(16) RecordBuffer {
    uint8_t bytes[kRecordSize];
};

// === OWNING CONTAINERS — module-static, process-lifetime, GROWTH-STABLE =====
//
// Records: a vector of unique_ptr<RecordBuffer>. The vector itself may
// reallocate, but it holds POINTERS — the boxed RecordBuffer never moves, so
// the I_Mod* returned by an earlier BuildRecord stays valid across later calls.
//
// Strings: a std::deque<std::string>. A deque NEVER relocates existing elements
// on growth (it appends new chunks), so a .c_str() taken from an earlier string
// and written into an earlier record stays valid forever. THIS IS THE BUG CLASS
// CALLED OUT IN THE STEP BRIEF: a std::vector<std::string> push_back can
// reallocate and dangle every prior .c_str() already stored in earlier records.
// The deque is the growth-stable choice; cap-48's second-build assertion is the
// falsifiable proof.
//
// These are never freed while the game runs — the records must outlive MOUNT +
// every downstream pass (string-LIFETIME is the load-bearing concern).
std::vector<std::unique_ptr<RecordBuffer>> g_records;
std::deque<std::string>                    g_strings;

// Intern a copy of `s` into the process-lifetime, address-stable string store
// and return the .c_str() of the stored copy. The returned pointer outlives
// every BuildRecord call (deque elements never relocate).
const char* InternString(const std::string& s) {
    g_strings.push_back(s);
    return g_strings.back().c_str();
}

// Write a pointer-width value into the record at `off`.
void PutPtr(uint8_t* rec, size_t off, const void* value) {
    std::memcpy(rec + off, &value, sizeof(value));
}

}  // namespace

void* BuildRecord(const ModRecordInput& in) {
    // Resolve the I_Mod vtable pair by Address Library id — NEVER a hardcoded
    // RVA/VA (AP1). Resolve returns 0 on version mismatch / unverified row.
    const uintptr_t vtablePrimary =
        address_library::Resolve(kImodVtablePrimaryId);
    const uintptr_t vtableSubObject =
        address_library::Resolve(kImodVtableSubObjectId);

    if (vtablePrimary == 0 || vtableSubObject == 0) {
        // Fail LOUD + name the consequence (fail-state-logging.md): a record
        // with a null vtable crashes MOUNT on the first virtual dispatch, so
        // we refuse to build one and return nullptr for the caller to reject.
        LOG_ERROR_KV(kCat, "build_record_vtable_unresolved",
                     log::KV("mod_id", in.id),
                     log::KV("imod_vtable_primary_id", kImodVtablePrimaryId),
                     log::KV("imod_vtable_primary_va", reinterpret_cast<void*>(vtablePrimary)),
                     log::KV("imod_vtable_subobject_id", kImodVtableSubObjectId),
                     log::KV("imod_vtable_subobject_va", reinterpret_cast<void*>(vtableSubObject)),
                     log::KV("consequence",
                        "I_Mod vtable id did not resolve (version mismatch / "
                        "unverified seed row); a record with a null vtable WILL "
                        "crash MOUNT on first virtual dispatch — record NOT built, "
                        "returning nullptr"));
        return nullptr;
    }

    // Allocate + zero a 0x70-byte record. Zero-init covers the +0x50..0x6F
    // scalar tail (U.6.3) and any field we don't explicitly set.
    g_records.push_back(std::make_unique<RecordBuffer>());
    uint8_t* rec = g_records.back()->bytes;
    std::memset(rec, 0, kRecordSize);

    // Vtables (+0x00 / +0x18).
    PutPtr(rec, kOffVtablePrimary,   reinterpret_cast<const void*>(vtablePrimary));
    PutPtr(rec, kOffVtableSubObject, reinterpret_cast<const void*>(vtableSubObject));

    // String fields — intern each into the address-stable store, write the
    // stored copy's .c_str() into the record.
    PutPtr(rec, kOffRootPathSlash,   InternString(in.rootPathSlash));
    PutPtr(rec, kOffId,              InternString(in.id));
    PutPtr(rec, kOffRootPathNoSlash, InternString(in.rootPathNoSlash));
    PutPtr(rec, kOffDisplayName,     InternString(in.displayName));
    PutPtr(rec, kOffDescription,     InternString(in.description));
    PutPtr(rec, kOffAuthor,          InternString(in.author));
    PutPtr(rec, kOffVersion,         InternString(in.version));
    PutPtr(rec, kOffCreatedDate,     InternString(in.createdDate));

    LOG_INFO_KV(kCat, "build_record",
                log::KV("mod_id", in.id),
                log::KV("record", static_cast<void*>(rec)),
                log::KV("record_count", g_records.size()));

    return rec;
}

size_t BuiltRecordCount() {
    return g_records.size();
}

}  // namespace kcdx::mod_absorb
