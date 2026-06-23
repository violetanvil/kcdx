#include "find_slots.h"

#include <windows.h>  // MultiByteToWideChar / WideCharToMultiByte (path widen/narrow)

#include <atomic>
#include <cstring>
#include <io.h>       // _wfindfirst64 / _wfindnext64 / _wfinddata64_t / _findclose
#include <unordered_set>

#include "asset_index.h"       // GetBuiltIndex + FoldEngineAliasToIndexKey (alias fold)
#include "boot_trace.h"        // FS_BOOT_TRACE — boot-window slot trace (KI-0026)
#include "enum_diff_probe.h"   // === DIAGNOSTIC (PROBE Y) — ReplayAndDiffFind (enumeration vanilla-differential)
#include "file_handle.h"       // the kcdx handle pool — find-handle mint/peek/advance/close
#include "open_slots.h"        // kcdx_AdjustFileName (slot-1 resolution)
#include "../asset_overlay.h"  // NormalizeVPath (the shared index key fold)
#include "../log.h"

// The kcdx DIRECTORY-ITERATOR slot impls (slots 63/64/65) — see find_slots.h for
// the slot set, the BODY-VERIFIED ABI, the find-data buffer layout (design §8 P5),
// and the unified-enumeration model. This file mirrors enum_slots.cpp's slot-14
// union in STATEFUL handle form: the disk walk UNION the index's pak vpaths,
// loose-skip de-duped, the iteration spread across FindFirst (seed + first entry)
// / FindNext (advance) / FindClose (release), the state held in a kcdx find-handle.

namespace kcdx::fs_takeover {

namespace {

constexpr const char* kCat = "FS_FIND";

// The engine's universal path cap (CryEngine ICryPak g_nMaxPath) — open_slots /
// enum_slots use the same. A real path is well under 2048.
constexpr size_t kMaxPath = 2048;

// One-shot first-find log latch (same hot-path discipline as enum_slots).
std::atomic<bool> g_loggedFirstFind{false};

// Widen a UTF-8/ASCII path for the wide CRT find APIs. False (caller fails loud)
// on an over-cap path. Mirrors enum_slots.cpp / open_slots.cpp WidenPath.
bool WidenPath(const char* path, wchar_t* wpath, size_t wcap) {
    return MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath,
                               static_cast<int>(wcap)) > 0;
}

// The byte length of the directory prefix of `pattern` (through the last '/' or
// '\', inclusive); 0 if the pattern has no separator. Mirrors enum_slots.cpp
// DirPrefixLen. NO allocation.
size_t DirPrefixLen(const char* pattern) {
    size_t last = 0;
    for (size_t i = 0; pattern[i] != '\0'; ++i)
        if (pattern[i] == '/' || pattern[i] == '\\') last = i + 1;
    return last;
}

// The normalized DIRECTORY prefix of the requested vpath pattern, for matching
// index vpaths (the directory part before the glob, NormalizeVPath'd so it
// compares against the index's normalized keys). Mirrors enum_slots.cpp
// IndexDirPrefix. Cold enumeration path — the std::string is acceptable here.
//
// The engine's pak aliases are folded here too (FoldEngineAliasToIndexKey), the
// SAME fold ResolveVPath applies on the open path — so an enumeration of an
// aliased dir (`FindFirst "data/gameshaders/*.ext"`) matches the index keys
// stored under the pak root (`shaders/`), exactly as opening one shader by name
// does. Without this the enum form missed the 21 shaders the `shaders/*.ext`
// form found (KI-0028); kcdx owns the alias on EVERY path, not just open.
std::string IndexDirPrefix(const char* pattern) {
    const size_t dlen = DirPrefixLen(pattern);
    std::string prefix = asset_overlay::NormalizeVPath(std::string(pattern, dlen));
    FoldEngineAliasToIndexKey(prefix);
    return prefix;
}

// The normalized FILENAME GLOB MASK of the requested vpath pattern — the
// basename past the last separator ("gender__*.xml" from
// "Libs/Tables/rpg/gender__*.xml"; "*.cfg" from "Config/CVarGroups/*.cfg"),
// NormalizeVPath'd the same way base names are folded for the de-dup so the
// match is case-insensitive consistent with the rest. A pattern with no filename
// part yields an empty mask (matches everything — see WildcardMatch). Cold
// enumeration path — the std::string is acceptable here.
std::string IndexNameMask(const char* pattern) {
    const size_t dlen = DirPrefixLen(pattern);
    return asset_overlay::NormalizeVPath(std::string(pattern + dlen));
}

// SOURCE: mirrors the disk arm's _wfindfirst64 filename-glob filtering — both
// arms honor the SAME mask (the symmetry KI-0027's index arm broke by matching
// the directory prefix only). A two-pointer glob matcher over the engine's mask
// shapes ("<base>__*.<ext>" and "*.<ext>"): '*' matches any run (incl. empty),
// '?' matches exactly one char; no character classes (the engine's masks use
// none). Both sides are already NormalizeVPath'd (lowercase), so the compare is
// the engine's case-insensitive resolution. An EMPTY mask matches everything (a
// directory pattern carried no filename glob → no filename filter, the
// pre-KI-0027 directory-only behavior for those patterns).
bool WildcardMatch(const std::string& name, const std::string& mask) {
    if (mask.empty()) return true;  // no filename glob → match all (a '*').
    size_t n = 0, m = 0;            // cursors into name / mask
    size_t star = std::string::npos;  // last '*' position in mask, or npos
    size_t starN = 0;                 // name cursor when that '*' was taken
    while (n < name.size()) {
        if (m < mask.size() && (mask[m] == '?' || mask[m] == name[n])) {
            ++m; ++n;                          // literal or '?' consumes one
        } else if (m < mask.size() && mask[m] == '*') {
            star = m++; starN = n;             // remember '*', consume zero so far
        } else if (star != std::string::npos) {
            m = star + 1; n = ++starN;         // backtrack: '*' eats one more char
        } else {
            return false;                      // mismatch, no '*' to fall back on
        }
    }
    while (m < mask.size() && mask[m] == '*') ++m;  // trailing '*'(s) match empty
    return m == mask.size();
}

// Run the engine on-disk walk for `resolvedPattern` (the slot-1-resolved disk
// pattern "<dir>/<glob>") on kcdx's OWN CRT (_wfindfirst64/_wfindnext64), pushing
// each entry's BASE NAME + its directory flag into the parallel out-vectors. The
// `.`/`..` pseudo-entries the engine consumer skips at the name offset are
// dropped here so they never enter the unified set. Mirrors the (1) disk-walk arm
// of enum_slots.cpp kcdx_ForEachFile. Logs + skips an unconvertible name; logs a
// widen failure (the enumeration then rests on the index arm alone).
void DiskWalk(const char* resolvedPattern,
              std::vector<std::string>& outNames,
              std::vector<bool>& outIsDir) {
    if (!resolvedPattern || resolvedPattern[0] == '\0') return;
    wchar_t wpattern[kMaxPath];
    if (!WidenPath(resolvedPattern, wpattern, kMaxPath)) {
        LOG_WARN_KV(kCat, "find_widen_failed",
            kcdx::log::KV("pattern", std::string(resolvedPattern)));
        return;
    }
    _wfinddata64_t fd;
    const intptr_t h = _wfindfirst64(wpattern, &fd);
    if (h == -1) return;  // no on-disk match — the index arm may still have some.
    do {
        char nameUtf8[kMaxPath];
        const int nn = WideCharToMultiByte(CP_UTF8, 0, fd.name, -1, nameUtf8,
                                           kMaxPath, nullptr, nullptr);
        if (nn <= 0) continue;  // unconvertible name — skip.
        // Drop the '.' and '..' pseudo-entries the engine consumer skips anyway
        // (it reads buf[0x24]/[0x25] for them) so they never enter the set.
        if (nameUtf8[0] == '.' &&
            (nameUtf8[1] == '\0' || (nameUtf8[1] == '.' && nameUtf8[2] == '\0')))
            continue;
        outNames.emplace_back(nameUtf8);
        outIsDir.push_back((fd.attrib & _A_SUBDIR) != 0);
    } while (_wfindnext64(h, &fd) == 0);
    _findclose(h);
}

}  // namespace

// === The pure unified-enumeration core (headless-testable) ===================

std::vector<FindEntry> BuildUnifiedFindEntries(
    const std::vector<std::string>& diskNames,
    const std::vector<bool>& diskIsDir,
    const AssetIndex& index,
    const std::string& normPrefix,
    const std::string& nameMask) {
    std::vector<FindEntry> entries;
    entries.reserve(diskNames.size());

    // (1) The engine's on-disk entries (loose overrides are real disk files this
    //     set already carries). Build a seen-set of base names for the de-dup.
    std::unordered_set<std::string> seen;
    seen.reserve(diskNames.size() * 2);
    for (size_t i = 0; i < diskNames.size(); ++i) {
        const bool dir = (i < diskIsDir.size()) ? diskIsDir[i] : false;
        entries.push_back(FindEntry{ diskNames[i], dir });
        // Seen by the NORMALIZED base name so the de-dup matches the index's
        // normalized vpath keys (a case-different disk name still de-dups a
        // case-different index vpath — the engine's resolution is case-insensitive).
        seen.insert(asset_overlay::NormalizeVPath(diskNames[i]));
    }

    // (2) The index's PAK-resident vpaths directly under `normPrefix` (single
    //     directory level — no deeper subdir), as FILE entries, SKIPPING any base
    //     name a disk entry already surfaced (the loose-skip de-dup: a loose
    //     vpath is covered by the disk walk; only Pak sources are index-only).
    //     The two sets are disjoint by construction — a loose vpath is a real
    //     disk file the (1) walk saw; a pak vpath the disk walk cannot see. This
    //     is the SAME predicate enum_slots.cpp applies, plus the base-name
    //     extraction (FindData carries the base name, not the full vpath).
    // === DIAGNOSTIC (PROBE Q) — KI-0028 synthetic directory entries ===========
    // VERIFIED mechanism: the engine enumerates a shader dir (`Shaders/HWScripts/
    // *.*`) expecting the immediate-child SUBDIR (`CryFX`), but the index-walk arm
    // below historically SKIPPED any deeper-subdir vpath (line "single-level only")
    // and emitted ONLY file entries — never a directory entry. The 180 source
    // shaders live one level deeper (`shaders/hwscripts/cryfx/*.cfx`), so a
    // single-level FindFirst returned 0, the engine never discovered the source
    // tree, shader-system init stalled, only 1 PSO was built (PROBE P), every frame
    // black. The engine's own _findfirst64 returns subdir entries (a dir walk
    // yields files AND subdirs), which is why vanilla (swap-off) works.
    //
    // PROBE Q tests ONE variable: does emitting a synthetic DIRECTORY entry for
    // each distinct immediate-child subdir cause the engine to RECURSE into it?
    //   Outcome A: the swap-on log shows a follow-up FindFirst into the emitted
    //     subdir (e.g. `shaders/hwscripts/cryfx/*.*`) → the engine recurses on a
    //     dir entry → the synthetic-dir-entry fix is CORRECT (promote to the fix).
    //   Outcome B: no deeper FindFirst follows → the engine wants a different
    //     enumeration contract (recursive `**`, or a manifest) → a different fix.
    // Falsifiable either way; observes ground truth (the engine's next call), not a
    // theory. NO-RESIDUE: on retire, promote-to-fix or remove (working-artifacts).
    std::unordered_set<std::string> emittedSubdirs;  // de-dup synthetic dir entries
    long long synthDirs = 0;  // PROBE Q telemetry — distinct subdirs emitted

    for (const auto& kv : index) {
        const std::string& vpath = kv.first;
        const ByteSource& src = kv.second;
        if (src.kind != ByteSource::Kind::Pak) continue;       // loose → in (1).
        if (vpath.size() <= normPrefix.size()) continue;
        if (vpath.compare(0, normPrefix.size(), normPrefix) != 0) continue;
        const size_t sep = vpath.find('/', normPrefix.size());
        if (sep != std::string::npos) {
            // A deeper subdir. PROBE Q: emit the IMMEDIATE child subdir name as a
            // synthetic DIRECTORY entry (deduped), instead of silently skipping —
            // so a single-level FindFirst surfaces the subdir the engine recurses
            // into. The subdir name = the path segment between normPrefix and the
            // next '/'. The mask filter does NOT apply to a directory (the engine's
            // own dir walk returns subdirs regardless of a "*.<ext>" file glob).
            std::string subdir = vpath.substr(normPrefix.size(),
                                              sep - normPrefix.size());
            if (subdir.empty()) continue;
            const std::string subKey = asset_overlay::NormalizeVPath(subdir);
            if (seen.count(subKey)) continue;            // a disk subdir already has it
            if (!emittedSubdirs.insert(subKey).second) continue;  // already emitted
            entries.push_back(FindEntry{ std::move(subdir), /*isDir=*/true });
            ++synthDirs;
            continue;
        }
        // The base name = the vpath past the prefix (single level → no further
        // separator, already guaranteed above).
        std::string baseName = vpath.substr(normPrefix.size());
        // Apply the filename glob mask — the SAME glob the disk arm's
        // _wfindfirst64 applies to its entries. Without this the index arm
        // returned EVERY pak vpath under the directory, ignoring the
        // "<base>__*.<ext>" mask (KI-0027: a "gender__*.xml" glob matched all 528
        // tables in the dir, not the ~0 actual __* overrides). baseName is already
        // normalized (a vpath key), so it compares against the normalized mask.
        if (!WildcardMatch(baseName, nameMask)) continue;
        if (seen.count(asset_overlay::NormalizeVPath(baseName))) continue;  // de-dup.
        entries.push_back(FindEntry{ std::move(baseName), /*isDir=*/false });
    }

    if (synthDirs > 0) {
        LOG_DEBUG_KV(kCat, "probe_q_synth_dirs",
            kcdx::log::KV("prefix", normPrefix),
            kcdx::log::KV("mask", nameMask),
            kcdx::log::KV("synth_dirs", synthDirs),
            kcdx::log::KV::BareStr("detail",
                "PROBE Q: emitted synthetic directory entries for the immediate "
                "child subdirs under this prefix. If the engine RECURSES, a "
                "follow-up FindFirst into one of these subdirs appears next in the "
                "trace — the synthetic-dir-entry fix is correct."));
    }
    // === END PROBE Q ===

    return entries;
}

bool FillFindData(void* findData, size_t nameCap, const FindEntry& entry) {
    if (!findData) return false;
    uint8_t* buf = static_cast<uint8_t*>(findData);
    // Zero the 36-byte header (bytes 0x00..0x23 — the attr byte + the engine
    // header's reserved/size/time region the table-glob consumers do NOT read).
    std::memset(buf, 0, kFindDataNameOffset);
    // Attribute byte @0x00 — set 0x10 for a directory, cleared (already 0 from
    // the memset) for a file.
    if (entry.isDir) buf[kFindDataAttrOffset] |= kFindDataDirBit;
    // Entry base name @0x24, NUL-terminated, bounded to the caller's name region.
    char* nameDst = reinterpret_cast<char*>(buf + kFindDataNameOffset);
    if (nameCap == 0) { return true; }  // no room even for a NUL — header-only.
    const size_t copy = entry.name.size() < (nameCap - 1)
                            ? entry.name.size() : (nameCap - 1);
    std::memcpy(nameDst, entry.name.data(), copy);
    nameDst[copy] = '\0';
    return true;
}

// === slot 63 — FindFirst ====================================================
intptr_t kcdx_FindFirst(void* self, const char* pattern, void* findData,
                        int /*flags*/) {
    if (!self || !pattern || !findData) {
        // The engine null-arg contract → no match (-1). A null findData has no
        // buffer to fill; a null pattern has nothing to resolve. Fail to the
        // no-match arm the consumer loops on, not a crash (AP14: a defined
        // no-match, logged).
        LOG_ERROR_KV(kCat, "findfirst_null_arg",
            kcdx::log::KV::BareStr("pattern", pattern ? pattern : "<null>"),
            kcdx::log::KV::BareStr("detail",
                "FindFirst received a null self/pattern/findData — returning the "
                "no-match contract (-1). No find-handle minted."));
        return -1;
    }

    // (1) Resolve the pattern's disk form via slot 1 (kcdx_AdjustFileName), then
    //     run the engine on-disk walk on kcdx's CRT (same as enum_slots' (1)).
    char diskPattern[kMaxPath];
    diskPattern[0] = '\0';
    void* r = kcdx_AdjustFileName(self, pattern, diskPattern, /*nFlags=*/0);
    const char* resolvedPattern =
        r ? static_cast<const char*>(r) : diskPattern;

    std::vector<std::string> diskNames;
    std::vector<bool> diskIsDir;
    DiskWalk(resolvedPattern, diskNames, diskIsDir);

    // (2) Build the unified set (disk UNION index pak-vpaths under the prefix
    //     that MATCH THE FILENAME GLOB, loose-skip de-duped) via the pure core.
    //     The disk arm already honored the glob (_wfindfirst64 took the full
    //     pattern); the index arm honors it via nameMask — both arms, the SAME
    //     glob (KI-0027).
    const std::string normPrefix = IndexDirPrefix(pattern);
    const std::string nameMask = IndexNameMask(pattern);
    const AssetIndex& index = GetBuiltIndex();
    std::vector<FindEntry> entries =
        BuildUnifiedFindEntries(diskNames, diskIsDir, index, normPrefix, nameMask);

    if (entries.empty()) {
        // No unified-set entry — the engine's no-match contract (-1). The
        // consumer's `if (-1 < handle)` then skips its whole do/while loop.
        TraceEnum("FindFirst", pattern, 0);
        return -1;
    }

    // Mint a kcdx find-handle seeded with the set (parallel name + dir-flag
    // vectors, moved into the pool). The find-handle is the SAME odd-tagged pool
    // handle the read family uses; the engine holds it opaquely and passes it back.
    std::vector<std::string> names;
    std::vector<uint8_t> isDir;
    names.reserve(entries.size());
    isDir.reserve(entries.size());
    for (auto& e : entries) {
        names.push_back(std::move(e.name));
        isDir.push_back(e.isDir ? 1 : 0);
    }
    // FS-op trace contract: log the returned ENTRY NAMES (capped), not just the
    // count — the KI-0027-class gap (a 528-over-match looked identical to a right
    // walk by count alone). Traced from `names` BEFORE the move into the pool;
    // gated (boot window only), so the sample-string build is a cold-path cost.
    TraceEnumNames("FindFirst", pattern, names);
    // === DIAGNOSTIC (PROBE Y) — enumeration vanilla-differential. kcdx built
    // `names`/`isDir` (its synthesized unified set) for `pattern`; replay the
    // SAME pattern through the captured engine ORIGINAL FindFirst/Next/Close and
    // log ENUM_DIFF iff the entry SETS differ. Read-only; boot-window gated;
    // called BEFORE the move into MintFind (the vectors are consumed below).
    // NO-RESIDUE: remove with PROBE Y. ===
    ReplayAndDiffFind(self, pattern, names, isDir);
    const KcdxHandle h = MintFind(std::move(names), std::move(isDir));
    if (h == 0) {
        // Mint failed (logged loud by the pool) — fail to the no-match contract
        // rather than a half-open iterator.
        return -1;
    }

    // Fill the caller's find-data with the FIRST entry, then advance the cursor
    // so FindNext emits the second. The name region is the caller's buffer past
    // the 36-byte header; bound the name copy to kFindDataNameCap (a conservative
    // MAX_PATH bound — see find_slots.h SOURCE note; the region's exact capacity
    // past +0x24 is unverified, so a MAX_PATH-class cap cannot overrun it).
    std::string firstName;
    bool firstIsDir = false;
    if (!FindPeek(h, &firstName, &firstIsDir)) {
        // Should not happen (entries non-empty), but fail loud + release.
        LOG_ERROR_KV(kCat, "findfirst_peek_empty",
            kcdx::log::KV::BareStr("pattern", pattern),
            kcdx::log::KV::BareStr("detail",
                "FindFirst seeded a non-empty set but FindPeek returned empty — "
                "releasing the handle and returning no-match."));
        Close(h);
        return -1;
    }
    FillFindData(findData, kFindDataNameCap, FindEntry{ firstName, firstIsDir });
    FindAdvance(h);

    if (!g_loggedFirstFind.exchange(true, std::memory_order_relaxed)) {
        LOG_DEBUG_KV(kCat, "kcdx_find_first",
            kcdx::log::KV("pattern", std::string(pattern)),
            kcdx::log::KV("entries", static_cast<uint64_t>(entries.size())),
            kcdx::log::KV::BareStr("detail",
                "kcdx FindFirst minted a find-handle over the unified set — the "
                "engine's on-disk entries (kcdx CRT _wfindfirst64 walk) PLUS the "
                "index's pak-resident vpaths under the prefix (loose-skip "
                "de-duped). The engine holds the kcdx find-handle and passes it "
                "back to kcdx's FindNext/FindClose; no engine CCryPakFindData, no "
                "engine-CRT iterator state in kcdx's path (§5.1)."));
    }
    // (FindFirst entry-set already traced via TraceEnumNames above, before the move.)

    // The find-handle is returned as the engine's iterator handle. It is the
    // odd-tagged kcdx handle id (a small positive value ≥ 3, well above -1), so
    // the consumer's `-1 < handle` test passes and it begins the FindNext loop.
    return static_cast<intptr_t>(h);
}

// === slot 64 — FindNext =====================================================
intptr_t kcdx_FindNext(void* self, intptr_t handle, void* findData) {
    if (!findData || handle < 0) {
        // A null buffer or a no-match handle (the consumer should not call
        // FindNext on -1, but defend) → exhausted (-1).
        return -1;
    }
    const KcdxHandle h = static_cast<KcdxHandle>(handle);

    std::string name;
    bool isDir = false;
    if (!FindPeek(h, &name, &isDir)) {
        // Exhausted (or a bad/closed handle — FindPeek logged the latter). The
        // consumer's `while (-1 < iVar3)` stops. Return -1 either way; a bad
        // handle stopping the loop is the safe degradation (the consumer then
        // calls FindClose, which no-ops on the already-bad handle).
        return -1;
    }
    FillFindData(findData, kFindDataNameCap, FindEntry{ name, isDir });
    FindAdvance(h);
    // Continue signal: any value with -1 < ret. 0 is the canonical "more entries"
    // value the consumer loops on (`while (-1 < iVar3)`).
    return 0;
}

// === slot 65 — FindClose ====================================================
int kcdx_FindClose(void* self, intptr_t handle) {
    if (handle < 0) return 0;  // no-match handle — nothing to release.
    const KcdxHandle h = static_cast<KcdxHandle>(handle);
    // Release the find-handle's pool slot. Close() clears the find-cursor state
    // and frees the slot for reuse; a bad/already-closed handle is a logged
    // no-op (returns non-zero), which is the engine's own FindClose tolerance.
    return Close(h);
}

}  // namespace kcdx::fs_takeover
