// CAP-113 — kcdx owns the full open->read->close lifecycle on its own CRT.
//
// The end-to-end regression proof for the file-system-takeover open+read slot
// cutover (file-system-takeover design §4.4/§4.5/§5/§9): kcdx resolves every
// name, mints a kcdx HANDLE-ID (an odd-tagged integer opaque to the engine), and
// operates every read ENTIRELY on kcdx's own statically-linked CRT — structurally
// removing the cross-CRT FILE* crash class. cap-112 proved the index resolves a
// vpath; THIS proves the lifecycle through the ACTUAL kcdx slot impls
// (kcdx_FOpen / kcdx_FOpenRaw / the read family / kcdx_FClose).
//
// This plugin compiles the engine's OWN open/read slot impls into its DLL
// (the cap-112 standalone-stub shape, extended): open_slots.cpp + read_slots.cpp
// + file_handle.cpp (the kcdx slot impls + the handle pool) + asset_index.cpp +
// pak_reader.cpp + vendored miniz.c, plus a log-sink stub (kcdx::log symbols) and
// an overlay-SEAM stub (NormalizeVPath + GetOverlayMap + the test-only
// SetTestOverlayMap driver). Every engine TU is BYTE-IDENTICAL to the engine
// build — the stubs supply only the log + overlay seams, never the slot logic
// under test. The slot impls take member-call args with `self` first (the
// engine's `this`); the impls ignore `self` (they `(void)self;` — never deref
// it), so the test calls them with self=nullptr.
//
// === The FOpen resolution wiring (read from open_slots.cpp, not assumed) ======
//
// kcdx_FOpen -> OpenResolvedAndMint -> ResolveVPath(GetBuiltIndex(),
// NormalizeVPath(pName)). So FOpen resolves through the PROCESS-LIFETIME built
// index (asset_index.h GetBuiltIndex). The test populates it exactly the way the
// seat does: SetTestOverlayMap(<synthetic loose override>) -> BuildAssetIndex(
// <game>/Data) -> SetBuiltIndex(std::move(idx)). BuildAssetIndex CDR-parses the
// vanilla paks AND ingests the overlay map, so ONE built index carries both lanes
// (a pak vpath + a loose override at a distinct synthetic key). SetBuiltIndex is
// SET-ONCE (a CAS latch — asset_index.cpp), so the index is built ONCE up front
// with both lanes, then every check resolves through it.
//
// === Four falsifiable claims (each states exactly what makes it FAIL) =========
//
//   (1) PAK FULL LIFECYCLE: resolve a known vanilla pak vpath -> kcdx_FOpen mints
//       a kcdx handle (IsKcdxHandle true) -> read the FULL entry through the kcdx
//       read family -> the bytes are BYTE-CORRECT (CRC-32 of the read-back ==
//       the entry's CDR-recorded CRC-32, computed INDEPENDENTLY with mz_crc32) ->
//       kcdx_FClose. The STORED oracle is GeomCaches.pak entry[0]
//       (Objects/manmade/common_decorations/flags/flag.cax, offset 0,
//       size 352571, method 0, crc 0xd7b807ab). A DEFLATE sub-check additionally
//       scans the pak's CDR for a method-8 entry and runs the same lifecycle on
//       it (exercising the inflate path) when one is found. FAILS if: open
//       returns 0 / a non-kcdx handle; the read returns a wrong byte count; the
//       CRC of the read-back mismatches the CDR CRC.
//   (2) LOOSE FULL LIFECYCLE: write a small known-bytes file to a temp disk path,
//       key a synthetic loose override to a synthetic vpath -> kcdx_FOpen mints a
//       kcdx handle -> read back -> the bytes EXACTLY match what was written ->
//       close. Proves the Loose arm opens + reads on kcdx's CRT (_wfopen_s /
//       fread / fclose). FAILS if: open fails / returns a non-kcdx handle; the
//       read bytes mismatch the written bytes.
//   (3) FOpenRaw MINTS + RESOLVES: kcdx_FOpenRaw on the same pak vpath with a
//       caller outResolvedBuf returns a kcdx handle AND writes the resolved name
//       into the buffer. FAILS if: returns 0 / a non-kcdx handle; the buffer is
//       untouched.
//   (4) BAD-HANDLE FAILS LOUD: a FOREIGN handle (a fabricated even-tagged
//       value, never kcdx-minted) and a freshly-CLOSED handle, passed to the read
//       family, return the documented FAIL value — NOT a success-shaped wrong-
//       bytes return. FAILS if: a foreign/closed handle yields a success-shaped
//       result (a non-zero read, a 0/non-negative seek/tell, a 0 close).
//
// PASS requires ALL of (1)-(4). One folded row, cap-113-fs-takeover-lifecycle,
// whose reason names which sub-check failed (cap-112's shape).

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "kcdx/Interfaces.h"

#include "asset_index.h"
#include "file_handle.h"
#include "open_slots.h"
#include "read_slots.h"
#include "pak_reader.h"
#include "../../src/asset_overlay.h"

// mz_crc32 — the INDEPENDENT byte-correctness oracle. MINIZ_NO_ZLIB_COMPATIBLE_
// NAMES disables miniz's zlib-compat macros (notably `#define crc32 mz_crc32`),
// which would otherwise mangle a `PakEntry::crc32` field access into `->mz_crc32`.
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "miniz.h"

// The overlay-seam stub's test-only driver (defined in
// asset_overlay_seam_stub.cpp). Not part of the engine asset_overlay surface —
// it lets this test populate the overlay map the index ingests.
namespace kcdx::asset_overlay {
void SetTestOverlayMap(const OverlayMap& m);
}

namespace {

// Manifest bare name — must match [plugin].name in kcdx.toml.
const char* kName = "cap_113_fs_takeover_lifecycle";
const char* kRow  = "cap-113-fs-takeover-lifecycle";

const kcdxInterface* g_api  = nullptr;
kcdxPluginHandle     g_self = kcdxInvalidPluginHandle;
kcdxLogger           g_log;

// The game Data dir (per-machine; on absence every pak-backed check reports FAIL
// with a clear reason — never crashes, never silent-skips). Same known-constant
// path + graceful-FAIL-on-absence as cap-110/111/112.
const wchar_t* kGameDataDir =
    L"E:/SteamLibrary/steamapps/common/KingdomComeDeliverance2/Data";

// The known vanilla STORED vpath + its CDR-recorded identity (GeomCaches.pak
// entry[0]) — the cap-112 fixture. The byte-correctness oracle is its CDR CRC-32.
const char*    kVanillaVPath = "Objects/manmade/common_decorations/flags/flag.cax";
const uint64_t kExpectSize   = 352571;      // uncompressed_size
const uint32_t kExpectCrc    = 0xd7b807abu; // CRC-32 of the uncompressed bytes

// A clearly-synthetic loose vpath — must NOT collide with any real pak vpath, so
// the loose-arm check is independent of the pak entries in the same built index.
const char* kLooseVPath = "kcdx_test/cap113/loose_probe.bin";

namespace fst = kcdx::fs_takeover;
namespace ao  = kcdx::asset_overlay;

void Report(bool pass, const char* reason) {
    if (pass) g_log.Info ("CAP113", "PASS %s: %s", kRow, reason);
    else      g_log.Error("CAP113", "FAIL %s: %s", kRow, reason);
    g_api->ReportTestResult(g_self, kRow, pass ? 1 : 0, reason);
}

// CRC-32 over a buffer via miniz — the INDEPENDENT oracle. ReadPakEntry also
// CRC-checks internally; computing it again here over the bytes the kcdx READ
// FAMILY returned proves the read path delivered the correct bytes (not just that
// the open inflated correctly), so the check is not a tautology against the
// reader's own internal verify.
uint32_t Crc32(const uint8_t* data, size_t n) {
    return static_cast<uint32_t>(
        mz_crc32(mz_crc32(0, nullptr, 0), data, n));
}

// Full open->read-whole->close lifecycle through the kcdx slots for a vpath that
// resolves to a PAK source. Reads the whole entry via kcdx_FReadRaw (slot 39 —
// seeks to 0 then reads `size` bytes), CRC-verifies the read-back against
// `expectCrc`, closes via kcdx_FClose. Returns true on a byte-correct lifecycle;
// on failure writes a self-contained reason into `why`.
bool PakLifecycle(const char* vpath, uint64_t expectSize, uint32_t expectCrc,
                  const char* which, char* why, size_t whyCap) {
    void* hv = fst::kcdx_FOpen(nullptr, vpath, "rb", 0);
    const fst::KcdxHandle h = reinterpret_cast<fst::KcdxHandle>(hv);
    if (h == 0 || !fst::IsKcdxHandle(h)) {
        std::snprintf(why, whyCap,
            "(1) %s '%s': kcdx_FOpen did NOT mint a kcdx handle (got %llu, "
            "IsKcdxHandle=%d) — open returned 0 or a non-odd-tagged value",
            which, vpath, (unsigned long long)h, (int)fst::IsKcdxHandle(h));
        return false;
    }
    // Read the FULL entry (the read family operates on kcdx's CRT — the cross-CRT
    // straddle is structurally absent: the engine holds only the opaque id).
    std::vector<uint8_t> buf(static_cast<size_t>(expectSize));
    const size_t got = fst::kcdx_FReadRaw(nullptr, buf.data(),
                                          static_cast<size_t>(expectSize), hv);
    if (got != expectSize) {
        std::snprintf(why, whyCap,
            "(1) %s '%s': kcdx read family returned %llu bytes, expected %llu "
            "(short/over read through the kcdx slot path)",
            which, vpath, (unsigned long long)got,
            (unsigned long long)expectSize);
        fst::kcdx_FClose(nullptr, hv);
        return false;
    }
    const uint32_t crc = Crc32(buf.data(), buf.size());
    if (crc != expectCrc) {
        std::snprintf(why, whyCap,
            "(1) %s '%s': read-back CRC-32 0x%08X != CDR-recorded 0x%08X — the "
            "kcdx read path delivered WRONG bytes (a wrong seek/inflate/copy)",
            which, vpath, crc, expectCrc);
        fst::kcdx_FClose(nullptr, hv);
        return false;
    }
    if (fst::kcdx_FClose(nullptr, hv) != 0) {
        std::snprintf(why, whyCap,
            "(1) %s '%s': kcdx_FClose on a live kcdx handle did NOT return 0 "
            "(success) — close path broken", which, vpath);
        return false;
    }
    return true;
}

}  // namespace

// === kcdxPlugin_Load ==================================================
//
// All four claims are file reads + pure CPU + pool ops — no game lifecycle
// dependency — so the row self-checks and reports here, at load (cap-112's shape).

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    g_api  = api;
    g_self = api->GetPluginHandle(kName);
    g_log  = kcdxLogger(api, g_self);
    g_log.Info("INIT", "kcdxPlugin_Load called (engine v0x%08X)", api->kcdxVersion);

    char reason[900];
    char why[700];

    // --- Build ONE process-lifetime index carrying BOTH lanes -----------------
    // SetBuiltIndex is set-once, so the index is built up front with the vanilla
    // pak set AND a synthetic loose override (at a distinct key), then every check
    // resolves through GetBuiltIndex() via the slot impls.
    //
    // Write the loose probe's known bytes to a temp disk path first; key a
    // synthetic loose override to it via the seam stub; BuildAssetIndex ingests
    // both lanes.
    std::vector<uint8_t> looseBytes;
    looseBytes.reserve(256);
    for (int i = 0; i < 256; ++i)
        looseBytes.push_back(static_cast<uint8_t>((i * 37 + 11) & 0xFF));

    wchar_t tmpDir[MAX_PATH];
    wchar_t tmpFile[MAX_PATH];
    std::string looseDiskUtf8;
    bool looseFileWritten = false;
    if (GetTempPathW(MAX_PATH, tmpDir) != 0 &&
        GetTempFileNameW(tmpDir, L"kcx", 0, tmpFile) != 0) {
        FILE* tf = nullptr;
        if (_wfopen_s(&tf, tmpFile, L"wb") == 0 && tf) {
            const size_t w = std::fwrite(looseBytes.data(), 1, looseBytes.size(), tf);
            std::fclose(tf);
            looseFileWritten = (w == looseBytes.size());
        }
        // Narrow the temp path to UTF-8 for the overlay diskPath (the open slot
        // re-widens it for _wfopen_s; the round-trip must be faithful).
        const int n = WideCharToMultiByte(CP_UTF8, 0, tmpFile, -1, nullptr, 0,
                                          nullptr, nullptr);
        if (n > 0) {
            looseDiskUtf8.resize(static_cast<size_t>(n - 1));
            WideCharToMultiByte(CP_UTF8, 0, tmpFile, -1, looseDiskUtf8.data(), n,
                                nullptr, nullptr);
        }
    }

    ao::OverlayMap overlay;
    if (looseFileWritten && !looseDiskUtf8.empty()) {
        ao::OverlayEntry oe;
        oe.owningPlugin = "cap_113_test";
        oe.diskPath     = looseDiskUtf8;
        overlay[ao::NormalizeVPath(kLooseVPath)] = oe;
    }
    ao::SetTestOverlayMap(overlay);

    fst::AssetIndex idx = fst::BuildAssetIndex(kGameDataDir);
    fst::SetBuiltIndex(std::move(idx));

    // Confirm the vanilla pak vpath is present in the built index — if the Data
    // dir / fixture pak is absent on this machine there is nothing to open
    // through; report FAIL with a clear reason (never a silent skip, never a
    // crash). This mirrors cap-112's absence handling.
    const fst::ByteSource* pak =
        fst::ResolveVPath(fst::GetBuiltIndex(), kVanillaVPath);
    if (pak == nullptr || pak->kind != fst::ByteSource::Kind::Pak) {
        std::snprintf(reason, sizeof(reason),
            "(1) vanilla vpath '%s' did not resolve to a Pak source in the index "
            "built over %ls — the fixture pak GeomCaches.pak was not found under "
            "<game>/Data on this machine (nothing to run the open->read->close "
            "lifecycle through). This is FAIL, not a skip",
            kVanillaVPath, kGameDataDir);
        Report(false, reason);
        return true;
    }

    // --- (1) PAK FULL LIFECYCLE — STORED oracle -------------------------------
    if (!PakLifecycle(kVanillaVPath, kExpectSize, kExpectCrc, "STORED",
                      why, sizeof(why))) {
        Report(false, why);
        return true;
    }

    // --- (1b) PAK FULL LIFECYCLE — DEFLATE sub-check (optional, strongest) -----
    // Cheaply scan the same pak's CDR for a method-8 (DEFLATE) entry and run the
    // identical lifecycle on it, exercising the inflate path through the kcdx read
    // family. If no DEFLATE entry is found the STORED lifecycle above is the bar
    // (byte-correctness regardless of method); the result note records which ran.
    std::string deflateNote = "no DEFLATE entry scanned (STORED lifecycle is the proof)";
    {
        std::vector<fst::PakEntry> entries;
        std::string perr;
        // The vanilla pak path the index recorded for the STORED entry's source.
        const std::wstring pakPath = pak->pakFile;
        if (fst::ParsePakCentralDirectory(pakPath, entries, perr)) {
            const fst::PakEntry* def = nullptr;
            for (const auto& e : entries) {
                // A non-trivial DEFLATE entry (skip 0-byte entries — nothing to
                // prove on an empty inflate).
                if (e.method == 8 && e.uncompressed_size > 0) { def = &e; break; }
            }
            if (def != nullptr) {
                // Resolve the DEFLATE entry's vpath through the SAME built index
                // (it was keyed from this pak at build) and run the lifecycle.
                if (!PakLifecycle(def->name.c_str(), def->uncompressed_size,
                                  def->crc32, "DEFLATE", why, sizeof(why))) {
                    Report(false, why);
                    return true;
                }
                char dn[300];
                std::snprintf(dn, sizeof(dn),
                    "DEFLATE entry '%s' (usize=%llu) inflated + CRC-verified "
                    "through the kcdx read family",
                    def->name.c_str(), (unsigned long long)def->uncompressed_size);
                deflateNote = dn;
            }
        }
        // A CDR re-parse failure here does NOT fail the row — the STORED lifecycle
        // is the load-bearing proof; the DEFLATE pass is the optional strongest
        // form. The note records that it was not exercised.
    }

    // --- (2) LOOSE FULL LIFECYCLE ---------------------------------------------
    if (!looseFileWritten || looseDiskUtf8.empty()) {
        std::snprintf(reason, sizeof(reason),
            "(2) could not stage the loose-override probe file in the temp dir "
            "(GetTempFileNameW/_wfopen_s failed) — the Loose-arm open->read->close "
            "lifecycle could not be exercised. FAIL (the test infra could not set "
            "up its fixture), not a skip");
        Report(false, reason);
        return true;
    }
    {
        const fst::ByteSource* loose =
            fst::ResolveVPath(fst::GetBuiltIndex(), kLooseVPath);
        if (loose == nullptr || loose->kind != fst::ByteSource::Kind::Loose) {
            std::snprintf(reason, sizeof(reason),
                "(2) the synthetic loose override of '%s' did not resolve to a "
                "Loose source in the built index (kind=%d) — the overlay lane was "
                "not ingested",
                kLooseVPath, loose ? (int)loose->kind : -1);
            Report(false, reason);
            return true;
        }
        void* hv = fst::kcdx_FOpen(nullptr, kLooseVPath, "rb", 0);
        const fst::KcdxHandle h = reinterpret_cast<fst::KcdxHandle>(hv);
        if (h == 0 || !fst::IsKcdxHandle(h)) {
            std::snprintf(reason, sizeof(reason),
                "(2) kcdx_FOpen on the loose override '%s' did NOT mint a kcdx "
                "handle (got %llu, IsKcdxHandle=%d) — the Loose arm did not open "
                "on kcdx's CRT", kLooseVPath, (unsigned long long)h,
                (int)fst::IsKcdxHandle(h));
            Report(false, reason);
            return true;
        }
        std::vector<uint8_t> rb(looseBytes.size());
        const size_t got = fst::kcdx_FReadRaw(nullptr, rb.data(), rb.size(), hv);
        if (got != looseBytes.size() ||
            std::memcmp(rb.data(), looseBytes.data(), looseBytes.size()) != 0) {
            std::snprintf(reason, sizeof(reason),
                "(2) loose read-back mismatch: read %llu of %llu bytes, or the "
                "bytes differ from what was written — the Loose arm's "
                "_wfopen/fread did not deliver the file's bytes",
                (unsigned long long)got, (unsigned long long)looseBytes.size());
            fst::kcdx_FClose(nullptr, hv);
            Report(false, reason);
            return true;
        }
        if (fst::kcdx_FClose(nullptr, hv) != 0) {
            std::snprintf(reason, sizeof(reason),
                "(2) kcdx_FClose on the live loose handle did NOT return 0");
            Report(false, reason);
            return true;
        }
    }

    // --- (3) FOpenRaw (slot 35) MINTS + RESOLVES ------------------------------
    {
        char resolved[2048];
        resolved[0] = '\0';
        void* hv = fst::kcdx_FOpenRaw(nullptr, kVanillaVPath, "rb",
                                      resolved, (int)sizeof(resolved));
        const fst::KcdxHandle h = reinterpret_cast<fst::KcdxHandle>(hv);
        if (h == 0 || !fst::IsKcdxHandle(h)) {
            std::snprintf(reason, sizeof(reason),
                "(3) kcdx_FOpenRaw on '%s' did NOT mint a kcdx handle (got %llu, "
                "IsKcdxHandle=%d)", kVanillaVPath, (unsigned long long)h,
                (int)fst::IsKcdxHandle(h));
            Report(false, reason);
            return true;
        }
        if (resolved[0] == '\0') {
            std::snprintf(reason, sizeof(reason),
                "(3) kcdx_FOpenRaw minted a handle but left outResolvedBuf "
                "UNTOUCHED — the resolved-name copy contract was not honored");
            fst::kcdx_FClose(nullptr, hv);
            Report(false, reason);
            return true;
        }
        fst::kcdx_FClose(nullptr, hv);
    }

    // --- (4) BAD-HANDLE PATHS FAIL LOUD ---------------------------------------
    // A foreign handle (a fabricated EVEN value — never kcdx-minted, the low TAG
    // bit clear) and a freshly-CLOSED handle must return the documented FAIL
    // value from each read-family op, NOT a success-shaped wrong-bytes result.
    {
        void* foreign = reinterpret_cast<void*>(static_cast<uintptr_t>(0x1000));
        if (fst::IsKcdxHandle(reinterpret_cast<fst::KcdxHandle>(foreign))) {
            std::snprintf(reason, sizeof(reason),
                "(4) the fabricated foreign handle 0x1000 was (wrongly) accepted "
                "as a kcdx handle — the odd-tag discriminator is broken");
            Report(false, reason);
            return true;
        }
        uint8_t scratch[64];
        // FReadRaw on a foreign handle → 0 bytes (never a success-shaped read).
        if (fst::kcdx_FReadRaw(nullptr, scratch, sizeof(scratch), foreign) != 0) {
            std::snprintf(reason, sizeof(reason),
                "(4) kcdx_FReadRaw on a FOREIGN handle returned a non-zero byte "
                "count — a bad handle yielded success-shaped bytes (silent "
                "wrong-bytes), instead of a loud 0");
            Report(false, reason);
            return true;
        }
        // FSeek on a foreign handle → non-zero (failure); FTell → -1; FClose → EOF.
        if (fst::kcdx_FSeek(nullptr, foreign, 0, SEEK_SET) == 0) {
            std::snprintf(reason, sizeof(reason),
                "(4) kcdx_FSeek on a FOREIGN handle returned 0 (success) — a bad "
                "handle reported a successful seek");
            Report(false, reason);
            return true;
        }
        if (fst::kcdx_FTell(nullptr, foreign) != -1) {
            std::snprintf(reason, sizeof(reason),
                "(4) kcdx_FTell on a FOREIGN handle did not return -1 — a bad "
                "handle reported a position");
            Report(false, reason);
            return true;
        }
        if (fst::kcdx_FClose(nullptr, foreign) == 0) {
            std::snprintf(reason, sizeof(reason),
                "(4) kcdx_FClose on a FOREIGN handle returned 0 (success) — a bad "
                "handle reported a successful close");
            Report(false, reason);
            return true;
        }

        // A freshly-CLOSED kcdx handle: open the vanilla pak vpath, close it, then
        // a read-family op on the now-dead id must fail loud (use-after-close).
        void* hv = fst::kcdx_FOpen(nullptr, kVanillaVPath, "rb", 0);
        const fst::KcdxHandle h = reinterpret_cast<fst::KcdxHandle>(hv);
        if (h != 0 && fst::IsKcdxHandle(h)) {
            fst::kcdx_FClose(nullptr, hv);  // close it; the id is now dead
            if (fst::kcdx_FReadRaw(nullptr, scratch, sizeof(scratch), hv) != 0) {
                std::snprintf(reason, sizeof(reason),
                    "(4) kcdx_FReadRaw on a freshly-CLOSED kcdx handle returned a "
                    "non-zero byte count — a use-after-close yielded success-"
                    "shaped bytes, instead of a loud 0");
                Report(false, reason);
                return true;
            }
            if (fst::kcdx_FClose(nullptr, hv) == 0) {
                std::snprintf(reason, sizeof(reason),
                    "(4) a double kcdx_FClose returned 0 (success) — a closed "
                    "handle reported a successful close");
                Report(false, reason);
                return true;
            }
        }
        // (If the open above failed, (1) already FAILed the row earlier; reaching
        // here with a non-kcdx h is impossible given (1) passed.)
    }

    // All four claims passed.
    std::snprintf(reason, sizeof(reason),
        "kcdx full open->read->close lifecycle on its own CRT PASS — "
        "(1) vanilla pak '%s' opened via kcdx_FOpen (minted a kcdx handle-id), "
        "read byte-correct through the kcdx read family (CRC-32 0x%08X over "
        "%llu bytes == CDR-recorded), closed; %s. "
        "(2) a loose override opened+read on kcdx's CRT, 256 known bytes matched. "
        "(3) kcdx_FOpenRaw minted a kcdx handle AND wrote the resolved name. "
        "(4) a foreign handle + a closed handle each FAILED LOUD across the read "
        "family (0 bytes / non-zero seek / -1 tell / non-zero close) — never a "
        "silent wrong-bytes return. The cross-CRT FILE* class is structurally "
        "removed: the engine holds only an opaque odd-tagged id",
        kVanillaVPath, kExpectCrc, (unsigned long long)kExpectSize,
        deflateNote.c_str());
    Report(true, reason);
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
