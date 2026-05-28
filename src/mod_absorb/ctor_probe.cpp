#include "ctor_probe.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "MinHook.h"

#include "../log.h"
#include "../refdb.h"

// Comprehensive init-cycle observation probe — see ctor_probe.h for the
// framing (three capture points, two-boot outcome map, transient lifetime).
// This .cpp owns: MinHook plumbing (POINT A entry + POINT C return),
// OnSelectEntry (POINT B), and the read-only classification helpers
// (per-slot classify + SEH-guarded deref + vtable validation + vector
// walks + CryString header read).

namespace kcdx::mod_absorb::ctor_probe {

namespace {

constexpr const char* kCat = "MOD_ABSORB_PROBE";

// wh::C_ModManager ctor — refdb curated name "ModManager_ctor". Resolved at
// install time via refdb::ResolveByName (never a hardcoded RVA); the row
// carries the per-build RVA plus the verified ABI: __fastcall returning ptr,
// with 3 args (ptr outResult /*rcx*/, ptr sys /*rdx*/, ptr modsDir /*r8*/).

// C_ModManager state size. The probe walks the full range as 8-byte slots so
// any field the ctor writes shows up.
constexpr size_t kObjectSize = 0x68;

using CtorFn_t = void* (__fastcall*)(void* outResult, void* sys, void* modsDir);

std::atomic<CtorFn_t> g_orig{nullptr};
std::atomic<bool>     g_installed{false};
std::atomic<bool>     g_installSucceeded{false};

// Three independent one-shot guards — one per capture point.
std::atomic<bool>     g_capturedA{false};   // POINT A — ctor entry
std::atomic<bool>     g_capturedB{false};   // POINT B — SELECT entry
std::atomic<bool>     g_capturedC{false};   // POINT C — ctor return

// === WHGame module bounds ====================================================
//
// Captured at Install() time and held as constants for classification. The
// .text bounds let the per-slot classifier distinguish a "vtable_rva" (image
// pointer inside .text — code) from an "image_ptr" (image pointer in
// rdata/data — non-code metadata). PE-header walk: the module base is the
// IMAGE_DOS_HEADER; e_lfanew points at the IMAGE_NT_HEADERS; section headers
// follow the optional header.

uintptr_t g_whgameBase    = 0;
uintptr_t g_whgameTextLo  = 0;
uintptr_t g_whgameTextHi  = 0;
uintptr_t g_whgameImageHi = 0;

bool ResolveWhgameBounds(HMODULE whgame) {
    if (!whgame) return false;
    const auto base = reinterpret_cast<uintptr_t>(whgame);

    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(whgame);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;

    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    g_whgameBase    = base;
    g_whgameImageHi = base + nt->OptionalHeader.SizeOfImage;

    auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        // ".text" is conventionally the first section + has the CODE bit.
        // Match by name since the canonical name is stable.
        if (std::memcmp(section->Name, ".text", 5) == 0) {
            g_whgameTextLo = base + section->VirtualAddress;
            g_whgameTextHi = g_whgameTextLo + section->Misc.VirtualSize;
            return true;
        }
    }
    return false;
}

// === Per-slot classification =================================================
//
// One bucket per observed value shape. The classifier returns a stable string
// label for the dev log; it never derefs. A "pointer-like" bucket signals to
// the caller that an SEH-guarded follow-through deref is warranted.

enum class SlotKind {
    Zero,
    SmallInt,
    AsciiRun,
    VtableRva,
    ImagePtr,
    HeapPtr,
    StackPtr,
    UnknownPtr,
};

const char* SlotKindStr(SlotKind k) {
    switch (k) {
        case SlotKind::Zero:       return "zero";
        case SlotKind::SmallInt:   return "small_int";
        case SlotKind::AsciiRun:   return "ascii_run";
        case SlotKind::VtableRva:  return "vtable_rva";
        case SlotKind::ImagePtr:   return "image_ptr";
        case SlotKind::HeapPtr:    return "heap_ptr";
        case SlotKind::StackPtr:   return "stack_ptr";
        case SlotKind::UnknownPtr: return "unknown_ptr";
    }
    return "unknown";
}

bool IsAsciiRun(uint64_t v) {
    for (int i = 0; i < 4; ++i) {
        const uint8_t b = static_cast<uint8_t>((v >> (i * 8)) & 0xFFu);
        if (b < 0x20 || b > 0x7E) return false;
    }
    return true;
}

SlotKind Classify(uint64_t v) {
    if (v == 0) return SlotKind::Zero;
    if (v < 0x10000) return SlotKind::SmallInt;
    if (IsAsciiRun(v)) return SlotKind::AsciiRun;
    if (g_whgameTextLo && v >= g_whgameTextLo && v < g_whgameTextHi) {
        return SlotKind::VtableRva;
    }
    if (g_whgameBase && v >= g_whgameBase && v < g_whgameImageHi) {
        return SlotKind::ImagePtr;
    }
    // Heap regions on Win64 typically live in the 0x0000_01??_???????? or
    // 0x0000_02??_???????? user-space range. Stack lives in the upper user
    // range (above 0x0000_7F_????????????). These are rough buckets — a
    // misclassification only affects the dev-log label, not safety.
    const uint64_t kHeapLo    = 0x0000010000000000ull;
    const uint64_t kHeapHi    = 0x0000030000000000ull;
    const uint64_t kStackLo   = 0x00007F0000000000ull;
    if (v >= kHeapLo && v < kHeapHi) return SlotKind::HeapPtr;
    if (v >= kStackLo)               return SlotKind::StackPtr;
    return SlotKind::UnknownPtr;
}

bool IsPointerLike(SlotKind k) {
    switch (k) {
        case SlotKind::VtableRva:
        case SlotKind::ImagePtr:
        case SlotKind::HeapPtr:
        case SlotKind::UnknownPtr:
            return true;
        default:
            return false;
    }
}

// === SEH-guarded reads ======================================================
//
// Every deref outside the C_ModManager's own 0x68 bytes goes through one of
// these. An AV is a valid diagnostic outcome (it tells us +0x30 is NOT a
// pointer in this boot), not a failure — the probe catches and logs it.

bool SafeReadBytes(const void* src, void* dst, size_t n) {
    __try {
        std::memcpy(dst, src, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Read up to `maxLen` printable-ASCII chars from `src`, stopping at a NUL or
// at the first non-printable. Writes a NUL-terminated string into `dst`.
// Returns the number of chars written (excluding the terminator). Stops
// returning 0 on the FIRST byte being non-printable (so an empty result is
// the signal "not a string").
size_t SafeReadCString(const void* src, char* dst, size_t maxLen) {
    if (maxLen == 0) return 0;
    dst[0] = '\0';
    if (!src) return 0;
    size_t written = 0;
    __try {
        const auto* p = reinterpret_cast<const uint8_t*>(src);
        for (size_t i = 0; i + 1 < maxLen; ++i) {
            const uint8_t b = p[i];
            if (b == 0) break;
            if (b < 0x20 || b > 0x7E) {
                dst[written] = '\0';
                return written;
            }
            dst[written++] = static_cast<char>(b);
        }
        dst[written] = '\0';
        return written;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        dst[0] = '\0';
        return 0;
    }
}

// === Hex-bytes formatter ====================================================
//
// 0x40 bytes → "XX XX XX ... XX" — 64*3 + NUL = 193 chars. We use 256 for
// margin.

void FormatBytesHex(const uint8_t* bytes, size_t n, char* out, size_t outLen) {
    if (outLen == 0) return;
    out[0] = '\0';
    size_t written = 0;
    for (size_t i = 0; i < n && written + 3 < outLen; ++i) {
        int w = snprintf(out + written, outLen - written,
                         i == 0 ? "%02X" : " %02X", bytes[i]);
        if (w <= 0) break;
        written += static_cast<size_t>(w);
    }
}

// === Logging helpers ========================================================

void LogDerefAv(const void* target, const char* context) {
    LOG_INFO_KV(kCat, "deref_av",
        kcdx::log::KV("at", reinterpret_cast<uintptr_t>(target)),
        kcdx::log::KV::BareStr("context", context));
}

// Dump 0x40 bytes at `target` to the log, both as a hex blob and as a best-
// effort C-string. Caller has already classified `target` as pointer-like.
// `pointKey` is the dev-log key for which capture point owns the dump.
void DerefAndDump(const void* target, const char* contextLabel,
                  const char* pointKey) {
    constexpr size_t kDumpBytes = 0x40;
    uint8_t buf[kDumpBytes];
    if (!SafeReadBytes(target, buf, kDumpBytes)) {
        LogDerefAv(target, contextLabel);
        return;
    }

    char hexBuf[256];
    FormatBytesHex(buf, kDumpBytes, hexBuf, sizeof(hexBuf));

    char strBuf[72];
    const size_t strLen = SafeReadCString(target, strBuf, sizeof(strBuf));

    LOG_INFO_KV(kCat, pointKey,
        kcdx::log::KV::BareStr("context", contextLabel),
        kcdx::log::KV("at",       reinterpret_cast<uintptr_t>(target)),
        kcdx::log::KV("size_hex", static_cast<uintptr_t>(kDumpBytes)),
        kcdx::log::KV::BareStr("bytes", hexBuf),
        kcdx::log::KV("cstr_len", static_cast<uint64_t>(strLen)),
        kcdx::log::KV("cstr",     strBuf));
}

// === Vtable validation =====================================================
//
// Read 8 function pointers at `vtable`, classify each as code-pointer
// (inside .text) or not. Emits one summary line + 8 per-slot lines for the
// dev log. SEH-guarded.

void ValidateVtable(const void* vtable, const char* slotLabel) {
    if (!vtable) return;

    uintptr_t fns[8] = {0};
    if (!SafeReadBytes(vtable, fns, sizeof(fns))) {
        LogDerefAv(vtable, slotLabel);
        return;
    }

    int codeCount = 0;
    char codeFlags[16] = {0};
    for (int i = 0; i < 8; ++i) {
        const bool inText = g_whgameTextLo &&
                            fns[i] >= g_whgameTextLo &&
                            fns[i] <  g_whgameTextHi;
        if (inText) ++codeCount;
        codeFlags[i] = inText ? '1' : '0';
    }
    codeFlags[8] = '\0';

    const uintptr_t addr = reinterpret_cast<uintptr_t>(vtable);
    const uintptr_t rva  = g_whgameBase && addr >= g_whgameBase
                         ? addr - g_whgameBase : 0;

    LOG_INFO_KV(kCat, "vtable_dump",
        kcdx::log::KV::BareStr("at_label", slotLabel),
        kcdx::log::KV("at",        addr),
        kcdx::log::KV("rva",       rva),
        kcdx::log::KV("code_count", static_cast<long long>(codeCount)),
        kcdx::log::KV::BareStr("code_flags", codeFlags));

    // One line per slot for full grep coverage.
    for (int i = 0; i < 8; ++i) {
        const bool inText = g_whgameTextLo &&
                            fns[i] >= g_whgameTextLo &&
                            fns[i] <  g_whgameTextHi;
        char idxStr[4];
        snprintf(idxStr, sizeof(idxStr), "%d", i);
        LOG_INFO_KV(kCat, "vtable_slot",
            kcdx::log::KV::BareStr("vtable_label", slotLabel),
            kcdx::log::KV::BareStr("slot_idx", idxStr),
            kcdx::log::KV("fn",         fns[i]),
            kcdx::log::KV::BareStr("kind", inText ? "code" : "non_code"));
    }
}

// === CryString header read for +0x10 ========================================
//
// The 8 bytes at +0x10 are a `char* data` pointer; the 16 bytes BEFORE `data`
// are the {pad, nRefs, nLength, nAllocSize} header; the chars at `data` are
// the content.

void DumpCryStringAtOffset10(const void* obj) {
    if (!obj) return;
    void* data = nullptr;
    const auto* base = reinterpret_cast<const uint8_t*>(obj);
    std::memcpy(&data, base + 0x10, sizeof(data));

    LOG_INFO_KV(kCat, "cs_header",
        kcdx::log::KV("data_ptr", reinterpret_cast<uintptr_t>(data)));

    if (!data) {
        LOG_INFO_KV(kCat, "cs_header",
            kcdx::log::KV::BareStr("note", "data_ptr_null"));
        return;
    }

    // Header at (data - 16): four u32 fields.
    struct CryStringHeader {
        uint32_t pad;
        uint32_t nRefs;
        uint32_t nLength;
        uint32_t nAllocSize;
    };
    CryStringHeader hdr{};
    const auto* hdrAddr =
        reinterpret_cast<const uint8_t*>(data) - sizeof(hdr);
    if (!SafeReadBytes(hdrAddr, &hdr, sizeof(hdr))) {
        LogDerefAv(hdrAddr, "cs_header_at_minus_16");
        return;
    }

    LOG_INFO_KV(kCat, "cs_header",
        kcdx::log::KV("at",          reinterpret_cast<uintptr_t>(hdrAddr)),
        kcdx::log::KV("pad",         static_cast<uint64_t>(hdr.pad)),
        kcdx::log::KV("n_refs",      static_cast<uint64_t>(hdr.nRefs)),
        kcdx::log::KV("n_length",    static_cast<uint64_t>(hdr.nLength)),
        kcdx::log::KV("n_alloc_size", static_cast<uint64_t>(hdr.nAllocSize)));

    // Content at `data`.
    char strBuf[72];
    const size_t got = SafeReadCString(data, strBuf, sizeof(strBuf));
    LOG_INFO_KV(kCat, "cs_chars",
        kcdx::log::KV("at",      reinterpret_cast<uintptr_t>(data)),
        kcdx::log::KV("len",     static_cast<uint64_t>(got)),
        kcdx::log::KV("content", strBuf));
}

// === Vector-shape walks =====================================================
//
// Walk 1 — enabled list at +0x30 / +0x38 / +0x40. Treat as a std::vector
// of I_Mod* (8-byte pointers).
//
// Walk 2 — scanned list at +0x18 / +0x20. Try both pointer-vector
// (8-byte stride) and direct-record-array (0x70 stride) interpretations
// since the seed prose is ambiguous about which it is.

void WalkEnabledList(const void* obj) {
    const auto* base = reinterpret_cast<const uint8_t*>(obj);
    uint64_t begin = 0, end = 0, cap = 0;
    std::memcpy(&begin, base + 0x30, sizeof(begin));
    std::memcpy(&end,   base + 0x38, sizeof(end));
    std::memcpy(&cap,   base + 0x40, sizeof(cap));

    if (begin == 0 && end == 0 && cap == 0) {
        LOG_INFO_KV(kCat, "walk1_enabled",
            kcdx::log::KV::BareStr("state", "empty_all_zero"),
            kcdx::log::KV("begin", begin),
            kcdx::log::KV("end",   end),
            kcdx::log::KV("cap",   cap));
        return;
    }

    // Range sanity.
    const bool orderOk  = begin <= end && end <= cap;
    const bool strideOk = ((end - begin) % 8) == 0;
    uint64_t count = orderOk && strideOk ? (end - begin) / 8 : 0;
    const bool countSane = orderOk && strideOk && count < 1000;

    LOG_INFO_KV(kCat, "walk1_enabled",
        kcdx::log::KV::BareStr("state",
            countSane ? "sane" : "corrupted"),
        kcdx::log::KV("begin",       begin),
        kcdx::log::KV("end",         end),
        kcdx::log::KV("cap",         cap),
        kcdx::log::KV("order_ok",    orderOk),
        kcdx::log::KV("stride_ok",   strideOk),
        kcdx::log::KV("count",       count));

    if (!countSane || count == 0) return;

    // SEH-guarded deref of *begin = first I_Mod*. Then deref **begin = first
    // I_Mod's vtable. Compare against the refdb-resolved I_Mod primary vtable.
    void* firstMod = nullptr;
    if (!SafeReadBytes(reinterpret_cast<const void*>(begin),
                       &firstMod, sizeof(firstMod))) {
        LogDerefAv(reinterpret_cast<const void*>(begin), "walk1_first_imod_ptr");
        return;
    }

    LOG_INFO_KV(kCat, "walk1_enabled",
        kcdx::log::KV::BareStr("state", "first_imod_ptr"),
        kcdx::log::KV("imod", reinterpret_cast<uintptr_t>(firstMod)));

    if (!firstMod) return;

    // First 0x40 bytes of the I_Mod record.
    DerefAndDump(firstMod, "walk1_first_imod_body", "walk1_enabled");

    // Vtable check: read qword at firstMod, compare to the refdb-resolved
    // I_Mod primary vtable. The refdb returns a per-version RVA; bias by the
    // WHGame base captured at Install() to get a VA. found=false here means the
    // canonical name is not in the DB for the running build — fail-loud already
    // logged by refdb under category REFDB; skip the compare and continue.
    void* imodVtable = nullptr;
    if (!SafeReadBytes(firstMod, &imodVtable, sizeof(imodVtable))) {
        LogDerefAv(firstMod, "walk1_first_imod_vtable_read");
        return;
    }
    const auto primaryRes = kcdx::refdb::ResolveByName("ImodVtable_primary");
    if (!primaryRes.found) {
        LOG_ERROR_KV(kCat, "walk1_enabled",
            kcdx::log::KV::BareStr("state", "first_imod_vtable_compare_skipped"),
            kcdx::log::KV::BareStr("name",  "ImodVtable_primary"),
            kcdx::log::KV::BareStr("detail",
                "refdb::ResolveByName(ImodVtable_primary) returned not-found; "
                "the vtable-compare diagnostic is skipped this fire (the probe "
                "still observed the I_Mod pointer + first-record body). See the "
                "preceding REFDB ERROR for the specific reason token"));
        return;
    }
    const uintptr_t expected = g_whgameBase + primaryRes.rva;
    LOG_INFO_KV(kCat, "walk1_enabled",
        kcdx::log::KV::BareStr("state", "first_imod_vtable_compare"),
        kcdx::log::KV("actual",   reinterpret_cast<uintptr_t>(imodVtable)),
        kcdx::log::KV("expected_imod_vtable_primary", expected),
        kcdx::log::KV("matches", reinterpret_cast<uintptr_t>(imodVtable)
                                 == expected));
}

void WalkScannedList(const void* obj) {
    const auto* base = reinterpret_cast<const uint8_t*>(obj);
    uint64_t begin = 0, end = 0;
    std::memcpy(&begin, base + 0x18, sizeof(begin));
    std::memcpy(&end,   base + 0x20, sizeof(end));

    if (begin == 0 && end == 0) {
        LOG_INFO_KV(kCat, "walk2_scanned",
            kcdx::log::KV::BareStr("state", "empty_all_zero"),
            kcdx::log::KV("begin", begin),
            kcdx::log::KV("end",   end));
        return;
    }

    const bool orderOk    = begin <= end;
    const uint64_t spread = orderOk ? (end - begin) : 0;

    // (a) Vector-of-pointers interpretation: 8-byte stride.
    const bool strideOkA  = (spread % 8) == 0;
    const uint64_t countA = strideOkA ? spread / 8 : 0;
    const bool saneA      = orderOk && strideOkA && countA < 1000;

    // (b) Direct-record-array interpretation: 0x70 stride.
    const bool strideOkB  = (spread % 0x70) == 0;
    const uint64_t countB = strideOkB ? spread / 0x70 : 0;
    const bool saneB      = orderOk && strideOkB && countB < 1000;

    LOG_INFO_KV(kCat, "walk2_scanned",
        kcdx::log::KV("begin",      begin),
        kcdx::log::KV("end",        end),
        kcdx::log::KV("spread",     spread),
        kcdx::log::KV("count_a_ptr",   countA),
        kcdx::log::KV("count_a_sane",  saneA),
        kcdx::log::KV("count_b_record", countB),
        kcdx::log::KV("count_b_sane",   saneB));

    // If interpretation (b) yields a sane count > 0, dump the first record's
    // first 0x40 bytes; its vtable at +0x00 should match the I_Mod primary
    // vtable if it is truly a direct I_Mod record.
    if (saneB && countB > 0) {
        DerefAndDump(reinterpret_cast<const void*>(begin),
                     "walk2_first_record_body", "walk2_scanned");
        void* maybeVtable = nullptr;
        if (SafeReadBytes(reinterpret_cast<const void*>(begin),
                          &maybeVtable, sizeof(maybeVtable))) {
            const auto primaryRes =
                kcdx::refdb::ResolveByName("ImodVtable_primary");
            if (!primaryRes.found) {
                LOG_ERROR_KV(kCat, "walk2_scanned",
                    kcdx::log::KV::BareStr("state",
                        "first_record_vtable_compare_skipped"),
                    kcdx::log::KV::BareStr("name", "ImodVtable_primary"),
                    kcdx::log::KV::BareStr("detail",
                        "refdb::ResolveByName(ImodVtable_primary) returned "
                        "not-found; the vtable-compare diagnostic is skipped "
                        "this fire. See the preceding REFDB ERROR for the "
                        "specific reason token"));
            } else {
                const uintptr_t expected = g_whgameBase + primaryRes.rva;
                LOG_INFO_KV(kCat, "walk2_scanned",
                    kcdx::log::KV::BareStr("state",
                                           "first_record_vtable_compare"),
                    kcdx::log::KV("actual",
                                  reinterpret_cast<uintptr_t>(maybeVtable)),
                    kcdx::log::KV("expected_imod_vtable_primary", expected),
                    kcdx::log::KV("matches",
                                  reinterpret_cast<uintptr_t>(maybeVtable)
                                  == expected));
            }
        }
    }

    // For interpretation (a) > 0, dump the first slot as an I_Mod*.
    if (saneA && countA > 0) {
        void* firstPtr = nullptr;
        if (!SafeReadBytes(reinterpret_cast<const void*>(begin),
                           &firstPtr, sizeof(firstPtr))) {
            LogDerefAv(reinterpret_cast<const void*>(begin),
                       "walk2_first_ptr");
        } else {
            LOG_INFO_KV(kCat, "walk2_scanned",
                kcdx::log::KV::BareStr("state", "first_ptr"),
                kcdx::log::KV("ptr", reinterpret_cast<uintptr_t>(firstPtr)));
            if (firstPtr) {
                DerefAndDump(firstPtr, "walk2_first_ptr_target",
                             "walk2_scanned");
            }
        }
    }
}

// === Per-slot dump =========================================================
//
// Iterate 8-byte slots 0x00..0x60, classify each, log non-zero slots with
// their classification, and (for pointer-like slots) deref + dump 0x40
// bytes at the target. POINT B uses pointKey "point_b_slot"; POINT C uses
// "point_c_slot" + "point_c_deref" for the follow-through reads.

void DumpAllSlots(const void* obj, const char* slotKey,
                  const char* derefKey) {
    const auto* base = reinterpret_cast<const uint8_t*>(obj);
    char nonzeroBuf[256];
    nonzeroBuf[0] = '\0';
    size_t nonzeroLen = 0;

    for (size_t off = 0; off + 8 <= kObjectSize; off += 8) {
        uint64_t value = 0;
        std::memcpy(&value, base + off, sizeof(value));
        if (value == 0) continue;

        if (nonzeroLen + 8 < sizeof(nonzeroBuf)) {
            int n = snprintf(nonzeroBuf + nonzeroLen,
                             sizeof(nonzeroBuf) - nonzeroLen,
                             nonzeroLen == 0 ? "0x%02zX" : ",0x%02zX", off);
            if (n > 0) nonzeroLen += static_cast<size_t>(n);
        }

        const SlotKind kind = Classify(value);
        char offStr[8];
        snprintf(offStr, sizeof(offStr), "0x%02zX", off);

        LOG_INFO_KV(kCat, slotKey,
            kcdx::log::KV::BareStr("offset",     offStr),
            kcdx::log::KV("value",               static_cast<uintptr_t>(value)),
            kcdx::log::KV::BareStr("kind",       SlotKindStr(kind)));

        // Follow-through deref for pointer-like slots.
        if (IsPointerLike(kind)) {
            DerefAndDump(reinterpret_cast<const void*>(value),
                         offStr, derefKey);
        }
    }

    LOG_INFO_KV(kCat, slotKey,
        kcdx::log::KV::BareStr("summary",         "nonzero_offsets"),
        kcdx::log::KV::BareStr("nonzero_offsets",
                               nonzeroBuf[0] ? nonzeroBuf : "(none)"));
}

// === Hooked ctor (POINT A + POINT C) ========================================

void* __fastcall HookedCtor(void* outResult, void* sys, void* modsDir) {
    CtorFn_t orig = g_orig.load(std::memory_order_acquire);
    if (!orig) {
        LOG_ERROR_KV(kCat, "orig_ctor_null_at_dispatch",
            kcdx::log::KV::BareStr("detail",
                "the MinHook trampoline is null at dispatch; cannot forward "
                "to the original ctor this fire"));
        return nullptr;
    }

    // POINT A — one-shot.
    bool expectedA = false;
    const bool firstA = g_capturedA.compare_exchange_strong(
        expectedA, true, std::memory_order_acq_rel);

    if (firstA) {
        LOG_INFO_KV(kCat, "point_a_entry",
            kcdx::log::KV("out_result", outResult),
            kcdx::log::KV("sys",        sys),
            kcdx::log::KV("mods_dir",   modsDir));

        // Raw bytes of *outResult (uninitialized caller stack alloc — logged
        // for diff against POINT C). Safe to read: outResult is a stack-
        // alloc inside the caller's frame, mapped + writable.
        if (outResult) {
            uint8_t rawBuf[kObjectSize];
            if (SafeReadBytes(outResult, rawBuf, kObjectSize)) {
                char hexBuf[512];
                FormatBytesHex(rawBuf, 32, hexBuf, sizeof(hexBuf));
                LOG_INFO_KV(kCat, "point_a_outresult_raw",
                    kcdx::log::KV("at",
                        reinterpret_cast<uintptr_t>(outResult)),
                    kcdx::log::KV::BareStr("first_32_bytes", hexBuf));
                FormatBytesHex(rawBuf + 32, 32, hexBuf, sizeof(hexBuf));
                LOG_INFO_KV(kCat, "point_a_outresult_raw",
                    kcdx::log::KV("at",
                        reinterpret_cast<uintptr_t>(
                            static_cast<uint8_t*>(outResult) + 32)),
                    kcdx::log::KV::BareStr("next_32_bytes", hexBuf));
                FormatBytesHex(rawBuf + 64, kObjectSize - 64,
                               hexBuf, sizeof(hexBuf));
                LOG_INFO_KV(kCat, "point_a_outresult_raw",
                    kcdx::log::KV("at",
                        reinterpret_cast<uintptr_t>(
                            static_cast<uint8_t*>(outResult) + 64)),
                    kcdx::log::KV::BareStr("tail_bytes", hexBuf));
            } else {
                LogDerefAv(outResult, "point_a_outresult_raw");
            }
        }

        // Arg3 (modsDir) deref. The ctor body's first effective op is
        // `mov rdx, [r14]` (r14 = arg3) — confirming arg3 is a pointer-to-
        // something, not a direct CryString. Dump 0x40 bytes at *modsDir.
        if (modsDir) {
            void* target = nullptr;
            if (SafeReadBytes(modsDir, &target, sizeof(target))) {
                LOG_INFO_KV(kCat, "point_a_arg3_deref",
                    kcdx::log::KV("modsDir_ptr",
                                  reinterpret_cast<uintptr_t>(modsDir)),
                    kcdx::log::KV("modsDir_deref",
                                  reinterpret_cast<uintptr_t>(target)));
                if (target) {
                    DerefAndDump(target, "arg3_deref_target",
                                 "point_a_arg3_deref");
                }
            } else {
                LogDerefAv(modsDir, "point_a_arg3_deref_first_read");
            }
        }
    }

    // Forward to the original ctor — observe-only, never mutate.
    void* ret = orig(outResult, sys, modsDir);

    // POINT C — one-shot.
    bool expectedC = false;
    const bool firstC = g_capturedC.compare_exchange_strong(
        expectedC, true, std::memory_order_acq_rel);

    if (!firstC || !outResult) {
        if (firstC && !outResult) {
            LOG_ERROR_KV(kCat, "out_result_null_after_ctor",
                kcdx::log::KV::BareStr("detail",
                    "the ctor returned with outResult null — cannot snapshot "
                    "the C_ModManager state"));
        }
        return ret;
    }

    // The ctor writes its 0x68-byte object at `*outResult` (not at outResult
    // itself). The allocator at 0x1804f7820 returns the new heap block; the
    // ctor stores into [rsi] = outResult last, so by here *outResult is the
    // object.
    void* obj = nullptr;
    std::memcpy(&obj, outResult, sizeof(obj));
    if (!obj) {
        LOG_ERROR_KV(kCat, "point_c_entry",
            kcdx::log::KV::BareStr("detail",
                "outResult dereffed to null — ctor allocation likely failed"));
        return ret;
    }

    LOG_INFO_KV(kCat, "point_c_entry",
        kcdx::log::KV("obj",          reinterpret_cast<uintptr_t>(obj)),
        kcdx::log::KV("object_size",  static_cast<uintptr_t>(kObjectSize)),
        kcdx::log::KV("whgame_base",  g_whgameBase),
        kcdx::log::KV("text_lo",      g_whgameTextLo),
        kcdx::log::KV("text_hi",      g_whgameTextHi),
        kcdx::log::KV("image_hi",     g_whgameImageHi));

    // Per-slot dump + deref of pointer-like slots.
    DumpAllSlots(obj, "point_c_slot", "point_c_deref");

    // Targeted reads that go beyond per-slot classification.

    // Vtable validation at +0x00 (the wh::C_ModManager vftable per the
    // disassembly's `lea [rip + 0x2d01f67]`).
    {
        void* vtable = nullptr;
        std::memcpy(&vtable, reinterpret_cast<const uint8_t*>(obj) + 0x00,
                    sizeof(vtable));
        ValidateVtable(vtable, "+0x00_main_vtable");

        // Compare against the refdb-resolved I_Mod primary vtable
        // (curated name "ImodVtable_primary") — different class, but the seed
        // prose's "I_Mod primary/subobject" hint deserves an explicit compare
        // line for the reader. found=false → fail-loud already logged by
        // refdb under category REFDB; skip the compare and continue the dump.
        const auto primaryRes =
            kcdx::refdb::ResolveByName("ImodVtable_primary");
        if (!primaryRes.found) {
            LOG_ERROR_KV(kCat, "vtable_dump",
                kcdx::log::KV::BareStr("compare",
                                       "+0x00_vs_imod_vtable_primary_skipped"),
                kcdx::log::KV::BareStr("name", "ImodVtable_primary"),
                kcdx::log::KV::BareStr("detail",
                    "refdb::ResolveByName(ImodVtable_primary) returned "
                    "not-found; the compare line is skipped this fire. See "
                    "the preceding REFDB ERROR for the specific reason token"));
        } else {
            const uintptr_t expectedPrimary = g_whgameBase + primaryRes.rva;
            LOG_INFO_KV(kCat, "vtable_dump",
                kcdx::log::KV::BareStr("compare",
                                       "+0x00_vs_imod_vtable_primary"),
                kcdx::log::KV("at_plus_00",
                              reinterpret_cast<uintptr_t>(vtable)),
                kcdx::log::KV("expected_imod_vtable_primary", expectedPrimary),
                kcdx::log::KV("matches",
                              reinterpret_cast<uintptr_t>(vtable)
                              == expectedPrimary));
        }
    }

    // Vtable validation at +0x18 (sub-object vptr per ctor disassembly at
    // 0x180da0f15: `lea r9, [rip + 0x2d01f13]` ... `mov [rbx + 0x18], rdi`
    // — actually zeroed in the disassembly; the seed prose says +0x18 is
    // the sub-object vptr but the visible ctor xor-zeroes rdi → +0x18.
    // Read what's THERE post-ctor regardless of which interpretation holds).
    {
        void* subVtable = nullptr;
        std::memcpy(&subVtable, reinterpret_cast<const uint8_t*>(obj) + 0x18,
                    sizeof(subVtable));
        if (subVtable) {
            ValidateVtable(subVtable, "+0x18_sub_vtable");
            const auto subRes =
                kcdx::refdb::ResolveByName("ImodVtable_subobject");
            if (!subRes.found) {
                LOG_ERROR_KV(kCat, "vtable_dump",
                    kcdx::log::KV::BareStr("compare",
                        "+0x18_vs_imod_vtable_subobject_skipped"),
                    kcdx::log::KV::BareStr("name", "ImodVtable_subobject"),
                    kcdx::log::KV::BareStr("detail",
                        "refdb::ResolveByName(ImodVtable_subobject) returned "
                        "not-found; the compare line is skipped this fire. "
                        "See the preceding REFDB ERROR for the specific "
                        "reason token"));
            } else {
                const uintptr_t expectedSub = g_whgameBase + subRes.rva;
                LOG_INFO_KV(kCat, "vtable_dump",
                    kcdx::log::KV::BareStr("compare",
                                           "+0x18_vs_imod_vtable_subobject"),
                    kcdx::log::KV("at_plus_18",
                                  reinterpret_cast<uintptr_t>(subVtable)),
                    kcdx::log::KV("expected_imod_vtable_subobject", expectedSub),
                    kcdx::log::KV("matches",
                                  reinterpret_cast<uintptr_t>(subVtable)
                                  == expectedSub));
            }
        }
    }

    // CryString deep dump at +0x10.
    DumpCryStringAtOffset10(obj);

    // Vector walks.
    WalkEnabledList(obj);
    WalkScannedList(obj);

    LOG_INFO_KV(kCat, "point_c_summary",
        kcdx::log::KV("obj",          reinterpret_cast<uintptr_t>(obj)),
        kcdx::log::KV("return_value", ret));

    return ret;
}

}  // namespace

// === POINT B — SELECT entry (called from select_detour.cpp) ================

void OnSelectEntry(void* self) {
    bool expected = false;
    if (!g_capturedB.compare_exchange_strong(expected, true,
                                             std::memory_order_acq_rel)) {
        return;
    }

    LOG_INFO_KV(kCat, "point_b_entry",
        kcdx::log::KV("self",         reinterpret_cast<uintptr_t>(self)),
        kcdx::log::KV("object_size",  static_cast<uintptr_t>(kObjectSize)),
        kcdx::log::KV("whgame_base",  g_whgameBase),
        kcdx::log::KV("text_lo",      g_whgameTextLo),
        kcdx::log::KV("text_hi",      g_whgameTextHi),
        kcdx::log::KV("image_hi",     g_whgameImageHi));

    if (!self) {
        LOG_INFO_KV(kCat, "point_b_summary",
            kcdx::log::KV::BareStr("note",
                                   "self null — nothing to dump"));
        return;
    }

    DumpAllSlots(self, "point_b_slot", "point_b_slot");

    LOG_INFO_KV(kCat, "point_b_summary",
        kcdx::log::KV("self", reinterpret_cast<uintptr_t>(self)));
}

bool Install() {
    bool expected = false;
    if (!g_installed.compare_exchange_strong(expected, true,
                                             std::memory_order_acq_rel)) {
        return g_installSucceeded.load(std::memory_order_acquire);
    }

    HMODULE whgame = GetModuleHandleW(L"WHGame.dll");
    if (!whgame) {
        LOG_ERROR_KV(kCat, "install_failed",
            kcdx::log::KV::BareStr("reason",
                "WHGame.dll not mapped at Install time — the ctor probe "
                "cannot be installed this boot"));
        return false;
    }

    // Capture WHGame base + .text bounds for classification. If the PE walk
    // fails we still install — the dev log will show every slot as
    // "unknown_ptr" / "heap_ptr" since the .text gate never triggers, but
    // the deref + dump path remains functional.
    if (!ResolveWhgameBounds(whgame)) {
        LOG_WARN_KV(kCat, "install_warn",
            kcdx::log::KV::BareStr("detail",
                "WHGame PE-header walk failed — vtable_rva / image_ptr "
                "classification disabled; probe continues with reduced "
                "labeling fidelity"));
    } else {
        LOG_INFO_KV(kCat, "install_info",
            kcdx::log::KV::BareStr("detail", "whgame bounds resolved"),
            kcdx::log::KV("base",     g_whgameBase),
            kcdx::log::KV("text_lo",  g_whgameTextLo),
            kcdx::log::KV("text_hi",  g_whgameTextHi),
            kcdx::log::KV("image_hi", g_whgameImageHi));
    }

    // Resolve the ctor address via the refdb curated name. found=false means
    // the canonical name is not in the DB for the running build — fail-loud
    // already logged by refdb under category REFDB; the install aborts.
    const auto ctorRes = kcdx::refdb::ResolveByName("ModManager_ctor");
    if (!ctorRes.found) {
        LOG_ERROR_KV(kCat, "install_failed",
            kcdx::log::KV::BareStr("reason",
                "refdb::ResolveByName(ModManager_ctor) returned not-found — "
                "the canonical name is absent or its row is not verified for "
                "the running build; the ctor probe is inactive this boot. "
                "See the preceding REFDB ERROR for the specific reason token"),
            kcdx::log::KV::BareStr("name", "ModManager_ctor"));
        return false;
    }
    const uintptr_t target = g_whgameBase + ctorRes.rva;

    MH_STATUS si = MH_Initialize();
    if (si != MH_OK && si != MH_ERROR_ALREADY_INITIALIZED) {
        LOG_ERROR_KV(kCat, "install_failed",
            kcdx::log::KV::BareStr("reason",
                "MH_Initialize failed; the ctor probe is inactive this boot"),
            kcdx::log::KV("mh_status", static_cast<long long>(si)));
        return false;
    }

    void* targetPtr = reinterpret_cast<void*>(target);
    void* origPtr   = nullptr;
    MH_STATUS s = MH_CreateHook(targetPtr,
                                reinterpret_cast<void*>(&HookedCtor),
                                &origPtr);
    if (s != MH_OK) {
        LOG_ERROR_KV(kCat, "install_failed",
            kcdx::log::KV::BareStr("reason",
                "MH_CreateHook on ModManager_ctor failed; the ctor probe is "
                "inactive this boot"),
            kcdx::log::KV("target", reinterpret_cast<uintptr_t>(targetPtr)),
            kcdx::log::KV("mh_status", static_cast<long long>(s)));
        return false;
    }
    g_orig.store(reinterpret_cast<CtorFn_t>(origPtr),
                 std::memory_order_release);

    s = MH_EnableHook(targetPtr);
    if (s != MH_OK) {
        LOG_ERROR_KV(kCat, "install_failed",
            kcdx::log::KV::BareStr("reason",
                "MH_EnableHook on ModManager_ctor failed; the ctor probe is "
                "inactive this boot"),
            kcdx::log::KV("target", reinterpret_cast<uintptr_t>(targetPtr)),
            kcdx::log::KV("mh_status", static_cast<long long>(s)));
        return false;
    }

    LOG_INFO_KV(kCat, "install_ok",
        kcdx::log::KV("target", reinterpret_cast<uintptr_t>(targetPtr)),
        kcdx::log::KV::BareStr("name", "ModManager_ctor"),
        kcdx::log::KV::BareStr("detail",
            "init-cycle observation probe armed — POINT A (ctor entry), "
            "POINT B (SELECT entry via select_detour OnSelectEntry call), "
            "POINT C (ctor return) one-shots will dump on first fire under "
            "category MOD_ABSORB_PROBE"));
    g_installSucceeded.store(true, std::memory_order_release);
    return true;
}

}  // namespace kcdx::mod_absorb::ctor_probe
