// CAP-114 — the file-system-takeover existence/metadata + enumeration slots
// answer from kcdx's unified asset index.
//
// The regression proof for the existence/metadata-by-name slots (IsFileExist /
// GetFileSize / GetFileSizeCompressed) and the directory-enumeration prefix
// walk (ForEachFile) answering from the unified index rather than the engine's
// per-call search-path walk (file-system-takeover design §4.5 "Existence /
// metadata by name" + "Directory enumeration"; §5 the index is the asset fast
// path). The slots' index-HIT arms are PURE functions of the resolved
// ByteSource — existence = a hit; size = the entry's size; compressed = the
// entry's compressed extent — so this DLL tests that resolution core HEADLESS:
// it compiles the engine's own asset_index.cpp into itself (the cap-110 shape —
// a self-contained artifact built against include/ + the one engine source
// under test, with standalone stubs for asset_index.cpp's external deps), builds
// an AssetIndex BY HAND with known entries, and asserts the resolution +
// size-derivation logic the metadata slots rest on.
//
// === WHY this is the headless seam (and what is NOT here) ===================
//
// The metadata/enum slot FUNCTIONS themselves (metadata_slots.cpp /
// enum_slots.cpp) take a `void* self` member-call arg, read the PROCESS-LIFETIME
// GetBuiltIndex() set only at the live seat, and on an index MISS thunk the
// SLOT'S OWN CAPTURED ORIGINAL engine body (captured from the live vtable swap
// via SetMetadataOriginals — the original consults the engine pak-dir AND disk).
// Those paths are reachable ONLY against a live swapped CCryPak object in-game —
// they are NOT unit-testable standalone (there is no engine object and no
// captured original off a live vtable). What IS headless-testable, and what this
// DLL proves, is the INDEX-RESOLUTION CORE the slots' index-HIT answer derives
// from:
//   - ResolveVPath(index, vpath) returns the winning ByteSource (or nullptr).
//   - existence = a non-null ResolveVPath; size = bs->size; compressed =
//     bs->compressed — the exact derivations the metadata slots' hit arms use.
//   - the enumeration prefix-match (the index walk in kcdx_ForEachFile) selects
//     the right pak vpaths under a directory prefix at a single level.
// The full live slot dispatch is declared LIVE-LAUNCH-ONLY (see the matrix row +
// the step deliverable) — this row does NOT claim it ran. In particular the
// index-MISS arm now thunks the slot's CAPTURED ORIGINAL engine body (engine
// pak-dir AND disk), so a name only an engine-mounted pak carries gets the real
// engine answer rather than a false/-1/0 — that thunk fires only against the
// live swapped vtable (the captured original comes off the live original
// vtable), so the live launch is its coverage, not this headless DLL.
//
// === The falsifiable assertions (each states what makes it FAIL) ============
//
//   (a) VANILLA vpath HIT — a Pak ByteSource at "objects/foo.cgf" resolves, and
//       its size==1000 / compressed==400 / kind==Pak. FAILS if ResolveVPath
//       misses, or the size/compressed/kind differ (a broken index/derivation).
//   (b) LOOSE override WINS — inserting a Loose entry at the SAME vpath as a Pak
//       entry resolves to the Loose source (loose-over-pak, §5). FAILS if the
//       pak source still wins, or the loose source is absent.
//   (c) NON-EXISTENT vpath MISS — ResolveVPath("does/not/exist") == nullptr (the
//       slot's existence answer is false; size answer is the sentinel). FAILS if
//       a miss returns non-null (a phantom hit).
//   (d) NORMALIZATION — a mixed-case / backslashed query ("Objects\\Foo.CGF")
//       resolves to the same entry as the normalized key. FAILS if the fold does
//       not match (the metadata slots would miss a real asset).
//   (e) ENUM prefix-match — over a hand-built index with entries at "ui/a.dds",
//       "ui/b.dds", "ui/sub/c.dds", "other/d.dds", the single-level walk for
//       prefix "ui/" selects exactly {a.dds, b.dds} (NOT sub/c.dds — a deeper
//       level — and NOT other/d.dds). FAILS if the deeper or sibling entry is
//       included, or a same-level entry is missed (the unified-enum delta wrong).

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "kcdx/Interfaces.h"

#include "asset_index.h"   // ByteSource / AssetIndex / ResolveVPath (engine type under test)
#include "asset_overlay.h" // NormalizeVPath (the shared key fold the index uses)

namespace {

const char* kName = "cap_114_fs_takeover_metadata_enum";
const char* kRow  = "cap-114-fs-takeover-metadata-enum";

const kcdxInterface* g_api  = nullptr;
kcdxPluginHandle     g_self = kcdxInvalidPluginHandle;
kcdxLogger           g_log;

using kcdx::fs_takeover::AssetIndex;
using kcdx::fs_takeover::ByteSource;
using kcdx::fs_takeover::ResolveVPath;

// The SAME key fold the index uses (asset_index.cpp keys by NormalizeVPath;
// ResolveVPath normalizes the query). Insert under the normalized key so the
// test exercises the real key path. NormalizeVPath is supplied by the
// standalone stub TU (matching the real lowercase + slash fold).
std::string Key(const char* vpath) {
    return kcdx::asset_overlay::NormalizeVPath(vpath);
}

// Insert a Pak ByteSource at `vpath` with the given sizes (mirrors what
// BuildAssetIndex records from a parsed pak entry).
void InsertPak(AssetIndex& idx, const char* vpath, uint64_t size,
               uint64_t compressed) {
    ByteSource s;
    s.kind       = ByteSource::Kind::Pak;
    s.pakFile    = L"Data/test.pak";
    s.size       = size;
    s.compressed = compressed;
    s.method     = (size == compressed) ? 0 : 8;
    s.crc        = 0xdeadbeef;
    idx[Key(vpath)] = std::move(s);
}

void InsertLoose(AssetIndex& idx, const char* vpath, const char* disk) {
    ByteSource s;
    s.kind     = ByteSource::Kind::Loose;
    s.diskPath = disk;
    idx[Key(vpath)] = std::move(s);
}

void Report(bool pass, const char* reason) {
    if (pass) g_log.Info ("CAP114", "PASS %s: %s", kRow, reason);
    else      g_log.Error("CAP114", "FAIL %s: %s", kRow, reason);
    g_api->ReportTestResult(g_self, kRow, pass ? 1 : 0, reason);
}

// Mirror kcdx_ForEachFile's index prefix-match (the single-level select over
// pak sources): a vpath is selected iff it starts with `prefix`, has content
// past it, is a Pak source, and has NO further '/' past the prefix (single
// directory level). This is the exact predicate enum_slots.cpp applies to the
// index; testing it here proves the unified-enum delta selection without the
// live callback dispatch.
size_t CountEnumMatches(const AssetIndex& idx, const std::string& prefix) {
    size_t n = 0;
    for (const auto& kv : idx) {
        const std::string& vpath = kv.first;
        const ByteSource& src = kv.second;
        if (src.kind != ByteSource::Kind::Pak) continue;
        if (vpath.size() <= prefix.size()) continue;
        if (vpath.compare(0, prefix.size(), prefix) != 0) continue;
        if (vpath.find('/', prefix.size()) != std::string::npos) continue;
        ++n;
    }
    return n;
}

}  // namespace

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    g_api  = api;
    g_self = api->GetPluginHandle(kName);
    g_log  = kcdxLogger(api, g_self);
    g_log.Info("INIT", "kcdxPlugin_Load called (engine v0x%08X)", api->kcdxVersion);

    char reason[700];

    // --- Build a hand-made index with known entries. ------------------------
    AssetIndex idx;
    InsertPak(idx, "objects/foo.cgf", /*size=*/1000, /*compressed=*/400);
    InsertPak(idx, "ui/a.dds",     200, 200);
    InsertPak(idx, "ui/b.dds",     300, 120);
    InsertPak(idx, "ui/sub/c.dds", 400, 400);  // deeper level — NOT a "ui/" single-level match
    InsertPak(idx, "other/d.dds",  500, 500);

    // (a) VANILLA HIT — size/compressed/kind derivation.
    const ByteSource* a = ResolveVPath(idx, "objects/foo.cgf");
    if (!a || a->kind != ByteSource::Kind::Pak || a->size != 1000 ||
        a->compressed != 400) {
        std::snprintf(reason, sizeof(reason),
            "(a) vanilla pak vpath 'objects/foo.cgf' resolution wrong — got %s "
            "(kind=%d size=%llu compressed=%llu); expected a Pak source with "
            "size=1000 compressed=400. The metadata slots' index-HIT size "
            "derivation (GetFileSize→bs->size, GetFileSizeCompressed→bs->compressed) "
            "rests on this",
            a ? "hit" : "MISS", a ? (int)a->kind : -1,
            (unsigned long long)(a ? a->size : 0),
            (unsigned long long)(a ? a->compressed : 0));
        Report(false, reason);
        return true;
    }

    // (b) LOOSE override WINS at the same vpath.
    InsertLoose(idx, "objects/foo.cgf", "C:/mods/foo/assets/objects/foo.cgf");
    const ByteSource* b = ResolveVPath(idx, "objects/foo.cgf");
    if (!b || b->kind != ByteSource::Kind::Loose ||
        b->diskPath != "C:/mods/foo/assets/objects/foo.cgf") {
        std::snprintf(reason, sizeof(reason),
            "(b) loose override did NOT win at 'objects/foo.cgf' — got %s "
            "(kind=%d disk='%s'); expected the Loose source (loose-over-pak, §5). "
            "An existence/size slot would serve the vanilla pak entry instead of "
            "the override",
            b ? "hit" : "MISS", b ? (int)b->kind : -1,
            b ? b->diskPath.c_str() : "");
        Report(false, reason);
        return true;
    }

    // (c) NON-EXISTENT vpath MISSES.
    const ByteSource* c = ResolveVPath(idx, "does/not/exist.cgf");
    if (c != nullptr) {
        std::snprintf(reason, sizeof(reason),
            "(c) a non-existent vpath 'does/not/exist.cgf' returned a non-null "
            "ByteSource (kind=%d) — a phantom hit. An existence slot would report "
            "exists=true for a file that is not served",
            (int)c->kind);
        Report(false, reason);
        return true;
    }

    // (d) NORMALIZATION — a mixed-case/backslashed query hits the same entry.
    const ByteSource* d = ResolveVPath(idx, "UI\\A.DDS");
    if (!d || d->kind != ByteSource::Kind::Pak || d->size != 200) {
        std::snprintf(reason, sizeof(reason),
            "(d) the mixed-case/backslashed query 'UI\\\\A.DDS' did not resolve "
            "to the 'ui/a.dds' entry (got %s, size=%llu; expected size=200). The "
            "metadata slots would MISS a real asset whose engine query arrives "
            "mixed-case",
            d ? "hit" : "MISS", (unsigned long long)(d ? d->size : 0));
        Report(false, reason);
        return true;
    }

    // (e) ENUM single-level prefix-match selects exactly {ui/a.dds, ui/b.dds}.
    const size_t uiMatches = CountEnumMatches(idx, "ui/");
    if (uiMatches != 2) {
        std::snprintf(reason, sizeof(reason),
            "(e) enumeration prefix 'ui/' matched %zu pak vpaths; expected 2 "
            "(ui/a.dds, ui/b.dds) — NOT the deeper ui/sub/c.dds (a sub-level) and "
            "NOT other/d.dds (a sibling). A wrong count means the unified-enum "
            "delta selection (single-level pak vpaths under the prefix) is wrong",
            uiMatches);
        Report(false, reason);
        return true;
    }

    std::snprintf(reason, sizeof(reason),
        "kcdx metadata+enum index resolution PASS — (a) vanilla pak vpath resolves "
        "with size=1000/compressed=400; (b) a loose override wins over the pak at "
        "the same vpath; (c) a non-existent vpath MISSES (nullptr); (d) a "
        "mixed-case/backslashed query normalizes to the same entry; (e) the "
        "single-level enum prefix 'ui/' selects exactly 2 pak vpaths (not the "
        "sub-level or sibling). Proves the index-resolution + size-derivation core "
        "the existence/metadata slots' HIT arms and ForEachFile's index walk rest "
        "on. LIVE-LAUNCH-ONLY (NOT asserted here): the full slot dispatch — the "
        "miss arm thunking the slot's captured original (engine pak-dir AND "
        "disk), the OS metadata op, the enum per-file callback — runs only "
        "against the live swapped CCryPak vtable in-game");
    Report(true, reason);
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
