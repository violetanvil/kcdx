// CAP-115 — the unified asset index covers the Engine pak root, so an
// engine-pak-resident file is an index HIT (the KI-0026 boot-crash fix).
//
// The regression proof for the file-system-takeover index covering BOTH vanilla
// pak roots — <game>/Data AND <game>/Engine (design file-system-takeover.md §5,
// v1.8). The engine reads its own config/shader files from the Engine/*.pak
// archives; the canonical example is engine_core.thread_config, which lives in
// Engine.pak as 'Config/engine_core.thread_config' (normalized
// 'config/engine_core.thread_config'). Before the fix the index walked only
// <game>/Data, so that vpath was an index MISS: kcdx's miss arm resolved it to a
// loose path and _wfopen'd it, the open failed (the file is pak-resident in an
// engine pak), and the engine raised CSystem::FatalError(0xC8) "Error loading
// thread config" at graphics-init (KI-0026). Indexing the Engine root makes that
// file an index HIT kcdx serves through its own PKZIP/DEFLATE reader.
//
// This plugin compiles the engine's asset_index.cpp + pak_reader.cpp into its own
// DLL (the cap-112 shape) PLUS vendored miniz.c (pak_reader.cpp's read path pulls
// miniz), a log-sink stub (kcdx::log symbols), and an overlay-SEAM stub
// (asset_overlay::NormalizeVPath + GetOverlayMap + a test-only SetTestOverlayMap
// driver). asset_index.cpp is BYTE-IDENTICAL to the engine build — the stub
// supplies the overlay seam, not the index logic. It runs TWO assertions at boot:
//
//   (a) DATA-ONLY MISS (the negative control — proves this file IS engine-pak-
//       resident, i.e. was the KI-0026 miss): build the index over <game>/Data
//       ALONE (engineDir empty), assert the engine vpath
//       'config/engine_core.thread_config' does NOT resolve (nullptr) — it is
//       not in any Data pak.
//   (b) DATA+ENGINE HIT (the fix): build the index over BOTH <game>/Data AND
//       <game>/Engine, assert the SAME engine vpath now resolves to a Pak
//       ByteSource whose {size,compressed,method} match Engine.pak's CDR entry.
//
// (a)+(b) together are the load-bearing FALSIFIABLE claim: the file is reachable
// through kcdx's index ONLY because the index now covers the Engine root. A
// regression that dropped the Engine root (or never walked it) yields (b) MISS →
// FAIL; a wrong file (one already in a Data pak) would not MISS in (a) → FAIL.
// This is a DIRECT index-hit assertion against the failing-path file KI-0026's
// evidence names — NOT a boot-survival proxy.
//
// === The fixture (how it was derived — regenerate/verify) =================
//
// Engine.pak lives in <game>/Engine and was read statically (the same CDR recipe
// cap-110/112 document) to confirm the entry's identity:
//
//   import struct
//   d=open("<game>/Engine/Engine.pak","rb").read(); pos=d.rfind(b"PK\x05\x06")
//   _,_,_,_,ent,_,cd,_=struct.unpack_from("<IHHHHIIH",d,pos)
//   # walk the central directory for name=='Config/engine_core.thread_config':
//   #   method=8 (DEFLATE) usize=20096 csize=3950 crc=0x0b1a86bc lho=42854
//
// The test does NOT assume the index by position: it resolves by the known
// normalized vpath and checks the recorded {size,compressed,method} against the
// CDR values. A different Engine.pak layout on a new game version still resolves
// a valid Pak source for that vpath; if the vpath itself were gone the row FAILs
// loud (and the matrix note flags a game-version pak change).

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "kcdx/Interfaces.h"

#include "asset_index.h"
#include "../../src/asset_overlay.h"

// The overlay-seam stub's test-only driver (defined in
// asset_overlay_seam_stub.cpp). Not part of the engine asset_overlay surface —
// it lets this test populate the overlay map the index ingests (kept empty here:
// the assertions are pure vanilla-pak resolution).
namespace kcdx::asset_overlay {
void SetTestOverlayMap(const OverlayMap& m);
}

namespace {

// Manifest bare name — must match [plugin].name in kcdx.toml.
const char* kName = "cap_115_engine_pak_index";
const char* kRow  = "cap-115-engine-pak-index";

const kcdxInterface* g_api  = nullptr;
kcdxPluginHandle     g_self = kcdxInvalidPluginHandle;
kcdxLogger           g_log;

// The game Data + Engine dirs (per-machine; on absence the row reports FAIL with
// a clear reason — never crashes, never silent-skips). Same known-constant path
// + graceful-FAIL-on-absence as cap-110/111/112.
const wchar_t* kGameDataDir =
    L"E:/SteamLibrary/steamapps/common/KingdomComeDeliverance2/Data";
const wchar_t* kGameEngineDir =
    L"E:/SteamLibrary/steamapps/common/KingdomComeDeliverance2/Engine";

// The engine-pak-resident vpath + its CDR-recorded identity (Engine.pak's
// 'Config/engine_core.thread_config'). NormalizeVPath lowercases the pak entry
// name, so the resolved key is 'config/engine_core.thread_config'. This is the
// file that crashed at graphics-init in KI-0026.
const char*    kEngineVPath    = "config/engine_core.thread_config";
const uint64_t kExpectSize     = 20096;  // uncompressed_size
const uint64_t kExpectCompr    = 3950;   // compressed_size
const uint16_t kExpectMethod   = 8;      // DEFLATE

namespace fst = kcdx::fs_takeover;
namespace ao  = kcdx::asset_overlay;

void Report(bool pass, const char* reason) {
    if (pass) g_log.Info ("CAP115", "PASS %s: %s", kRow, reason);
    else      g_log.Error("CAP115", "FAIL %s: %s", kRow, reason);
    g_api->ReportTestResult(g_self, kRow, pass ? 1 : 0, reason);
}

}  // namespace

// === kcdxPlugin_Load ==================================================
//
// Both assertions are file reads + pure CPU — no game lifecycle dependency, so
// the row self-checks and reports here, at load. No overlay is injected (the
// assertions are vanilla-pak resolution only).

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    g_api  = api;
    g_self = api->GetPluginHandle(kName);
    g_log  = kcdxLogger(api, g_self);
    g_log.Info("INIT", "kcdxPlugin_Load called (engine v0x%08X)", api->kcdxVersion);

    ao::SetTestOverlayMap(ao::OverlayMap{});  // no overlay — pure vanilla resolution.

    char reason[900];

    // --- (a) DATA-ONLY MISS: the engine vpath is NOT in any Data pak. -------
    // Build the index over <game>/Data ALONE (engineDir omitted → empty → the
    // Engine root is skipped, exactly the pre-fix single-root behavior). The
    // engine config file lives ONLY in Engine.pak, so it must MISS here — this
    // is the negative control proving the file is genuinely engine-pak-resident
    // (the KI-0026 miss), not already served by a Data pak.
    fst::AssetIndex idxDataOnly = fst::BuildAssetIndex(kGameDataDir);
    const fst::ByteSource* missed = fst::ResolveVPath(idxDataOnly, kEngineVPath);
    if (missed != nullptr) {
        std::snprintf(reason, sizeof(reason),
            "(a) engine vpath '%s' UNEXPECTEDLY resolved (kind=%d) in a Data-only "
            "index built over %ls — it is supposed to be Engine-pak-resident "
            "ONLY. Either the fixture target is wrong (it lives in a Data pak too) "
            "or the game layout changed; the negative control is invalid",
            kEngineVPath, (int)missed->kind, kGameDataDir);
        Report(false, reason);
        return true;
    }

    // --- (b) DATA+ENGINE HIT: the fix — the engine vpath now resolves. ------
    // Build over BOTH roots; the engine config file must now be an index HIT
    // (a Pak ByteSource), with the {size,compressed,method} the CDR recorded.
    fst::AssetIndex idxBoth = fst::BuildAssetIndex(kGameDataDir, kGameEngineDir);
    const fst::ByteSource* hit = fst::ResolveVPath(idxBoth, kEngineVPath);
    if (hit == nullptr) {
        std::snprintf(reason, sizeof(reason),
            "(b) engine vpath '%s' did NOT resolve in the index built over "
            "<game>/Data + <game>/Engine — the Engine root was not walked / "
            "indexed (this is the KI-0026 crash path: the file would be an index "
            "miss, _wfopen a non-existent loose path, fail, and fatal at "
            "graphics-init 0xC8). Engine.pak expected at %ls — if it is absent on "
            "this machine there was nothing to index; this is FAIL, not a skip",
            kEngineVPath, kGameEngineDir);
        Report(false, reason);
        return true;
    }
    if (hit->kind != fst::ByteSource::Kind::Pak) {
        std::snprintf(reason, sizeof(reason),
            "(b) engine vpath '%s' resolved to a NON-Pak source (kind=%d) — "
            "expected a Pak source (it is a vanilla engine-pak entry, no overlay "
            "injected)", kEngineVPath, (int)hit->kind);
        Report(false, reason);
        return true;
    }
    if (hit->size != kExpectSize || hit->compressed != kExpectCompr ||
        hit->method != kExpectMethod) {
        std::snprintf(reason, sizeof(reason),
            "(b) engine vpath '%s' Pak source {size=%llu,compressed=%llu,method=%u} "
            "!= Engine.pak CDR-recorded {size=%llu,compressed=%llu,method=%u} — a "
            "wrong CDR parse for the Engine root, or a game-version pak change",
            kEngineVPath,
            (unsigned long long)hit->size, (unsigned long long)hit->compressed,
            hit->method,
            (unsigned long long)kExpectSize, (unsigned long long)kExpectCompr,
            kExpectMethod);
        Report(false, reason);
        return true;
    }

    // Both (a) and (b) passed.
    std::snprintf(reason, sizeof(reason),
        "kcdx unified asset index covers the Engine pak root PASS — (a) the "
        "engine config vpath '%s' MISSES in a Data-only index (it is "
        "Engine-pak-resident, the KI-0026 miss); (b) building over <game>/Data + "
        "<game>/Engine makes it an index HIT — a Pak ByteSource "
        "{size=%llu,compressed=%llu,method=%u (DEFLATE)} from Engine.pak. The "
        "file that fatalled graphics-init at 0xC8 is now reachable through kcdx's "
        "own PKZIP/DEFLATE reader; the takeover serves it, not the engine",
        kEngineVPath, (unsigned long long)kExpectSize,
        (unsigned long long)kExpectCompr, kExpectMethod);
    Report(true, reason);
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
