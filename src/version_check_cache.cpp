// version_check_cache — implementation. See version_check_cache.h for the
// contract + the on-disk byte layout rationale.

#include "version_check_cache.h"

#include <cstdio>      // FILE i/o for the on-disk read/write
#include <cstring>     // memcmp
#include <filesystem>
#include <system_error>
#include <unordered_map>

#include "log.h"
#include "paths.h"

namespace kcdx::version_check_cache {

namespace {

const char* kCategory = "VERCACHE";

namespace fs = std::filesystem;

// ----------------------------------------------------------------------------
// Wire-format constants. The codec discipline mirrors lua_cosave_serial: a
// magic prefix + a wire format-version byte; an unknown version is REFUSED
// rather than mis-parsed.
// ----------------------------------------------------------------------------

// Magic — 'K','V' (Kcdx Version-check). Distinguishes this file from arbitrary
// bytes and gives Load a cheap first sanity check.
constexpr uint8_t kMagic0 = 'K';
constexpr uint8_t kMagic1 = 'V';

// Wire-format version of THIS codec (the byte LAYOUT). Bump when the byte
// layout changes; Load refuses an unknown version rather than mis-parsing.
// Distinct from kCacheSchemaVersion (the CHECK-LOGIC identity in the header).
constexpr uint8_t kWireFormatVersion = 1;

// Header is [magic0][magic1][wire-format-version][cache-schema-version]. A
// mismatched cache-schema-version rejects the whole file (the records were
// produced by different check logic).
constexpr size_t kHeaderLen = 4;

// The cache file lives at <game-bin>/kcdx-engine/cache/version_check.bin.
fs::path CacheFilePath() {
    return kcdx::paths::EngineDataDirPath() / "cache" / "version_check.bin";
}

// ---- little-endian integer + length-prefixed-blob serialization helpers ----
// (KCD2 is x86-64; bytes stored LE. Same shape as the cosave codec.)

void AppendU32LE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void AppendU64LE(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
    }
}

void AppendBytes(std::vector<uint8_t>& out, const uint8_t* p, size_t n) {
    AppendU32LE(out, static_cast<uint32_t>(n));
    out.insert(out.end(), p, p + n);
}

void AppendStr(std::vector<uint8_t>& out, const std::string& s) {
    AppendBytes(out, reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

// A bounds-checked sequential reader over a fixed buffer. Every Take* returns
// false (the parse fails loud) on a short read rather than reading OOB.
struct Reader {
    const uint8_t* p = nullptr;
    size_t         remaining = 0;

    bool TakeU8(uint8_t& out) {
        if (remaining < 1) return false;
        out = *p++;
        --remaining;
        return true;
    }
    bool TakeU32(uint32_t& out) {
        if (remaining < 4) return false;
        out = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
              (static_cast<uint32_t>(p[2]) << 16) |
              (static_cast<uint32_t>(p[3]) << 24);
        p += 4;
        remaining -= 4;
        return true;
    }
    bool TakeU64(uint64_t& out) {
        if (remaining < 8) return false;
        out = 0;
        for (int i = 0; i < 8; ++i) out |= static_cast<uint64_t>(p[i]) << (i * 8);
        p += 8;
        remaining -= 8;
        return true;
    }
    // Read a u32-length-prefixed blob into out. Guards the length against the
    // remaining buffer BEFORE consuming, so a crafted huge length cannot
    // over-read or trigger a giant allocation.
    bool TakeBlob(std::vector<uint8_t>& out) {
        uint32_t n = 0;
        if (!TakeU32(n)) return false;
        if (n > remaining) return false;  // claimed length exceeds the buffer.
        out.assign(p, p + n);
        p += n;
        remaining -= n;
        return true;
    }
    bool TakeStr(std::string& out) {
        std::vector<uint8_t> b;
        if (!TakeBlob(b)) return false;
        out.assign(reinterpret_cast<const char*>(b.data()), b.size());
        return true;
    }
};

// Read a whole file into `out`. Returns false on open/read failure (NOT on a
// missing file specifically — the caller distinguishes via fs::exists).
bool ReadWholeFile(const fs::path& path, std::vector<uint8_t>& out) {
    FILE* f = nullptr;
#if defined(_WIN32)
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return false;
#else
    f = std::fopen(path.string().c_str(), "rb");
    if (!f) return false;
#endif
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    if (sz < 0) { std::fclose(f); return false; }
    std::fseek(f, 0, SEEK_SET);
    out.resize(static_cast<size_t>(sz));
    size_t got = sz > 0 ? std::fread(out.data(), 1, static_cast<size_t>(sz), f) : 0;
    std::fclose(f);
    if (got != static_cast<size_t>(sz)) return false;
    return true;
}

bool WriteWholeFile(const fs::path& path, const std::vector<uint8_t>& data) {
    FILE* f = nullptr;
#if defined(_WIN32)
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return false;
#else
    f = std::fopen(path.string().c_str(), "wb");
    if (!f) return false;
#endif
    size_t want = data.size();
    size_t put = want > 0 ? std::fwrite(data.data(), 1, want, f) : 0;
    bool ok = (std::fclose(f) == 0) && (put == want);
    return ok;
}

// ----------------------------------------------------------------------------
// In-memory cache. Keyed by pluginName for O(1) Lookup/Upsert. Records also
// re-serialize in a stable order (sorted by name) for a deterministic file.
// ----------------------------------------------------------------------------

std::unordered_map<std::string, Record> g_records;

FuncStatus DecodeFuncStatus(uint8_t v) {
    switch (v) {
        case 0: return FuncStatus::Unchanged;
        case 1: return FuncStatus::Changed;
        default: return FuncStatus::CannotCheck;  // 2 and any unknown → safe-side.
    }
}

bool KeysEqual(const InvalidationKey& a, const InvalidationKey& b,
               const char** mismatchField) {
    if (a.gameVer != b.gameVer) { *mismatchField = "game_ver"; return false; }
    if (a.sqliteSha != b.sqliteSha) { *mismatchField = "sqlite_sha"; return false; }
    if (a.tomlMtime != b.tomlMtime) { *mismatchField = "toml_mtime"; return false; }
    if (a.entrypointsMtime != b.entrypointsMtime) {
        *mismatchField = "entrypoints_mtime"; return false;
    }
    *mismatchField = nullptr;
    return true;
}

}  // namespace

bool Load() {
    g_records.clear();

    fs::path path = CacheFilePath();
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        // First launch / fresh install — not an error. Empty cache, all recheck.
        LOG_DEBUG_KV(kCategory, "cache_absent",
            ::kcdx::log::KV("path", path.u8string()),
            ::kcdx::log::KV("note", "no version_check.bin yet — every plugin rechecks (first launch)"));
        return false;
    }

    std::vector<uint8_t> buf;
    if (!ReadWholeFile(path, buf)) {
        LOG_WARN_KV(kCategory, "cache_read_error",
            ::kcdx::log::KV("reason", "file_read_error"),
            ::kcdx::log::KV("path", path.u8string()),
            ::kcdx::log::KV("note", "could not read version_check.bin — treating as empty; every plugin rechecks"));
        return false;
    }

    Reader r{buf.data(), buf.size()};

    // Header — fail loud on bad magic / unknown wire version / schema mismatch.
    uint8_t m0, m1, wire, schema;
    if (buf.size() < kHeaderLen || !r.TakeU8(m0) || !r.TakeU8(m1) ||
        !r.TakeU8(wire) || !r.TakeU8(schema)) {
        LOG_WARN_KV(kCategory, "cache_corrupt",
            ::kcdx::log::KV("reason", "truncated_header"),
            ::kcdx::log::KV("path", path.u8string()),
            ::kcdx::log::KV("note", "version_check.bin too small for its header — treating as empty"));
        return false;
    }
    if (m0 != kMagic0 || m1 != kMagic1) {
        LOG_WARN_KV(kCategory, "cache_corrupt",
            ::kcdx::log::KV("reason", "bad_magic"),
            ::kcdx::log::KV("path", path.u8string()),
            ::kcdx::log::KV("note", "version_check.bin is not a kcdx cache file (bad magic) — treating as empty"));
        return false;
    }
    if (wire != kWireFormatVersion) {
        LOG_WARN_KV(kCategory, "cache_version_mismatch",
            ::kcdx::log::KV("reason", "unknown_wire_version"),
            ::kcdx::log::KV("found", (long long)wire),
            ::kcdx::log::KV("expected", (long long)kWireFormatVersion),
            ::kcdx::log::KV("note", "version_check.bin was written by a different cache codec — treating as empty, every plugin rechecks"));
        return false;
    }
    if (schema != kCacheSchemaVersion) {
        LOG_INFO_KV(kCategory, "cache_schema_bumped",
            ::kcdx::log::KV("reason", "cache_schema_version_mismatch"),
            ::kcdx::log::KV("found", (long long)schema),
            ::kcdx::log::KV("expected", (long long)kCacheSchemaVersion),
            ::kcdx::log::KV("note", "check logic changed since the cache was written — discarding the whole cache, every plugin rechecks"));
        return false;
    }

    uint32_t recCount = 0;
    if (!r.TakeU32(recCount)) {
        LOG_WARN_KV(kCategory, "cache_corrupt",
            ::kcdx::log::KV("reason", "truncated_record_count"),
            ::kcdx::log::KV("path", path.u8string()));
        return false;
    }

    std::unordered_map<std::string, Record> parsed;
    parsed.reserve(recCount);
    for (uint32_t i = 0; i < recCount; ++i) {
        Record rec;
        uint64_t tm = 0, em = 0;
        uint8_t posture = 0;
        uint32_t funcCount = 0;
        if (!r.TakeStr(rec.key.pluginName) ||
            !r.TakeStr(rec.key.gameVer) ||
            !r.TakeBlob(rec.key.sqliteSha) ||
            !r.TakeU64(tm) ||
            !r.TakeU64(em) ||
            !r.TakeU8(posture) ||
            !r.TakeU32(funcCount)) {
            LOG_WARN_KV(kCategory, "cache_corrupt",
                ::kcdx::log::KV("reason", "truncated_record"),
                ::kcdx::log::KV("record_index", (unsigned long long)i),
                ::kcdx::log::KV("note", "version_check.bin record truncated — discarding the whole cache, every plugin rechecks"));
            return false;  // partial parse is never served — fail to empty.
        }
        rec.key.tomlMtime = tm;
        rec.key.entrypointsMtime = em;
        rec.posture = (posture == static_cast<uint8_t>(Posture::RefuseEntry))
                          ? Posture::RefuseEntry
                          : Posture::WarnAndTry;
        rec.results.reserve(funcCount);
        for (uint32_t f = 0; f < funcCount; ++f) {
            FuncResult fr;
            uint8_t st = 0;
            if (!r.TakeStr(fr.targetKey) || !r.TakeU8(st)) {
                LOG_WARN_KV(kCategory, "cache_corrupt",
                    ::kcdx::log::KV("reason", "truncated_func_result"),
                    ::kcdx::log::KV("record_index", (unsigned long long)i),
                    ::kcdx::log::KV("func_index", (unsigned long long)f));
                return false;
            }
            fr.status = DecodeFuncStatus(st);
            rec.results.push_back(std::move(fr));
        }
        std::string name = rec.key.pluginName;
        parsed.emplace(std::move(name), std::move(rec));
    }

    g_records = std::move(parsed);
    LOG_DEBUG_KV(kCategory, "cache_loaded",
        ::kcdx::log::KV("path", path.u8string()),
        ::kcdx::log::KV("records", (unsigned long long)g_records.size()));
    return true;
}

bool Lookup(const InvalidationKey& key,
            std::vector<FuncResult>& outResults,
            Posture& outPosture) {
    auto it = g_records.find(key.pluginName);
    if (it == g_records.end()) {
        LOG_DEBUG_KV(kCategory, "cache_miss",
            ::kcdx::log::KV("plugin", key.pluginName),
            ::kcdx::log::KV("reason", "no_record"));
        return false;
    }
    const char* mismatch = nullptr;
    if (!KeysEqual(it->second.key, key, &mismatch)) {
        LOG_DEBUG_KV(kCategory, "cache_miss",
            ::kcdx::log::KV("plugin", key.pluginName),
            ::kcdx::log::KV("reason", "invalidated"),
            ::kcdx::log::KV("changed_input", mismatch ? mismatch : "(unknown)"));
        return false;
    }
    outResults = it->second.results;
    outPosture = it->second.posture;
    LOG_DEBUG_KV(kCategory, "cache_hit",
        ::kcdx::log::KV("plugin", key.pluginName),
        ::kcdx::log::KV("functions", (unsigned long long)outResults.size()));
    return true;
}

void Upsert(const Record& rec) {
    g_records[rec.key.pluginName] = rec;
}

bool Save() {
    std::vector<uint8_t> buf;
    buf.push_back(kMagic0);
    buf.push_back(kMagic1);
    buf.push_back(kWireFormatVersion);
    buf.push_back(kCacheSchemaVersion);
    AppendU32LE(buf, static_cast<uint32_t>(g_records.size()));

    for (const auto& kv : g_records) {
        const Record& rec = kv.second;
        AppendStr(buf, rec.key.pluginName);
        AppendStr(buf, rec.key.gameVer);
        AppendBytes(buf, rec.key.sqliteSha.data(), rec.key.sqliteSha.size());
        AppendU64LE(buf, rec.key.tomlMtime);
        AppendU64LE(buf, rec.key.entrypointsMtime);
        buf.push_back(static_cast<uint8_t>(rec.posture));
        AppendU32LE(buf, static_cast<uint32_t>(rec.results.size()));
        for (const FuncResult& fr : rec.results) {
            AppendStr(buf, fr.targetKey);
            buf.push_back(static_cast<uint8_t>(fr.status));
        }
    }

    fs::path path = CacheFilePath();
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);  // idempotent; cache/ dir.
    if (ec) {
        LOG_WARN_KV(kCategory, "cache_write_error",
            ::kcdx::log::KV("reason", "mkdir_failed"),
            ::kcdx::log::KV("path", path.parent_path().u8string()),
            ::kcdx::log::KV("note", "could not create the cache directory — the cache is not persisted this launch; the next launch rechecks"));
        return false;
    }

    // Atomic-ish: write to a sibling temp file then rename over the target so a
    // crash mid-write can never leave a half-file the next Load() would reject.
    fs::path tmp = path;
    tmp += ".tmp";
    if (!WriteWholeFile(tmp, buf)) {
        LOG_WARN_KV(kCategory, "cache_write_error",
            ::kcdx::log::KV("reason", "temp_write_failed"),
            ::kcdx::log::KV("path", tmp.u8string()),
            ::kcdx::log::KV("note", "could not write the cache temp file — the cache is not persisted this launch; the next launch rechecks"));
        return false;
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        // Rename failed (e.g. target locked). Fall back to a direct overwrite so
        // we still persist; if THAT fails, give up loud (next launch rechecks).
        std::error_code rmEc;
        fs::remove(tmp, rmEc);
        if (!WriteWholeFile(path, buf)) {
            LOG_WARN_KV(kCategory, "cache_write_error",
                ::kcdx::log::KV("reason", "rename_and_overwrite_failed"),
                ::kcdx::log::KV("path", path.u8string()),
                ::kcdx::log::KV("note", "could not persist the cache — the next launch rechecks"));
            return false;
        }
    }

    LOG_DEBUG_KV(kCategory, "cache_saved",
        ::kcdx::log::KV("path", path.u8string()),
        ::kcdx::log::KV("records", (unsigned long long)g_records.size()));
    return true;
}

void Reset() {
    g_records.clear();
}

}  // namespace kcdx::version_check_cache
