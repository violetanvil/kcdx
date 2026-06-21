#include "read_slots.h"

#include <climits>  // SIZE_MAX (size*count overflow guard)
#include <cstdio>   // SEEK_SET

#include "boot_trace.h"  // FS_BOOT_TRACE — read-family boot-window trace (KI-0026 PROBE K)
#include "file_handle.h"
#include "../log.h"

// The kcdx READ-family slot impls — see read_slots.h for the slot set, the
// per-slot FINDINGS ABI, and the cross-CRT invariant. Each impl is a thin shim:
// extract the kcdx handle from its member-call arg, forward to the file_handle
// pool op (which does the I/O on kcdx's CRT). The pool validates the kcdx tag
// and fails loud on a foreign/closed handle (AP14) — these shims never
// second-guess it.

namespace kcdx::fs_takeover {

namespace {

constexpr const char* kCat = "FS_READ";

// Reinterpret a void*/FILE*/tagged-int handle arg as a KcdxHandle. Under total
// ownership every handle the read family sees is kcdx-minted (FOpen always mints
// — §5); the pool op validates the tag and fails loud if it is not (file_handle).
KcdxHandle H(void* handle) {
    return reinterpret_cast<KcdxHandle>(handle);
}

}  // namespace

// === Core read family ======================================================

// slot 38 — FReadRaw-by-pak-index (BODY-VERIFIED). The engine's body dispatches
// on `taggedHandle-1 < pakEntryCount` (pak arm) vs an OS FILE* (OS arm); under
// total ownership the handle is ALWAYS a kcdx handle, so the pool's own tag
// dispatch (loose vs pak) serves it. fwrite-shaped read: buf/size/count, the
// tagged handle last. Returns bytes read (the engine reads count*size bytes).
size_t kcdx_FReadRaw_byPakIndex(void* self, void* buf, size_t size,
                                size_t count, long long taggedHandle) {
    (void)self;
    // SOURCE: FINDINGS slot-38 body — FUN_180461304(p1,p2,p3,p4,p5); p5 (arg 5,
    // `taggedHandle`) is the tagged handle/pak-index. This is the ONE read slot
    // whose handle is arg 5, not the FILE*-position arg its siblings use — a
    // wrong arg reads a garbage handle (AP19). The handle is arg 5; correct.
    if (count != 0 && size > SIZE_MAX / count) {
        // size*count would overflow → a wrapped small/zero `want` is a silent
        // short read reported as success (AP14). The multiplicands come from an
        // untrusted engine member call (input-validation.md); fail LOUD, return 0
        // (the engine sees a failed read, never a silent wrong byte count).
        LOG_ERROR_KV(kCat, "read_size_overflow",
            kcdx::log::KV::BareStr("slot", "FReadRaw_byPakIndex"),
            kcdx::log::KV("size", static_cast<long long>(size)),
            kcdx::log::KV("count", static_cast<long long>(count)));
        return 0;
    }
    bool ok = false;
    const size_t want = size * count;
    const size_t got = Read(static_cast<KcdxHandle>(taggedHandle), buf, want, ok);
    // FS-op trace AFTER the read — names the file (resolved from the handle) +
    // bytes wanted/got + result, so a wrong/short/failed read is readable in the
    // log (was: opaque handle only). FS_BOOT_TRACE (PROBE K).
    TraceRead("FReadRaw_byPakIndex", taggedHandle,
              static_cast<long long>(want), static_cast<long long>(got), ok);
    // The engine's FReadRaw returns the byte count read (front-3 body — the OS
    // arm returns fread's element count*size). Return bytes read; a short read
    // at EOF is normal (ok stays true). ok==false means a bad handle / CRT error
    // (already logged loud by the pool).
    return got;
}

// slot 39 — FReadRaw (BODY-VERIFIED). The body fseeks the handle to 0, then
// reads `size` bytes (count=size, element=1) — it reads the WHOLE source from
// the start (decomp: `fseek(p4,0,0); FUN_1804d7ab4(p2,1,p3,p4)`). Replicate:
// seek-to-0 then read `size` bytes on kcdx's CRT.
size_t kcdx_FReadRaw(void* self, void* buf, size_t size, void* handle) {
    (void)self;
    const KcdxHandle h = H(handle);
    Seek(h, 0, SEEK_SET);  // the body's leading fseek-to-0
    bool ok = false;
    const size_t got = Read(h, buf, size, ok);
    TraceRead("FReadRaw", static_cast<long long>(h),
              static_cast<long long>(size), static_cast<long long>(got), ok);  // FS_BOOT_TRACE
    return got;
}

// slot 40 — FGetCachedFileData (BODY-VERIFIED). Returns the cached whole-file
// buffer; writes the size into *outSizeDst. The pool serves a pak source's
// inflated buffer zero-copy, a loose source's whole-file cache.
void* kcdx_FGetCachedFileData(void* self, void* handle, long long* outSizeDst) {
    (void)self;
    long long sz = 0;
    const void* data = GetCachedFileData(H(handle), &sz);
    if (outSizeDst) *outSizeDst = sz;
    // FS-op trace AFTER — want=-1 (whole-file fetch, no count), got=sz, ok=data!=null.
    TraceRead("FGetCachedFileData", static_cast<long long>(H(handle)),
              -1, sz, data != nullptr);  // FS_BOOT_TRACE
    return const_cast<void*>(data);  // the engine reads it; the pool owns it (stable until Close)
}

// slot 41 — FWrite (LEAF-IDENTIFIED, fwrite-shaped). Writes count*size bytes from
// buf to the loose handle on kcdx's CRT. Returns bytes written.
size_t kcdx_FWrite(void* self, const void* buf, size_t size, size_t count,
                   void* handle) {
    (void)self;
    if (count != 0 && size > SIZE_MAX / count) {
        // size*count would overflow → a wrapped small/zero count is a silent
        // short write reported as success (AP14). Untrusted engine multiplicands
        // (input-validation.md); fail LOUD, return 0 (the engine sees a failed
        // write, never a silent wrong byte count).
        LOG_ERROR_KV(kCat, "write_size_overflow",
            kcdx::log::KV::BareStr("slot", "FWrite"),
            kcdx::log::KV("size", static_cast<long long>(size)),
            kcdx::log::KV("count", static_cast<long long>(count)));
        return 0;
    }
    bool ok = false;
    const size_t want = size * count;
    const size_t wrote = Write(H(handle), buf, want, ok);
    TraceRead("FWrite", static_cast<long long>(H(handle)),
              static_cast<long long>(want), static_cast<long long>(wrote), ok);  // FS_BOOT_TRACE
    return wrote;
}

// slot 53 — FSeek (LEAF-IDENTIFIED, fseek-shaped). Seeks the handle; returns 0
// on success, non-zero on failure (libc fseek convention).
int kcdx_FSeek(void* self, void* handle, long offset, int origin) {
    (void)self;
    const int rc = Seek(H(handle), static_cast<long long>(offset), origin);
    TraceRead("FSeek", static_cast<long long>(H(handle)), -1, -1, rc == 0);  // FS_BOOT_TRACE
    return rc;
}

// slot 54 — FTell (LEAF-IDENTIFIED, _ftelli64-shaped). Current position, or -1.
long long kcdx_FTell(void* self, void* handle) {
    (void)self;
    const KcdxHandle h = H(handle);
    const long long pos = Tell(h);
    TraceRead("FTell", static_cast<long long>(h), -1, pos, pos >= 0);  // FS_BOOT_TRACE
    return pos;
}

// slot 55 — FClose (LEAF-IDENTIFIED, fclose-shaped). Closes the handle on kcdx's
// CRT (kcdx fclose for loose; drop the pak buffer). Returns 0 / EOF.
int kcdx_FClose(void* self, void* handle) {
    (void)self;
    // Trace BEFORE Close — after close the handle is dead and the vpath cannot
    // resolve (VpathForHandle rejects a closed slot). FS_BOOT_TRACE.
    TraceRead("FClose", static_cast<long long>(H(handle)), -1, -1, true);
    return Close(H(handle));
}

// slot 56 — FEof (LEAF-IDENTIFIED, feof-shaped). Non-zero at EOF, 0 otherwise.
int kcdx_FEof(void* self, void* handle) {
    (void)self;
    TraceRead("FEof", static_cast<long long>(H(handle)), -1, -1, true);  // FS_BOOT_TRACE
    return Eof(H(handle)) ? 1 : 0;
}

// === Read variants =========================================================

// slot 43 — FGets (LEAF-IDENTIFIED, fgets-shaped). Returns buf / nullptr.
char* kcdx_FGets(void* self, char* buf, int maxCount, void* handle) {
    (void)self;
    char* r = Gets(H(handle), buf, maxCount);
    TraceRead("FGets", static_cast<long long>(H(handle)), maxCount, -1, r != nullptr);  // FS_BOOT_TRACE
    return r;
}

// slot 44 — FGetc (LEAF-IDENTIFIED, fgetc-shaped). Next byte as int, or EOF(-1).
int kcdx_FGetc(void* self, void* handle) {
    (void)self;
    const int c = Getc(H(handle));
    TraceRead("FGetc", static_cast<long long>(H(handle)), -1, -1, c != -1);  // FS_BOOT_TRACE
    return c;
}

// slot 46 — FGetSize-by-handle (BODY-VERIFIED, FUN_180460c08, RVA 0x460C08).
// Returns the file's byte SIZE, NOT a _fileno. The body is handle-tag dispatched:
// the PAK arm returns the entry's stored uncompressed-size field; the OS arm does
// _fileno → _fstat64i32 → st_size (the _fileno is only the first hop to size).
// The engine's FRead OS arm (slot 40, +0x140) calls THIS slot, stores the return
// as the size, then reads that many bytes — so a wrong return here makes the
// engine size its read wrong (KI-0026: the prior _fileno impl returned -1 for a
// pak handle → engine read size=-1 → "couldn't get length" → 0xC8). The earlier
// "fileno (LEAF-IDENTIFIED, _fileno-shaped)" label was the leaf-mislabel: the
// import-table _fileno discriminator was read as the role without reading that it
// is a sub-step of the size computation (AP19). By-HANDLE size; distinct from the
// by-NAME GetFileSize (slot 45, +0x168, kcdx_GetFileSize).
long long kcdx_FGetSize(void* self, void* handle) {
    (void)self;
    const KcdxHandle h = H(handle);
    const long long sz = FileSize(h);
    TraceRead("FGetSize", static_cast<long long>(h), -1, sz, sz >= 0);  // FS_BOOT_TRACE
    return sz;
}

// slot 47 — FUngetc (LEAF-IDENTIFIED, ungetc-shaped). The char / EOF(-1).
int kcdx_FUngetc(void* self, int ch, void* handle) {
    (void)self;
    TraceRead("FUngetc", static_cast<long long>(H(handle)), -1, -1, true);  // FS_BOOT_TRACE
    return Ungetc(H(handle), ch);
}

// slot 57 — FError (LEAF-IDENTIFIED, ferror-shaped). 0 = no error.
int kcdx_FError(void* self, void* handle) {
    (void)self;
    TraceRead("FError", static_cast<long long>(H(handle)), -1, -1, true);  // FS_BOOT_TRACE
    return Error(H(handle));
}

// slot 58 — FGetErrno (LEAF-IDENTIFIED, _errno-shaped). The stream errno.
int kcdx_FGetErrno(void* self, void* handle) {
    (void)self;
    TraceRead("FGetErrno", static_cast<long long>(H(handle)), -1, -1, true);  // FS_BOOT_TRACE
    return GetErrno(H(handle));
}

// slot 59 — FFlush (LEAF-IDENTIFIED, fflush-shaped). 0 on success.
int kcdx_FFlush(void* self, void* handle) {
    (void)self;
    TraceRead("FFlush", static_cast<long long>(H(handle)), -1, -1, true);  // FS_BOOT_TRACE
    return Flush(H(handle));
}

// slot 66 — FGetModificationTime (BODY-VERIFIED). 2-arg; returns the packed
// FILETIME last-write time (__int64). RDX=handle, RAX=FILETIME.
long long kcdx_FGetModificationTime(void* self, void* handle) {
    (void)self;
    TraceRead("FGetModificationTime", static_cast<long long>(H(handle)), -1, -1, true);  // FS_BOOT_TRACE
    return GetModificationTime(H(handle));
}

}  // namespace kcdx::fs_takeover
