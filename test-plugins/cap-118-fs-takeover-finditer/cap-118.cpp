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
    // Empty mask = no filename glob → match everything (a directory-only pattern,
    // the pre-KI-0027 behavior for those patterns). Assertions (a)/(b) verify the
    // union/de-dup/single-level select holds under a match-all mask; (f) below
    // verifies the RESTRICTIVE "<base>__*" mask the KI-0027 bug was blind to.
    std::vector<FindEntry> set =
        BuildUnifiedFindEntries(diskNames, diskIsDir, idx, prefix, /*nameMask=*/"");

    // (a) UNION + de-dup — under an EMPTY (match-all) mask the set is
    //     {a.dds (disk), b.dds (index pak), sub (synthetic DIR for ui/sub/c.dds)}.
    //     The empty mask is a match-all glob (== "*.*"/"*"), so PROBE Q's synthetic
    //     directory entry for the immediate-child subdir 'sub' IS emitted (the
    //     engine's own dir walk returns subdirs for a match-all glob — KI-0028
    //     MaskMatchesDirectories). The deeper FILE 'sub/c.dds' is NOT emitted as a
    //     file (single-level); it is surfaced as the 'sub' DIRECTORY the engine
    //     recurses into. (g) below verifies the mask gate: a specific-ext mask
    //     '*.xml' does NOT emit 'sub'.
    const bool aOnce    = CountEntry(set, "a.dds") == 1;
    const bool hasB     = HasEntry(set, "b.dds");
    const bool hasSubDir = HasEntry(set, "sub");           // synthetic dir (match-all mask)
    const bool noDeepFile = !HasEntry(set, "c.dds") && !HasEntry(set, "sub/c.dds");
    const bool noSib    = !HasEntry(set, "d.dds");
    if (set.size() != 3 || !aOnce || !hasB || !hasSubDir || !noDeepFile || !noSib) {
        std::snprintf(reason, sizeof(reason),
            "(a) unified set wrong — size=%zu (expected 3), a.dds count=%zu "
            "(expected 1, the loose-skip de-dup), has b.dds=%d (expected 1), "
            "has 'sub' synthetic dir=%d (expected 1 — empty mask is match-all, so "
            "the immediate-child subdir IS emitted), no deeper FILE=%d, no "
            "sibling=%d. Under a match-all mask the set is {a.dds (disk), b.dds "
            "(index pak), sub (synthetic dir for ui/sub/c.dds)} — a wrong count "
            "means the union/de-dup/single-level/synthetic-dir select is broken",
            set.size(), CountEntry(set, "a.dds"), (int)hasB, (int)hasSubDir,
            (int)noDeepFile, (int)noSib);
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

    // (f) RESTRICTIVE FILENAME-GLOB MASK — the KI-0027 regression. A
    //     "<base>__*.xml"-style mask over a prefix whose index has BOTH matching
    //     ("gender__patch.xml") AND non-matching ("gender.xml" — no __;
    //     "soul_ability__x.xml" — wrong base; "other.xml") pak entries must
    //     return ONLY the matching subset. cap-118 previously used an effectively-
    //     "*" mask (the "ui/" set above), so it NEVER exercised a restrictive
    //     mask — the index arm matched the DIRECTORY only and returned every entry
    //     under the prefix (live: "gender__*.xml matched=528", the whole dir, not
    //     the ~0 actual __* overrides → the engine merged 528 unrelated tables as
    //     gender overrides → "Database system error"). FAILS if any non-"gender__*"
    //     pak vpath is emitted (the exact KI-0027 over-match), or the matching
    //     entry is dropped.
    {
        AssetIndex midx;
        InsertPak(midx, "libs/tables/rpg/gender__patch.xml");   // MATCHES gender__*.xml
        InsertPak(midx, "libs/tables/rpg/gender.xml");          // no '__' → no match
        InsertPak(midx, "libs/tables/rpg/soul_ability__x.xml"); // wrong base → no match
        InsertPak(midx, "libs/tables/rpg/other.xml");           // unrelated → no match

        const std::string mprefix = Key("libs/tables/rpg/");
        const std::string mmask   = Key("gender__*.xml");
        std::vector<FindEntry> mset =
            BuildUnifiedFindEntries(/*diskNames=*/{}, /*diskIsDir=*/{}, midx,
                                    mprefix, mmask);

        const bool onlyMatch = mset.size() == 1 &&
                               HasEntry(mset, "gender__patch.xml");
        const bool noBare    = !HasEntry(mset, "gender.xml");
        const bool noWrong   = !HasEntry(mset, "soul_ability__x.xml");
        const bool noOther   = !HasEntry(mset, "other.xml");
        if (!onlyMatch || !noBare || !noWrong || !noOther) {
            std::snprintf(reason, sizeof(reason),
                "(f) restrictive mask 'gender__*.xml' wrong — set size=%zu "
                "(expected 1), has gender__patch.xml=%d (expected 1), "
                "no gender.xml=%d, no soul_ability__x.xml=%d, no other.xml=%d. The "
                "index arm must honor the FILENAME glob, not the directory only — "
                "emitting any non-'gender__*' vpath is the KI-0027 over-match (the "
                "glob returned the whole directory; the engine merged unrelated "
                "tables as overrides → table-DB load fatal)",
                mset.size(), (int)HasEntry(mset, "gender__patch.xml"),
                (int)noBare, (int)noWrong, (int)noOther);
            Report(false, reason);
            return true;
        }
    }

    // (g) SYNTHETIC-DIR MASK GATE — the KI-0028 regression. PROBE Q emits a
    //     synthetic DIRECTORY entry for an immediate-child subdir so a single-level
    //     FindFirst surfaces the subdir the engine recurses into. But that emission
    //     must honor the engine's _wfindfirst64 dir-vs-file glob semantics: a
    //     specific-extension mask ("*.xml") returns FILES ONLY (a directory has no
    //     extension, so it does not match), while a match-all mask ("*.*"/"*"/"")
    //     returns subdirs too. The bug: PROBE Q emitted the subdir IGNORING the
    //     mask, so FindFirst("prefabs/*.xml") returned the 66 'prefabs/<subdir>'
    //     directory entries (vanilla returns only the 3 real .xml files) — bogus
    //     dir entries on the content/geometry enum path. FAILS if a "*.xml" mask
    //     emits the subdir, OR a "*.*" mask drops it.
    {
        AssetIndex didx;
        InsertPak(didx, "prefabs/chest.xml");        // a real top-level .xml file
        InsertPak(didx, "prefabs/animal/cow.xml");   // under a subdir → 'animal' is the synthetic dir
        const std::string dprefix = Key("prefabs/");

        // *.xml (specific ext) → FILES ONLY: must return {chest.xml}, NO 'animal' dir.
        std::vector<FindEntry> xmlSet =
            BuildUnifiedFindEntries(/*diskNames=*/{}, /*diskIsDir=*/{}, didx,
                                    dprefix, Key("*.xml"));
        const bool xmlNoDir = !HasEntry(xmlSet, "animal");
        const bool xmlHasFile = HasEntry(xmlSet, "chest.xml");

        // *.* (match-all) → INCLUDES the subdir: must return 'animal' as a dir.
        std::vector<FindEntry> allSet =
            BuildUnifiedFindEntries(/*diskNames=*/{}, /*diskIsDir=*/{}, didx,
                                    dprefix, Key("*.*"));
        const bool allHasDir = HasEntry(allSet, "animal");

        if (!xmlNoDir || !xmlHasFile || !allHasDir) {
            std::snprintf(reason, sizeof(reason),
                "(g) synthetic-dir mask gate wrong — '*.xml' has 'animal' dir=%d "
                "(MUST be 0 — a specific-ext glob excludes directories, like "
                "_wfindfirst64), '*.xml' has chest.xml=%d (expected 1), '*.*' has "
                "'animal' dir=%d (MUST be 1 — a match-all glob includes subdirs). "
                "The bug (KI-0028): PROBE Q emitted the subdir IGNORING the mask, so "
                "FindFirst('prefabs/*.xml') returned bogus directory entries the "
                "engine never asked for on the content-enum path",
                (int)HasEntry(xmlSet, "animal"), (int)xmlHasFile,
                (int)allHasDir);
            Report(false, reason);
            return true;
        }
    }

    std::snprintf(reason, sizeof(reason),
        "kcdx FindFirst/FindNext/FindClose unified-enumeration core PASS — "
        "(a) the union of the disk walk + the index pak-vpaths under 'ui/' (empty "
        "match-all mask) is {a.dds (disk), b.dds (index pak), sub (synthetic dir "
        "for ui/sub/c.dds)}: the loose override 'a.dds' appears once (loose-skip "
        "de-dup), the deeper FILE 'ui/sub/c.dds' surfaces as the 'sub' DIRECTORY "
        "(not a file), the sibling 'other/d.dds' excluded; (b) the pak-only 'b.dds' the disk "
        "walk cannot see IS surfaced (the KI-0027 fix); (c) a file's find-data "
        "clears attr bit 0x10 @0x00 and writes the base name NUL-terminated @0x24; "
        "(d) a directory SETS bit 0x10; (e) a long name is bounded + terminated "
        "within the caller buffer; (f) a restrictive 'gender__*.xml' mask returns "
        "ONLY the matching pak vpath (gender__patch.xml), excluding gender.xml / "
        "soul_ability__x.xml / other.xml — the index arm honors the filename glob, "
        "not the directory only (the KI-0027 mask-blind over-match regression); "
        "(g) a '*.xml' specific-ext mask excludes the synthetic subdir entry while "
        "'*.*' includes it (the KI-0028 PROBE Q mask-bypass fix — _wfindfirst64 "
        "dir-vs-file glob semantics, so a content enum never gets bogus dirs). "
        "Proves the unified-enumeration + find-data ABI "
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
