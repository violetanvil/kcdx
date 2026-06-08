#include "survival.h"

#include <windows.h>

#include <cstdio>      // FILE i/o
#include <cstring>     // memcmp
#include <filesystem>  // std::filesystem::path
#include <string>
#include <vector>

#include "blake3.h"
#include "log.h"
#include "paths.h"     // ToUtf8 (path -> std::string under C++20+ char8_t)
#include "pe_helpers.h"

namespace kcdx::survival {

namespace {

const char* kCategory = "SURVIVAL";

// Encode a raw digest as lowercase hex (for log lines that show the mismatch).
std::string ToHex(const uint8_t* p, size_t n) {
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        out.push_back(kHex[(p[i] >> 4) & 0xF]);
        out.push_back(kHex[p[i] & 0xF]);
    }
    return out;
}

// Build a CannotCheck result with the given reason token (already logged).
Result CannotCheck(const char* reason) {
    Result r;
    r.status = Status::CannotCheck;
    r.reason = reason;
    return r;
}

// Resolve WHGame.dll's on-disk path. Returns false if the module is not loaded
// (handle null) or the path could not be retrieved.
bool ResolveModulePath(std::wstring& pathOut, bool& moduleLoaded) {
    moduleLoaded = false;
    HMODULE h = GetModuleHandleW(L"WHGame.dll");
    if (!h) {
        return false;  // not mapped
    }
    moduleLoaded = true;

    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(h, buf, static_cast<DWORD>(std::size(buf)));
    if (n == 0 || n >= std::size(buf)) {
        return false;  // path not resolvable / truncated
    }
    pathOut.assign(buf, n);
    return true;
}

// Read the whole on-disk file into `out`. Returns false on open/read failure.
bool ReadWholeFile(const std::wstring& path, std::vector<uint8_t>& out) {
    HANDLE hf = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
    if (hf == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER size;
    if (!GetFileSizeEx(hf, &size) || size.QuadPart <= 0) {
        CloseHandle(hf);
        return false;
    }
    out.resize(static_cast<size_t>(size.QuadPart));

    size_t total = 0;
    while (total < out.size()) {
        DWORD want = static_cast<DWORD>(
            (out.size() - total) > 0x40000000u ? 0x40000000u
                                                : (out.size() - total));
        DWORD got = 0;
        if (!ReadFile(hf, out.data() + total, want, &got, nullptr) || got == 0) {
            CloseHandle(hf);
            return false;
        }
        total += got;
    }
    CloseHandle(hf);
    return true;
}

}  // namespace

Result SurvivalCheck(uint32_t rva, size_t length,
                     const uint8_t* expectedHash32, size_t expectedLen) {
    // --- Non-byte entity: an empty expected hash is "not applicable", NEVER a
    // "Changed". (A vtable slot / data offset carries no content_hash.) ------
    if (expectedHash32 == nullptr || expectedLen == 0) {
        LOG_DEBUG_KV(kCategory, "not_applicable",
            ::kcdx::log::KV("reason", "not_applicable"),
            ::kcdx::log::KV("rva", (unsigned long long)rva),
            ::kcdx::log::KV("note", "empty expected hash — non-byte entity, no check"));
        return CannotCheck("not_applicable");
    }

    // A present-but-wrong-width expected hash is a malformed DB row, not a
    // mismatch — fail loud, do not compare a truncated digest.
    if (expectedLen != kHashLen) {
        LOG_ERROR_KV(kCategory, "expected_hash_bad_length",
            ::kcdx::log::KV("reason", "expected_hash_bad_length"),
            ::kcdx::log::KV("rva", (unsigned long long)rva),
            ::kcdx::log::KV("expected_len", (unsigned long long)expectedLen),
            ::kcdx::log::KV("want_len", (unsigned long long)kHashLen));
        return CannotCheck("expected_hash_bad_length");
    }

    if (length == 0) {
        LOG_ERROR_KV(kCategory, "length_zero",
            ::kcdx::log::KV("reason", "length_zero"),
            ::kcdx::log::KV("rva", (unsigned long long)rva),
            ::kcdx::log::KV("note", "zero-length span — nothing to hash"));
        return CannotCheck("length_zero");
    }

    // --- Locate WHGame.dll's on-disk path. ---------------------------------
    std::wstring modulePath;
    bool moduleLoaded = false;
    if (!ResolveModulePath(modulePath, moduleLoaded)) {
        if (!moduleLoaded) {
            LOG_ERROR_KV(kCategory, "module_not_mapped",
                ::kcdx::log::KV("reason", "module_not_mapped"),
                ::kcdx::log::KV("module", "WHGame.dll"),
                ::kcdx::log::KV("note", "module not loaded in process"));
            return CannotCheck("module_not_mapped");
        }
        LOG_ERROR_KV(kCategory, "file_not_found",
            ::kcdx::log::KV("reason", "file_not_found"),
            ::kcdx::log::KV("module", "WHGame.dll"),
            ::kcdx::log::KV("note", "on-disk path could not be resolved"));
        return CannotCheck("file_not_found");
    }

    // --- Read the ON-DISK backing file (NOT live memory — the crux). --------
    std::vector<uint8_t> fileData;
    if (!ReadWholeFile(modulePath, fileData)) {
        std::string path8 = kcdx::paths::ToUtf8(std::filesystem::path(modulePath));
        LOG_ERROR_KV(kCategory, "file_open_error",
            ::kcdx::log::KV("reason", "file_open_error"),
            ::kcdx::log::KV("path", path8),
            ::kcdx::log::KV("note", "could not open/read the module's on-disk file"));
        return CannotCheck("file_open_error");
    }

    // --- Map [rva, rva+length) to its on-disk file offset. ------------------
    size_t fileOffset = 0;
    if (!pe::RvaToFileOffsetOnDisk(fileData.data(), fileData.size(),
                                   rva, length, fileOffset)) {
        LOG_ERROR_KV(kCategory, "rva_out_of_range",
            ::kcdx::log::KV("reason", "rva_out_of_range"),
            ::kcdx::log::KV("rva", (unsigned long long)rva),
            ::kcdx::log::KV("length", (unsigned long long)length),
            ::kcdx::log::KV("file_size", (unsigned long long)fileData.size()),
            ::kcdx::log::KV("note", "span maps to no on-disk section"));
        return CannotCheck("rva_out_of_range");
    }

    // Defensive: RvaToFileOffsetOnDisk already bounds the span to the buffer,
    // but re-check before the read so a future helper-change can never read OOB.
    if (fileOffset > fileData.size() || length > fileData.size() - fileOffset) {
        LOG_ERROR_KV(kCategory, "read_error",
            ::kcdx::log::KV("reason", "read_error"),
            ::kcdx::log::KV("file_offset", (unsigned long long)fileOffset),
            ::kcdx::log::KV("length", (unsigned long long)length),
            ::kcdx::log::KV("file_size", (unsigned long long)fileData.size()),
            ::kcdx::log::KV("note", "mapped span runs past the file buffer"));
        return CannotCheck("read_error");
    }

    // --- Hash the on-disk span and compare. ---------------------------------
    uint8_t got[blake3::kHashLen];
    blake3::Hash256(fileData.data() + fileOffset, length, got);

    if (std::memcmp(got, expectedHash32, kHashLen) == 0) {
        LOG_DEBUG_KV(kCategory, "unchanged",
            ::kcdx::log::KV("rva", (unsigned long long)rva),
            ::kcdx::log::KV("length", (unsigned long long)length),
            ::kcdx::log::KV("hash", ToHex(got, kHashLen)));
        Result r;
        r.status = Status::Unchanged;
        return r;
    }

    LOG_WARN_KV(kCategory, "changed",
        ::kcdx::log::KV("rva", (unsigned long long)rva),
        ::kcdx::log::KV("length", (unsigned long long)length),
        ::kcdx::log::KV("computed", ToHex(got, kHashLen)),
        ::kcdx::log::KV("expected", ToHex(expectedHash32, kHashLen)));
    Result r;
    r.status = Status::Changed;
    return r;
}

}  // namespace kcdx::survival
