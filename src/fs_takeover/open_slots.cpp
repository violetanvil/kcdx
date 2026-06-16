#include "open_slots.h"

#include <windows.h>  // MultiByteToWideChar — widen the disk path for _wfopen_s

#include <atomic>
#include <cstdio>
#include <cstring>
#include <iterator>  // std::size (wmode bound)
#include <string>
#include <vector>

#include "asset_index.h"
#include "file_handle.h"
#include "pak_reader.h"
#include "../asset_overlay.h"  // NormalizeVPath (the shared index key fold)
#include "../log.h"

// The kcdx OPEN-family slot impls — see open_slots.h for the slot set + ABIs.
//
// Cross-CRT invariant (§9): every open here mints a kcdx handle (file_handle.h),
// opened on kcdx's CRT (kcdx _wfopen for a loose/non-asset/write path; kcdx's
// pak_reader::ReadPakEntry for a pak asset). No engine-CRT handle is ever minted
// or returned. slot-1 resolution on a miss thunks the ORIGINAL body, which
// returns a STRING and touches no handle/CRT (§5 — the safe long-tail thunk).

namespace kcdx::fs_takeover {

namespace {

constexpr const char* kCat = "FS_OPEN";

// The engine's universal path cap (CryEngine ICryPak::g_nMaxPath). Same cap the
// asset_overlay HOOK-1/HOOK-2 used for its outBuf write + _wfopen widen — the
// proven shape this generalizes. A real disk path is always well under 2048; an
// over-cap path fails loud, never an OOB write (the KI-0004 class is excluded).
constexpr size_t kMaxPath = 2048;

// The captured original AdjustFileName body (slot 1) — set by the swap at
// capture time. The slot-1 impl thunks to it on an index MISS. acquire/release
// so the kcdx vtable's first dispatch sees the fully-stored pointer.
std::atomic<AdjustFileNameOrigFn_t> g_originalAdjustFileName{nullptr};

// One-shot HIT log latches — the open slots are HOT (FOpen/AdjustFileName fire
// constantly); log the first asset-served open of the session, then stay silent
// (logging.md / memory.md — never per-call on the hot path). Relaxed: a pure
// logged-yet latch with no happens-before to publish (concurrency.md).
std::atomic<bool> g_loggedFirstResolve{false};
std::atomic<bool> g_loggedFirstOpen{false};

// Widen a UTF-8/ASCII disk path to a wchar_t buffer for _wfopen_s. Returns false
// (the caller fails the open loud) if the path overflows the cap. Mirrors the
// asset_overlay HOOK-2 widen exactly.
bool WidenPath(const std::string& diskPath, wchar_t* wpath, size_t wcap) {
    const int wlen = MultiByteToWideChar(CP_UTF8, 0, diskPath.c_str(), -1,
                                         wpath, static_cast<int>(wcap));
    return wlen > 0;
}

// Open a LOOSE disk path on kcdx's CRT and mint a kcdx loose handle. Returns the
// handle, or 0 on failure (logged loud — never a silent broken handle, AP14).
KcdxHandle OpenLooseAndMint(const std::string& diskPath, const char* mode,
                            const char* whichSlot, const char* vpathForLog) {
    wchar_t wpath[kMaxPath];
    if (!WidenPath(diskPath, wpath, kMaxPath)) {
        LOG_ERROR_KV(kCat, "loose_widen_failed",
            kcdx::log::KV::BareStr("slot", whichSlot),
            kcdx::log::KV("disk", diskPath));
        return 0;
    }
    wchar_t wmode[16];
    const char* m = (mode && mode[0]) ? mode : "rb";
    if (MultiByteToWideChar(CP_UTF8, 0, m, -1, wmode,
                            static_cast<int>(std::size(wmode))) <= 0) {
        LOG_ERROR_KV(kCat, "loose_mode_widen_failed",
            kcdx::log::KV::BareStr("slot", whichSlot),
            kcdx::log::KV::BareStr("mode", m));
        return 0;
    }
    FILE* fp = nullptr;
    const errno_t oerr = _wfopen_s(&fp, wpath, wmode);
    if (oerr != 0 || !fp) {
        // A loose open that fails (a non-asset path that does not exist for a
        // read, a write target whose dir is missing, …) — fail loud. The engine
        // sees a failed open (0), exactly as its own FOpen returns null. NOT a
        // silent broken handle (AP14).
        LOG_WARN_KV(kCat, "loose_open_failed",
            kcdx::log::KV::BareStr("slot", whichSlot),
            kcdx::log::KV("vpath", std::string(vpathForLog ? vpathForLog : "")),
            kcdx::log::KV("disk", diskPath),
            kcdx::log::KV::BareStr("mode", m),
            kcdx::log::KV("errno", static_cast<long long>(oerr)));
        return 0;
    }
    const KcdxHandle h = MintLoose(fp);
    if (h == 0) {
        std::fclose(fp);  // mint failed (logged in MintLoose) — don't leak the FILE*
        return 0;
    }
    return h;
}

// Open a PAK byte-source (inflate the whole entry on kcdx's CRT) and mint a kcdx
// pak handle. Returns the handle, or 0 on failure (logged loud).
KcdxHandle OpenPakAndMint(const ByteSource& src, const char* whichSlot,
                          const char* vpathForLog) {
    // Reconstruct the PakEntry the reader needs from the index ByteSource (the
    // index stored {offset, size, method, crc}; the reader's ReadPakEntry reads
    // the entry's bytes on kcdx's CRT — kcdx _wfopen/fread/inflate, no engine
    // ZipDir). The entry name is only used for the reader's log context.
    PakEntry entry;
    entry.name                = vpathForLog ? vpathForLog : "";
    entry.local_header_offset = src.offset;
    entry.uncompressed_size   = src.size;
    // The compressed byte count ReadPakEntry reads from the data start before
    // inflating — carried through the index ByteSource (NOT re-derivable here:
    // ReadPakEntry reads exactly entry.compressed_size bytes; a DEFLATE entry's
    // compressed size differs from its uncompressed size). The index records it
    // from the CDR at build time (asset_index.cpp).
    entry.compressed_size     = src.compressed;
    entry.method              = src.method;
    entry.crc32               = src.crc;

    std::vector<uint8_t> bytes;
    std::string err;
    if (!ReadPakEntry(src.pakFile, entry, bytes, err)) {
        LOG_WARN_KV(kCat, "pak_open_failed",
            kcdx::log::KV::BareStr("slot", whichSlot),
            kcdx::log::KV("vpath", std::string(vpathForLog ? vpathForLog : "")),
            kcdx::log::KV("error", err));
        return 0;
    }
    return MintPak(std::move(bytes));
}

// The shared open body for slot 36 / slot 35: resolve pName via the unified
// index, open the byte-source on kcdx's CRT, mint + return a kcdx handle. Writes
// the resolved disk-path string into resolvedOut (≤ kMaxPath) when non-null (the
// FOpenRaw outResolvedBuf contract). Returns the handle, or 0 on a failed open.
//
// EVERY path mints a kcdx handle (§5): an index ASSET hit (loose or pak), AND a
// non-asset/write/miss path (resolved to a real disk path via the original
// AdjustFileName, then kcdx _wfopen'd). There is NO path that returns an
// engine-CRT handle.
KcdxHandle OpenResolvedAndMint(void* self, const char* pName, const char* szMode,
                               uint32_t nFlags, const char* whichSlot,
                               char* resolvedOut, int resolvedCap) {
    const std::string vpath = pName;
    const std::string key = asset_overlay::NormalizeVPath(vpath);

    // 1. Index lookup (the O(1) asset fast path, §5). A hit is a loose override
    //    or a vanilla pak entry.
    const ByteSource* bs = ResolveVPath(GetBuiltIndex(), key);

    KcdxHandle handle = 0;
    std::string resolvedDisk;  // what we write into resolvedOut (FOpenRaw)

    if (bs && bs->kind == ByteSource::Kind::Loose) {
        resolvedDisk = bs->diskPath;
        handle = OpenLooseAndMint(bs->diskPath, szMode, whichSlot, key.c_str());
    } else if (bs && bs->kind == ByteSource::Kind::Pak) {
        // The pak source resolves to its pak file on disk; the read serves from
        // the inflated in-memory buffer (no per-read pak re-touch).
        // Log-only: faithful UTF-8 of the (possibly non-ASCII) pak path — a
        // wchar→char narrowing assign would mojibake it. The serve path uses
        // bs->pakFile directly; this is only what we write into resolvedDisk for
        // the log. Inverse of the WidenPath/MultiByteToWideChar shape above.
        const int n = WideCharToMultiByte(CP_UTF8, 0, bs->pakFile.c_str(), -1,
                                          nullptr, 0, nullptr, nullptr);
        if (n > 0) {
            resolvedDisk.resize(static_cast<size_t>(n - 1));  // n includes the NUL
            WideCharToMultiByte(CP_UTF8, 0, bs->pakFile.c_str(), -1,
                                resolvedDisk.data(), n, nullptr, nullptr);
        }
        handle = OpenPakAndMint(*bs, whichSlot, key.c_str());
    } else {
        // 2. Index MISS — a non-asset / write / unindexed name (a save, config,
        //    cache, write target). §5: kcdx STILL resolves it to a real disk-
        //    path STRING via the captured ORIGINAL AdjustFileName (safe — string
        //    only, no handle, no CRT), then opens THAT path on kcdx's CRT and
        //    mints a kcdx handle. The miss thunks RESOLUTION, never the OPEN.
        AdjustFileNameOrigFn_t origResolve =
            g_originalAdjustFileName.load(std::memory_order_acquire);
        if (!origResolve) {
            LOG_ERROR_KV(kCat, "open_no_original_resolver",
                kcdx::log::KV::BareStr("slot", whichSlot),
                kcdx::log::KV("vpath", vpath),
                kcdx::log::KV::BareStr("detail",
                    "an index-miss open needs the captured original "
                    "AdjustFileName to resolve the long-tail name to a disk "
                    "path, but it is null (the swap did not capture slot 1) — "
                    "the open fails loud rather than guessing a path."));
            return 0;
        }
        char resolveBuf[kMaxPath];
        resolveBuf[0] = '\0';
        // The original resolver writes the concrete path into resolveBuf and
        // returns a char* to it (the engine's return==outBuf convention).
        void* r = origResolve(self, pName, resolveBuf, nFlags);
        const char* resolvedC = r ? static_cast<const char*>(r) : resolveBuf;
        if (!resolvedC || !resolvedC[0]) {
            // The original resolved nothing — the engine itself would open
            // nothing. Return 0 (a failed open), loud, NOT a silent broken
            // handle (AP14). (A non-existent read target legitimately resolves
            // to a path that then fails to open; that case lands in the loose
            // open below with its own loud warn.)
            LOG_WARN_KV(kCat, "open_miss_unresolved",
                kcdx::log::KV::BareStr("slot", whichSlot),
                kcdx::log::KV("vpath", vpath),
                kcdx::log::KV::BareStr("detail",
                    "the original AdjustFileName resolved the long-tail name to "
                    "an empty path — no open performed (the engine would open "
                    "nothing here too)."));
            return 0;
        }
        resolvedDisk = resolvedC;
        handle = OpenLooseAndMint(resolvedDisk, szMode, whichSlot, key.c_str());
    }

    // Write the resolved path into the FOpenRaw caller buffer (clamped). A real
    // path is well under the cap; an over-cap path is truncated LOUD and the
    // open still proceeds (the handle is what FOpenRaw returns; the resolved-name
    // copy is an auxiliary out-param). Mirrors the HOOK-1 bounded-write shape.
    if (resolvedOut && resolvedCap > 0) {
        const int cap = resolvedCap < static_cast<int>(kMaxPath)
                            ? resolvedCap : static_cast<int>(kMaxPath);
        const int written = std::snprintf(resolvedOut, static_cast<size_t>(cap),
                                          "%s", resolvedDisk.c_str());
        if (written < 0 || written >= cap) {
            LOG_WARN_KV(kCat, "resolved_name_over_cap",
                kcdx::log::KV::BareStr("slot", whichSlot),
                kcdx::log::KV("cap", static_cast<long long>(cap)),
                kcdx::log::KV("disk", resolvedDisk));
        }
    }

    if (handle != 0) {
        bool expected = false;
        if (g_loggedFirstOpen.compare_exchange_strong(expected, true,
                                                      std::memory_order_relaxed)) {
            LOG_DEBUG_KV(kCat, "kcdx_open_first",
                kcdx::log::KV::BareStr("slot", whichSlot),
                kcdx::log::KV("vpath", key),
                kcdx::log::KV("disk", resolvedDisk),
                kcdx::log::KV::BareStr("detail",
                    "kcdx FOpen minted a kcdx handle on kcdx's CRT — the engine "
                    "holds an opaque kcdx handle-id and operates it ONLY through "
                    "kcdx's read slots. No engine-CRT handle exists."));
        }
    }
    return handle;
}

}  // namespace

void SetOriginalAdjustFileName(AdjustFileNameOrigFn_t fn) {
    g_originalAdjustFileName.store(fn, std::memory_order_release);
}

void* kcdx_AdjustFileName(void* self, const char* pName, void* outBuf,
                          uint32_t nFlags) {
    // A null pName/outBuf → defer to the original (it owns its null-arg
    // contract); kcdx cannot resolve into a null buffer.
    if (!pName || !outBuf) {
        AdjustFileNameOrigFn_t orig =
            g_originalAdjustFileName.load(std::memory_order_acquire);
        return orig ? orig(self, pName, outBuf, nFlags) : nullptr;
    }

    const std::string key = asset_overlay::NormalizeVPath(pName);
    const ByteSource* bs = ResolveVPath(GetBuiltIndex(), key);

    char* out = static_cast<char*>(outBuf);

    // ASSET HIT, LOOSE source — resolve to the override file's concrete disk
    // path. This is the proven HOOK-1 shape: the engine's return==outBuf
    // convention, so a return-consuming caller (a GetFileSize-by-name, or a
    // caller that opens the returned path) gets the loose override's real path.
    //
    // A PAK hit and a MISS both fall to the ORIGINAL resolver below (NOT this
    // arm): slot-1 returns a STRING, and a pak-resident asset has NO loose disk
    // path to return — returning the PAK FILE path would be wrong (a caller that
    // re-opened that string would open the pak file itself as a loose file). The
    // pak bytes are served at FOPEN time, where kcdx_FOpen independently
    // index-resolves the original vpath and serves the pak entry from kcdx's own
    // reader — regardless of what slot-1 returned. So slot-1 for a pak asset
    // only needs the engine's normal resolved string, which the original
    // resolver produces correctly (it touches no handle, no CRT — §5 safe). This
    // is the conservative correct reading where §5 is silent on the slot-1
    // return for a pak-resident asset: serve the loose path where one exists,
    // thunk resolution otherwise; FOpen owns the pak byte-serve either way.
    if (bs && bs->kind == ByteSource::Kind::Loose) {
        const int written = std::snprintf(out, kMaxPath, "%s",
                                          bs->diskPath.c_str());
        if (written < 0 || static_cast<size_t>(written) >= kMaxPath) {
            // Over-cap — decline this resolution loud and fall to the original
            // (it resolves from pName itself), NOT a truncated mis-serve (AP14).
            LOG_WARN_KV(kCat, "resolve_path_over_cap",
                kcdx::log::KV("vpath", key),
                kcdx::log::KV("disk", bs->diskPath));
            AdjustFileNameOrigFn_t orig =
                g_originalAdjustFileName.load(std::memory_order_acquire);
            return orig ? orig(self, pName, outBuf, nFlags) : out;
        }
        bool expected = false;
        if (g_loggedFirstResolve.compare_exchange_strong(
                expected, true, std::memory_order_relaxed)) {
            LOG_DEBUG_KV(kCat, "kcdx_resolve_first",
                kcdx::log::KV("vpath", key),
                kcdx::log::KV("disk", bs->diskPath),
                kcdx::log::KV::BareStr("kind", "loose"));
        }
        return out;
    }

    // MISS (or a PAK hit) — §5: NOT a hand-back. kcdx still resolves EVERY name.
    // Thunk the
    // captured ORIGINAL AdjustFileName for the long tail (a save, config, cache,
    // write target). The original returns a STRING and operates only the
    // engine object's intact data members (search-path vector, alias table,
    // pakPriority cvar — preserved by the vtable-pointer-only swap); it touches
    // NO handle and NO CRT, so it cannot reintroduce the cross-CRT straddle.
    AdjustFileNameOrigFn_t orig =
        g_originalAdjustFileName.load(std::memory_order_acquire);
    if (!orig) {
        LOG_ERROR_KV(kCat, "resolve_no_original",
            kcdx::log::KV("vpath", key),
            kcdx::log::KV::BareStr("detail",
                "an index-miss resolution needs the captured original "
                "AdjustFileName, but it is null (the swap did not capture slot "
                "1) — returning the input path unresolved (the engine's "
                "callers handle a null/identity resolution). This is a swap "
                "wiring defect, surfaced loud."));
        // Best-effort: echo pName into outBuf so a caller reading outBuf does
        // not read uninitialized memory; the engine treats an unresolved path
        // as the literal name.
        std::snprintf(out, kMaxPath, "%s", pName);
        return out;
    }
    return orig(self, pName, outBuf, nFlags);
}

void* kcdx_FOpen(void* self, const char* pName, const char* szMode,
                 uint32_t nFlags) {
    if (!pName) return nullptr;  // the engine's null-arg contract → null open
    const KcdxHandle h = OpenResolvedAndMint(self, pName, szMode, nFlags,
                                             "FOpen", /*resolvedOut=*/nullptr,
                                             /*resolvedCap=*/0);
    return reinterpret_cast<void*>(h);  // 0 (failed open) or the kcdx handle-id
}

void* kcdx_FOpenRaw(void* self, const char* pName, const char* szMode,
                    void* outResolvedBuf, int bufCap) {
    if (!pName) return nullptr;
    const KcdxHandle h = OpenResolvedAndMint(
        self, pName, szMode, /*nFlags=*/0, "FOpenRaw",
        static_cast<char*>(outResolvedBuf), bufCap);
    return reinterpret_cast<void*>(h);
}

}  // namespace kcdx::fs_takeover
