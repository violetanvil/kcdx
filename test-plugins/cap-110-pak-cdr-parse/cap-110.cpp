// CAP-110 — kcdx's own PKZIP central-directory parser reads a real vanilla pak.
//
// The regression proof for the file-system-takeover pak reader's directory
// half (design file-system-takeover.md §6: kcdx reads pak bytes with its OWN
// PKZIP central-directory reader, every byte on kcdx's CRT, no engine ZipDir;
// §5: the per-entry {offset,size,method,crc} feed the unified index). This
// plugin compiles the engine's pak_reader.cpp into its own DLL (the cap-109
// shape — a self-contained test artifact built against include/ only) and runs
// TWO assertions at boot:
//
//   (a) FULL CDR PARSE of a real vanilla pak (GeomCaches.pak). Asserts the
//       parsed entry count == 8 (the observed truth) AND a known entry's
//       {local-header offset, sizes, method, crc} match the actual on-disk
//       bytes — a falsifiable fixture (a broken parser yields a wrong count or
//       wrong fields, not a pass).
//
//   (b) FORMAT-UNIFORMITY check (the standing assertion that replaces the
//       one-time static probe). Scans several vanilla paks and asserts each is
//       standard PKZIP — first 4 bytes PK\x03\x04, a locatable PK\x05\x06 EOCD,
//       no zip64 marker. FAILS LOUD if any vanilla pak deviates, so a future
//       game version that changes the pak format trips this row rather than
//       silently corrupting a read.
//
// PASS requires BOTH (a) and (b). Both are pure CPU + file reads with no engine
// lifecycle dependency, so the row self-checks and reports from kcdxPlugin_Load.
//
// === The known GeomCaches fixture (how it was derived — regenerate/verify) ==
//
// The expected values below were read directly from the pak's central directory
// with this Python (a static on-disk read; re-run to regenerate on a new game
// version):
//
//   import struct
//   data = open(r"<game>/Data/GeomCaches.pak","rb").read()
//   pos  = data.rfind(b"PK\x05\x06")
//   _,_,_,_,ent,_,cd_off,_ = struct.unpack_from("<IHHHHIIH", data, pos)
//   # ent == 8; walk the CDR from cd_off:
//   p = cd_off
//   (csig,_,_,_,method,_,_,crc,csize,usize,nl,el,cl,_,_,_,lho) = \
//       struct.unpack_from("<IHHHHHHIIIHHHHHII", data, p)
//   name = data[p+46:p+46+nl].decode()
//   # entry[0]: method=0 (STORED) crc=0xd7b807ab lho=0 csize=usize=352571
//   #           name='Objects/manmade/common_decorations/flags/flag.cax'
//
// Entry 0 is the first (STORED, lho=0) entry — the same one the format-confirm
// read recorded. A future reader re-runs the recipe and confirms byte-identity.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "kcdx/Interfaces.h"

#include "pak_reader.h"

namespace {

// Manifest bare name — must match [plugin].name in kcdx.toml.
const char* kName = "cap_110_pak_cdr_parse";
const char* kRow  = "cap-110-pak-cdr-parse";

const kcdxInterface* g_api  = nullptr;
kcdxPluginHandle     g_self = kcdxInvalidPluginHandle;
kcdxLogger           g_log;

// The game Data dir is read from a known path with a clear comment. The path
// can differ per machine; on absence the row reports FAIL with a clear reason
// (never crashes, never silent-passes). (Surfaced to the maintainer: whether
// this should come from an engine API rather than a constant — see the
// deliverable's surfaced-items note.)
const wchar_t* kGameDataDir =
    L"E:/SteamLibrary/steamapps/common/KingdomComeDeliverance2/Data";

// --- (a) fixture: the known GeomCaches.pak truth (derived per the header). ---
const wchar_t* kFixturePak = L"GeomCaches.pak";
constexpr size_t   kExpectEntryCount      = 8;
// Entry 0 — the first (STORED) entry, local header at offset 0.
constexpr uint64_t kExpectE0_lho          = 0x00000000ull;
constexpr uint64_t kExpectE0_csize        = 352571ull;       // 0x0005613b
constexpr uint64_t kExpectE0_usize        = 352571ull;       // STORED → equal
constexpr uint16_t kExpectE0_method       = 0;               // STORED
constexpr uint32_t kExpectE0_crc          = 0xd7b807abu;
const char*        kExpectE0_name =
    "Objects/manmade/common_decorations/flags/flag.cax";

// --- (b) format-uniformity: a fixed list of real vanilla paks to scan. -------
const wchar_t* kFormatPaks[] = {
    L"GeomCaches.pak",
    L"Tables.pak",
    L"Scripts.pak",
    L"Animations.pak",
    L"Characters.pak",
};

std::wstring JoinPath(const wchar_t* dir, const wchar_t* file) {
    std::wstring p(dir);
    if (!p.empty() && p.back() != L'/' && p.back() != L'\\') p.push_back(L'/');
    p.append(file);
    return p;
}

// A standard-PKZIP head+tail check on a vanilla pak, all on kcdx's CRT. Returns
// true if the file is standard PKZIP (PK\x03\x04 head, locatable PK\x05\x06
// EOCD, no zip64 marker). On any deviation/absence, fills `why` and returns
// false. This is the standing assertion replacing the one-time static probe.
bool IsStandardPkzip(const std::wstring& path, std::string& why) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || f == nullptr) {
        why = "file not found / could not open";
        return false;
    }
    bool ok = false;
    do {
        unsigned char head[4] = {};
        if (std::fread(head, 1, 4, f) != 4) { why = "could not read first 4 bytes"; break; }
        // Local file header signature PK\x03\x04 — no proprietary header prepended.
        if (!(head[0] == 0x50 && head[1] == 0x4B && head[2] == 0x03 && head[3] == 0x04)) {
            char b[96];
            std::snprintf(b, sizeof(b), "head is %02x%02x%02x%02x, not PK\\x03\\x04",
                          head[0], head[1], head[2], head[3]);
            why = b;
            break;
        }
        // Read the tail and require a PK\x05\x06 EOCD AND no zip64 marker.
        if (std::fseek(f, 0, SEEK_END) != 0) { why = "fseek EOF failed"; break; }
        long sz = std::ftell(f);
        if (sz < 22) { why = "file too small for an EOCD"; break; }
        const long scan = sz < (0xFFFF + 22) ? sz : (0xFFFF + 22);
        std::vector<unsigned char> tail(static_cast<size_t>(scan));
        if (std::fseek(f, sz - scan, SEEK_SET) != 0) { why = "fseek tail failed"; break; }
        if (std::fread(tail.data(), 1, tail.size(), f) != tail.size()) {
            why = "tail read came up short"; break;
        }
        bool eocd = false, zip64 = false;
        for (size_t i = 0; i + 4 <= tail.size(); ++i) {
            const unsigned char* p = tail.data() + i;
            if (p[0] == 0x50 && p[1] == 0x4B && p[2] == 0x05 && p[3] == 0x06) eocd = true;
            if (p[0] == 0x50 && p[1] == 0x4B && p[2] == 0x06 &&
                (p[3] == 0x06 || p[3] == 0x07)) zip64 = true;
        }
        if (zip64) { why = "zip64 EOCD/locator present in tail"; break; }
        if (!eocd) { why = "no PK\\x05\\x06 EOCD in tail"; break; }
        ok = true;
    } while (false);
    std::fclose(f);
    return ok;
}

void Report(bool pass, const char* reason) {
    if (pass) g_log.Info ("CAP110", "PASS %s: %s", kRow, reason);
    else      g_log.Error("CAP110", "FAIL %s: %s", kRow, reason);
    g_api->ReportTestResult(g_self, kRow, pass ? 1 : 0, reason);
}

}  // namespace

// === kcdxPlugin_Load ==================================================
//
// Both assertions are file reads + pure CPU — no game lifecycle dependency, so
// the row self-checks and reports here, at load.

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    g_api  = api;
    g_self = api->GetPluginHandle(kName);
    g_log  = kcdxLogger(api, g_self);
    g_log.Info("INIT", "kcdxPlugin_Load called (engine v0x%08X)", api->kcdxVersion);

    char reason[600];

    // --- (a) Full CDR parse of the fixture pak. -----------------------------
    const std::wstring fixturePath = JoinPath(kGameDataDir, kFixturePak);
    std::vector<kcdx::fs_takeover::PakEntry> entries;
    std::string parseErr;
    if (!kcdx::fs_takeover::ParsePakCentralDirectory(fixturePath, entries, parseErr)) {
        std::snprintf(reason, sizeof(reason),
            "(a) CDR parse of the fixture pak failed: %s. Fixture pak expected at "
            "<game>/Data/%ls — if the path differs on this machine the parser had "
            "nothing to read; this is FAIL, not a skip",
            parseErr.c_str(), kFixturePak);
        Report(false, reason);
        return true;
    }

    if (entries.size() != kExpectEntryCount) {
        std::snprintf(reason, sizeof(reason),
            "(a) parsed entry count %zu != expected %zu for the fixture pak — the "
            "CDR walk read the wrong number of records",
            entries.size(), kExpectEntryCount);
        Report(false, reason);
        return true;
    }

    const kcdx::fs_takeover::PakEntry& e0 = entries[0];
    const bool e0_ok =
        e0.local_header_offset == kExpectE0_lho &&
        e0.compressed_size     == kExpectE0_csize &&
        e0.uncompressed_size   == kExpectE0_usize &&
        e0.method              == kExpectE0_method &&
        e0.crc32               == kExpectE0_crc &&
        e0.name                == kExpectE0_name;
    if (!e0_ok) {
        std::snprintf(reason, sizeof(reason),
            "(a) fixture entry[0] mismatch — got lho=0x%llx csize=%llu usize=%llu "
            "method=%u crc=0x%08x name='%s'; expected lho=0x%llx csize=%llu usize=%llu "
            "method=%u crc=0x%08x name='%s'",
            (unsigned long long)e0.local_header_offset, (unsigned long long)e0.compressed_size,
            (unsigned long long)e0.uncompressed_size, e0.method, e0.crc32, e0.name.c_str(),
            (unsigned long long)kExpectE0_lho, (unsigned long long)kExpectE0_csize,
            (unsigned long long)kExpectE0_usize, kExpectE0_method, kExpectE0_crc, kExpectE0_name);
        Report(false, reason);
        return true;
    }

    // --- (b) Format-uniformity check across several vanilla paks. -----------
    const size_t formatCount = sizeof(kFormatPaks) / sizeof(kFormatPaks[0]);
    size_t scanned = 0;
    for (size_t i = 0; i < formatCount; ++i) {
        const std::wstring p = JoinPath(kGameDataDir, kFormatPaks[i]);
        std::string why;
        if (!IsStandardPkzip(p, why)) {
            std::snprintf(reason, sizeof(reason),
                "(b) vanilla pak %ls is NOT standard PKZIP: %s — the format-uniformity "
                "assertion tripped (a game version may have changed the pak format)",
                kFormatPaks[i], why.c_str());
            Report(false, reason);
            return true;
        }
        ++scanned;
    }

    // Both (a) and (b) passed.
    std::snprintf(reason, sizeof(reason),
        "kcdx PKZIP CDR parse PASS — (a) parsed the fixture pak's central directory: "
        "%zu entries, entry[0] {lho=0x%llx, csize=%llu, usize=%llu, method=%u (STORED), "
        "crc=0x%08x, name='%s'} matched the on-disk bytes; (b) format-uniformity: "
        "%zu/%zu vanilla paks confirmed standard PKZIP (PK\\x03\\x04 head, PK\\x05\\x06 "
        "EOCD, no zip64). Proves kcdx's own central-directory reader extracts correct "
        "per-entry {offset,size,method,crc} and vanilla paks remain standard PKZIP",
        entries.size(), (unsigned long long)e0.local_header_offset,
        (unsigned long long)e0.compressed_size, (unsigned long long)e0.uncompressed_size,
        e0.method, e0.crc32, e0.name.c_str(), scanned, formatCount);
    Report(true, reason);
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
