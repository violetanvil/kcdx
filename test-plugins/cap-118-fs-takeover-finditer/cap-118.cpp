// CAP-118 — the file-system-takeover FindFirst/FindNext/FindClose iterator
// triplet (slots 63/64/65) enumerates kcdx's UNIFIED set over a directory prefix,
// with the engine's find-data buffer ABI filled correctly.
//
// The regression proof for the stateful directory-iterator the table-DB
// override-glob (`Libs/Tables/<base>__*.<ext>`) and the general by-name dir
// listing dispatch through (file-system-takeover design §5.1; KI-0027 — with
// 63/64/65 THUNK, kcdx served no pak-resident entries for those globs and the
// table-database load fatalled, err_id=259). The triplet's load-bearing logic is
// the UNIFIED-ENUMERATION CORE — the union of the engine's on-disk entries and
// the index's pak-resident vpaths under the prefix, loose-skip de-duped — plus
// the find-data buffer fill (the design §8 P5 ABI: attr byte @0x00 bit 0x10 =
// dir, inline base name @0x24). Both are PURE functions, so this DLL tests them
// HEADLESS: it compiles the engine's own find_slots.cpp into itself (the cap-114
// shape — a self-contained artifact built against include/ + the engine source
// under test, with standalone stubs for find_slots.cpp's external deps), injects
// a synthetic disk-walk result + a hand-built AssetIndex, and asserts the unified
// entry set + the find-data layout.
//
// === WHY this is the headless seam (and what is NOT here) ===================
//
// The slot FUNCTIONS themselves (kcdx_FindFirst/FindNext/FindClose) take a
// `void* self` member-call arg, run a LIVE _wfindfirst64 disk walk over a
// slot-1-resolved disk pattern, read the PROCESS-LIFETIME GetBuiltIndex() set the
// live seat fills, and mint into the live handle pool. Those paths are reachable
// only against a live swapped CCryPak object in-game — NOT unit-testable
// standalone (no engine object, no live disk dir, no seated index). What IS
// headless-testable, and what this DLL proves, is the union/de-dup core the disk
// walk + index feed into, and the find-data fill the engine consumer reads:
//   - BuildUnifiedFindEntries(diskNames, diskIsDir, index, normPrefix) — the disk
//     UNION index-pak-vpaths, single-level, loose-skip de-duped.
//   - FillFindData(buf, cap, entry) — the §8 P5 buffer layout.
// The full live slot dispatch (the _wfindfirst64 walk, the find-handle pool mint,
// the per-call buffer fill against a live object) is declared LIVE-LAUNCH-ONLY
// (the matrix row + the step deliverable) — this row does NOT claim it ran. The
// LIVE gate is the boot reaching the world with no err_id=259 (the table-DB load
// succeeding because the glob now sees the pak-resident `__*` overrides).
//
// === The falsifiable assertions (each states what makes it FAIL) ============
//
//   (a) UNIFIED UNION + de-dup — a prefix "ui/" with a disk entry "a.dds" and
//       index PAK vpaths "ui/a.dds" (a loose override already on disk),
//       "ui/b.dds", "ui/sub/c.dds", "other/d.dds" yields exactly {a.dds (disk),
//       b.dds (index pak)} — NOT a SECOND "a.dds" (the loose-skip de-dup), NOT
//       sub/c.dds (a deeper level), NOT other/d.dds (a sibling). FAILS if the set
//       count differs, the de-dup re-emits a.dds, or a wrong vpath is included.
//   (b) INDEX-ONLY pak delta is surfaced — "b.dds" (a pak entry the disk walk
//       canNOT see — the override-glob's whole purpose) is present in the set.
//       FAILS if the pak-resident entry the engine disk walk misses is absent
//       (the KI-0027 failure: the table glob saw no pak entries).
//   (c) FIND-DATA fill, FILE entry — FillFindData for a file clears bit 0x10 at
//       offset 0x00 and writes the base name NUL-terminated at offset 0x24.
//       FAILS if 0x10 is set on a file, or the name lands at the wrong offset /
//       is not NUL-terminated (the consumer's strlen/strcmp/strstr at &buf[0x24]
//       would mis-read every entry — the §8 P5 ABI).
//   (d) FIND-DATA fill, DIRECTORY entry — FillFindData for a directory SETS bit
//       0x10 at offset 0x00 (the consumer SKIPS it: `(buf[0] & 0x10) != 0`).
//       FAILS if a directory entry is emitted with 0x10 clear (the consumer would
//       treat a subdirectory as a matchable file).
//   (e) NAME BOUNDING — a base name longer than the caller's name cap is
//       truncated and NUL-terminated within the buffer (never an overrun). FAILS
//       if the copy exceeds the cap or omits the terminator.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "kcdx/Interfaces.h"

#include "find_slots.h"    // BuildUnifiedFindEntries / FillFindData / FindEntry (under test)
#include "asset_index.h"   // ByteSource / AssetIndex (the index the union reads)
#include "asset_overlay.h" // NormalizeVPath (the shared key fold the prefix + index use)

namespace {

const char* kName = "cap_118_fs_takeover_finditer";
const char* kRow  = "cap-118-fs-takeover-finditer";

const kcdxInterface* g_api  = nullptr;
kcdxPluginHandle     g_self = kcdxInvalidPluginHandle;
kcdxLogger           g_log;

using kcdx::fs_takeover::AssetIndex;
using kcdx::fs_takeover::ByteSource;
using kcdx::fs_takeover::BuildUnifiedFindEntries;
using kcdx::fs_takeover::FillFindData;
using kcdx::fs_takeover::FindEntry;
using kcdx::fs_takeover::kFindDataAttrOffset;
using kcdx::fs_takeover::kFindDataNameOffset;
using kcdx::fs_takeover::kFindDataDirBit;

// The SAME key fold the index uses (asset_index keys by NormalizeVPath; the
// prefix is NormalizeVPath'd too). Insert under the normalized key so the test
// exercises the real key path. NormalizeVPath is the REAL fold (the stub TU).
std::string Key(const char* vpath) {
    return kcdx::asset_overlay::NormalizeVPath(vpath);
}

void InsertPak(AssetIndex& idx, const char* vpath) {
    ByteSource s;
    s.kind    = ByteSource::Kind::Pak;
    s.pakFile = L"Data/test.pak";
    s.size    = 100;
    s.method  = 0;
    idx[Key(vpath)] = std::move(s);
}

void Report(bool pass, const char* reason) {
    if (pass) g_log.Info ("CAP118", "PASS %s: %s", kRow, reason);
    else      g_log.Error("CAP118", "FAIL %s: %s", kRow, reason);
    g_api->ReportTestResult(g_self, kRow, pass ? 1 : 0, reason);
}

// Is `name` present in the unified entry set?
bool HasEntry(const std::vector<FindEntry>& set, const char* name) {
    for (const auto& e : set) if (e.name == name) return true;
    return false;
}

// How many entries in the set carry `name` (for the de-dup assertion — a loose
// override must appear EXACTLY once, from the disk arm, never re-emitted by the
// index arm).
size_t CountEntry(const std::vector<FindEntry>& set, const char* name) {
    size_t n = 0;
    for (const auto& e : set) if (e.name == name) ++n;
    return n;
}

}  // namespace

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    g_api  = api;
    g_self = api->GetPluginHandle(kName);
    g_log  = kcdxLogger(api, g_self);
    g_log.Info("INIT", "kcdxPlugin_Load called (engine v0x%08X)", api->kcdxVersion);

    char reason[800];

    // --- Build the synthetic disk-walk result + a hand-built index. ----------
    // The disk walk surfaced "a.dds" (a loose override that is a real disk file)
    // under the "ui/" prefix. The index carries pak vpaths: "ui/a.dds" (the SAME
    // loose override, also recorded — the de-dup must drop it from the index
    // arm), "ui/b.dds" (a pak-only entry the disk walk CANNOT see — the glob's
    // whole point), "ui/sub/c.dds" (a deeper level), "other/d.dds" (a sibling).
    std::vector<std::string> diskNames = { "a.dds" };
    std::vector<bool>        diskIsDir = { false };

    AssetIndex idx;
    InsertPak(idx, "ui/a.dds");      // same as the disk override → de-dup drops it
    InsertPak(idx, "ui/b.dds");      // pak-only → the index-delta the glob needs
    InsertPak(idx, "ui/sub/c.dds");  // deeper level → excluded (single-level only)
    InsertPak(idx, "other/d.dds");   // sibling prefix → excluded

    const std::string prefix = Key("ui/");
    std::vector<FindEntry> set =
        BuildUnifiedFindEntries(diskNames, diskIsDir, idx, prefix);

    // (a) UNION + de-dup — exactly {a.dds, b.dds}; no second a.dds, no sub/c.dds,
    //     no other/d.dds.
    const bool aOnce   = CountEntry(set, "a.dds") == 1;
    const bool hasB    = HasEntry(set, "b.dds");
    const bool noDeep  = !HasEntry(set, "c.dds") && !HasEntry(set, "sub/c.dds");
    const bool noSib   = !HasEntry(set, "d.dds");
    if (set.size() != 2 || !aOnce || !hasB || !noDeep || !noSib) {
        std::snprintf(reason, sizeof(reason),
            "(a) unified set wrong — size=%zu (expected 2), a.dds count=%zu "
            "(expected 1, the loose-skip de-dup), has b.dds=%d (expected 1), "
            "no deeper=%d, no sibling=%d. The union of the disk walk + the index "
            "pak-vpaths under 'ui/' must be exactly {a.dds (disk), b.dds (index "
            "pak)} — a wrong count means the union/de-dup/single-level select is "
            "broken (the table-glob would enumerate the wrong entries)",
            set.size(), CountEntry(set, "a.dds"), (int)hasB, (int)noDeep,
            (int)noSib);
        Report(false, reason);
        return true;
    }

    // (b) INDEX-ONLY pak delta surfaced — b.dds (the pak entry the disk walk
    //     cannot see) IS present. This is the KI-0027 fix: the override-glob now
    //     sees the pak-resident entries.
    if (!HasEntry(set, "b.dds")) {
        std::snprintf(reason, sizeof(reason),
            "(b) the index-only pak vpath 'ui/b.dds' is ABSENT from the unified "
            "set — the pak-resident entry the engine disk walk cannot see was not "
            "surfaced. This is the KI-0027 failure mode: the table-DB override-"
            "glob sees no pak entries and the table-database load fatals "
            "(err_id=259)");
        Report(false, reason);
        return true;
    }

    // (c) FIND-DATA fill, FILE — bit 0x10 CLEAR at 0x00, base name @0x24,
    //     NUL-terminated.
    uint8_t buf[64];
    std::memset(buf, 0xFF, sizeof(buf));  // pre-poison so a missed write shows.
    if (!FillFindData(buf, /*nameCap=*/sizeof(buf) - kFindDataNameOffset,
                      FindEntry{ "b.dds", /*isDir=*/false })) {
        Report(false, "(c) FillFindData returned false for a valid file entry");
        return true;
    }
    const bool fileDirBitClear =
        (buf[kFindDataAttrOffset] & kFindDataDirBit) == 0;
    const char* fileName = reinterpret_cast<const char*>(buf + kFindDataNameOffset);
    const bool fileNameOk = std::strcmp(fileName, "b.dds") == 0;
    if (!fileDirBitClear || !fileNameOk) {
        std::snprintf(reason, sizeof(reason),
            "(c) find-data fill for a FILE wrong — attr@0x00=0x%02X (bit 0x10 "
            "must be CLEAR for a file; the consumer skips an entry when set), "
            "name@0x24='%s' (expected 'b.dds', NUL-terminated). A wrong attr bit "
            "or name offset means the engine consumer's (buf[0]&0x10) skip and its "
            "strlen/strcmp/strstr at &buf[0x24] mis-read every entry (§8 P5 ABI)",
            buf[kFindDataAttrOffset], fileName);
        Report(false, reason);
        return true;
    }

    // (d) FIND-DATA fill, DIRECTORY — bit 0x10 SET at 0x00.
    std::memset(buf, 0xFF, sizeof(buf));
    FillFindData(buf, sizeof(buf) - kFindDataNameOffset,
                 FindEntry{ "subdir", /*isDir=*/true });
    if ((buf[kFindDataAttrOffset] & kFindDataDirBit) == 0) {
        std::snprintf(reason, sizeof(reason),
            "(d) find-data fill for a DIRECTORY did NOT set bit 0x10 at offset "
            "0x00 (attr=0x%02X) — the engine consumer would treat the "
            "subdirectory as a matchable file (it skips an entry only when "
            "(buf[0] & 0x10) != 0)",
            buf[kFindDataAttrOffset]);
        Report(false, reason);
        return true;
    }

    // (e) NAME BOUNDING — a long name truncates within the cap, NUL-terminated.
    std::memset(buf, 0xFF, sizeof(buf));
    const size_t cap = 8;  // tiny name cap: 7 name bytes + a NUL.
    FillFindData(buf, cap, FindEntry{ "abcdefghijklmnop", /*isDir=*/false });
    const char* trunc = reinterpret_cast<const char*>(buf + kFindDataNameOffset);
    const bool boundedOk = std::strlen(trunc) == cap - 1 &&
                           std::strncmp(trunc, "abcdefg", cap - 1) == 0 &&
                           trunc[cap - 1] == '\0';
    if (!boundedOk) {
        std::snprintf(reason, sizeof(reason),
            "(e) a name longer than the cap was not bounded — got '%s' (len=%zu); "
            "expected 'abcdefg' (cap-1=%zu bytes) NUL-terminated within the "
            "buffer. An unbounded copy overruns the caller's find-data name region",
            trunc, std::strlen(trunc), cap - 1);
        Report(false, reason);
        return true;
    }

    std::snprintf(reason, sizeof(reason),
        "kcdx FindFirst/FindNext/FindClose unified-enumeration core PASS — "
        "(a) the union of the disk walk + the index pak-vpaths under 'ui/' is "
        "exactly {a.dds (disk), b.dds (index pak)}: the loose override 'a.dds' "
        "appears once (loose-skip de-dup), the deeper 'ui/sub/c.dds' and sibling "
        "'other/d.dds' excluded (single-level); (b) the pak-only 'b.dds' the disk "
        "walk cannot see IS surfaced (the KI-0027 fix); (c) a file's find-data "
        "clears attr bit 0x10 @0x00 and writes the base name NUL-terminated @0x24; "
        "(d) a directory SETS bit 0x10; (e) a long name is bounded + terminated "
        "within the caller buffer. Proves the unified-enumeration + find-data ABI "
        "the slots 63/64/65 rest on. LIVE-LAUNCH-ONLY (NOT asserted here): the "
        "_wfindfirst64 disk walk, the find-handle pool mint, the per-call buffer "
        "fill against the live swapped CCryPak object — the live gate is the boot "
        "reaching the world with no table-DB err_id=259");
    Report(true, reason);
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
