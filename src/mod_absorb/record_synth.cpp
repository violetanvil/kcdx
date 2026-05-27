#include "record_synth.h"

#include <cstdint>
#include <cstring>
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
// (verified against the binary, ASLR-stable; seed rows 3105/3106). Resolved at BuildRecord time,
// NEVER hardcoded as an RVA/VA — RVAs shift per game update.
constexpr uint64_t kImodVtablePrimaryId   = 3105;  // ImodVtable_primary   -> +0x00
constexpr uint64_t kImodVtableSubObjectId = 3106;  // ImodVtable_subobject -> +0x18

// I_Mod record size + field offsets (verified against the binary — docs/mod-loader-absorb.md).
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
// Strings: each I_Mod string field is a CryStringT — NOT a bare char*. A
// CryStringT field stores a pointer to the CHAR DATA of a ref-counted buffer
// laid out, immediately before the chars:
//
//     [int32 pad/0][int32 nRefs=1][int32 nLength][int32 nAllocSize][char data...][\0]
//      data-16       data-12        data-8         data-4            data+0
//
// The engine reads nLength (at data-8) to size string copies during MOUNT.
// This layout is confirmed against live native records (every native
// field's data-8 int == its exact string length, data-12 == 1). Writing a bare
// std::string::c_str() here put unrelated heap bytes where nLength lives → the
// engine read a multi-GB garbage length → CryFatalError / memcpy AV (the
// mod-loader-takeover-mount-crash known-issue). So we build a real CryString
// buffer per field.
//
// Each buffer is boxed in a unique_ptr<vector<uint8_t>> (the vector's heap block
// is address-stable for the buffer's lifetime; boxing keeps the pointer-into-it
// valid even as g_cryStrings reallocates its pointer array). Process-lifetime,
// never freed — the records must outlive MOUNT + every downstream pass.
std::vector<std::unique_ptr<RecordBuffer>>          g_records;
std::vector<std::unique_ptr<std::vector<uint8_t>>>  g_cryStrings;

// CryString header: 16 bytes (4 × int32) preceding the char data, so the chars
// land 16-aligned and data-12/-8/-4 hold {nRefs, nLength, nAllocSize}.
constexpr size_t kCryStrHeader = 16;

// Build a process-lifetime CryStringT buffer for `s` and return the pointer to
// its CHAR DATA (what the I_Mod field stores). The buffer holds
// [pad=0][nRefs=1][nLength][nAllocSize][chars][\0]; the engine reads nLength at
// data-8. Address-stable: the boxed vector's heap block never moves.
const char* InternCryString(const std::string& s) {
    const int32_t len = static_cast<int32_t>(s.size());
    auto buf = std::make_unique<std::vector<uint8_t>>(kCryStrHeader + s.size() + 1, 0);
    uint8_t* p = buf->data();
    // Header (little-endian int32s): [data-16]=0, [data-12]=nRefs(1),
    // [data-8]=nLength, [data-4]=nAllocSize(==len). Verified against the binary.
    const int32_t pad = 0, nRefs = 1, nAlloc = len;
    std::memcpy(p + 0,  &pad,    4);
    std::memcpy(p + 4,  &nRefs,  4);
    std::memcpy(p + 8,  &len,    4);   // nLength at (data-8): data = p + 16
    std::memcpy(p + 12, &nAlloc, 4);
    char* data = reinterpret_cast<char*>(p + kCryStrHeader);
    std::memcpy(data, s.data(), s.size());
    data[s.size()] = '\0';
    g_cryStrings.push_back(std::move(buf));
    return data;
}

// Write a pointer-width value into the record at `off`.
void PutPtr(uint8_t* rec, size_t off, const void* value) {
    std::memcpy(rec + off, &value, sizeof(value));
}

}  // namespace

void* BuildRecord(const ModRecordInput& in) {
    // Resolve the I_Mod vtable pair by Address Library id — NEVER a hardcoded
    // RVA/VA (RVAs shift per game update). Resolve returns 0 on version mismatch
    // / unverified row.
    const uintptr_t vtablePrimary =
        address_library::Resolve(kImodVtablePrimaryId);
    const uintptr_t vtableSubObject =
        address_library::Resolve(kImodVtableSubObjectId);

    if (vtablePrimary == 0 || vtableSubObject == 0) {
        // Fail LOUD + name the consequence: a record
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

    // String fields — each is a CryStringT: build a real {nRefs,nLength,
    // nAllocSize,chars} buffer and store the pointer to its char data. A bare
    // char* here makes the engine read a garbage nLength (the
    // mod-loader-takeover-mount-crash known-issue, verified against the binary).
    PutPtr(rec, kOffRootPathSlash,   InternCryString(in.rootPathSlash));
    PutPtr(rec, kOffId,              InternCryString(in.id));
    PutPtr(rec, kOffRootPathNoSlash, InternCryString(in.rootPathNoSlash));
    PutPtr(rec, kOffDisplayName,     InternCryString(in.displayName));
    PutPtr(rec, kOffDescription,     InternCryString(in.description));
    PutPtr(rec, kOffAuthor,          InternCryString(in.author));
    PutPtr(rec, kOffVersion,         InternCryString(in.version));
    PutPtr(rec, kOffCreatedDate,     InternCryString(in.createdDate));

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
