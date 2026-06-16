#include "read_slots.h"

#include <climits>  // SIZE_MAX (size*count overflow guard)
#include <cstdio>   // SEEK_SET

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
    return Read(h, buf, size, ok);
}

// slot 40 — FGetCachedFileData (BODY-VERIFIED). Returns the cached whole-file
// buffer; writes the size into *outSizeDst. The pool serves a pak source's
// inflated buffer zero-copy, a loose source's whole-file cache.
void* kcdx_FGetCachedFileData(void* self, void* handle, long long* outSizeDst) {
    (void)self;
    long long sz = 0;
    const void* data = GetCachedFileData(H(handle), &sz);
    if (outSizeDst) *outSizeDst = sz;
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
    return Write(H(handle), buf, size * count, ok);
}

// slot 53 — FSeek (LEAF-IDENTIFIED, fseek-shaped). Seeks the handle; returns 0
// on success, non-zero on failure (libc fseek convention).
int kcdx_FSeek(void* self, void* handle, long offset, int origin) {
    (void)self;
    return Seek(H(handle), static_cast<long long>(offset), origin);
}

// slot 54 — FTell (LEAF-IDENTIFIED, _ftelli64-shaped). Current position, or -1.
long long kcdx_FTell(void* self, void* handle) {
    (void)self;
    return Tell(H(handle));
}

// slot 55 — FClose (LEAF-IDENTIFIED, fclose-shaped). Closes the handle on kcdx's
// CRT (kcdx fclose for loose; drop the pak buffer). Returns 0 / EOF.
int kcdx_FClose(void* self, void* handle) {
    (void)self;
    return Close(H(handle));
}

// slot 56 — FEof (LEAF-IDENTIFIED, feof-shaped). Non-zero at EOF, 0 otherwise.
int kcdx_FEof(void* self, void* handle) {
    (void)self;
    return Eof(H(handle)) ? 1 : 0;
}

// === Read variants =========================================================

// slot 43 — FGets (LEAF-IDENTIFIED, fgets-shaped). Returns buf / nullptr.
char* kcdx_FGets(void* self, char* buf, int maxCount, void* handle) {
    (void)self;
    return Gets(H(handle), buf, maxCount);
}

// slot 44 — FGetc (LEAF-IDENTIFIED, fgetc-shaped). Next byte as int, or EOF(-1).
int kcdx_FGetc(void* self, void* handle) {
    (void)self;
    return Getc(H(handle));
}

// slot 46 — fileno (LEAF-IDENTIFIED, _fileno-shaped). The fd (loose) or -1 (pak).
int kcdx_Fileno(void* self, void* handle) {
    (void)self;
    return Fileno(H(handle));
}

// slot 47 — FUngetc (LEAF-IDENTIFIED, ungetc-shaped). The char / EOF(-1).
int kcdx_FUngetc(void* self, int ch, void* handle) {
    (void)self;
    return Ungetc(H(handle), ch);
}

// slot 57 — FError (LEAF-IDENTIFIED, ferror-shaped). 0 = no error.
int kcdx_FError(void* self, void* handle) {
    (void)self;
    return Error(H(handle));
}

// slot 58 — FGetErrno (LEAF-IDENTIFIED, _errno-shaped). The stream errno.
int kcdx_FGetErrno(void* self, void* handle) {
    (void)self;
    return GetErrno(H(handle));
}

// slot 59 — FFlush (LEAF-IDENTIFIED, fflush-shaped). 0 on success.
int kcdx_FFlush(void* self, void* handle) {
    (void)self;
    return Flush(H(handle));
}

// slot 66 — FGetModificationTime (BODY-VERIFIED). 2-arg; returns the packed
// FILETIME last-write time (__int64). RDX=handle, RAX=FILETIME.
long long kcdx_FGetModificationTime(void* self, void* handle) {
    (void)self;
    return GetModificationTime(H(handle));
}

}  // namespace kcdx::fs_takeover
