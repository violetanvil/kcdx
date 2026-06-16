#pragma once

// The kcdx READ-family slot impls — the slots the takeover flips THUNK→KCDX so
// kcdx operates every handle ENTIRELY on its own CRT (file-system-takeover §4.4).
//
// Each impl is a thin vtable-ABI SHIM: it takes the engine's member-call args
// (built to the FINDINGS ABI per slot — BODY-VERIFIED or LEAF-IDENTIFIED, cited
// per slot in read_slots.cpp), extracts the kcdx handle-id, and forwards to the
// file_handle pool op (file_handle.h), which performs the I/O on kcdx's CRT. The
// engine NEVER operates the handle — it holds the opaque id and passes it back.
//
// CROSS-CRT INVARIANT (§9): every read slot here is KCDX (never THUNK). A
// thunked read slot would fread/fseek/fclose the kcdx handle-id on the ENGINE's
// CRT — the exact straddle the takeover removes. Keeping every handle-operating
// slot KCDX is the one §4.3 thunk-flip the per-slot table forbids (§4.4).
//
// On x64 Windows there is ONE calling convention (RCX,RDX,R8,R9,stack), so a
// plain `T fn(void* self, ...)` IS the member-call shape (`self`==`this` in RCX)
// — mirroring the existing KcdxFOpenMarker slot impl (no explicit __fastcall).
//
// HANDLE EXTRACTION: each slot receives the kcdx handle in the SAME arg position
// the engine's tagged-union dispatch reads it (the FINDINGS member ABI per slot
// — slot 38's tagged handle is arg 5; the others take the handle as the FILE*
// arg). The shim reinterprets that void*/FILE* arg as a KcdxHandle and forwards
// it; the pool validates the kcdx tag (file_handle.h IsKcdxHandle).

#include <cstddef>
#include <cstdint>

namespace kcdx::fs_takeover {

// --- Core read family (FINDINGS BODY-VERIFIED: 38/39/40; LEAF-IDENTIFIED: 41/
//     53/54/55/56). ----------------------------------------------------------

// slot 38 FReadRaw-by-pak-index. ABI (BODY-VERIFIED): size_t (this, void* buf,
// size_t size, size_t count, longlong taggedHandle) — 5-arg; the handle is arg 5.
size_t kcdx_FReadRaw_byPakIndex(void* self, void* buf, size_t size,
                                size_t count, long long taggedHandle);

// slot 39 FReadRaw. ABI (BODY-VERIFIED): size_t (this, void* buf, size_t size,
// FILE* handle) — 4-arg. The body fseeks the handle to 0 then reads `size`
// bytes (it reads the WHOLE source from the start — front3/decomp body).
size_t kcdx_FReadRaw(void* self, void* buf, size_t size, void* handle);

// slot 40 FGetCachedFileData. ABI (BODY-VERIFIED): void* (this, FILE* handle,
// longlong* outSizeDst) — 3-arg. Returns the cached whole-file buffer, writes
// the size into *outSizeDst.
void* kcdx_FGetCachedFileData(void* self, void* handle, long long* outSizeDst);

// slot 41 FWrite. ABI (LEAF-IDENTIFIED, fwrite-shaped): size_t (this, const
// void* buf, size_t size, size_t count, FILE* handle).
size_t kcdx_FWrite(void* self, const void* buf, size_t size, size_t count,
                   void* handle);

// slot 53 FSeek. ABI (LEAF-IDENTIFIED, fseek-shaped): int (this, FILE* handle,
// long offset, int origin).
int kcdx_FSeek(void* self, void* handle, long offset, int origin);

// slot 54 FTell. ABI (LEAF-IDENTIFIED, _ftelli64-shaped): __int64 (this, FILE*
// handle).
long long kcdx_FTell(void* self, void* handle);

// slot 55 FClose. ABI (LEAF-IDENTIFIED, fclose-shaped): int (this, FILE* handle).
int kcdx_FClose(void* self, void* handle);

// slot 56 FEof. ABI (LEAF-IDENTIFIED, feof-shaped): int (this, FILE* handle).
int kcdx_FEof(void* self, void* handle);

// --- Read variants (FINDINGS LEAF-IDENTIFIED: 43/44/46/47/57/58/59; BODY-
//     VERIFIED: 66). -----------------------------------------------------------

// slot 43 FGets. ABI (LEAF-IDENTIFIED, fgets-shaped): char* (this, char* buf,
// int maxCount, FILE* handle).
char* kcdx_FGets(void* self, char* buf, int maxCount, void* handle);

// slot 44 FGetc. ABI (LEAF-IDENTIFIED, fgetc-shaped): int (this, FILE* handle).
int kcdx_FGetc(void* self, void* handle);

// slot 46 fileno. ABI (LEAF-IDENTIFIED, _fileno-shaped): int (this, FILE* handle).
int kcdx_Fileno(void* self, void* handle);

// slot 47 FUngetc. ABI (LEAF-IDENTIFIED, ungetc-shaped): int (this, int ch,
// FILE* handle).
int kcdx_FUngetc(void* self, int ch, void* handle);

// slot 57 FError. ABI (LEAF-IDENTIFIED, ferror-shaped): int (this, FILE* handle).
int kcdx_FError(void* self, void* handle);

// slot 58 FGetErrno. ABI (LEAF-IDENTIFIED, _errno-shaped): int (this, FILE*
// handle).
int kcdx_FGetErrno(void* self, void* handle);

// slot 59 FFlush. ABI (LEAF-IDENTIFIED, fflush-shaped): int (this, FILE* handle).
int kcdx_FFlush(void* self, void* handle);

// slot 66 FGetModificationTime. ABI (BODY-VERIFIED): __int64 (this, FILE*
// handle) — 2-arg; returns the packed FILETIME last-write time.
long long kcdx_FGetModificationTime(void* self, void* handle);

}  // namespace kcdx::fs_takeover
