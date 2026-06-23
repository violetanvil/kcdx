#include "metadata_slots.h"

#include <windows.h>  // GetFileAttributesW, MultiByteToWideChar

#include <atomic>
#include <cstdint>
#include <intrin.h>    // === DIAGNOSTIC (PROBE W) === _ReturnAddress (caller attribution)
#include <string>
#include <sys/stat.h>  // struct _stat64 / _wstat64 (the slot-45/92/93 loose-hit size)

#include "asset_index.h"         // ByteSource / GetBuiltIndex / ResolveVPath (normalizes the query in-body)
#include "boot_trace.h"          // FS_BOOT_TRACE — boot-window full slot trace (KI-0026 F.3)
#include "../log.h"

// The kcdx EXISTENCE / METADATA-by-name slot impls — see metadata_slots.h for
// the slot set, the per-slot BODY-VERIFIED ABI, and the answer model.
//
// THE ANSWER MODEL (design §5): each slot resolves the vpath against the
// unified index in O(1). An index HIT answers from the ByteSource (existence =
// true; size = the entry's size — this covers kcdx's index-only pak vpaths the
// engine original would not see). An index MISS is NOT a hand-back and NOT an
// OS-disk-only op — it thunks the slot's OWN CAPTURED ORIGINAL engine body with
// the SAME args and returns its result verbatim. The original consults BOTH the
// engine pak-directory (FUN_1804631f0) AND disk, so a name living only in an
// engine-mounted pak the index does not carry still gets the real engine
// answer. Index-hit-first-then-original-on-miss composes correctly: the index
// covers what it has; the original covers everything else, byte-for-byte.
//
// §-SAFETY OF THE MISS THUNK: the captured original returns a VALUE and touches
// only the engine object's intact members (pak vector, search-path vector,
// alias table — all preserved by the vtable-pointer-only swap). It mints no
// handle and uses no CRT — the same §-safe class as the slot-1 resolution
// thunk (open_slots.cpp). A null captured original (a swap-wiring defect) fails
// LOUD and returns the slot's not-found sentinel — never a silent guess (AP14).
//
// HOT PATH (memory.md / logging.md): existence/size fire constantly. NO
// per-call allocation on the answer path, NO per-call log (a one-shot
// first-resolve latch only). Every FAILURE branch logs loud (logging.md / AP14).

namespace kcdx::fs_takeover {

namespace {

constexpr const char* kCat = "FS_META";

// The engine's universal path cap (CryEngine ICryPak::g_nMaxPath), the same cap
// open_slots.cpp uses. Used only by the index-HIT loose-stat helper below.
constexpr size_t kMaxPath = 2048;

// One-shot first-resolve latch — the metadata slots are HOT; log the first
// metadata resolve of the session, then stay silent (logging.md / memory.md —
// never per-call on the hot path). Relaxed: a pure logged-yet latch with no
// happens-before to publish (concurrency.md).
std::atomic<bool> g_loggedFirstMeta{false};

// === The 8 captured-original metadata-slot bodies — stored by the swap at
// capture time (SetMetadataOriginals), thunked by each slot's MISS arm. Each is
// the slot's BODY-VERIFIED member-call ABI (see metadata_slots.h). acquire/
// release so the kcdx vtable's first metadata dispatch sees the stored pointer
// (concurrency.md — a happens-before publish edge, like slot 1's capture). ====
std::atomic<IsFolderOrigFn_t>             g_origIsFolder{nullptr};
std::atomic<GetFileSizeOrigFn_t>          g_origGetFileSize{nullptr};
std::atomic<IsFileExist3OrigFn_t>         g_origIsFileExist3{nullptr};
std::atomic<GetFileAttributesOrigFn_t>    g_origGetFileAttributes{nullptr};
std::atomic<GetFileStatOrigFn_t>          g_origGetFileStat{nullptr};
std::atomic<IsFileExist2OrigFn_t>         g_origIsFileExist2{nullptr};
std::atomic<GetFileSizeOnDiskOrigFn_t>    g_origGetFileSizeOnDisk{nullptr};
std::atomic<GetFileSizeCompressedOrigFn_t> g_origGetFileSizeCompressed{nullptr};

// Widen a UTF-8/ASCII disk path for the wide CRT/Win32 metadata calls (so a
// non-ASCII loose-override path resolves correctly). Returns false (the caller
// falls to the miss thunk) on an over-cap path. Used only by the index-HIT
// loose arms below. Mirrors open_slots.cpp WidenPath.
bool WidenPath(const char* diskPath, wchar_t* wpath, size_t wcap) {
    const int wlen = MultiByteToWideChar(CP_UTF8, 0, diskPath, -1, wpath,
                                         static_cast<int>(wcap));
    return wlen > 0;
}

// Stat a LOOSE override's disk file into a struct _stat64 on kcdx's CRT (wide
// form, so non-ASCII paths work) for its TRUE size. Used only by the index-HIT
// loose arms of slots 45/92/93. Returns the _wstat64 result (0 = ok, -1 = no file).
int StatDiskPath(const char* diskPath, struct _stat64* st) {
    wchar_t wpath[kMaxPath];
    if (!WidenPath(diskPath, wpath, kMaxPath)) return -1;
    return _wstat64(wpath, st);
}

// The first-metadata-resolve log latch — fire once, name the vpath + how it
// answered, then stay silent forever (hot-path discipline).
void LogFirstMeta(const char* whichSlot, const char* vpath, const char* how) {
    bool expected = false;
    if (g_loggedFirstMeta.compare_exchange_strong(expected, true,
                                                  std::memory_order_relaxed)) {
        LOG_DEBUG_KV(kCat, "kcdx_meta_first",
            kcdx::log::KV::BareStr("slot", whichSlot),
            kcdx::log::KV("vpath", std::string(vpath ? vpath : "")),
            kcdx::log::KV::BareStr("how", how),
            kcdx::log::KV::BareStr("detail",
                "kcdx existence/metadata slot answered from the unified index "
                "(hit) or thunked the slot's captured original engine body "
                "(miss → the original consults the engine pak-dir AND disk, "
                "returns a value, mints no handle, uses no CRT — §-safe)."));
    }
}

// Log a null-captured-original swap-wiring defect, once-latched off the same hot
// latch (a miss on a never-wired original is a defect, not a hot event). Loud
// (AP14): the slot then returns its not-found sentinel rather than guessing.
void LogMissingOriginal(const char* whichSlot, const char* vpath) {
    LOG_ERROR_KV(kCat, "meta_no_captured_original",
        kcdx::log::KV::BareStr("slot", whichSlot),
        kcdx::log::KV("vpath", std::string(vpath ? vpath : "")),
        kcdx::log::KV::BareStr("detail",
            "an index-miss metadata answer needs the slot's captured original "
            "engine body, but it is null (the swap did not capture this slot's "
            "original via SetMetadataOriginals) — returning the slot's "
            "not-found sentinel rather than guessing. This is a swap-wiring "
            "defect, surfaced loud."));
}

// === DIAGNOSTIC (PROBE W) — IsFileExist3 vanilla-differential helper. kcdx
// answered EXISTS(=1) from its index for `pName`; call the captured original with
// the SAME (pName, location) and log iff it disagrees. Read-only (the original is
// the engine pak-dir + disk existence check the miss arm already thunks); kcdx's
// answer is unaffected. NO-RESIDUE: remove with PROBE W. ===
void DiffExist3(void* self, const char* pName, int location, uintptr_t caller) {
    // PROBE W cost gate: run the doubled engine-original call ONLY the first time
    // we see this caller (the per-caller signal is what matters; the storm fires
    // this thousands of times/sec — gating it is what keeps the machine usable).
    if (!BootTraceCallerFirstSeen(caller, /*kind=existence*/1)) return;
    IsFileExist3OrigFn_t orig = g_origIsFileExist3.load(std::memory_order_acquire);
    if (!orig) return;  // no captured original → nothing to compare (already loud elsewhere)
    TraceVanillaDiff("IsFileExist3", pName, 1, orig(self, pName, location) ? 1 : 0,
                     caller);
}

}  // namespace

void SetMetadataOriginals(const void* const* originalVtable) {
    if (!originalVtable) {
        LOG_ERROR_KV(kCat, "set_metadata_originals_null_vtable",
            kcdx::log::KV::BareStr("detail",
                "SetMetadataOriginals called with a null original vtable — the "
                "8 metadata-slot originals are NOT captured; every metadata "
                "index-miss this boot will hit the loud null-original path."));
        return;
    }
    // Each slot's original body is the original vtable's slot-N entry. The
    // reinterpret_cast binds the void* slot pointer to the slot's verified ABI.
    g_origIsFolder.store(
        reinterpret_cast<IsFolderOrigFn_t>(originalVtable[13]),
        std::memory_order_release);
    g_origGetFileSize.store(
        reinterpret_cast<GetFileSizeOrigFn_t>(originalVtable[45]),
        std::memory_order_release);
    g_origIsFileExist3.store(
        reinterpret_cast<IsFileExist3OrigFn_t>(originalVtable[67]),
        std::memory_order_release);
    g_origGetFileAttributes.store(
        reinterpret_cast<GetFileAttributesOrigFn_t>(originalVtable[68]),
        std::memory_order_release);
    g_origGetFileStat.store(
        reinterpret_cast<GetFileStatOrigFn_t>(originalVtable[69]),
        std::memory_order_release);
    g_origIsFileExist2.store(
        reinterpret_cast<IsFileExist2OrigFn_t>(originalVtable[70]),
        std::memory_order_release);
    g_origGetFileSizeOnDisk.store(
        reinterpret_cast<GetFileSizeOnDiskOrigFn_t>(originalVtable[92]),
        std::memory_order_release);
    g_origGetFileSizeCompressed.store(
        reinterpret_cast<GetFileSizeCompressedOrigFn_t>(originalVtable[93]),
        std::memory_order_release);
    LOG_INFO_KV(kCat, "metadata_originals_captured",
        kcdx::log::KV::BareStr("detail",
            "captured the 8 metadata-slot original engine bodies "
            "(13/45/67/68/69/70/92/93) from the live object's original vtable — "
            "each slot's index-miss arm thunks its own original (engine pak-dir "
            "AND disk), so a name only an engine-mounted pak carries still gets "
            "the real engine answer."));
}

// === slot 13 — IsFolder ====================================================
//
// The index holds FILE vpaths, never directory stubs, so it carries no folder
// answer. There is no index arm — every call thunks the captured original (the
// engine's own slot1 + _findfirst64 dir probe, which sees pak-dir AND disk).
bool kcdx_IsFolder(void* self, const char* pName) {
    if (!pName) return false;  // engine null-arg contract → not a folder
    IsFolderOrigFn_t orig = g_origIsFolder.load(std::memory_order_acquire);
    if (!orig) { LogMissingOriginal("IsFolder", pName); return false; }
    const bool r = orig(self, pName);
    LogFirstMeta("IsFolder", pName, "original");
    TraceMeta("IsFolder", pName, "original", r ? 1 : 0);
    return r;
}

// === slot 45 — GetFileSize-by-name (3-arg: this, name, bDiskOnly) ==========
//
// Index HIT → the ByteSource's uncompressed size (loose: stat the diskPath; pak:
// bs->size). bDiskOnly: when set, a pak (index) source is skipped — only a
// loose/disk source answers (the engine's bDiskOnly semantics). MISS → thunk the
// captured original (engine pak-dir AND disk, bDiskOnly-honored in-body).
uint64_t kcdx_GetFileSize(void* self, const char* pName, char bDiskOnly) {
    // Not-found return is 0, not (uint64)-1 — the verified body (FUN_182418b48)
    // maps its internal OS-getter "no size" signal to 0 before returning; every
    // engine caller of slot 45 reads 0 as absent.
    if (!pName) return 0;
    const uintptr_t caller =
        BootTraceCallerRva(reinterpret_cast<uintptr_t>(_ReturnAddress()));  // PROBE W

    const ByteSource* bs = ResolveVPath(GetBuiltIndex(), pName);
    if (bs) {
        if (bs->kind == ByteSource::Kind::Pak) {
            if (bDiskOnly) {
                // bDiskOnly skips the pak/index arm — fall to the original (the
                // pak file is not the asset's "disk" form; the original honors
                // bDiskOnly in-body).
            } else {
                LogFirstMeta("GetFileSize", pName, "index-pak");
                TraceMeta("GetFileSize", pName, "index-pak",
                          static_cast<long long>(bs->size));
                // PROBE W — kcdx returns the index size; does vanilla agree? A
                // size divergence can mis-steer a loader (alloc/read/skip). Read-
                // only; kcdx's answer unchanged.
                if (BootTraceCallerFirstSeen(caller, /*kind=size*/2)) {
                    if (GetFileSizeOrigFn_t o =
                            g_origGetFileSize.load(std::memory_order_acquire)) {
                        TraceVanillaDiff("GetFileSize", pName,
                                         static_cast<long long>(bs->size),
                                         static_cast<long long>(o(self, pName, bDiskOnly)),
                                         caller);
                    }
                }
                return bs->size;
            }
        } else {  // Loose: the override IS a disk file; stat it for the true size.
            struct _stat64 st;
            if (StatDiskPath(bs->diskPath.c_str(), &st) == 0) {
                LogFirstMeta("GetFileSize", pName, "index-loose");
                TraceMeta("GetFileSize", pName, "index-loose",
                          static_cast<long long>(st.st_size));
                return static_cast<uint64_t>(st.st_size);
            }
            // The loose override resolved but its disk file failed to stat — a
            // real anomaly (the index says it exists). Fail loud, fall to the
            // miss thunk (the original re-resolves the name and may still answer).
            LOG_WARN_KV(kCat, "getfilesize_loose_stat_failed",
                kcdx::log::KV("vpath", std::string(pName)),
                kcdx::log::KV("disk", bs->diskPath));
        }
    }

    // MISS (or a bDiskOnly pak skip, or a loose-stat anomaly) — thunk the slot's
    // captured original with the SAME (pName, bDiskOnly); return verbatim.
    GetFileSizeOrigFn_t orig = g_origGetFileSize.load(std::memory_order_acquire);
    if (!orig) {
        LogMissingOriginal("GetFileSize", pName);
        return 0;  // the verified body's not-found return (see slot doc)
    }
    const uint64_t r = orig(self, pName, bDiskOnly);
    LogFirstMeta("GetFileSize", pName, "original");
    TraceMeta("GetFileSize", pName, "original", static_cast<long long>(r));
    return r;
}

// === slot 67 — IsFileExist (3-arg: this, name, location) ===================
//
// location gating (BODY-VERIFIED): ==2 → pak-only; ==1 → disk-only; ==0/other →
// either (the body checks disk then pak, pakPriority-ordered). Index HIT: a Pak
// source satisfies pak/either; a Loose source satisfies disk/either. MISS (incl.
// a location-filtered index source) → thunk the captured original (the full
// location-gated engine pak-dir AND disk check — incl. the location==2 pak-dir
// lookup the index miss cannot answer). NO hardcoded location==2 false.
bool kcdx_IsFileExist3(void* self, const char* pName, int location) {
    if (!pName) return false;
    // PROBE W: the engine return address (module-relative) — captured at the slot
    // entry so it names the ENGINE subsystem that asked, not this kcdx body.
    const uintptr_t caller =
        BootTraceCallerRva(reinterpret_cast<uintptr_t>(_ReturnAddress()));

    const ByteSource* bs = ResolveVPath(GetBuiltIndex(), pName);
    if (bs) {
        const bool isPak = (bs->kind == ByteSource::Kind::Pak);
        // location==2 → pak-only; ==1 → disk-only; else either.
        if (location == 2) {
            if (isPak) { LogFirstMeta("IsFileExist3", pName, "index-pak");
                         TraceMeta("IsFileExist3", pName, "index-pak", 1);
                         DiffExist3(self, pName, location, caller);  // PROBE W
                         return true; }
        } else if (location == 1) {
            if (!isPak) {  // a loose disk override — confirm the disk file exists.
                wchar_t wpath[kMaxPath];
                if (WidenPath(bs->diskPath.c_str(), wpath, kMaxPath) &&
                    GetFileAttributesW(wpath) != INVALID_FILE_ATTRIBUTES) {
                    LogFirstMeta("IsFileExist3", pName, "index-loose");
                    TraceMeta("IsFileExist3", pName, "index-loose", 1);
                    DiffExist3(self, pName, location, caller);  // PROBE W
                    return true;
                }
            }
        } else {  // either
            LogFirstMeta("IsFileExist3", pName, "index-either");
            TraceMeta("IsFileExist3", pName, "index-either", 1);
            DiffExist3(self, pName, location, caller);  // PROBE W
            return true;
        }
        // location filtered the index source out → fall to the original.
    }

    // MISS / location-filtered — thunk the captured original. The original does
    // the full location-gated engine pak-dir AND disk check, so a location==2
    // name living only in an engine-mounted pak the index does not carry gets
    // the real engine answer (NOT a hardcoded false).
    IsFileExist3OrigFn_t orig = g_origIsFileExist3.load(std::memory_order_acquire);
    if (!orig) { LogMissingOriginal("IsFileExist3", pName); return false; }
    const bool r = orig(self, pName, location);
    LogFirstMeta("IsFileExist3", pName, "original");
    TraceMeta("IsFileExist3", pName, "original", r ? 1 : 0);
    return r;
}

// === slot 68 — GetFileAttributes / IsFolder(disk) ==========================
//
// The original is a DISK attribute probe (slot1 → GetFileAttributesA → the
// packed dir-flag form, 0 on a non-existent path); it carries no pak-dir lookup
// and the index holds no attribute answer. So there is no index arm — every call
// thunks the captured original and returns its packed attribute result verbatim.
uint64_t kcdx_GetFileAttributes(void* self, const char* pName) {
    if (!pName) return 0;
    GetFileAttributesOrigFn_t orig =
        g_origGetFileAttributes.load(std::memory_order_acquire);
    if (!orig) { LogMissingOriginal("GetFileAttributes", pName); return 0; }
    const uint64_t r = orig(self, pName);
    LogFirstMeta("GetFileAttributes", pName, "original");
    TraceMeta("GetFileAttributes", pName, "original", static_cast<long long>(r));
    return r;
}

// === slot 69 — GetFileStat (_stat64) =======================================
//
// The original is a _stat64 of the resolved path (disk stat); the index carries
// no stat record. So there is no index arm — every call thunks the captured
// original (which resolves + _stat64s on the engine's CRT, writing outStat) and
// returns its int verbatim. 0 on success, -1 on failure.
int kcdx_GetFileStat(void* self, const char* pName, void* outStat) {
    if (!pName || !outStat) return -1;  // engine null-arg contract
    GetFileStatOrigFn_t orig = g_origGetFileStat.load(std::memory_order_acquire);
    if (!orig) { LogMissingOriginal("GetFileStat", pName); return -1; }
    const int rc = orig(self, pName, outStat);
    if (rc == 0) LogFirstMeta("GetFileStat", pName, "original");
    TraceMeta("GetFileStat", pName, "original", static_cast<long long>(rc));
    return rc;
}

// === slot 70 — IsFileExist (2-arg) =========================================
//
// Index HIT for a file vpath → exists=true (the index holds files, never dir
// stubs, so the original's dir-entry exclusion is satisfied by construction).
// MISS → thunk the captured original (engine pak-dir entry check, dir-excluding).
bool kcdx_IsFileExist2(void* self, const char* pName) {
    if (!pName) return false;
    const uintptr_t caller =
        BootTraceCallerRva(reinterpret_cast<uintptr_t>(_ReturnAddress()));  // PROBE W

    if (ResolveVPath(GetBuiltIndex(), pName)) {
        LogFirstMeta("IsFileExist2", pName, "index");
        TraceMeta("IsFileExist2", pName, "index", 1);
        // === DIAGNOSTIC (PROBE W) — vanilla-differential: kcdx says EXISTS from
        // its index; what would the engine ORIGINAL say? A divergence (kcdx=1,
        // vanilla=0) is a pak-resident vpath the engine's own pak-dir would NOT
        // find → an existence answer that can steer a loader to load/skip
        // differently. Read-only; kcdx's answer (true) is returned unchanged.
        // Gated once-per-caller (the doubled original-call is the cost).
        if (BootTraceCallerFirstSeen(caller, /*kind=existence*/1)) {
            if (IsFileExist2OrigFn_t orig =
                    g_origIsFileExist2.load(std::memory_order_acquire)) {
                TraceVanillaDiff("IsFileExist2", pName, 1, orig(self, pName) ? 1 : 0,
                                 caller);
            }
        }
        // === END PROBE W ===
        return true;  // an index entry is always a file (never a dir stub).
    }

    // MISS — thunk the captured original (the engine pak-dir entry check, which
    // excludes dir entries) with the SAME pName; return verbatim.
    IsFileExist2OrigFn_t orig = g_origIsFileExist2.load(std::memory_order_acquire);
    if (!orig) { LogMissingOriginal("IsFileExist2", pName); return false; }
    const bool r = orig(self, pName);
    LogFirstMeta("IsFileExist2", pName, "original");
    TraceMeta("IsFileExist2", pName, "original", r ? 1 : 0);
    return r;
}

// === slot 92 — GetFileSizeOnDisk / GetFileSize(uncompressed) ===============
//
// Index HIT → the ByteSource's uncompressed size (loose: stat; pak: bs->size).
// MISS → thunk the captured original (engine pak-dir size compute). Returns 0 on
// a non-existent name (the body's lVar2=0 default).
long long kcdx_GetFileSizeOnDisk(void* self, const char* pName) {
    if (!pName) return 0;

    const ByteSource* bs = ResolveVPath(GetBuiltIndex(), pName);
    if (bs) {
        if (bs->kind == ByteSource::Kind::Pak) {
            LogFirstMeta("GetFileSizeOnDisk", pName, "index-pak");
            TraceMeta("GetFileSizeOnDisk", pName, "index-pak",
                      static_cast<long long>(bs->size));
            return static_cast<long long>(bs->size);
        }
        struct _stat64 st;
        if (StatDiskPath(bs->diskPath.c_str(), &st) == 0) {
            LogFirstMeta("GetFileSizeOnDisk", pName, "index-loose");
            TraceMeta("GetFileSizeOnDisk", pName, "index-loose",
                      static_cast<long long>(st.st_size));
            return static_cast<long long>(st.st_size);
        }
        LOG_WARN_KV(kCat, "getfilesizeondisk_loose_stat_failed",
            kcdx::log::KV("vpath", std::string(pName)),
            kcdx::log::KV("disk", bs->diskPath));
        // fall through to the miss thunk.
    }

    GetFileSizeOnDiskOrigFn_t orig =
        g_origGetFileSizeOnDisk.load(std::memory_order_acquire);
    if (!orig) { LogMissingOriginal("GetFileSizeOnDisk", pName); return 0; }
    const long long r = orig(self, pName);
    LogFirstMeta("GetFileSizeOnDisk", pName, "original");
    TraceMeta("GetFileSizeOnDisk", pName, "original", r);
    return r;
}

// === slot 93 — GetFileSizeCompressed =======================================
//
// Index HIT → the ByteSource's COMPRESSED size (pak: bs->compressed; loose: ==
// the disk size — a loose file is stored, compressed==uncompressed). MISS →
// thunk the captured original (engine pak-dir compressed-size compute). Returns
// 0 on a non-existent name.
uint32_t kcdx_GetFileSizeCompressed(void* self, const char* pName) {
    if (!pName) return 0;

    const ByteSource* bs = ResolveVPath(GetBuiltIndex(), pName);
    if (bs) {
        if (bs->kind == ByteSource::Kind::Pak) {
            LogFirstMeta("GetFileSizeCompressed", pName, "index-pak");
            TraceMeta("GetFileSizeCompressed", pName, "index-pak",
                      static_cast<long long>(bs->compressed));
            return static_cast<uint32_t>(bs->compressed);
        }
        struct _stat64 st;
        if (StatDiskPath(bs->diskPath.c_str(), &st) == 0) {
            LogFirstMeta("GetFileSizeCompressed", pName, "index-loose");
            TraceMeta("GetFileSizeCompressed", pName, "index-loose",
                      static_cast<long long>(st.st_size));
            return static_cast<uint32_t>(st.st_size);  // loose = stored, compressed==disk size
        }
        LOG_WARN_KV(kCat, "getfilesizecompressed_loose_stat_failed",
            kcdx::log::KV("vpath", std::string(pName)),
            kcdx::log::KV("disk", bs->diskPath));
    }

    GetFileSizeCompressedOrigFn_t orig =
        g_origGetFileSizeCompressed.load(std::memory_order_acquire);
    if (!orig) { LogMissingOriginal("GetFileSizeCompressed", pName); return 0; }
    const uint32_t r = orig(self, pName);
    LogFirstMeta("GetFileSizeCompressed", pName, "original");
    TraceMeta("GetFileSizeCompressed", pName, "original",
              static_cast<long long>(r));
    return r;
}

}  // namespace kcdx::fs_takeover
