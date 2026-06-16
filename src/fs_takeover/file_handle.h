#pragma once

// The kcdx file-handle pool — the shared state the OPEN slots mint into and the
// READ slots operate (file-system-takeover design §4.4 + §5).
//
// Under total ownership kcdx's FOpen/FOpenRaw ALWAYS mint a kcdx handle (asset,
// non-asset, write alike — §5), and kcdx's read family (FRead/FSeek/FClose/…)
// operates it ENTIRELY on kcdx's own CRT. The engine never inspects or operates
// the handle — it only holds the opaque value kcdx returned and passes it back.
//
// === The kcdx handle-id representation (SETTLED — §4.4 constraint) ===========
//
// A kcdx handle is a SMALL TAGGED INTEGER returned as the FOpen result (a
// void*-width value the engine stores + hands back). Two requirements the §4.4
// "tagged-union contract" imposes, both met by the encoding below:
//
//   1. OPAQUE to the engine, operated only by kcdx's read slots — satisfied by
//      kcdx owning every read slot (the §4.4 load-bearing constraint: every
//      handle-operating slot is KCDX, never THUNK).
//   2. DISTINGUISHABLE in the SAME way the engine's tagged union is — the engine
//      read family's dispatch test is `handle-1 < pakEntryCount` → the pak arm
//      (a small index+1), else the OS arm (a real FILE*-class pointer). A kcdx
//      handle must NOT alias either engine arm, or a stray engine code path that
//      still ran the tag test (it cannot under total ownership, but the
//      contract is belt-and-suspenders) would mis-route it.
//
// THE ENCODING: a kcdx handle = `(id << 1) | 1` where `id` is the 1-based pool
// slot index. The low bit is the kcdx TAG (always 1 — every kcdx handle is ODD).
//   - A real engine FILE* is 16-byte aligned → low bits 0 → never odd → a kcdx
//     handle is never mistaken for the engine OS arm, and vice-versa.
//   - The engine pak arm is a small `index+1` (< pakEntryCount, a few thousand);
//     a kcdx handle is `(id<<1)|1`, an odd value whose id starts at 1 → smallest
//     kcdx handle is 3. It could numerically fall under pakEntryCount, BUT the
//     engine pak arm is never reached for a kcdx handle: kcdx owns the read
//     family, so the read slots route on the kcdx tag (low bit), not the engine
//     test. The odd-tag is what lets a kcdx read slot recognize its OWN handle
//     deterministically; the no-FILE*-aliasing property is what keeps it from a
//     stray engine OS-arm op. Both arms covered.
//
// WHY a tagged index, not a kcdx-tagged raw pointer into the pool: a small
// integer id is allocation-free to mint (an atomic bump + slot store), is
// trivially validated on the read side (range + tag + liveness), and never
// dangles as a raw pointer would if the pool storage ever reallocated. The id
// indirects through the pool's fixed slot array, decoupling the handle value
// from the storage address. (§4.4 explicitly sanctions either "a small kcdx
// handle id, or a kcdx-tagged pointer into a kcdx handle pool" — the id is the
// simpler, dangle-proof shape.)
//
// === Cross-CRT invariant (§9) ===============================================
//
// Every byte of an open byte-source lives inside this pool's slot state, on
// kcdx's CRT: a Loose source holds a kcdx `_wfopen`'d FILE* (read/sought/closed
// with kcdx fread/_fseeki64/fclose); a Pak source holds an in-memory inflated
// buffer (produced by pak_reader::ReadPakEntry on kcdx's CRT) plus a cursor. No
// engine ucrtbase call ever touches a slot. The handle the engine holds is a
// bare integer — there is nothing for the engine's CRT to operate even if it
// tried.
//
// === Concurrency (concurrency.md) ===========================================
//
// The engine opens/reads/closes from multiple threads, so the pool is shared
// cross-thread state. It is guarded by ONE mutex (the pool lock) — the simplest
// correct shape for a map the open path inserts into and the read path mutates
// (the cursor advances on each read). Lock-order: the pool lock is a LEAF — no
// other kcdx lock is acquired while it is held (the read/open slot bodies do
// their CRT I/O OUTSIDE the lock, holding it only to look up + copy the slot
// state needed, or under it for the brief cursor mutate). Documented at the
// acquire sites in file_handle.cpp.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace kcdx::fs_takeover {

// What an open kcdx handle points at — the byte-source state the read family
// operates. A tagged union of the two source kinds (mirrors ByteSource, but
// holds the OPEN state, not the resolution record).
struct OpenFile {
    enum class Kind { Loose, Pak };
    Kind kind = Kind::Loose;

    // True once Close() has run on this slot — a use-after-close is rejected
    // loud (AP14) rather than operating a stale FILE*/buffer.
    bool closed = false;

    // --- Loose (kind == Loose): a kcdx-CRT FILE*, read on kcdx's CRT. --------
    // Opened by FOpen via kcdx _wfopen_s; every read/seek/tell/close on it is a
    // kcdx CRT call (fread/_fseeki64/_ftelli64/fclose). The engine never sees it.
    FILE* fp = nullptr;

    // --- Pak (kind == Pak): an in-memory inflated buffer + a read cursor. -----
    // The whole entry's uncompressed bytes, produced by pak_reader::ReadPakEntry
    // (kcdx CRT, kcdx allocator) at OPEN time. The read family seeks + copies
    // out of this buffer — no pak file is re-touched per read, no engine ZipDir
    // in the path. `cursor` is the current read position (bytes from the start).
    std::vector<uint8_t> pakBytes;
    size_t cursor = 0;
    // For diagnostics + the EOF/Tell contract: the byte-source's full size.
    // (== pakBytes.size() for a Pak source; for Loose it is unused — the FILE*
    // is the source of truth there.)
    uint64_t size = 0;
};

// The opaque handle the engine holds. A void*-width odd-tagged integer (the
// encoding above). Minted by the open slots, operated by the read slots, never
// inspected by the engine.
using KcdxHandle = uintptr_t;

// True iff `h` is a kcdx-minted handle (the low TAG bit is set). A real engine
// FILE* (16-byte aligned) and the engine pak arm (an even index+1 is possible,
// but the read family never tag-tests a kcdx handle against the engine arm —
// see the header note) both fail this. Cheap: one bit test, no lock.
bool IsKcdxHandle(KcdxHandle h);

// Mint a handle for a freshly-opened LOOSE source (takes ownership of `fp`,
// closes it on Close()). Returns the opaque KcdxHandle (odd-tagged). Returns 0
// on pool failure (logged loud) — 0 is never a valid kcdx handle (its tag bit
// is clear), so a caller treats 0 as "mint failed" and fails the open loud.
KcdxHandle MintLoose(FILE* fp);

// Mint a handle for a freshly-opened PAK source (takes ownership of the
// inflated byte buffer by move). Returns the opaque KcdxHandle, or 0 on pool
// failure (logged loud).
KcdxHandle MintPak(std::vector<uint8_t>&& bytes);

// === The read-family operations on a kcdx handle (all on kcdx's CRT) ========
//
// Each takes a KcdxHandle, maps it to its OpenFile slot under the pool lock,
// performs the op on kcdx's CRT (Loose → fread/_fseeki64/…; Pak → buffer
// copy/cursor), and returns a result the slot impl threads back to the engine.
// A handle that is not a live kcdx handle (bad tag, out of range, already
// closed) makes each op FAIL LOUD (a logged error + a fail return) — never a
// silent wrong-bytes/empty/no-op (AP14).

// Read up to `bytes` bytes into `dst`. Returns the number of bytes actually
// read (0 at EOF or on a hard error — the error path logs; a short read at true
// EOF is normal and does NOT log). The fread/cursor-advance happens on kcdx's
// CRT. `ok` is set false on a bad/closed handle or a CRT error (distinct from a
// legitimate 0-byte EOF read, where ok stays true).
size_t Read(KcdxHandle h, void* dst, size_t bytes, bool& ok);

// Seek the handle's read position. `origin` is the libc whence (SEEK_SET=0 /
// SEEK_CUR=1 / SEEK_END=2). Returns 0 on success, non-zero on failure (bad
// handle or CRT seek error — logged). Loose → _fseeki64; Pak → cursor arithmetic
// bounds-checked against the buffer.
int Seek(KcdxHandle h, long long offset, int origin);

// Current read position (bytes from the start). Returns -1 on a bad/closed
// handle or a CRT tell error (logged). Loose → _ftelli64; Pak → the cursor.
long long Tell(KcdxHandle h);

// True iff the handle is at end-of-file. Returns true (the safe "stop reading"
// answer) on a bad/closed handle too — but logs it, so a bug surfaces while the
// engine's read loop terminates rather than spinning. Loose → feof; Pak →
// cursor >= size.
bool Eof(KcdxHandle h);

// Write `bytes` bytes from `src` (Loose write targets only — a Pak source is
// read-only; a write to a Pak handle fails loud). Returns bytes written; sets
// `ok` false on a bad/closed/read-only handle or a CRT error. Loose → fwrite.
size_t Write(KcdxHandle h, const void* src, size_t bytes, bool& ok);

// Flush a Loose write handle (fflush). No-op success for a Pak source (nothing
// to flush). Returns 0 on success, non-zero (logged) on a bad handle / CRT error.
int Flush(KcdxHandle h);

// The file's last error indicator (ferror-equivalent). 0 = no error. Loose →
// ferror; Pak → 0 (an in-memory buffer has no stream error state; a failed
// bounds check already failed loud at the op). A bad/closed handle returns
// non-zero (logged).
int Error(KcdxHandle h);

// The file's errno-equivalent. Loose → the CRT errno captured for this stream's
// last failing op (best-effort: the process errno at the last op); Pak → 0. A
// bad/closed handle returns a non-zero sentinel (logged).
int GetErrno(KcdxHandle h);

// Read one character (fgetc-equivalent): the next byte as an int, or EOF (-1) at
// end-of-stream / on error (a hard error logs; true EOF does not). Loose →
// fgetc; Pak → the next buffer byte, advancing the cursor.
int Getc(KcdxHandle h);

// Push one character back onto the stream (ungetc-equivalent). Returns the
// char on success, EOF (-1) on failure (logged). Loose → ungetc; Pak → step the
// cursor back one (bounds-checked).
int Ungetc(KcdxHandle h, int ch);

// Read a line into `dst` (fgets-equivalent): up to maxCount-1 bytes or through
// the next '\n' (inclusive), NUL-terminated. Returns `dst` on success, nullptr
// at EOF-before-any-byte / on error (logged on a hard error). Loose → fgets;
// Pak → copy out of the buffer up to '\n'/maxCount.
char* Gets(KcdxHandle h, char* dst, int maxCount);

// The OS file descriptor (fileno-equivalent), for a Loose handle whose FILE* has
// one. Returns -1 for a Pak source (no fd — an in-memory buffer) or a bad
// handle (logged). The engine treats -1 as "no fd"; this matches the engine's
// own fileno-on-a-pak-entry behavior (a pak entry has no fd either).
int Fileno(KcdxHandle h);

// The file's last-write time as a packed FILETIME (__int64), the slot-66
// FGetModificationTime contract. Loose → GetFileTime via the FILE*'s fd; Pak →
// 0 (an in-memory inflated buffer carries no on-disk mtime in this step — the
// pak entry's stored DOS time is not threaded through the index; a future step
// can carry it). A bad/closed handle returns 0 (logged).
long long GetModificationTime(KcdxHandle h);

// Read the cached whole-file buffer + size (FGetCachedFileData, slot 40). Returns
// a pointer to the source's full bytes and writes the size into *outSize. For a
// Pak source this is the inflated buffer directly (zero-copy). For a Loose
// source it reads the whole file into the slot's own cache buffer on first call
// (kcdx CRT) and returns that. Returns nullptr + *outSize=0 on a bad handle or a
// read failure (logged). The returned pointer is owned by the pool slot (stable
// until Close) — the engine reads, never frees, it.
const void* GetCachedFileData(KcdxHandle h, long long* outSize);

// Close the handle: close the Loose FILE* (kcdx fclose) or drop the Pak buffer,
// mark the slot closed, and free the slot for reuse. Returns 0 on success,
// EOF (-1) on a bad/already-closed handle (logged). Idempotent-safe: a
// double-close is a logged no-op, never a double-fclose.
int Close(KcdxHandle h);

}  // namespace kcdx::fs_takeover
