#include "pak_reader.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../log.h"

namespace kcdx::fs_takeover {

namespace {

constexpr const char* kCat = "PAK_READER";

// PKZIP signatures (format constants fixed by the ZIP spec — NOT game-binary
// targets, so no Address Library involvement; see file-system-takeover §6).
constexpr uint32_t kSigEOCD       = 0x06054b50;  // "PK\x05\x06" end-of-central-dir
constexpr uint32_t kSigCDR        = 0x02014b50;  // "PK\x01\x02" central-dir file header
constexpr uint32_t kSigZip64EOCD  = 0x06064b50;  // "PK\x06\x06" zip64 end-of-central-dir
constexpr uint32_t kSigZip64Loc   = 0x07064b50;  // "PK\x06\x07" zip64 EOCD locator

// Fixed-size record layouts (bytes), per the PKZIP spec.
constexpr size_t kEOCDSize   = 22;  // EOCD fixed part (before any comment)
constexpr size_t kCDRFixed   = 46;  // central-dir file header fixed part (before name/extra/comment)

// The PKZIP comment-length field is 16-bit, so the EOCD can sit at most this
// far back from EOF (max comment + the EOCD record itself).
constexpr size_t kMaxCommentScan = 0xFFFF + kEOCDSize;

// Defensive caps for untrusted input — cap BEFORE allocating for a count.
// A vanilla pak the engine ships holds thousands of entries, not millions; a
// count past this is a corrupt/hostile EOCD, rejected before any allocation.
constexpr uint32_t kMaxEntries = 5'000'000;
// A single entry name longer than this is a malformed/hostile record.
constexpr size_t kMaxNameLen = 0x10000;  // 64 KiB

// zip64 sentinel values: a 16-bit field reading 0xFFFF or a 32-bit field
// reading 0xFFFFFFFF in the EOCD means "the real value is in the zip64 EOCD",
// i.e. the archive is zip64. The design forbids zip64 — encountering a sentinel
// is a loud failure, not a silent mis-read.
constexpr uint16_t kZ16 = 0xFFFFu;
constexpr uint32_t kZ32 = 0xFFFFFFFFu;

// Little-endian field reads from a byte buffer at a validated offset. The
// callers bounds-check the offset+width against the buffer before each read.
uint16_t Read16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}
uint32_t Read32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// RAII for the kcdx-CRT FILE* — closes on every return path.
struct FileGuard {
    FILE* f = nullptr;
    ~FileGuard() { if (f) std::fclose(f); }
};

}  // namespace

bool ParsePakCentralDirectory(const std::wstring& pakPath,
                              std::vector<PakEntry>& outEntries,
                              std::string& outError) {
    outEntries.clear();
    outError.clear();

    // Narrow path tail for log context (the wide path does not format through
    // the printf-style logger). Last path component is enough to locate the pak.
    auto pakTag = [&]() -> std::string {
        std::string narrow;
        narrow.reserve(pakPath.size());
        for (wchar_t c : pakPath) narrow.push_back(c < 128 ? static_cast<char>(c) : '?');
        size_t slash = narrow.find_last_of("/\\");
        return slash == std::string::npos ? narrow : narrow.substr(slash + 1);
    };
    auto fail = [&](const std::string& why) -> bool {
        outEntries.clear();
        outError = why;
        LOG_ERROR("PAK_READER", "CDR parse failed pak=%s: %s",
                  pakTag().c_str(), why.c_str());
        return false;
    };

    // Open on kcdx's own CRT (kcdx _wfopen) — the engine's ucrtbase never
    // touches this read (design §6, the cross-CRT crash-free guarantee).
    FileGuard g;
    if (_wfopen_s(&g.f, pakPath.c_str(), L"rb") != 0 || g.f == nullptr) {
        return fail("could not open pak file on kcdx CRT (_wfopen_s)");
    }

    // File size (kcdx 64-bit fseek/ftell on the kcdx CRT). 64-bit width so a
    // pak >2GB (plausible for an untrusted texture mod) reports its true size
    // and seeks land — a 32-bit long truncates/negates above 2^31 (design §6:
    // never mis-read malformed/large input).
    if (_fseeki64(g.f, 0, SEEK_END) != 0) return fail("fseek to EOF failed");
    __int64 sizeL = _ftelli64(g.f);
    if (sizeL < 0) return fail("ftell returned a negative size");
    const uint64_t fileSize = static_cast<uint64_t>(sizeL);
    if (fileSize < kEOCDSize) {
        return fail("file too small to contain an EOCD record");
    }

    // --- Locate the EOCD: read the tail and scan backward for the signature.
    const uint64_t tailLen = fileSize < kMaxCommentScan ? fileSize : kMaxCommentScan;
    const uint64_t tailStart = fileSize - tailLen;
    std::vector<uint8_t> tail(static_cast<size_t>(tailLen));
    if (_fseeki64(g.f, static_cast<__int64>(tailStart), SEEK_SET) != 0) {
        return fail("fseek to EOCD tail failed");
    }
    if (std::fread(tail.data(), 1, tail.size(), g.f) != tail.size()) {
        return fail("fread of EOCD tail came up short");
    }

    // Reject zip64 BEFORE trusting the classic EOCD: a zip64 EOCD or locator in
    // the tail means the archive is zip64, which the reader does not support.
    for (size_t i = 0; i + 4 <= tail.size(); ++i) {
        const uint32_t sig = Read32(tail.data() + i);
        if (sig == kSigZip64EOCD || sig == kSigZip64Loc) {
            return fail("zip64 archive (zip64 EOCD/locator present) — unsupported");
        }
    }

    // Scan backward from the latest possible EOCD start for the signature. (The
    // EOCD's variable comment can in principle contain the signature bytes; the
    // last match whose declared comment-length reaches exactly EOF is the real
    // one — checked below. Scanning from the back finds it first.)
    size_t eocd = SIZE_MAX;
    if (tail.size() >= kEOCDSize) {
        for (size_t i = tail.size() - kEOCDSize + 1; i-- > 0;) {
            if (Read32(tail.data() + i) == kSigEOCD) { eocd = i; break; }
        }
    }
    if (eocd == SIZE_MAX) {
        return fail("no EOCD signature found at the file tail (not a PKZIP file?)");
    }

    // --- Read the EOCD fields (all little-endian, fixed offsets from the sig).
    const uint8_t* e = tail.data() + eocd;
    const uint16_t entriesThisDisk = Read16(e + 8);
    const uint16_t entriesTotal16  = Read16(e + 10);
    const uint32_t cdrSize32       = Read32(e + 12);
    const uint32_t cdrOffset32     = Read32(e + 16);
    const uint16_t commentLen      = Read16(e + 20);

    // A zip64 sentinel in any EOCD count/size/offset field means the true value
    // lives in a zip64 record we already rejected — but a malformed file could
    // carry a sentinel without the zip64 record, so reject it here too.
    if (entriesTotal16 == kZ16 || entriesThisDisk == kZ16 ||
        cdrSize32 == kZ32 || cdrOffset32 == kZ32) {
        return fail("zip64 sentinel in EOCD (0xFFFF/0xFFFFFFFF field) — unsupported");
    }

    // The EOCD's declared comment must reach exactly EOF — guards against a
    // false signature match inside an entry's data.
    const uint64_t eocdAbs = tailStart + eocd;
    if (eocdAbs + kEOCDSize + commentLen != fileSize) {
        return fail("EOCD comment length does not reach EOF (false signature match?)");
    }

    // A multi-disk/spanned archive declares fewer entries on this disk than in
    // total. Reject it explicitly (the CDR walk would also run past the buffer
    // and fail, but this names the cause). The reader serves single-file paks.
    if (entriesThisDisk != entriesTotal16) {
        return fail("spanned/multi-disk archive — unsupported");
    }

    const uint32_t entriesTotal = entriesTotal16;
    const uint64_t cdrSize   = cdrSize32;
    const uint64_t cdrOffset = cdrOffset32;

    // --- Bounds-check the CDR region against the file BEFORE reading it.
    if (cdrOffset > fileSize || cdrSize > fileSize ||
        cdrOffset + cdrSize > fileSize) {
        return fail("CDR offset/size points outside the file");
    }
    // The CDR must end at or before the EOCD it is described by.
    if (cdrOffset + cdrSize > eocdAbs) {
        return fail("CDR region overlaps or extends past the EOCD");
    }
    // Cap the entry count BEFORE allocating for it (untrusted external input).
    if (entriesTotal > kMaxEntries) {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "EOCD entry count %u exceeds the sane cap %u", entriesTotal, kMaxEntries);
        return fail(buf);
    }
    // A non-empty entry count needs at least the fixed CDR header per entry.
    if (entriesTotal != 0 && cdrSize < kCDRFixed) {
        return fail("CDR size too small for the declared entry count");
    }

    // --- Read the whole CDR region into a buffer (kcdx fread on kcdx CRT).
    std::vector<uint8_t> cdr(static_cast<size_t>(cdrSize));
    if (cdrSize != 0) {
        if (_fseeki64(g.f, static_cast<__int64>(cdrOffset), SEEK_SET) != 0) {
            return fail("fseek to CDR offset failed");
        }
        if (std::fread(cdr.data(), 1, cdr.size(), g.f) != cdr.size()) {
            return fail("fread of the central directory came up short");
        }
    }

    // Reserve only against the validated (capped) count — never the raw EOCD
    // figure (which the cap above has already bounded).
    outEntries.reserve(entriesTotal);

    // --- Walk each central-directory file header, fully bounds-checked.
    size_t pos = 0;
    for (uint32_t i = 0; i < entriesTotal; ++i) {
        // The fixed header must fit in what remains of the CDR buffer.
        if (pos + kCDRFixed > cdr.size()) {
            return fail("central-directory record runs past the CDR buffer");
        }
        const uint8_t* r = cdr.data() + pos;
        if (Read32(r) != kSigCDR) {
            char buf[96];
            std::snprintf(buf, sizeof(buf),
                          "bad central-directory record signature at entry %u", i);
            return fail(buf);
        }

        PakEntry entry;
        entry.method            = Read16(r + 10);
        entry.crc32             = Read32(r + 16);
        entry.compressed_size   = Read32(r + 20);
        entry.uncompressed_size = Read32(r + 24);
        const uint16_t nameLen  = Read16(r + 28);
        const uint16_t extraLen = Read16(r + 30);
        const uint16_t commentL = Read16(r + 32);
        entry.local_header_offset = Read32(r + 42);

        // A sentinel size/offset is the per-entry zip64 marker — reject loud.
        if (entry.compressed_size == kZ32 || entry.uncompressed_size == kZ32 ||
            entry.local_header_offset == kZ32) {
            char buf[96];
            std::snprintf(buf, sizeof(buf),
                          "zip64 sentinel in central-directory entry %u — unsupported", i);
            return fail(buf);
        }

        // Cap the name length before reading it (defends against a corrupt
        // length looping forever / reading past the buffer).
        if (nameLen > kMaxNameLen) {
            return fail("central-directory entry name length exceeds the cap");
        }
        // The variable section (name + extra + comment) must fit the buffer.
        const size_t varLen = static_cast<size_t>(nameLen) +
                              static_cast<size_t>(extraLen) +
                              static_cast<size_t>(commentL);
        if (pos + kCDRFixed + varLen > cdr.size()) {
            return fail("central-directory entry name/extra/comment runs past the CDR buffer");
        }

        const char* nameP = reinterpret_cast<const char*>(r + kCDRFixed);
        entry.name.assign(nameP, nameLen);

        // The local-header offset must point inside the file, before the CDR.
        if (entry.local_header_offset >= cdrOffset) {
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                          "entry %u local-header offset 0x%llx is not before the CDR", i,
                          static_cast<unsigned long long>(entry.local_header_offset));
            return fail(buf);
        }

        outEntries.push_back(std::move(entry));

        // Advance to the next record (the +varLen cannot overflow: each term is
        // a 16-bit field and pos is bounded by cdr.size() above).
        pos += kCDRFixed + varLen;
    }

    LOG_DEBUG_KV("PAK_READER", "parsed",
                 log::KV("pak", pakTag()),
                 log::KV("entries", static_cast<uint64_t>(outEntries.size())),
                 log::KV("cdr_off", static_cast<uint64_t>(cdrOffset)),
                 log::KV("cdr_size", static_cast<uint64_t>(cdrSize)));
    return true;
}

}  // namespace kcdx::fs_takeover
