#pragma once

// The kcdx EXISTENCE / METADATA-by-name slot impls — the slots the takeover
// flips THUNK→KCDX so existence/size/attribute/stat questions answer from
// kcdx's unified asset index (file-system-takeover design §4.5 "Existence /
// metadata by name" + §5). Each slot resolves a vpath against the index in
// O(1) on a HIT (the answer IS the index entry — exists=true; size = the
// ByteSource's size); a MISS thunks the slot's OWN captured original engine
// body (which answers from BOTH the engine pak-directory AND disk — §5 the
// long tail of names the asset index does not carry: saves/config/cache + any
// engine-mounted-pak entry). See "MISS-PATH RESOLUTION" below for the full
// model. NO handle is minted by any slot here (these are pure
// existence/size/attribute reads — open is open_slots.cpp).
//
// Every member-call ABI is built to the per-slot decompiled bodies read out of
// the game binary, cross-checked against the vtable role table. On x64
// Windows there is ONE calling convention (RCX,RDX,R8,R9,stack), so a plain
// `T fn(void* self, ...)` IS the member-call shape (`self`==`this` in RCX) —
// mirroring open_slots.cpp / read_slots.cpp (no explicit __fastcall).
//
// MISS-PATH RESOLUTION (the §5 long tail — the slot's OWN captured original):
// on an index MISS, each impl calls the slot's CAPTURED ORIGINAL engine body
// with the SAME args it received and returns its result verbatim. The original
// answers from BOTH the engine pak-directory (FUN_1804631f0) AND disk — so a
// name living ONLY in an engine-mounted pak the kcdx index does not carry gets
// the real engine answer, byte-for-byte, with no dependency on the index being
// complete. The index-HIT arm stays first (it covers kcdx's index-only pak
// vpaths the engine original would not see); the miss arm thunks the original.
// The original returns a value and touches only the engine object's intact
// members (pak vector, search-path vector, alias table — all preserved by the
// vtable-pointer-only swap); it mints no handle and uses no CRT, so it is
// §-safe (the same class as the slot-1 resolution thunk). Each original is
// captured at swap time from the live object's original vtable (mirroring the
// slot-1 capture) via SetMetadataOriginals below.

#include <cstdint>

namespace kcdx::fs_takeover {

// === Captured-original metadata-slot bodies — set by the swap at capture time.
//
// Each typedef is the slot's BODY-VERIFIED member-call ABI (the SAME signature
// as the matching kcdx_* impl below; on x64 there is ONE calling convention, so
// `T fn(void* self, ...)` IS the member-call shape, `self`==`this` in RCX). The
// swap captures all 8 from the live object's ORIGINAL vtable BEFORE overwriting
// the object's vtable pointer, via SetMetadataOriginals(originalVtable) — one
// call that stores every metadata-slot original (mirrors slot 1's capture).
using IsFolderOrigFn_t        = bool     (*)(void* self, const char* pName);
using GetFileSizeOrigFn_t     = uint64_t (*)(void* self, const char* pName, char bDiskOnly);
using IsFileExist3OrigFn_t    = bool     (*)(void* self, const char* pName, int location);
using GetFileAttributesOrigFn_t = uint64_t (*)(void* self, const char* pName);
using GetFileStatOrigFn_t     = int      (*)(void* self, const char* pName, void* outStat);
using IsFileExist2OrigFn_t    = bool     (*)(void* self, const char* pName);
using GetFileSizeOnDiskOrigFn_t = long long (*)(void* self, const char* pName);
using GetFileSizeCompressedOrigFn_t = uint32_t (*)(void* self, const char* pName);

// Store the 8 metadata-slot originals from the live object's original vtable.
// `originalVtable` is the captured original CCryPak vtable (the same array the
// swap reads the slot-1 original from); this reads slots 13/45/67/68/69/70/92/93
// off it. Acquire/release inside, so the kcdx vtable's first metadata dispatch
// sees the fully-stored pointers (concurrency.md).
void SetMetadataOriginals(const void* const* originalVtable);

// === The KCDX existence/metadata slot impls (the fn pointers the per-slot
// table KCDX rows point at; wired into the kcdx vtable by vtable_swap). =======

// slot 13 — IsFolder / dir-exists. ABI (BODY-VERIFIED, RVA 0x2419280):
//   bool (this, cstr pName)
// Original body: slot1(pName, flag 0x30400) → append '\' → _findfirst64 → bool.
// kcdx impl: the index holds file vpaths (never dir stubs), so it carries no
// folder answer → on EVERY call, thunk the captured original (which does the
// engine's own slot1 + _findfirst64 dir probe). Returns its bool verbatim.
bool kcdx_IsFolder(void* self, const char* pName);

// slot 45 — GetFileSize-by-name. ABI (BODY-VERIFIED, RVA 0x2418b48):
//   uint64 (this, cstr pName, char bDiskOnly)
// Original body: slot1(flag 2) → OS size (vtable+0x228) gated on pakPriority,
// else pak-dir size (FUN_1804631f0). kcdx impl: index HIT → the ByteSource's
// uncompressed size (loose: stat the diskPath; pak: bs->size). MISS → thunk the
// captured original (answers from engine pak-dir AND disk) with the SAME
// (pName, bDiskOnly) and return verbatim. bDiskOnly honored on the index arm:
// when set, a pak/index size is skipped (falls to the original). Returns the
// size, or 0 on a non-existent name (the verified body's not-found return —
// FUN_182418b48 maps its internal OS-getter (uint64)-1 "no OS size" signal to 0
// before returning; 0, not -1, is what every engine caller of slot 45 reads).
uint64_t kcdx_GetFileSize(void* self, const char* pName, char bDiskOnly);

// slot 67 — IsFileExist (3-arg). ABI (BODY-VERIFIED, RVA 0x463ec4):
//   bool (this, cstr pName, int location)
// Original body: slot1(flag 2) → pak-membership and/or OS disk-existence,
// pakPriority + `location`-gated (location==2 → pak-only; ==1 → disk-only;
// ==0/other → either, pakPriority order). kcdx impl: index HIT honors location
// (a Pak source satisfies pak/either; a Loose source satisfies disk/either) →
// true. MISS (incl. a location-filtered index source) → thunk the captured
// original (which does the full location-gated engine pak-dir AND disk check —
// including the location==2 pak-dir lookup the index miss cannot answer) with
// the SAME (pName, location) and return verbatim.
bool kcdx_IsFileExist3(void* self, const char* pName, int location);

// slot 68 — GetFileAttributes / IsFolder(disk). ABI (BODY-VERIFIED, RVA
// 0x241ac8c):
//   uint64 (this, cstr pName)
// Original body: slot1(flag 2) → GetFileAttributesA → returns the
// FILE_ATTRIBUTE_DIRECTORY bit (decompiled as `(attrs>>4)&1`-ish dir flag),
// 0 on a non-existent path. The original is a DISK attribute probe (slot1 →
// GetFileAttributesA) — it carries no pak-dir lookup and the index holds no
// dir/attribute answer, so kcdx has no index arm here → on EVERY call, thunk
// the captured original and return its packed attribute result verbatim.
uint64_t kcdx_GetFileAttributes(void* self, const char* pName);

// slot 69 — GetFileStat (_stat64). ABI (BODY-VERIFIED, RVA 0x4d5d58):
//   int (this, cstr pName, struct _stat64* outStat)
// Original body: _stat64 of a resolved path. The index carries no stat record,
// so kcdx has no index arm → on EVERY call, thunk the captured original (which
// resolves + _stat64s on the engine's CRT, writing outStat) and return its int
// verbatim. Returns 0 on success, -1 on failure (the _stat64 convention).
int kcdx_GetFileStat(void* self, const char* pName, void* outStat);

// slot 70 — IsFileExist (2-arg). ABI (BODY-VERIFIED, RVA 0x241abcc):
//   bool (this, cstr pName)
// Original body: slot1(flag 2) → pak-dir entry (FUN_1804631f0), excludes dir
// entries (entry type `!= 0xd`) and the empty type. kcdx impl: index HIT for a
// file vpath → true (the index holds files, never dir stubs, so the dir-entry
// exclusion is satisfied by construction). MISS → thunk the captured original
// (the engine pak-dir entry check, dir-excluding) with the SAME pName and
// return verbatim.
bool kcdx_IsFileExist2(void* self, const char* pName);

// slot 92 — GetFileSizeOnDisk / GetFileSize(uncompressed). ABI (BODY-VERIFIED,
// RVA 0x2419c00):
//   int64 (this, cstr pName)
// Original body: slot1(flag 2) → pak-dir entry + size compute (uncompressed
// size + base). kcdx impl: index HIT → the ByteSource's uncompressed size
// (loose: stat the diskPath; pak: bs->size). MISS → thunk the captured original
// (engine pak-dir size compute) with the SAME pName and return verbatim.
// Returns the size, or 0 on a non-existent name (the body's `lVar2=0` default).
long long kcdx_GetFileSizeOnDisk(void* self, const char* pName);

// slot 93 — GetFileSizeCompressed / GetArchivePath. ABI (BODY-VERIFIED, RVA
// 0x463a24):
//   uint32 (this, cstr pName)
// Original body: slot1(flag 2 via gEnv) → pak-dir entry → compressed-size
// compute (FUN_180463abc). kcdx impl: index HIT → the ByteSource's COMPRESSED
// size (pak: bs->compressed; loose: == the disk size, STORED-equivalent). MISS
// → thunk the captured original (engine pak-dir compressed-size compute) with
// the SAME pName and return verbatim. Returns the compressed size as uint32.
uint32_t kcdx_GetFileSizeCompressed(void* self, const char* pName);

}  // namespace kcdx::fs_takeover
