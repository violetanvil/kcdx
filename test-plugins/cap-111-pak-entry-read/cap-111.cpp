// CAP-111 — kcdx's own pak-entry read path delivers correct uncompressed bytes.
//
// The regression proof for the file-system-takeover pak reader's read half
// (design file-system-takeover.md §6: kcdx reads pak bytes with its OWN PKZIP
// reader, every byte on kcdx's CRT, no engine ZipDir; §4.4: no engine read leaf
// in the path — the cross-CRT crash-free guarantee). Step 2.2 located each
// entry (the central-directory parse); this step READS one — open + seek +
// read + STORED-copy / DEFLATE-inflate, all on kcdx's CRT.
//
// This plugin compiles the engine's pak_reader.cpp into its own DLL (the cap-110
// shape) PLUS the vendored miniz.c (the read path inflates), with a log-sink
// stub supplying the kcdx::log symbols pak_reader.cpp references. It runs TWO
// assertions at boot, both pure CPU + file reads:
//
//   (a) STORED read: read the first method-0 entry of GeomCaches.pak and assert
//       the output length == uncompressed_size AND its CRC-32 == the recorded
//       entry.crc32.
//   (b) DEFLATE read: find the first method-8 entry of Tables.pak (discovered by
//       iterating the parsed CDR — never a hardcoded index), read it, and assert
//       the output length == uncompressed_size AND its CRC-32 == entry.crc32.
//
// The CRC-32-against-the-recorded-crc check is the FALSIFIABLE claim: a correct
// read inflates/copies to bytes whose CRC matches what the central directory
// recorded; a wrong local-header parse, wrong seek, wrong inflate flag, or
// off-by-N yields a CRC mismatch (ReadPakEntry itself fails on it, returning
// false with a CRC-mismatch reason). PASS requires BOTH.
//
// === The fixtures (how they were derived — regenerate/verify) =============
//
// Both fixture paks live in <game>/Data and were read statically to confirm the
// read round-trips (a static on-disk read; re-run on a new game version):
//
//   import struct, zlib
//   def walk(path):
//       d=open(path,"rb").read(); pos=d.rfind(b"PK\x05\x06")
//       _,_,_,_,ent,_,cd,_=struct.unpack_from("<IHHHHIIH",d,pos); p=cd; out=[]
//       for _ in range(ent):
//           r=struct.unpack_from("<IHHHHHHIIIHHHHHII",d,p)
//           nl,el,cl=r[10],r[11],r[12]; name=d[p+46:p+46+nl].decode()
//           out.append((r[4],r[7],r[8],r[9],r[16],name)); p+=46+nl+el+cl  # method,crc,csize,usize,lho,name
//       return d,out
//   # GeomCaches.pak entry[0]: method=0 (STORED) crc=0xd7b807ab csize=usize=352571
//   #   name='Objects/manmade/common_decorations/flags/flag.cax' (lho=0)
//   # Tables.pak entry[0]:     method=8 (DEFLATE) crc=0x01da3966 csize=450 usize=1192
//   #   name='Libs/Tables/action/actor_action_fragment_id_mapping.tbl' (lho=0)
//
// The test does NOT assume those indices: it parses each pak's CDR and FINDS the
// first method-0 (a) / method-8 (b) entry, so a different layout on a new game
// version still selects a valid entry of the right kind. The recorded crc in the
// PakEntry is the oracle the read is checked against.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "kcdx/Interfaces.h"
#include "miniz.h"
// miniz.h's zlib-compat `crc32` → `mz_crc32` alias macro would clobber the
// PakEntry::crc32 member access below; drop it (mz_crc32 is called directly).
#undef crc32

#include "pak_reader.h"

namespace {

// Manifest bare name — must match [plugin].name in kcdx.toml.
const char* kName = "cap_111_pak_entry_read";
const char* kRow  = "cap-111-pak-entry-read";

const kcdxInterface* g_api  = nullptr;
kcdxPluginHandle     g_self = kcdxInvalidPluginHandle;
kcdxLogger           g_log;

// The game Data dir (per-machine; on absence the row reports FAIL with a clear
// reason — never crashes, never silent-skips). Surfaced to the maintainer: same
// open question as cap-110 — whether this should come from an engine API rather
// than a constant.
const wchar_t* kGameDataDir =
    L"E:/SteamLibrary/steamapps/common/KingdomComeDeliverance2/Data";

const wchar_t* kStoredPak  = L"GeomCaches.pak";  // all-STORED vanilla pak
const wchar_t* kDeflatePak = L"Tables.pak";      // DEFLATE-heavy vanilla pak

std::wstring JoinPath(const wchar_t* dir, const wchar_t* file) {
    std::wstring p(dir);
    if (!p.empty() && p.back() != L'/' && p.back() != L'\\') p.push_back(L'/');
    p.append(file);
    return p;
}

namespace fst = kcdx::fs_takeover;

// Parse a pak's CDR and find the first entry with the given method. Returns
// true + fills `out` on success; fills `why` and returns false on parse failure
// or no matching entry.
bool FindEntryByMethod(const std::wstring& pakPath, uint16_t method,
                       fst::PakEntry& out, std::string& why) {
    std::vector<fst::PakEntry> entries;
    std::string parseErr;
    if (!fst::ParsePakCentralDirectory(pakPath, entries, parseErr)) {
        why = "CDR parse failed: " + parseErr;
        return false;
    }
    for (const fst::PakEntry& e : entries) {
        if (e.method == method) { out = e; return true; }
    }
    char b[96];
    std::snprintf(b, sizeof(b), "no method-%u entry in this pak (%zu entries)",
                  method, entries.size());
    why = b;
    return false;
}

void Report(bool pass, const char* reason) {
    if (pass) g_log.Info ("CAP111", "PASS %s: %s", kRow, reason);
    else      g_log.Error("CAP111", "FAIL %s: %s", kRow, reason);
    g_api->ReportTestResult(g_self, kRow, pass ? 1 : 0, reason);
}

}  // namespace

// === kcdxPlugin_Load ==================================================
//
// Both reads are file reads + pure CPU — no game lifecycle dependency, so the
// row self-checks and reports here, at load.

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    g_api  = api;
    g_self = api->GetPluginHandle(kName);
    g_log  = kcdxLogger(api, g_self);
    g_log.Info("INIT", "kcdxPlugin_Load called (engine v0x%08X)", api->kcdxVersion);

    char reason[700];

    // --- (a) STORED read: first method-0 entry of GeomCaches.pak. -----------
    const std::wstring storedPath = JoinPath(kGameDataDir, kStoredPak);
    fst::PakEntry sEntry;
    std::string sWhy;
    if (!FindEntryByMethod(storedPath, /*method=*/0, sEntry, sWhy)) {
        std::snprintf(reason, sizeof(reason),
            "(a) could not locate a STORED entry in %ls: %s. Fixture pak expected "
            "at <game>/Data/%ls — if the path differs on this machine there was "
            "nothing to read; this is FAIL, not a skip",
            kStoredPak, sWhy.c_str(), kStoredPak);
        Report(false, reason);
        return true;
    }
    std::vector<uint8_t> sBytes;
    std::string sReadErr;
    if (!fst::ReadPakEntry(storedPath, sEntry, sBytes, sReadErr)) {
        std::snprintf(reason, sizeof(reason),
            "(a) STORED read of '%s' (crc=0x%08x, usize=%llu) FAILED: %s",
            sEntry.name.c_str(), sEntry.crc32,
            (unsigned long long)sEntry.uncompressed_size, sReadErr.c_str());
        Report(false, reason);
        return true;
    }
    if (sBytes.size() != sEntry.uncompressed_size) {
        std::snprintf(reason, sizeof(reason),
            "(a) STORED read of '%s' produced %zu bytes != uncompressed_size %llu",
            sEntry.name.c_str(), sBytes.size(),
            (unsigned long long)sEntry.uncompressed_size);
        Report(false, reason);
        return true;
    }
    // ReadPakEntry verifies the CRC internally and fails on mismatch — re-assert
    // it here so the test's PASS rests on the falsifiable claim directly (the
    // output bytes' CRC equals the recorded entry.crc32), not only on the
    // read's internal gate.
    const mz_ulong sCrc = mz_crc32(mz_crc32(0, nullptr, 0), sBytes.data(), sBytes.size());
    if (static_cast<uint32_t>(sCrc) != sEntry.crc32) {
        std::snprintf(reason, sizeof(reason),
            "(a) STORED read of '%s': output CRC 0x%08x != recorded 0x%08x",
            sEntry.name.c_str(), static_cast<uint32_t>(sCrc), sEntry.crc32);
        Report(false, reason);
        return true;
    }

    // --- (b) DEFLATE read: first method-8 entry of Tables.pak. --------------
    const std::wstring deflatePath = JoinPath(kGameDataDir, kDeflatePak);
    fst::PakEntry dEntry;
    std::string dWhy;
    if (!FindEntryByMethod(deflatePath, /*method=*/8, dEntry, dWhy)) {
        std::snprintf(reason, sizeof(reason),
            "(b) could not locate a DEFLATE entry in %ls: %s. Fixture pak expected "
            "at <game>/Data/%ls — if the path differs on this machine there was "
            "nothing to read; this is FAIL, not a skip",
            kDeflatePak, dWhy.c_str(), kDeflatePak);
        Report(false, reason);
        return true;
    }
    std::vector<uint8_t> dBytes;
    std::string dReadErr;
    if (!fst::ReadPakEntry(deflatePath, dEntry, dBytes, dReadErr)) {
        std::snprintf(reason, sizeof(reason),
            "(b) DEFLATE read of '%s' (crc=0x%08x, csize=%llu, usize=%llu) FAILED: %s",
            dEntry.name.c_str(), dEntry.crc32,
            (unsigned long long)dEntry.compressed_size,
            (unsigned long long)dEntry.uncompressed_size, dReadErr.c_str());
        Report(false, reason);
        return true;
    }
    if (dBytes.size() != dEntry.uncompressed_size) {
        std::snprintf(reason, sizeof(reason),
            "(b) DEFLATE read of '%s' produced %zu bytes != uncompressed_size %llu",
            dEntry.name.c_str(), dBytes.size(),
            (unsigned long long)dEntry.uncompressed_size);
        Report(false, reason);
        return true;
    }
    const mz_ulong dCrc = mz_crc32(mz_crc32(0, nullptr, 0), dBytes.data(), dBytes.size());
    if (static_cast<uint32_t>(dCrc) != dEntry.crc32) {
        std::snprintf(reason, sizeof(reason),
            "(b) DEFLATE read of '%s': output CRC 0x%08x != recorded 0x%08x",
            dEntry.name.c_str(), static_cast<uint32_t>(dCrc), dEntry.crc32);
        Report(false, reason);
        return true;
    }

    // Both (a) and (b) passed.
    std::snprintf(reason, sizeof(reason),
        "kcdx pak-entry read PASS — (a) STORED '%s' read %zu bytes (usize=%llu), "
        "CRC 0x%08x matched; (b) DEFLATE '%s' inflated %zu bytes (csize=%llu, "
        "usize=%llu), CRC 0x%08x matched. Proves kcdx's own read path parses the "
        "local file header, seeks, reads, and STORED-copies / DEFLATE-inflates a "
        "real vanilla pak entry to CRC-correct bytes entirely on kcdx's CRT",
        sEntry.name.c_str(), sBytes.size(),
        (unsigned long long)sEntry.uncompressed_size, sEntry.crc32,
        dEntry.name.c_str(), dBytes.size(),
        (unsigned long long)dEntry.compressed_size,
        (unsigned long long)dEntry.uncompressed_size, dEntry.crc32);
    Report(true, reason);
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
