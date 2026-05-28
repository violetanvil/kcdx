#include "record_validate.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "../log.h"
#include "../refdb.h"

// Synthesized-record self-validation — see record_validate.h for the surface +
// the invariants + the keystone-crash provenance this guard exists to catch.

namespace kcdx::mod_absorb {

namespace {

constexpr const char* kCat = "MOD_ABSORB";

// Canonical refdb names for the I_Mod vtable pair (resolved at validate time
// — NEVER a hardcoded RVA/VA). The same names record_synth sets; the robust
// check is "non-null + inside the WHGame image", and we additionally confirm
// the slot equals the resolved address (record_synth writes exactly these).
constexpr const char* kImodVtablePrimaryName   = "ImodVtable_primary";
constexpr const char* kImodVtableSubObjectName = "ImodVtable_subobject";

// I_Mod record field offsets — MUST mirror record_synth.cpp's field map
// (docs/mod-loader-absorb.md "The I_Mod record layout").
constexpr size_t kOffVtablePrimary   = 0x00;
constexpr size_t kOffVtableSubObject = 0x18;

// The 8 CryString string fields, in record order, with a name for diagnostics.
struct StringField { size_t off; const char* name; };
constexpr StringField kStringFields[] = {
    {0x08, "rootPathSlash"},
    {0x10, "id"},
    {0x20, "rootPathNoSlash"},
    {0x28, "displayName"},
    {0x30, "description"},
    {0x38, "author"},
    {0x40, "version"},
    {0x48, "createdDate"},
};

// CryString header layout, immediately BEFORE the char data:
//   [int32 pad/0][int32 nRefs][int32 nLength][int32 nAllocSize][char data...]
//    data-16       data-12       data-8         data-4           data+0
// The engine reads nLength (data-8) to size every copy of the string. A garbage
// nLength is exactly what crashes MOUNT.
constexpr size_t kCryStrHeader = 16;

// Cap strlen so a non-terminated buffer does not scan forever. A real record's
// string is a mod path / name / version — far under 64 KB. Over-cap = FAIL.
constexpr size_t kStrlenCap = 64 * 1024;

// SEH-guarded raw read of one CryString field's header + char length. NO C++
// objects with destructors in this function (SEH and C++ unwinding cannot share
// a frame). Reads the three header int32s at data-12/-8/-4 and walks the chars
// to a NUL (capped). On ANY access violation reading the field, sets *readable
// = false and returns — the caller FAILS the record rather than the validator
// AV-ing. `outOverCap` is set true if the scan hit the cap before a NUL.
void ReadCryStringFieldGuarded(const char* data,
                               bool* readable,
                               int32_t* nRefs,
                               int32_t* nLength,
                               int32_t* nAllocSize,
                               size_t* strLen,
                               bool* outOverCap) {
    *readable   = false;
    *nRefs      = 0;
    *nLength    = 0;
    *nAllocSize = 0;
    *strLen     = 0;
    *outOverCap = false;

    if (data == nullptr) {
        return;  // null field pointer — caller FAILS it; not "unreadable".
    }

    __try {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
        // Header int32s (little-endian; read by memcpy to avoid alignment UB).
        std::memcpy(nRefs,      p - 12, 4);
        std::memcpy(nLength,    p - 8,  4);
        std::memcpy(nAllocSize, p - 4,  4);

        // strlen, capped. Walk byte-by-byte (each read is inside the guard).
        size_t i = 0;
        for (; i < kStrlenCap; ++i) {
            if (data[i] == '\0') break;
        }
        if (i >= kStrlenCap) {
            *outOverCap = true;  // no NUL within the cap — FAIL.
        }
        *strLen   = i;
        *readable = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // A wild pointer faulted mid-read — leave *readable = false.
        *readable = false;
    }
}

}  // namespace

bool ValidateSynthRecord(const void* rec,
                         const std::string& loadOrderName,
                         const std::string& id) {
    // The consequence string appended to every failure (the keystone-crash
    // framing the existing null-drop discipline uses).
    constexpr const char* kConsequence =
        "would crash MOUNT — record dropped, the mod will NOT load";

    if (rec == nullptr) {
        LOG_ERROR_KV(kCat, "validate_record_null",
                     kcdx::log::KV("load_order_name", loadOrderName),
                     kcdx::log::KV("id", id),
                     kcdx::log::KV("invariant", "record pointer non-null"),
                     kcdx::log::KV("consequence", kConsequence));
        return false;
    }

    const auto* base = reinterpret_cast<const uint8_t*>(rec);

    // ---- Vtable slots: non-null + inside the WHGame.dll image range. -------
    // Resolve the expected pair (never a hardcoded address); the robust check
    // is non-null + in-image, and we additionally confirm the slot equals the
    // resolved id since record_synth writes exactly these.
    HMODULE whgame = GetModuleHandleW(L"WHGame.dll");
    uintptr_t imgBase = 0, imgEnd = 0;
    if (whgame) {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(whgame);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
            reinterpret_cast<const uint8_t*>(whgame) + dos->e_lfanew);
        imgBase = reinterpret_cast<uintptr_t>(whgame);
        imgEnd  = imgBase + nt->OptionalHeader.SizeOfImage;
    }

    const uintptr_t resolvedPrimary   = refdb::ResolveAddrByName(kImodVtablePrimaryName);
    const uintptr_t resolvedSubObject = refdb::ResolveAddrByName(kImodVtableSubObjectName);

    void* vtPrimary   = nullptr;
    void* vtSubObject = nullptr;
    std::memcpy(&vtPrimary,   base + kOffVtablePrimary,   sizeof(vtPrimary));
    std::memcpy(&vtSubObject, base + kOffVtableSubObject, sizeof(vtSubObject));

    struct VtCheck { void* slot; const char* name; size_t off; uintptr_t resolved; };
    const VtCheck vts[] = {
        {vtPrimary,   "vtable_primary",   kOffVtablePrimary,   resolvedPrimary},
        {vtSubObject, "vtable_subobject", kOffVtableSubObject, resolvedSubObject},
    };
    for (const VtCheck& v : vts) {
        const uintptr_t slot = reinterpret_cast<uintptr_t>(v.slot);
        const bool nonNull = (slot != 0);
        const bool inImage = (imgBase != 0 && slot >= imgBase && slot < imgEnd);
        // If the resolved id is available, the slot must equal it; if WHGame's
        // image range could not be read (imgBase == 0), fall back to the
        // resolved-equality check alone.
        const bool matchesResolved = (v.resolved != 0 && slot == v.resolved);
        const bool ok = nonNull && (imgBase != 0 ? inImage : matchesResolved) &&
                        (v.resolved == 0 || matchesResolved);
        if (!ok) {
            LOG_ERROR_KV(kCat, "validate_record_vtable",
                         kcdx::log::KV("load_order_name", loadOrderName),
                         kcdx::log::KV("id", id),
                         kcdx::log::KV("field", v.name),
                         kcdx::log::KV("offset", (uint64_t)v.off),
                         kcdx::log::KV("slot", v.slot),
                         kcdx::log::KV("whgame_image_base", reinterpret_cast<void*>(imgBase)),
                         kcdx::log::KV("whgame_image_end", reinterpret_cast<void*>(imgEnd)),
                         kcdx::log::KV("resolved_expected", reinterpret_cast<void*>(v.resolved)),
                         kcdx::log::KV("invariant",
                            "vtable slot non-null AND inside the WHGame.dll image "
                            "(== the resolved I_Mod vtable)"),
                         kcdx::log::KV("consequence",
                            "a null/out-of-range vtable crashes MOUNT on the first "
                            "virtual dispatch — record dropped, the mod will NOT load"));
            return false;
        }
    }

    // ---- The 8 CryString string fields: header invariants. -----------------
    for (const StringField& f : kStringFields) {
        const char* data = nullptr;
        std::memcpy(&data, base + f.off, sizeof(data));

        bool readable = false, overCap = false;
        int32_t nRefs = 0, nLength = 0, nAllocSize = 0;
        size_t strLen = 0;
        ReadCryStringFieldGuarded(data, &readable, &nRefs, &nLength,
                                  &nAllocSize, &strLen, &overCap);

        if (data == nullptr) {
            LOG_ERROR_KV(kCat, "validate_record_field_null",
                         kcdx::log::KV("load_order_name", loadOrderName),
                         kcdx::log::KV("id", id),
                         kcdx::log::KV("field", f.name),
                         kcdx::log::KV("offset", (uint64_t)f.off),
                         kcdx::log::KV("invariant", "string field pointer non-null"),
                         kcdx::log::KV("consequence", kConsequence));
            return false;
        }
        if (!readable) {
            LOG_ERROR_KV(kCat, "validate_record_field_unreadable",
                         kcdx::log::KV("load_order_name", loadOrderName),
                         kcdx::log::KV("id", id),
                         kcdx::log::KV("field", f.name),
                         kcdx::log::KV("offset", (uint64_t)f.off),
                         kcdx::log::KV("data", reinterpret_cast<const void*>(data)),
                         kcdx::log::KV("invariant",
                            "the CryString header + char data must be readable "
                            "(a wild pointer faulted the guarded read)"),
                         kcdx::log::KV("consequence", kConsequence));
            return false;
        }
        if (overCap) {
            LOG_ERROR_KV(kCat, "validate_record_field_overcap",
                         kcdx::log::KV("load_order_name", loadOrderName),
                         kcdx::log::KV("id", id),
                         kcdx::log::KV("field", f.name),
                         kcdx::log::KV("offset", (uint64_t)f.off),
                         kcdx::log::KV("strlen_cap", (uint64_t)kStrlenCap),
                         kcdx::log::KV("invariant",
                            "the char data is NUL-terminated within a sane length "
                            "cap (a non-terminated buffer scanned past the cap)"),
                         kcdx::log::KV("consequence", kConsequence));
            return false;
        }

        // nLength == strlen — the load-bearing invariant. A garbage nLength is
        // EXACTLY what crashed MOUNT (the engine sizes its copy from it).
        if (nLength < 0 || static_cast<size_t>(nLength) != strLen) {
            LOG_ERROR_KV(kCat, "validate_record_field_length",
                         kcdx::log::KV("load_order_name", loadOrderName),
                         kcdx::log::KV("id", id),
                         kcdx::log::KV("field", f.name),
                         kcdx::log::KV("offset", (uint64_t)f.off),
                         kcdx::log::KV("nlength", (int64_t)nLength),
                         kcdx::log::KV("strlen", (uint64_t)strLen),
                         kcdx::log::KV("invariant",
                            "CryString nLength (int32 at data-8) == strlen(data) "
                            "and non-negative"),
                         kcdx::log::KV("consequence",
                            "the engine sizes every string copy from nLength; a "
                            "garbage length drives a huge fatal allocation during "
                            "MOUNT — record dropped, the mod will NOT load"));
            return false;
        }
        // nRefs >= 1.
        if (nRefs < 1) {
            LOG_ERROR_KV(kCat, "validate_record_field_refs",
                         kcdx::log::KV("load_order_name", loadOrderName),
                         kcdx::log::KV("id", id),
                         kcdx::log::KV("field", f.name),
                         kcdx::log::KV("offset", (uint64_t)f.off),
                         kcdx::log::KV("nrefs", (int64_t)nRefs),
                         kcdx::log::KV("invariant",
                            "CryString nRefs (int32 at data-12) >= 1"),
                         kcdx::log::KV("consequence", kConsequence));
            return false;
        }
        // nAllocSize >= nLength.
        if (nAllocSize < nLength) {
            LOG_ERROR_KV(kCat, "validate_record_field_allocsize",
                         kcdx::log::KV("load_order_name", loadOrderName),
                         kcdx::log::KV("id", id),
                         kcdx::log::KV("field", f.name),
                         kcdx::log::KV("offset", (uint64_t)f.off),
                         kcdx::log::KV("nallocsize", (int64_t)nAllocSize),
                         kcdx::log::KV("nlength", (int64_t)nLength),
                         kcdx::log::KV("invariant",
                            "CryString nAllocSize (int32 at data-4) >= nLength"),
                         kcdx::log::KV("consequence", kConsequence));
            return false;
        }
    }

    return true;
}

}  // namespace kcdx::mod_absorb
