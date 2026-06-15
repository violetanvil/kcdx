#include "pak_reader.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "miniz.h"
// miniz.h defines a zlib-compat `crc32` → `mz_crc32` alias macro; undo it so it
// does not clobber PakEntry::crc32 (the member access `entry.crc32` would
// otherwise textually rewrite to `entry.mz_crc32`). kcdx calls `mz_crc32`
// directly, so dropping the unqualified alias loses nothing.
#undef crc32

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
constexpr uint32_t kSigLFH        = 0x04034b50;  // "PK\x03\x04" local file header

// PKZIP compression methods kcdx serves (design §6 — only 0 and 8 occur).
constexpr uint16_t kMethodStored  = 0;
constexpr uint16_t kMethodDeflate = 8;

// Fixed-size record layouts (bytes), per the PKZIP spec.
constexpr size_t kEOCDSize   = 22;  // EOCD fixed part (before any comment)
constexpr size_t kCDRFixed   = 46;  // central-dir file header fixed part (before name/extra/comment)
constexpr size_t kLFHFixed   = 30;  // local file header fixed part (before name/extra)

// A single pak entry is an asset file — large but bounded. Cap the per-entry
// compressed AND uncompressed sizes BEFORE allocating a read/inflate buffer, so
// a corrupt/hostile entry claiming a multi-GB size cannot trigger a huge
// allocation. 512 MiB comfortably exceeds the largest real vanilla asset (the
// observed largest GeomCaches entry is ~9.4 MB) while bounding a zip-bomb.
constexpr uint64_t kMaxEntryBytes = 512ull * 1024 * 1024;  // 512 MiB

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

bool ReadPakEntry(const std::wstring& pakPath,
                  const PakEntry& entry,
                  std::vector<uint8_t>& outBytes,
                  std::string& outError) {
    outBytes.clear();
    outError.clear();

    auto pakTag = [&]() -> std::string {
        std::string narrow;
        narrow.reserve(pakPath.size());
        for (wchar_t c : pakPath) narrow.push_back(c < 128 ? static_cast<char>(c) : '?');
        size_t slash = narrow.find_last_of("/\\");
        return slash == std::string::npos ? narrow : narrow.substr(slash + 1);
    };
    auto fail = [&](const std::string& why) -> bool {
        outBytes.clear();
        outError = why;
        LOG_ERROR("PAK_READER", "entry read failed pak=%s entry='%s': %s",
                  pakTag().c_str(), entry.name.c_str(), why.c_str());
        return false;
    };

    // Cap the declared sizes BEFORE allocating any read/inflate buffer — a
    // corrupt entry claiming a multi-GB size must not drive a huge allocation
    // (untrusted external input; also bounds a zip-bomb's inflate target).
    if (entry.compressed_size > kMaxEntryBytes) {
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "compressed_size %llu exceeds the sane per-entry cap %llu",
                      static_cast<unsigned long long>(entry.compressed_size),
                      static_cast<unsigned long long>(kMaxEntryBytes));
        return fail(buf);
    }
    if (entry.uncompressed_size > kMaxEntryBytes) {
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "uncompressed_size %llu exceeds the sane per-entry cap %llu",
                      static_cast<unsigned long long>(entry.uncompressed_size),
                      static_cast<unsigned long long>(kMaxEntryBytes));
        return fail(buf);
    }
    // Reject an unsupported method up front (design §6 — only STORED/DEFLATE).
    if (entry.method != kMethodStored && entry.method != kMethodDeflate) {
        char buf[96];
        std::snprintf(buf, sizeof(buf),
                      "unsupported compression method %u (only 0=STORED, 8=DEFLATE)",
                      entry.method);
        return fail(buf);
    }
    // STORED stores the bytes verbatim — the two sizes must agree.
    if (entry.method == kMethodStored &&
        entry.compressed_size != entry.uncompressed_size) {
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "STORED entry compressed_size %llu != uncompressed_size %llu",
                      static_cast<unsigned long long>(entry.compressed_size),
                      static_cast<unsigned long long>(entry.uncompressed_size));
        return fail(buf);
    }

    // Open on kcdx's own CRT — the engine's ucrtbase never touches this read
    // (design §6 / §4.4, the cross-CRT crash-free guarantee).
    FileGuard g;
    if (_wfopen_s(&g.f, pakPath.c_str(), L"rb") != 0 || g.f == nullptr) {
        return fail("could not open pak file on kcdx CRT (_wfopen_s)");
    }

    // File size (kcdx 64-bit fseek/ftell) — every offset is bounds-checked
    // against this before a read (an entry pointing past EOF fails loud).
    if (_fseeki64(g.f, 0, SEEK_END) != 0) return fail("fseek to EOF failed");
    __int64 sizeL = _ftelli64(g.f);
    if (sizeL < 0) return fail("ftell returned a negative size");
    const uint64_t fileSize = static_cast<uint64_t>(sizeL);

    // --- Parse the LOCAL FILE HEADER at entry.local_header_offset.
    // The LFH carries its OWN name/extra lengths (which can differ from the
    // central-directory record's); the compressed data starts AFTER them. The
    // CDR's lengths are NOT reused here.
    if (entry.local_header_offset > fileSize ||
        entry.local_header_offset + kLFHFixed > fileSize) {
        return fail("local-header offset + fixed header runs past EOF");
    }
    uint8_t lfh[kLFHFixed];
    if (_fseeki64(g.f, static_cast<__int64>(entry.local_header_offset), SEEK_SET) != 0) {
        return fail("fseek to local file header failed");
    }
    if (std::fread(lfh, 1, kLFHFixed, g.f) != kLFHFixed) {
        return fail("fread of the local file header came up short");
    }
    if (Read32(lfh) != kSigLFH) {
        char buf[96];
        std::snprintf(buf, sizeof(buf),
                      "local file header signature is 0x%08x, not PK\\x03\\x04",
                      Read32(lfh));
        return fail(buf);
    }
    const uint16_t lfhNameLen  = Read16(lfh + 26);
    const uint16_t lfhExtraLen = Read16(lfh + 28);

    // Compute the data start from the LFH's own lengths and bounds-check the
    // whole compressed extent against the file (each term is bounded: the LFH
    // offset by fileSize above, the two 16-bit lengths by their type, the
    // compressed size by kMaxEntryBytes above — the sum cannot overflow u64).
    const uint64_t dataStart = entry.local_header_offset + kLFHFixed +
                               static_cast<uint64_t>(lfhNameLen) +
                               static_cast<uint64_t>(lfhExtraLen);
    if (dataStart > fileSize || dataStart + entry.compressed_size > fileSize) {
        return fail("entry data (start + compressed_size) runs past EOF");
    }

    // --- Read the compressed bytes (kcdx fread on kcdx CRT).
    std::vector<uint8_t> comp(static_cast<size_t>(entry.compressed_size));
    if (entry.compressed_size != 0) {
        if (_fseeki64(g.f, static_cast<__int64>(dataStart), SEEK_SET) != 0) {
            return fail("fseek to entry data start failed");
        }
        if (std::fread(comp.data(), 1, comp.size(), g.f) != comp.size()) {
            return fail("fread of the entry compressed bytes came up short");
        }
    }

    // --- Deliver the uncompressed bytes.
    if (entry.method == kMethodStored) {
        outBytes = std::move(comp);  // STORED — the bytes ARE the output.
    } else {
        // DEFLATE — inflate into a buffer sized to the DECLARED uncompressed
        // size (the cap above already bounded it). flags=0 → raw DEFLATE (no
        // zlib header), matching a PKZIP method-8 entry's stored stream. The
        // output buffer is never grown on the fly, so a zip-bomb is bounded to
        // the declared size; a stream that wants more fails (FAILED or a short
        // written count that the length check below rejects).
        outBytes.resize(static_cast<size_t>(entry.uncompressed_size));
        const size_t written = tinfl_decompress_mem_to_mem(
            outBytes.data(), outBytes.size(),
            comp.data(), comp.size(),
            /*flags=*/0);
        if (written == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) {
            outBytes.clear();
            return fail("tinfl_decompress_mem_to_mem failed to inflate the DEFLATE stream");
        }
        if (written != entry.uncompressed_size) {
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                          "inflated %llu bytes != declared uncompressed_size %llu",
                          static_cast<unsigned long long>(written),
                          static_cast<unsigned long long>(entry.uncompressed_size));
            return fail(buf);
        }
    }

    // --- Verify the uncompressed bytes' CRC-32 against the recorded value.
    // The strongest end-to-end correctness check: a wrong local-header parse,
    // seek, inflate flag, or off-by-N yields a wrong CRC. Always-on (cold path,
    // the cost is irrelevant; the correctness is not — design §6).
    const mz_ulong crc = mz_crc32(mz_crc32(0, nullptr, 0),
                                  outBytes.data(), outBytes.size());
    if (static_cast<uint32_t>(crc) != entry.crc32) {
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "CRC-32 mismatch: computed 0x%08x, recorded 0x%08x",
                      static_cast<uint32_t>(crc), entry.crc32);
        return fail(buf);
    }

    LOG_DEBUG_KV("PAK_READER", "read_entry",
                 log::KV("pak", pakTag()),
                 log::KV("name", entry.name),
                 log::KV("method", static_cast<uint64_t>(entry.method)),
                 log::KV("usize", static_cast<uint64_t>(outBytes.size())),
                 log::KV("data_off", dataStart));
    return true;
}

}  // namespace kcdx::fs_takeover
