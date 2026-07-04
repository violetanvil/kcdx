// CAP-112 — kcdx's unified asset index resolves a vpath O(1); overlay wins vanilla.
//
// The regression proof for the file-system-takeover unified asset index (design
// file-system-takeover.md §5: ONE vpath -> ByteSource map built at load, one
// O(1) lookup per open; §7: overlay-wins-vanilla precedence, computed once at
// build, IS the asset-replacement §4.4/§5.3 precedence). The index COMPOSES the
// overlay map (the §4.4/§5.3 winner the overlay layer already computed) — it
// does not re-implement that precedence; the only precedence the index itself
// applies is loose-over-pak.
//
// This plugin compiles the engine's asset_index.cpp + pak_reader.cpp into its own
// DLL (the cap-110/111 shape) PLUS vendored miniz.c (pak_reader.cpp's read path
// pulls miniz), a log-sink stub (kcdx::log symbols), and an overlay-SEAM stub
// (asset_overlay::NormalizeVPath + GetOverlayMap + the test-only SetTestOverlayMap
// driver). asset_index.cpp is BYTE-IDENTICAL to the engine build — the stub
// supplies the overlay seam, not the index logic. It runs TWO assertions at boot:
//
//   (a) PAK RESOLUTION: build the index over <game>/Data with NO overlay; assert
//       a known vanilla vpath resolves to a Pak ByteSource whose {offset,size,
//       method} match the values the CDR recorded.
//   (b) OVERLAY-WINS-VANILLA: inject a synthetic loose override keyed to that
//       SAME vanilla vpath, rebuild, assert ResolveVPath now returns the Loose
//       source (the override won), NOT the Pak source.
//   (c) BIND-ROOT KEYING: a NESTED level-pak file
//       (Data/Levels/kutnohorsko/level.pak's `leveldata.xml`, stored BARE in the
//       pak CD) resolves under its engine-requested bind-root-PREFIXED key
//       `Levels/kutnohorsko/leveldata.xml` AND does NOT resolve under the bare
//       name `leveldata.xml`. Proves the index keys each nested pak by the mount
//       point vanilla's OpenPack derives — the prefix bare-name keying dropped, so
//       the engine's bind-root-prefixed request missed every nested-pak entry.
//
// (b) and (c) are the load-bearing FALSIFIABLE claims: (b) a correct index
// resolves a vanilla vpath to its pak source UNLESS a loose override exists, in
// which case the override wins (a broken precedence yields the WRONG ByteSource
// KIND -> FAIL); (c) a nested pak's file resolves ONLY under its bind-root-
// prefixed key (a regression to bare keying makes the prefixed lookup miss OR the
// bare lookup hit -> FAIL). PASS requires ALL THREE.
//
// === The fixture (how it was derived — regenerate/verify) =================
//
// GeomCaches.pak lives in <game>/Data and was read statically (the same recipe
// cap-110/111 document) to confirm entry[0]'s identity:
//
//   import struct
//   d=open("<game>/Data/GeomCaches.pak","rb").read(); pos=d.rfind(b"PK\x05\x06")
//   _,_,_,_,ent,_,cd,_=struct.unpack_from("<IHHHHIIH",d,pos)
//   r=struct.unpack_from("<IHHHHHHIIIHHHHHII",d,cd)
//   # entry[0]: method=0 (STORED) crc=0xd7b807ab csize=usize=352571 lho=0
//   #   name='Objects/manmade/common_decorations/flags/flag.cax'
//
// The test does NOT assume the index by position: it resolves by the known vpath
// (normalized) and checks the recorded {offset,size,method} against entry[0]'s
// values. A different layout on a new game version still resolves a valid Pak
// source for that vpath; if the vpath itself were gone the row FAILs loud.

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
// it lets this test populate the overlay map the index ingests.
namespace kcdx::asset_overlay {
void SetTestOverlayMap(const OverlayMap& m);
}

namespace {

// Manifest bare name — must match [plugin].name in kcdx.toml.
const char* kName = "cap_112_asset_index";
const char* kRow  = "cap-112-asset-index";

const kcdxInterface* g_api  = nullptr;
kcdxPluginHandle     g_self = kcdxInvalidPluginHandle;
kcdxLogger           g_log;

// The game Data dir (per-machine; on absence the row reports FAIL with a clear
// reason — never crashes, never silent-skips). Same known-constant path +
// graceful-FAIL-on-absence as cap-110/111 (surfaced: whether this should come
// from an engine API rather than a constant).
const wchar_t* kGameDataDir =
    L"E:/SteamLibrary/steamapps/common/KingdomComeDeliverance2/Data";

// The known vanilla vpath + its CDR-recorded identity (GeomCaches.pak entry[0]).
const char*    kVanillaVPath = "Objects/manmade/common_decorations/flags/flag.cax";
const uint64_t kExpectOffset = 0;        // local_header_offset
const uint64_t kExpectSize   = 352571;   // uncompressed_size
const uint16_t kExpectMethod = 0;        // STORED

// (c) BIND-ROOT KEYING. A NESTED-pak file — level.pak under
// <game>/Data/Levels/kutnohorsko/ stores `leveldata.xml` BARE in its central
// directory, but the engine requests it by the pak's mount point
// `Levels/kutnohorsko/leveldata.xml` (the bind-root vanilla's OpenPack auto-
// derives from the pak's dir). The index MUST key it under the bind-root-prefixed
// form, or every level-resource request misses. The falsifiable claim: the PREFIXED key resolves AND the
// BARE key does NOT — proving the prefix is required, not merely present. If the
// bind-root keying regresses to bare pe.name, the prefixed lookup misses → FAIL.
const char* kNestedPrefixedVPath = "Levels/kutnohorsko/leveldata.xml";  // engine's request form
const char* kNestedBareVPath     = "leveldata.xml";                     // the bare CDR name (must NOT resolve)

namespace fst = kcdx::fs_takeover;
namespace ao  = kcdx::asset_overlay;

void Report(bool pass, const char* reason) {
    if (pass) g_log.Info ("CAP112", "PASS %s: %s", kRow, reason);
    else      g_log.Error("CAP112", "FAIL %s: %s", kRow, reason);
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

    char reason[800];

    // --- (a) PAK RESOLUTION: index over <game>/Data, no overlay. ------------
    ao::SetTestOverlayMap(ao::OverlayMap{});  // empty overlay for (a).
    fst::AssetIndex idxNoOverlay = fst::BuildAssetIndex(kGameDataDir);

    const fst::ByteSource* pak = fst::ResolveVPath(idxNoOverlay, kVanillaVPath);
    if (pak == nullptr) {
        std::snprintf(reason, sizeof(reason),
            "(a) vanilla vpath '%s' did not resolve in the index built over %ls "
            "(no Pak ByteSource keyed). Fixture pak GeomCaches.pak expected under "
            "<game>/Data — if the path differs on this machine there was nothing "
            "to index; this is FAIL, not a skip",
            kVanillaVPath, kGameDataDir);
        Report(false, reason);
        return true;
    }
    if (pak->kind != fst::ByteSource::Kind::Pak) {
        std::snprintf(reason, sizeof(reason),
            "(a) vanilla vpath '%s' resolved to a NON-Pak source (kind=%d) with no "
            "overlay injected — expected Pak", kVanillaVPath, (int)pak->kind);
        Report(false, reason);
        return true;
    }
    if (pak->offset != kExpectOffset || pak->size != kExpectSize ||
        pak->method != kExpectMethod) {
        std::snprintf(reason, sizeof(reason),
            "(a) vanilla vpath '%s' Pak source {offset=%llu,size=%llu,method=%u} "
            "!= recorded {offset=%llu,size=%llu,method=%u}",
            kVanillaVPath,
            (unsigned long long)pak->offset, (unsigned long long)pak->size,
            pak->method,
            (unsigned long long)kExpectOffset, (unsigned long long)kExpectSize,
            kExpectMethod);
        Report(false, reason);
        return true;
    }

    // --- (b) OVERLAY-WINS-VANILLA: inject a loose override, rebuild. --------
    // The synthetic override keys the SAME vanilla vpath to a fake loose disk
    // path (it need not exist — the test asserts the resolved SOURCE KIND, not a
    // read). NormalizeVPath the key exactly as the overlay map does in
    // production (the engine keys its OverlayMap normalized).
    const std::string fakeDisk = "C:/kcdx-test/overlay/flag.cax";
    ao::OverlayMap overlay;
    {
        ao::OverlayEntry oe;
        oe.owningPlugin = "cap_112_test";
        oe.diskPath     = fakeDisk;
        overlay[ao::NormalizeVPath(kVanillaVPath)] = oe;
    }
    ao::SetTestOverlayMap(overlay);
    fst::AssetIndex idxWithOverlay = fst::BuildAssetIndex(kGameDataDir);

    const fst::ByteSource* won = fst::ResolveVPath(idxWithOverlay, kVanillaVPath);
    if (won == nullptr) {
        std::snprintf(reason, sizeof(reason),
            "(b) vanilla vpath '%s' did not resolve after injecting a loose "
            "override — the override was lost", kVanillaVPath);
        Report(false, reason);
        return true;
    }
    if (won->kind != fst::ByteSource::Kind::Loose) {
        std::snprintf(reason, sizeof(reason),
            "(b) OVERLAY DID NOT WIN: vanilla vpath '%s' resolved to kind=%d "
            "(expected Loose) after injecting a loose override — loose did NOT "
            "overwrite the pak source (broken loose-over-pak precedence)",
            kVanillaVPath, (int)won->kind);
        Report(false, reason);
        return true;
    }
    if (won->diskPath != fakeDisk) {
        std::snprintf(reason, sizeof(reason),
            "(b) the winning Loose source's diskPath '%s' != injected override "
            "'%s'", won->diskPath.c_str(), fakeDisk.c_str());
        Report(false, reason);
        return true;
    }

    // --- (c) BIND-ROOT KEYING. ---------------------------------------------
    // Build over <game>/Data with no overlay (the (a) index already does, but
    // rebuild clean so (b)'s injected overlay does not leak into (c)). Assert the
    // nested level-pak file resolves under its bind-root-PREFIXED key and does NOT
    // resolve under its bare CDR name — the prefix is required, not incidental.
    ao::SetTestOverlayMap(ao::OverlayMap{});  // empty overlay for (c).
    fst::AssetIndex idxBindRoot = fst::BuildAssetIndex(kGameDataDir);

    const fst::ByteSource* nested = fst::ResolveVPath(idxBindRoot, kNestedPrefixedVPath);
    if (nested == nullptr) {
        std::snprintf(reason, sizeof(reason),
            "(c) BIND-ROOT MISS: nested level-pak file '%s' did NOT "
            "resolve — the engine requests it by this bind-root-prefixed path, but "
            "the index has no key for it. The failing shape: a "
            "nested pak keyed by bare pe.name (dropping the Levels/<lvl>/ mount "
            "prefix) misses every level request. Fixture level.pak expected under "
            "<game>/Data/Levels/kutnohorsko/ — if absent on this machine there was "
            "nothing to index; FAIL, not a skip", kNestedPrefixedVPath);
        Report(false, reason);
        return true;
    }
    if (nested->kind != fst::ByteSource::Kind::Pak) {
        std::snprintf(reason, sizeof(reason),
            "(c) nested level-pak file '%s' resolved to a NON-Pak source (kind=%d) "
            "with no overlay — expected Pak", kNestedPrefixedVPath, (int)nested->kind);
        Report(false, reason);
        return true;
    }
    // The bare CDR name must NOT resolve — if it does, the index kept bare keys
    // (the regression), the prefix above was incidental, and the falsifiable half
    // of the claim is broken.
    const fst::ByteSource* bare = fst::ResolveVPath(idxBindRoot, kNestedBareVPath);
    if (bare != nullptr) {
        std::snprintf(reason, sizeof(reason),
            "(c) BIND-ROOT KEYING REGRESSED: the bare CDR name '%s' RESOLVED "
            "(kind=%d) — the index kept bare keys instead of bind-root-prefixed "
            "ones. The engine never requests the bare name; a bare-keyed entry is "
            "the bind-root miss waiting to happen. The prefixed key must be the ONLY "
            "key", kNestedBareVPath, (int)bare->kind);
        Report(false, reason);
        return true;
    }

    // All of (a), (b), (c) passed.
    std::snprintf(reason, sizeof(reason),
        "kcdx unified asset index PASS — (a) vanilla vpath '%s' resolves O(1) to "
        "its Pak ByteSource {offset=%llu,size=%llu,method=%u (STORED)}; (b) a "
        "synthetic loose override of the SAME vpath WINS (Loose, diskPath='%s'); "
        "(c) BIND-ROOT: nested '%s' resolves under its mount-prefixed key "
        "while bare '%s' does NOT — the Levels/<lvl>/ prefix is required, proving "
        "the engine-requested form keys correctly. Proves the index composes the "
        "overlay over the discovered vanilla paks AND keys each nested pak by its "
        "bind-root, one O(1) lookup per open",
        kVanillaVPath, (unsigned long long)kExpectOffset,
        (unsigned long long)kExpectSize, kExpectMethod, fakeDisk.c_str(),
        kNestedPrefixedVPath, kNestedBareVPath);
    Report(true, reason);
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
