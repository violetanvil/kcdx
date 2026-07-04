#include "enum_slots.h"

#include <windows.h>  // MultiByteToWideChar (path widen)

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <io.h>       // _wfindfirst64 / _wfindnext64 / _wfinddata64_t

#include "asset_index.h"
#include "boot_trace.h"        // FS_BOOT_TRACE — boot-window slot trace
#include "open_slots.h"        // kcdx_AdjustFileName (slot-1 resolution)
#include "../asset_overlay.h"  // NormalizeVPath (the shared index key fold)
#include "../log.h"

// The kcdx DIRECTORY-ENUMERATION slot impls — see enum_slots.h for the slot set,
// the BODY-VERIFIED ABI, the unified-enumeration model, and why slots 15 / 101
// are NOT flipped here (surfaced decisions).
//
// THE ENUMERATION MODEL (design §1 totalizing invariant + §4.5 + §5): a
// directory walk enumerates the UNION of (a) the engine's on-disk entries for
// the prefix (the original _findfirst64-style walk, on kcdx's CRT) and (b) the
// unified index's vpaths under that prefix (the kcdx-served assets — loose
// overrides + pak entries the engine's disk walk would not see). Each entry
// fires the caller's per-file callback through the object's slot-15 entry (the
// engine original, kept THUNK), so a kcdx-served entry enumerates exactly like a
// vanilla one.

namespace kcdx::fs_takeover {

namespace {

constexpr const char* kCat = "FS_ENUM";

// The engine's universal path cap (CryEngine ICryPak::g_nMaxPath) — open_slots /
// metadata_slots use the same. A real path is well under 2048.
constexpr size_t kMaxPath = 2048;

// The per-entry callback the engine's ForEachFile invokes — the object's vtable
// slot 15 (vtable+0x78), BODY-VERIFIED from the slot-14 decompile:
//   (*slot15)(this, cbCtx, char* fullPath, userData)
// It builds + forwards each matched path to the caller's enumeration callback
// against the engine's intact member offsets (preserved by the vtable-pointer-
// only swap). kcdx invokes it through the OBJECT's current vtable so the call
// dispatches to whatever slot 15 resolves to (the engine original under the
// swap, since slot 15 stays THUNK this step).
using PerFileCallbackFn_t =
    void (*)(void* self, void* cbCtx, const char* fullPath, void* userData);

// One-shot first-enum log latch — enumeration is far rarer than open/read, but
// keep the same hot-path discipline (log the first unified enum, then silent).
std::atomic<bool> g_loggedFirstEnum{false};

// Read the object's slot-15 (per-file callback) entry from its CURRENT vtable.
// Under the swap this is g_kcdxVtable[15], set to the engine original (slot 15
// is THUNK this step). Returns null only if the object/vtable is malformed
// (fail loud at the call site).
PerFileCallbackFn_t ReadSlot15Callback(void* self) {
    if (!self) return nullptr;
    void** vtable = *reinterpret_cast<void***>(self);  // [self+0x00] = vtable ptr
    if (!vtable) return nullptr;
    return reinterpret_cast<PerFileCallbackFn_t>(vtable[15]);  // slot 15 = vtable+0x78
}

// Widen a UTF-8/ASCII path for the wide CRT find APIs. False (caller fails loud)
// on an over-cap path. Mirrors open_slots.cpp / metadata_slots.cpp WidenPath.
bool WidenPath(const char* path, wchar_t* wpath, size_t wcap) {
    return MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath,
                               static_cast<int>(wcap)) > 0;
}

// Split a resolved find-pattern ("<dir>/<glob>" e.g. "C:/game/Data/ui/*.dds")
// into its directory prefix ("<dir>/") for joining with each find result's bare
// filename. Returns the prefix length (through the last separator, inclusive);
// 0 if the pattern has no separator (a bare glob in the cwd). NO allocation.
size_t DirPrefixLen(const char* pattern) {
    size_t last = 0;
    for (size_t i = 0; pattern[i] != '\0'; ++i)
        if (pattern[i] == '/' || pattern[i] == '\\') last = i + 1;
    return last;
}

// The normalized DIRECTORY prefix of the requested vpath pattern, for matching
// index vpaths. The pattern's directory part (before the glob), NormalizeVPath'd
// (lowercase + forward-slash) so it compares against the index's normalized
// keys. Written into `out` (NormalizeVPath returns a std::string; this is the
// COLD enumeration path, not the per-open hot path, so the string is acceptable
// here — enumeration is not the per-frame surface memory.md scopes).
//
// The engine's pak aliases are folded here too (FoldEngineAliasToIndexKey), the
// SAME fold ResolveVPath + find_slots IndexDirPrefix apply — so a ForEachFile
// over an aliased dir (`data/gameshaders/`) matches the index keys stored under
// the pak root (`shaders/`). kcdx owns the alias on every enumeration path, not
// just open.
std::string IndexDirPrefix(const char* pattern) {
    const size_t dlen = DirPrefixLen(pattern);
    std::string prefix = asset_overlay::NormalizeVPath(std::string(pattern, dlen));
    FoldEngineAliasToIndexKey(prefix);
    return prefix;
}

}  // namespace

// === slot 14 — ForEachFile (unified disk + index enumeration) ==============
uint8_t kcdx_ForEachFile(void* self, void* cbCtx, const char* pPathPattern,
                         void* userData) {
    if (!self || !pPathPattern) return 0;  // engine null-arg contract → no match

    PerFileCallbackFn_t perFile = ReadSlot15Callback(self);
    if (!perFile) {
        // The object's slot-15 entry is unreadable — cannot dispatch the
        // per-file callback. Fail loud (AP14); the engine's own ForEachFile
        // would crash here, so a 0 return (no match) is the safe degradation.
        LOG_ERROR_KV(kCat, "foreachfile_no_callback",
            kcdx::log::KV::BareStr("pattern",
                pPathPattern ? pPathPattern : "<null>"),
            kcdx::log::KV::BareStr("detail",
                "could not read the object's slot-15 per-file callback entry "
                "(vtable+0x78) — the CCryPak object or its vtable is malformed; "
                "no enumeration performed this call."));
        return 0;
    }

    bool any = false;
    long long matched = 0;  // FS_BOOT_TRACE: entries the unified walk fired

    // ---- (1) Engine on-disk walk (the original _findfirst64 loop, on kcdx's
    //          CRT) over the resolved disk pattern. Resolve pPathPattern via
    //          slot 1 (kcdx_AdjustFileName) exactly as the original body does. --
    char diskPattern[kMaxPath];
    diskPattern[0] = '\0';
    void* r = kcdx_AdjustFileName(self, pPathPattern, diskPattern, /*nFlags=*/0);
    const char* resolvedPattern =
        r ? static_cast<const char*>(r) : diskPattern;

    if (resolvedPattern && resolvedPattern[0] != '\0') {
        wchar_t wpattern[kMaxPath];
        if (WidenPath(resolvedPattern, wpattern, kMaxPath)) {
            const size_t dlen = DirPrefixLen(resolvedPattern);
            _wfinddata64_t fd;
            const intptr_t h = _wfindfirst64(wpattern, &fd);
            if (h != -1) {
                do {
                    // Build "<dir>/<name>" (the engine's per-entry full path).
                    char full[kMaxPath];
                    // Narrow the find result's name back to UTF-8 for the
                    // callback (the callback takes a const char*).
                    char nameUtf8[kMaxPath];
                    const int nn = WideCharToMultiByte(CP_UTF8, 0, fd.name, -1,
                                                       nameUtf8, kMaxPath,
                                                       nullptr, nullptr);
                    if (nn <= 0) continue;  // unconvertible name — skip (logged below if none match)
                    const int w = std::snprintf(full, kMaxPath, "%.*s%s",
                                                static_cast<int>(dlen),
                                                resolvedPattern, nameUtf8);
                    if (w > 0 && w < static_cast<int>(kMaxPath)) {
                        perFile(self, cbCtx, full, userData);
                        any = true;
                        ++matched;
                    }
                } while (_wfindnext64(h, &fd) == 0);
                _findclose(h);
            }
        } else {
            LOG_WARN_KV(kCat, "foreachfile_widen_failed",
                kcdx::log::KV("pattern", std::string(resolvedPattern)));
        }
    }

    // ---- (2) Index walk: enumerate the kcdx-served vpaths under the SAME
    //          directory prefix (loose overrides + pak entries the disk walk
    //          above did not surface — they live in paks / override dirs). The
    //          callback fires with each matched vpath. De-dup against the disk
    //          walk is implicit: a loose override served from disk would ALSO
    //          have matched the disk walk; the index entry for it is the same
    //          vpath, so we only re-emit a vpath the disk walk could NOT see
    //          (pak entries) — but to avoid a double callback for a loose vpath
    //          that the disk walk already emitted, we emit from the index ONLY
    //          PAK sources here (a loose source's file is on disk and was
    //          already walked in (1) under the resolved override dir... see the
    //          surfaced de-dup note). A pak-resident vpath is the index-only set.
    const std::string prefix = IndexDirPrefix(pPathPattern);
    const AssetIndex& index = GetBuiltIndex();
    for (const auto& [vpath, src] : index) {
        // Only PAK sources are index-only (a loose override is a real disk file
        // the (1) walk already enumerated when the resolved pattern covered its
        // override dir). Emitting a pak vpath here adds exactly the entries the
        // engine's disk walk cannot see — the unified-set delta.
        if (src.kind != ByteSource::Kind::Pak) continue;
        // Prefix match: the vpath sits directly under the requested directory
        // (same directory level — not a deeper subdir, matching _findfirst64's
        // single-level semantics). A vpath is "under" the prefix if it starts
        // with it AND has no further separator past the prefix.
        if (vpath.size() <= prefix.size()) continue;
        if (vpath.compare(0, prefix.size(), prefix) != 0) continue;
        if (vpath.find('/', prefix.size()) != std::string::npos) continue;  // deeper subdir — skip (single-level)
        perFile(self, cbCtx, vpath.c_str(), userData);
        any = true;
        ++matched;
    }

    if (any) {
        bool expected = false;
        if (g_loggedFirstEnum.compare_exchange_strong(expected, true,
                                                      std::memory_order_relaxed)) {
            LOG_DEBUG_KV(kCat, "kcdx_enum_first",
                kcdx::log::KV("pattern", std::string(pPathPattern)),
                kcdx::log::KV::BareStr("detail",
                    "kcdx ForEachFile enumerated the unified set — the engine's "
                    "on-disk entries (kcdx CRT _wfindfirst64 walk) PLUS the "
                    "unified index's pak-resident vpaths under the same prefix; "
                    "each fired the caller's per-file callback via the object's "
                    "slot-15 entry (the engine original). No engine search-path "
                    "walk replaced — kcdx owns the enumeration."));
        }
    }

    // FS_BOOT_TRACE (F.3): every enum call in the boot window — the inbound
    // pattern + the count of entries the unified walk fired the callback for.
    // pPathPattern is a borrowed inbound pointer (no allocation on the traced
    // path). Predicted-skip after AfterGameApply.
    TraceEnum("ForEachFile", pPathPattern, matched);

    return any ? 1 : 0;
}

}  // namespace kcdx::fs_takeover
