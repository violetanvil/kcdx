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
// supplies the overlay seam, not the index logic. It runs FOUR assertions at boot:
//
//   (a) DATA-ONLY MISS (the negative control — proves this file IS engine-pak-
//       resident, i.e. was the KI-0026 miss): build the index over <game>/Data
//       ALONE (engineDir empty), assert the engine vpath
//       'config/engine_core.thread_config' does NOT resolve (nullptr) — it is
//       not in any Data pak.
//   (b) DATA+ENGINE HIT (the Engine root is walked): build the index over BOTH
//       <game>/Data AND <game>/Engine, assert the bare pak-relative key now
//       resolves to a Pak ByteSource whose {size,compressed,method} match
//       Engine.pak's CDR entry.
//   (c) ENGINE-ALIAS HIT (the KI-0026 alias-ownership fix): resolve the engine's
//       ACTUAL lookup form '%engine%/config/engine_core.thread_config' (the
//       aliased path it opens at graphics-init) against the same DATA+ENGINE
//       index, assert it lands on the SAME Engine.pak ByteSource as (b). kcdx
//       OWNS the %engine% alias — ResolveVPath expands '%engine%/X' to the
//       pak-root key 'X'.
//   (d) ALIAS-EXPANDED FORM MISSES (the slot-1-pak-serve fix — the form-mismatch
//       the broken first fix shipped): assert the alias-EXPANDED LOOSE form
//       'engine/config/engine_core.thread_config' does NOT resolve. This is the
//       form the engine's ORIGINAL resolver produces ('%engine%'→'engine\'); the
//       broken first fix thunked that original on a pak hit, so FOpen received
//       'engine/...' — NOT a stored key (only the bare 'config/...' key and the
//       '%engine%/...' alias the strip folds to it are keys), missed, and
//       fatalled at 0xC8. Asserting it MISSES pins the FORM DISTINCTION the fix
//       depends on: FOpen must receive the '%engine%/...' form, never
//       'engine/...'. (The in-body comment explains why the unit layer cannot
//       drive the full slot-1→FOpen flow — the live boot trace is the end-to-end
//       proof; this row guards the form-key contract the fix rests on.)
//
// (a)+(b)+(c)+(d) together are the load-bearing FALSIFIABLE claim. (a) proves the
// file is genuinely Engine-pak-resident; (b) proves the Engine root is walked; (c)
// proves kcdx owns the alias (the form the engine opens resolves); (d) proves the
// alias-expanded loose form is NOT a key — the form distinction the slot-1-pak-
// serve fix relies on. Before the alias-strip, (b) PASSED while the engine still
// MISSED on the '%engine%/'-prefixed form — what (c) catches (FAILS without the
// strip). Before the slot-1-pak-serve fix, (b)+(c) BOTH PASSED while the LIVE
// FOpen still missed, because it received the 'engine/...' form neither tested —
// the blind spot (d) closes (it FAILS if an `engine/` strip, Option 1 NOT chosen,
// were added that made 'engine/...' a hit, masking the mismatch). A regression
// that dropped the Engine root yields (b) MISS → FAIL; one that dropped the
// alias-strip yields (c) MISS → FAIL; one that re-admitted the 'engine/...' form
// yields (d) HIT → FAIL; a wrong file (one already in a Data pak) would not MISS
// in (a) → FAIL. This is a DIRECT index assertion against the failing-path file +
// every form KI-0026's two fixes turn on — NOT a boot-survival proxy.
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
// The engine's ACTUAL lookup form — the aliased path it opens at graphics-init.
// The %engine% alias is kcdx's to own (KI-0026): it expands to the pak-ROOT key
// kEngineVPath (the `%engine%/` prefix dropped). Resolving THIS prefixed input
// directly exercises the KI-0026 fix — it MISSES (the bug) without the alias-strip
// in ResolveVPath, HITS the same Engine.pak ByteSource as kEngineVPath with it.
const char*    kEngineAliasVPath = "%engine%/config/engine_core.thread_config";
const uint64_t kExpectSize     = 20096;  // uncompressed_size
const uint64_t kExpectCompr    = 3950;   // compressed_size
const uint16_t kExpectMethod   = 8;      // DEFLATE

// KI-0028 — the data/gameshaders alias fold. Shaders.pak (in <game>/Engine)
// stores shader entries under the `shaders/` pak root (e.g. 'Shaders/RunTime.ext'
// → normalized 'shaders/runtime.ext'), but the engine's shader subsystem opens
// them via the `data/gameshaders/` alias ('data/gameshaders/runtime.ext'). Without
// the fold every shader lookup in the alias form missed → loose-open errno=2 → the
// shader never loaded → the render pipeline PRESENTED (120fps) but composited
// every frame BLACK (KI-0028). FoldEngineAliasToIndexKey maps data/gameshaders/X
// → shaders/X, the same alias-ownership pattern as %engine%. Fixture: the CDR
// identity of Shaders.pak's 'Shaders/RunTime.ext' (read statically, same recipe).
const char*    kShaderBareVPath  = "shaders/runtime.ext";            // stored key
const char*    kShaderAliasVPath = "data/gameshaders/runtime.ext";   // engine's alias form
const uint64_t kShaderSize     = 30445;  // uncompressed_size
const uint64_t kShaderCompr    = 3656;   // compressed_size
const uint16_t kShaderMethod   = 8;      // DEFLATE

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

    // --- (c) ENGINE-ALIAS HIT: the KI-0026 fix — the engine's ACTUAL lookup
    // form resolves. The engine opens '%engine%/config/engine_core.thread_config'
    // (the aliased path), NOT the bare pak-relative key (b) used. kcdx owns the
    // %engine% alias: ResolveVPath expands it to the pak-ROOT key, so the prefixed
    // input must land on the SAME Engine.pak Pak ByteSource as (b). This FAILS if
    // the alias-strip is absent (the bug: the prefixed lookup keeps '%engine%/'
    // and misses the stored 'config/...' key) — the exact graphics-init 0xC8 path.
    const fst::ByteSource* aliasHit =
        fst::ResolveVPath(idxBoth, kEngineAliasVPath);
    if (aliasHit == nullptr) {
        std::snprintf(reason, sizeof(reason),
            "(c) the engine's ACTUAL aliased lookup '%s' did NOT resolve, while "
            "the bare pak-relative key '%s' did (b) — the %%engine%% alias is not "
            "expanded to the pak-root key in ResolveVPath, so the prefixed lookup "
            "misses the stored entry. This IS the KI-0026 crash path: the engine "
            "opens the aliased form, the index miss thunks a non-existent loose "
            "path, the open fails, and graphics-init fatals at 0xC8",
            kEngineAliasVPath, kEngineVPath);
        Report(false, reason);
        return true;
    }
    if (aliasHit->kind != fst::ByteSource::Kind::Pak ||
        aliasHit->size != hit->size || aliasHit->compressed != hit->compressed ||
        aliasHit->method != hit->method) {
        std::snprintf(reason, sizeof(reason),
            "(c) the aliased lookup '%s' resolved to a DIFFERENT source than the "
            "bare key '%s' — alias {kind=%d,size=%llu,compressed=%llu,method=%u} "
            "!= bare {kind=%d,size=%llu,compressed=%llu,method=%u}. The alias must "
            "expand to the SAME Engine.pak ByteSource, not a different/partial one",
            kEngineAliasVPath, kEngineVPath,
            (int)aliasHit->kind, (unsigned long long)aliasHit->size,
            (unsigned long long)aliasHit->compressed, aliasHit->method,
            (int)hit->kind, (unsigned long long)hit->size,
            (unsigned long long)hit->compressed, hit->method);
        Report(false, reason);
        return true;
    }

    // --- (d) ALIAS-EXPANDED FORM MISSES: the form-mismatch the broken first fix
    // shipped. The engine's ORIGINAL resolver expands the %engine% alias to a
    // LOOSE path 'engine/config/...' (the `%engine%`→`engine\` expansion). The
    // first fix's slot-1 thunked that original on a pak hit, so FOpen received the
    // 'engine/...' form — which is NOT a stored index key (only the bare
    // 'config/...' key and the '%engine%/...' alias the strip folds to it are).
    // FOpen missed, _wfopen'd the loose path, and graphics-init fatalled at 0xC8.
    // Assertions (b)/(c) both PASSED while the live FOpen missed precisely because
    // neither tested the 'engine/...' form FOpen actually received — that is the
    // blind spot this row closes.
    //
    // The unit test runs at the ResolveVPath layer; it CANNOT drive the full
    // slot-1→FOpen flow (the fix returns '%engine%/...' from slot-1 so FOpen
    // re-resolves the alias form, never the 'engine/...' form). So (d) asserts the
    // FORM DISTINCTION the fix depends on: the alias-EXPANDED 'engine/...' form
    // MISSES the index. This pins that 'engine/...' is NOT a key — proving (1) WHY
    // the old flow failed (FOpen saw a non-key form) and (2) that the fix's
    // correctness rests on FOpen receiving the '%engine%/...' form (which (c)
    // proved HITS), not the 'engine/...' form. (d) FAILS if an `engine/` strip
    // were added (Option 1, NOT chosen) that made 'engine/...' a hit too — which
    // would re-admit the form ambiguity and mask exactly this mismatch. The
    // end-to-end proof that FOpen now receives '%engine%/...' is the LIVE BOOT
    // TRACE ('how=index-pak-serve' at slot-1 then 'how=index-pak' at FOpen), which
    // a unit test cannot stand in for.
    const char* kEngineExpandedVPath = "engine/config/engine_core.thread_config";
    const fst::ByteSource* expandedMiss =
        fst::ResolveVPath(idxBoth, kEngineExpandedVPath);
    if (expandedMiss != nullptr) {
        std::snprintf(reason, sizeof(reason),
            "(d) the alias-EXPANDED form '%s' UNEXPECTEDLY resolved (kind=%d) — it "
            "must MISS: only the bare pak-relative key '%s' and the '%%engine%%/' "
            "alias form are stored keys. A hit here means an `engine/` strip was "
            "added (Option 1, not chosen), which masks the AdjustFileName-output / "
            "FOpen-input form-mismatch this row exists to catch (the broken first "
            "fix's blind spot). The fix relies on FOpen receiving the "
            "'%%engine%%/...' form, NOT 'engine/...'",
            kEngineExpandedVPath, (int)expandedMiss->kind, kEngineVPath);
        Report(false, reason);
        return true;
    }

    // --- (e) GAMESHADERS-ALIAS HIT: the KI-0028 fix — the engine's shader lookup
    // form resolves. The engine opens shaders via 'data/gameshaders/<X>.ext' (the
    // alias), but Shaders.pak stores them under 'shaders/<X>.ext'. kcdx owns the
    // data/gameshaders alias: FoldEngineAliasToIndexKey maps data/gameshaders/X →
    // shaders/X, so the alias-form lookup must land on the SAME Shaders.pak Pak
    // ByteSource as the bare 'shaders/<X>.ext' key. This FAILS without the fold (the
    // KI-0028 bug: the alias-form lookup keeps 'data/gameshaders/' and misses the
    // stored 'shaders/' key → loose-open errno=2 → the shader never loads → every
    // presented frame is BLACK). Both forms resolved against the DATA+ENGINE index.
    const fst::ByteSource* shBare  = fst::ResolveVPath(idxBoth, kShaderBareVPath);
    const fst::ByteSource* shAlias = fst::ResolveVPath(idxBoth, kShaderAliasVPath);
    if (shBare == nullptr) {
        std::snprintf(reason, sizeof(reason),
            "(e) the bare shader key '%s' did NOT resolve in the DATA+ENGINE index "
            "— Shaders.pak (in <game>/Engine) was not walked/indexed, or the entry "
            "moved on a game-version pak change. Expected a Pak source with "
            "{size=%llu,compressed=%llu,method=%u}",
            kShaderBareVPath, (unsigned long long)kShaderSize,
            (unsigned long long)kShaderCompr, kShaderMethod);
        Report(false, reason);
        return true;
    }
    if (shBare->kind != fst::ByteSource::Kind::Pak ||
        shBare->size != kShaderSize || shBare->compressed != kShaderCompr ||
        shBare->method != kShaderMethod) {
        std::snprintf(reason, sizeof(reason),
            "(e) the bare shader key '%s' resolved to {kind=%d,size=%llu,"
            "compressed=%llu,method=%u} != Shaders.pak CDR {size=%llu,compressed="
            "%llu,method=%u (DEFLATE)} — a wrong CDR parse or a game-version pak "
            "change", kShaderBareVPath, (int)shBare->kind,
            (unsigned long long)shBare->size, (unsigned long long)shBare->compressed,
            shBare->method, (unsigned long long)kShaderSize,
            (unsigned long long)kShaderCompr, kShaderMethod);
        Report(false, reason);
        return true;
    }
    if (shAlias == nullptr) {
        std::snprintf(reason, sizeof(reason),
            "(e) the engine's ACTUAL shader lookup form '%s' did NOT resolve, while "
            "the bare key '%s' did — the data/gameshaders alias is not folded to the "
            "stored 'shaders/' key. This IS the KI-0028 black-frame path: the engine "
            "opens the alias form, the index miss falls to a loose open (errno=2), "
            "the shader never loads, and the render pipeline presents (120fps) but "
            "composites every frame BLACK (no UI — the Scaleform shader missed too)",
            kShaderAliasVPath, kShaderBareVPath);
        Report(false, reason);
        return true;
    }
    if (shAlias->kind != shBare->kind || shAlias->size != shBare->size ||
        shAlias->compressed != shBare->compressed ||
        shAlias->method != shBare->method) {
        std::snprintf(reason, sizeof(reason),
            "(e) the shader alias lookup '%s' resolved to a DIFFERENT source than "
            "the bare key '%s' — alias {kind=%d,size=%llu} != bare {kind=%d,size="
            "%llu}. The data/gameshaders alias must fold to the SAME Shaders.pak "
            "ByteSource", kShaderAliasVPath, kShaderBareVPath,
            (int)shAlias->kind, (unsigned long long)shAlias->size,
            (int)shBare->kind, (unsigned long long)shBare->size);
        Report(false, reason);
        return true;
    }

    // (a), (b), (c), (d), and (e) all passed.
    std::snprintf(reason, sizeof(reason),
        "kcdx unified asset index covers the Engine pak root + owns the %%engine%% "
        "AND data/gameshaders aliases PASS — (a) the engine config vpath '%s' MISSES "
        "in a Data-only index (Engine-pak-resident, the KI-0026 miss); (b) building "
        "over <game>/Data + <game>/Engine makes the bare key an index HIT — a Pak "
        "ByteSource {size=%llu,method=%u} from Engine.pak; (c) the engine's aliased "
        "'%s' resolves to the SAME ByteSource (kcdx owns %%engine%%); (d) the "
        "alias-EXPANDED loose form '%s' MISSES (proving 'engine/...' is not a key); "
        "(e) KI-0028: the shader alias '%s' folds to the stored '%s' and HITS the "
        "SAME Shaders.pak source {size=%llu,method=%u} — without the fold the engine's "
        "shader opens missed, the pipeline presented but every frame was BLACK. Every "
        "engine pak-alias the render path uses is now an index HIT kcdx serves",
        kEngineVPath, (unsigned long long)kExpectSize, kExpectMethod,
        kEngineAliasVPath, kEngineExpandedVPath, kShaderAliasVPath, kShaderBareVPath,
        (unsigned long long)kShaderSize, kShaderMethod);
    Report(true, reason);
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
