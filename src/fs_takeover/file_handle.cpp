#include "file_handle.h"

#include <windows.h>  // GetFileTime / _get_osfhandle backing for slot-66 mtime

#include <cerrno>
#include <cstring>
#include <io.h>       // _fileno / _get_osfhandle
#include <mutex>

#include "../log.h"

// The kcdx file-handle pool — see file_handle.h for the handle encoding, the
// cross-CRT invariant (§9), and the concurrency model. Every CRT call here is
// kcdx's statically-linked CRT (fread/_fseeki64/fclose/…); no engine ucrtbase
// frame ever operates a pool slot.

namespace kcdx::fs_takeover {

namespace {

constexpr const char* kCat = "FS_HANDLE";

// The kcdx handle TAG: the low bit of every kcdx-minted handle. A real engine
// FILE* is 16-byte aligned (low bits 0) and the engine pak arm is never
// tag-tested against a kcdx handle (kcdx owns the read family), so the odd tag
// uniquely identifies a kcdx handle to kcdx's OWN read slots. (Handle encoding
// rationale: file_handle.h header.)
constexpr uintptr_t kKcdxTag = 0x1;

// The pool: a fixed slot vector + a freelist of reusable indices. The handle's
// id is the 1-based slot index; slot[id-1] holds the OpenFile. Process-lifetime
// (the engine holds handles across the whole session); guarded by g_poolLock.
//
// LOCK-ORDER: g_poolLock is a LEAF lock — no other kcdx lock is taken while it
// is held. The slot impls do their long CRT I/O OUTSIDE this lock where they
// can (copying the slot pointer out), and hold it only across a slot mutate.
// Here the ops hold it across the whole op for simplicity + correctness (the
// per-op CRT cost is a cold-path file read, not a hot in-process loop) — the
// pool never calls outward under the lock, so it cannot deadlock.
std::mutex g_poolLock;
std::vector<OpenFile> g_slots;        // index = id-1
std::vector<size_t>   g_freeList;     // reusable ids (1-based), LIFO

// Encode a 1-based slot id into the opaque odd-tagged handle.
KcdxHandle Encode(size_t id) {
    return (static_cast<uintptr_t>(id) << 1) | kKcdxTag;
}

// Decode a handle to its 1-based slot id, or 0 if `h` is not a kcdx handle.
size_t DecodeId(KcdxHandle h) {
    if ((h & kKcdxTag) == 0) return 0;  // not kcdx-tagged
    return static_cast<size_t>(h >> 1);
}

// Resolve a handle to its LIVE slot under the pool lock. Returns nullptr (and
// logs once at the call site's discretion) on a bad tag / out-of-range id /
// already-closed slot. CALLER MUST HOLD g_poolLock.
OpenFile* SlotLocked(KcdxHandle h) {
    const size_t id = DecodeId(h);
    if (id == 0 || id > g_slots.size()) return nullptr;
    OpenFile& s = g_slots[id - 1];
    if (s.closed) return nullptr;
    return &s;
}

// Allocate a slot (reuse a freelist id, else grow). Returns the 1-based id, or 0
// if the pool could not grow (never expected — logged loud by the caller).
// CALLER MUST HOLD g_poolLock.
size_t AllocSlotLocked() {
    if (!g_freeList.empty()) {
        const size_t id = g_freeList.back();
        g_freeList.pop_back();
        g_slots[id - 1] = OpenFile{};  // fresh state for reuse
        return id;
    }
    g_slots.emplace_back();
    return g_slots.size();  // 1-based id == new size
}

// Log a bad-handle use once per call (these are error paths, not hot — a
// mis-routed handle is a real defect to surface, AP14).
void LogBadHandle(const char* op, KcdxHandle h) {
    LOG_ERROR_KV(kCat, "bad_handle",
        kcdx::log::KV::BareStr("op", op),
        kcdx::log::KV("handle", static_cast<uint64_t>(h)),
        kcdx::log::KV::BareStr("detail",
            "a read-family op received a value that is not a live kcdx handle "
            "(bad tag, out-of-range id, or already closed) — failing loud rather "
            "than operating a stale/foreign handle. Under total ownership every "
            "handle the read family sees is kcdx-minted; this is a defect."));
}

}  // namespace

bool IsKcdxHandle(KcdxHandle h) {
    return (h & kKcdxTag) != 0;
}

KcdxHandle MintLoose(FILE* fp) {
    if (!fp) return 0;
    std::lock_guard<std::mutex> lock(g_poolLock);
    const size_t id = AllocSlotLocked();
    if (id == 0) {
        LOG_ERROR_KV(kCat, "mint_failed",
            kcdx::log::KV::BareStr("kind", "loose"),
            kcdx::log::KV::BareStr("detail",
                "could not allocate a handle-pool slot for a loose open — the "
                "open fails loud (the caller returns a failed open, never a "
                "silent broken handle)."));
        return 0;
    }
    OpenFile& s = g_slots[id - 1];
    s.kind   = OpenFile::Kind::Loose;
    s.closed = false;
    s.fp     = fp;
    return Encode(id);
}

KcdxHandle MintPak(std::vector<uint8_t>&& bytes) {
    std::lock_guard<std::mutex> lock(g_poolLock);
    const size_t id = AllocSlotLocked();
    if (id == 0) {
        LOG_ERROR_KV(kCat, "mint_failed",
            kcdx::log::KV::BareStr("kind", "pak"),
            kcdx::log::KV::BareStr("detail",
                "could not allocate a handle-pool slot for a pak open — the open "
                "fails loud (the caller returns a failed open, never a silent "
                "broken handle)."));
        return 0;
    }
    OpenFile& s = g_slots[id - 1];
    s.kind     = OpenFile::Kind::Pak;
    s.closed   = false;
    s.pakBytes = std::move(bytes);
    s.cursor   = 0;
    s.size     = s.pakBytes.size();
    return Encode(id);
}

size_t Read(KcdxHandle h, void* dst, size_t bytes, bool& ok) {
    ok = true;
    if (!dst || bytes == 0) return 0;
    std::lock_guard<std::mutex> lock(g_poolLock);
    OpenFile* s = SlotLocked(h);
    if (!s) { LogBadHandle("read", h); ok = false; return 0; }

    if (s->kind == OpenFile::Kind::Loose) {
        // kcdx CRT fread — the loose source is read entirely on kcdx's CRT.
        const size_t n = std::fread(dst, 1, bytes, s->fp);
        if (n < bytes && std::ferror(s->fp)) {
            // A hard CRT read error (not a short read at EOF) — log loud.
            LOG_ERROR_KV(kCat, "loose_read_error",
                kcdx::log::KV("handle", static_cast<uint64_t>(h)),
                kcdx::log::KV("requested", static_cast<uint64_t>(bytes)),
                kcdx::log::KV("got", static_cast<uint64_t>(n)));
            ok = false;
        }
        return n;
    }

    // Pak: copy out of the inflated buffer, advance the cursor (bounds-clamped;
    // a read past EOF returns the remaining bytes, then 0 — normal stream EOF).
    const size_t remaining = s->cursor <= s->pakBytes.size()
                                 ? s->pakBytes.size() - s->cursor
                                 : 0;
    const size_t n = bytes < remaining ? bytes : remaining;
    if (n != 0) {
        std::memcpy(dst, s->pakBytes.data() + s->cursor, n);
        s->cursor += n;
    }
    return n;
}

int Seek(KcdxHandle h, long long offset, int origin) {
    std::lock_guard<std::mutex> lock(g_poolLock);
    OpenFile* s = SlotLocked(h);
    if (!s) { LogBadHandle("seek", h); return -1; }

    if (s->kind == OpenFile::Kind::Loose) {
        if (_fseeki64(s->fp, offset, origin) != 0) {
            LOG_ERROR_KV(kCat, "loose_seek_error",
                kcdx::log::KV("handle", static_cast<uint64_t>(h)),
                kcdx::log::KV("offset", static_cast<long long>(offset)),
                kcdx::log::KV("origin", static_cast<long long>(origin)));
            return -1;
        }
        return 0;
    }

    // Pak: compute the new cursor against the buffer, bounds-checked.
    long long base = 0;
    switch (origin) {
        case SEEK_SET: base = 0; break;
        case SEEK_CUR: base = static_cast<long long>(s->cursor); break;
        case SEEK_END: base = static_cast<long long>(s->size); break;
        default:
            LOG_ERROR_KV(kCat, "pak_seek_bad_origin",
                kcdx::log::KV("handle", static_cast<uint64_t>(h)),
                kcdx::log::KV("origin", static_cast<long long>(origin)));
            return -1;
    }
    const long long target = base + offset;
    // A seek to exactly size is legal (the EOF position); past it / negative is
    // a failure (fail loud, do not clamp-and-pretend — AP14).
    if (target < 0 || target > static_cast<long long>(s->size)) {
        LOG_ERROR_KV(kCat, "pak_seek_out_of_range",
            kcdx::log::KV("handle", static_cast<uint64_t>(h)),
            kcdx::log::KV("target", static_cast<long long>(target)),
            kcdx::log::KV("size", static_cast<uint64_t>(s->size)));
        return -1;
    }
    s->cursor = static_cast<size_t>(target);
    return 0;
}

long long Tell(KcdxHandle h) {
    std::lock_guard<std::mutex> lock(g_poolLock);
    OpenFile* s = SlotLocked(h);
    if (!s) { LogBadHandle("tell", h); return -1; }
    if (s->kind == OpenFile::Kind::Loose) {
        const __int64 pos = _ftelli64(s->fp);
        if (pos < 0) {
            LOG_ERROR_KV(kCat, "loose_tell_error",
                kcdx::log::KV("handle", static_cast<uint64_t>(h)));
            return -1;
        }
        return static_cast<long long>(pos);
    }
    return static_cast<long long>(s->cursor);
}

bool Eof(KcdxHandle h) {
    std::lock_guard<std::mutex> lock(g_poolLock);
    OpenFile* s = SlotLocked(h);
    if (!s) { LogBadHandle("eof", h); return true; }  // safe stop answer
    if (s->kind == OpenFile::Kind::Loose) {
        return std::feof(s->fp) != 0;
    }
    return s->cursor >= s->size;
}

size_t Write(KcdxHandle h, const void* src, size_t bytes, bool& ok) {
    ok = true;
    if (!src || bytes == 0) return 0;
    std::lock_guard<std::mutex> lock(g_poolLock);
    OpenFile* s = SlotLocked(h);
    if (!s) { LogBadHandle("write", h); ok = false; return 0; }

    if (s->kind != OpenFile::Kind::Loose) {
        // A Pak source is read-only (an inflated in-memory entry). A write to it
        // is a contract violation — fail loud (AP14), never silently succeed.
        LOG_ERROR_KV(kCat, "write_to_pak",
            kcdx::log::KV("handle", static_cast<uint64_t>(h)),
            kcdx::log::KV::BareStr("detail",
                "a write was attempted on a pak (read-only, in-memory) handle — "
                "rejected loud. A write target always resolves to a loose disk "
                "path (§5), so a pak write handle is a defect."));
        ok = false;
        return 0;
    }
    const size_t n = std::fwrite(src, 1, bytes, s->fp);
    if (n < bytes) {
        LOG_ERROR_KV(kCat, "loose_write_short",
            kcdx::log::KV("handle", static_cast<uint64_t>(h)),
            kcdx::log::KV("requested", static_cast<uint64_t>(bytes)),
            kcdx::log::KV("got", static_cast<uint64_t>(n)));
        ok = false;
    }
    return n;
}

int Flush(KcdxHandle h) {
    std::lock_guard<std::mutex> lock(g_poolLock);
    OpenFile* s = SlotLocked(h);
    if (!s) { LogBadHandle("flush", h); return -1; }
    if (s->kind != OpenFile::Kind::Loose) return 0;  // nothing to flush
    if (std::fflush(s->fp) != 0) {
        LOG_ERROR_KV(kCat, "loose_flush_error",
            kcdx::log::KV("handle", static_cast<uint64_t>(h)));
        return -1;
    }
    return 0;
}

int Error(KcdxHandle h) {
    std::lock_guard<std::mutex> lock(g_poolLock);
    OpenFile* s = SlotLocked(h);
    if (!s) { LogBadHandle("ferror", h); return 1; }
    if (s->kind != OpenFile::Kind::Loose) return 0;  // no stream error state
    return std::ferror(s->fp);
}

int GetErrno(KcdxHandle h) {
    std::lock_guard<std::mutex> lock(g_poolLock);
    OpenFile* s = SlotLocked(h);
    if (!s) { LogBadHandle("errno", h); return EBADF; }
    if (s->kind != OpenFile::Kind::Loose) return 0;
    // Best-effort: the process errno at the last failing op on this stream. The
    // engine reads this only after a failing read/write, so the current errno
    // is the relevant value (the read family runs under the pool lock, so no
    // other slot op interleaves on this thread between the failure and here).
    return errno;
}

int Getc(KcdxHandle h) {
    std::lock_guard<std::mutex> lock(g_poolLock);
    OpenFile* s = SlotLocked(h);
    if (!s) { LogBadHandle("fgetc", h); return -1; }
    if (s->kind == OpenFile::Kind::Loose) {
        return std::fgetc(s->fp);  // returns EOF(-1) at end / on error
    }
    if (s->cursor >= s->pakBytes.size()) return -1;  // EOF
    return static_cast<int>(s->pakBytes[s->cursor++]);
}

int Ungetc(KcdxHandle h, int ch) {
    std::lock_guard<std::mutex> lock(g_poolLock);
    OpenFile* s = SlotLocked(h);
    if (!s) { LogBadHandle("ungetc", h); return -1; }
    if (s->kind == OpenFile::Kind::Loose) {
        return std::ungetc(ch, s->fp);
    }
    if (s->cursor == 0 || ch == -1) return -1;  // nothing to push back
    --s->cursor;
    // Honor the pushed-back value into the buffer position (ungetc semantics:
    // the next read returns `ch`). The buffer is kcdx-owned + mutable.
    s->pakBytes[s->cursor] = static_cast<uint8_t>(ch);
    return ch;
}

char* Gets(KcdxHandle h, char* dst, int maxCount) {
    if (!dst || maxCount <= 0) return nullptr;
    std::lock_guard<std::mutex> lock(g_poolLock);
    OpenFile* s = SlotLocked(h);
    if (!s) { LogBadHandle("fgets", h); return nullptr; }
    if (s->kind == OpenFile::Kind::Loose) {
        return std::fgets(dst, maxCount, s->fp);
    }
    // Pak: copy up to maxCount-1 bytes or through the next '\n' (inclusive).
    if (s->cursor >= s->pakBytes.size()) return nullptr;  // EOF before any byte
    int w = 0;
    while (w < maxCount - 1 && s->cursor < s->pakBytes.size()) {
        const char c = static_cast<char>(s->pakBytes[s->cursor++]);
        dst[w++] = c;
        if (c == '\n') break;
    }
    dst[w] = '\0';
    return dst;
}

int Fileno(KcdxHandle h) {
    std::lock_guard<std::mutex> lock(g_poolLock);
    OpenFile* s = SlotLocked(h);
    if (!s) { LogBadHandle("fileno", h); return -1; }
    if (s->kind != OpenFile::Kind::Loose) return -1;  // a pak entry has no fd
    return _fileno(s->fp);
}

long long GetModificationTime(KcdxHandle h) {
    std::lock_guard<std::mutex> lock(g_poolLock);
    OpenFile* s = SlotLocked(h);
    if (!s) { LogBadHandle("mtime", h); return 0; }
    if (s->kind != OpenFile::Kind::Loose) {
        // A pak source's on-disk mtime is not threaded through the inflated
        // buffer in this step (the index does not carry the pak entry's stored
        // DOS time). Return 0 (the engine's own pak path can return the entry's
        // cached time; carrying it is a future step). Not a failure — a defined
        // "no mtime available" for an in-memory pak entry.
        return 0;
    }
    // Loose: _fileno → _get_osfhandle → GetFileTime, the slot-66 OS-arm chain
    // (FINDINGS §"Slot 66" body). All on kcdx's CRT/handle.
    const int fd = _fileno(s->fp);
    if (fd < 0) return 0;
    const HANDLE hf = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    if (hf == INVALID_HANDLE_VALUE) return 0;
    FILETIME create{}, access{}, lastWrite{};
    if (!GetFileTime(hf, &create, &access, &lastWrite)) {
        LOG_ERROR_KV(kCat, "mtime_getfiletime_failed",
            kcdx::log::KV("handle", static_cast<uint64_t>(h)),
            kcdx::log::KV("win32_err", static_cast<uint64_t>(GetLastError())));
        return 0;
    }
    long long packed = 0;
    std::memcpy(&packed, &lastWrite, sizeof(packed));  // FILETIME → __int64
    return packed;
}

const void* GetCachedFileData(KcdxHandle h, long long* outSize) {
    if (outSize) *outSize = 0;
    std::lock_guard<std::mutex> lock(g_poolLock);
    OpenFile* s = SlotLocked(h);
    if (!s) { LogBadHandle("cached_data", h); return nullptr; }

    if (s->kind == OpenFile::Kind::Pak) {
        // Zero-copy: the inflated buffer IS the cached whole-file data.
        if (outSize) *outSize = static_cast<long long>(s->pakBytes.size());
        return s->pakBytes.data();
    }

    // Loose: read the whole file into the slot's own cache (kcdx CRT) once, then
    // serve it. Reuse pakBytes as the cache store for a Loose handle too (it is
    // the slot's owned byte buffer; the kind stays Loose, the FILE* untouched).
    if (s->pakBytes.empty()) {
        if (_fseeki64(s->fp, 0, SEEK_END) != 0) {
            LOG_ERROR_KV(kCat, "cached_seek_end_failed",
                kcdx::log::KV("handle", static_cast<uint64_t>(h)));
            return nullptr;
        }
        const __int64 sz = _ftelli64(s->fp);
        if (sz < 0) {
            LOG_ERROR_KV(kCat, "cached_tell_failed",
                kcdx::log::KV("handle", static_cast<uint64_t>(h)));
            return nullptr;
        }
        if (_fseeki64(s->fp, 0, SEEK_SET) != 0) {
            LOG_ERROR_KV(kCat, "cached_seek_set_failed",
                kcdx::log::KV("handle", static_cast<uint64_t>(h)));
            return nullptr;
        }
        s->pakBytes.resize(static_cast<size_t>(sz));
        if (sz > 0) {
            const size_t n = std::fread(s->pakBytes.data(), 1,
                                        static_cast<size_t>(sz), s->fp);
            if (n != static_cast<size_t>(sz)) {
                s->pakBytes.clear();
                LOG_ERROR_KV(kCat, "cached_read_short",
                    kcdx::log::KV("handle", static_cast<uint64_t>(h)),
                    kcdx::log::KV("requested", static_cast<uint64_t>(sz)),
                    kcdx::log::KV("got", static_cast<uint64_t>(n)));
                return nullptr;
            }
        }
        s->size = static_cast<uint64_t>(sz);
    }
    if (outSize) *outSize = static_cast<long long>(s->pakBytes.size());
    return s->pakBytes.data();
}

int Close(KcdxHandle h) {
    std::lock_guard<std::mutex> lock(g_poolLock);
    const size_t id = DecodeId(h);
    if (id == 0 || id > g_slots.size()) {
        LogBadHandle("fclose", h);
        return -1;  // EOF — bad handle
    }
    OpenFile& s = g_slots[id - 1];
    if (s.closed) {
        // Double-close — a logged no-op (never a double-fclose on a stale FILE*).
        LOG_ERROR_KV(kCat, "double_close",
            kcdx::log::KV("handle", static_cast<uint64_t>(h)),
            kcdx::log::KV::BareStr("detail",
                "fclose on an already-closed kcdx handle — no-op (the slot is "
                "already freed). A double-close is a caller defect, surfaced "
                "rather than silently re-closing a reused slot's FILE*."));
        return -1;
    }
    int rc = 0;
    if (s.kind == OpenFile::Kind::Loose && s.fp) {
        rc = std::fclose(s.fp);  // kcdx CRT fclose
        s.fp = nullptr;
    }
    s.pakBytes.clear();
    s.pakBytes.shrink_to_fit();
    s.cursor = 0;
    s.size   = 0;
    s.closed = true;
    g_freeList.push_back(id);  // the id is reusable now
    return rc;
}

}  // namespace kcdx::fs_takeover
