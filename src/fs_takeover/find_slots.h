#pragma once

// The kcdx DIRECTORY-ITERATOR slot impls — slots 63/64/65 FindFirst/FindNext/
// FindClose (CCryPak vtable +0x1F8/+0x200/+0x208), the engine's GENERAL stateful
// by-name directory enumeration (file-system-takeover design §5.1). The
// table-DB override-glob loader (`Libs/Tables/<base>__*.<ext>`) and a second
// general directory-listing consumer both dispatch through this triplet; with
// the slots THUNK kcdx serves no pak-resident entries for those globs and the
// table-database load fatals at boot (err_id=259, KI-0027).
//
// THE MODEL (design §5.1 — the SAME unified-set union as slot 14 kcdx_ForEachFile,
// in STATEFUL handle form). FindFirst seeds a kcdx-owned find-handle with the
// UNIFIED entry set for the pattern's directory prefix:
//   (1) the engine's on-disk entries (kcdx's OWN _wfindfirst64/_wfindnext64 walk
//       on kcdx's CRT — loose overrides are real disk files this walk surfaces),
//   PLUS
//   (2) the unified index's PAK-resident vpaths under the same prefix (the
//       index-only delta the disk walk cannot see — they live inside paks).
// The loose-skip IS the de-dup, exactly as kcdx_ForEachFile does it: a loose
// vpath is a real disk file the (1) walk already surfaced, so the index walk (2)
// emits ONLY Pak sources — the two sets are disjoint by construction. FindNext
// advances the handle through the seeded set; FindClose releases it.
//
// kcdx mints + owns the ENTIRE find-handle lifecycle — the engine never operates
// the iterator. It holds the kcdx-minted handle and passes it back to kcdx's
// FindNext/FindClose, the same cradle-to-grave ownership the read family has
// (§4.4). There is NO engine CCryPakFindData and NO engine-CRT iterator state in
// kcdx's path — a thunk-and-augment shape (let the engine mint the iterator,
// kcdx reach into its result) is REJECTED for the same reason §6 rejects driving
// the engine's ZipDir: it re-threads engine-CRT-allocated state into kcdx's path
// (the cross-runtime sharing the takeover eliminates) and is a coexistence
// retreat from the §1 total invariant.
//
// THE FIND-DATA BUFFER ABI (design §8 P5, RESOLVED — outcome A, read from the
// binary's TWO independent consumers, _research/ki0027-find-data-abi-recon/
// FINDINGS.md):
//   - byte @ offset 0x00 = attribute/flags word; bit 0x10 = DIRECTORY (the
//     consumer SKIPS an entry when (buf[0] & 0x10) != 0). kcdx CLEARS 0x10 for a
//     served file (pak or loose vpath); the table glob wants files.
//   - entry name @ offset 0x24 (36) = inline NUL-terminated C-string (the
//     consumer does strlen/strcmp/strstr over &buf[0x24]) — the entry's BASE NAME
//     (the filename the glob matches), NOT the full path, bounded to the buffer.
//   - bytes 0x01..0x23 = the engine header's reserved/size/time region the
//     table-glob consumers do NOT read — kcdx zero-fills them.
// The caller provides the buffer (a MAX_PATH-class region: a 36-byte header with
// the inline name contiguous after at +0x24).
//
// SCOPE — slot 101 (the CCryPakFindData object-iterator factory, +0x328) stays
// THUNK: no engine consumer kcdx must satisfy dispatches a directory glob through
// it (design §4.5/§5.1), and it is a DIFFERENT API (a separate object vftable).
// A future one-line flip if a consumer surfaces. This step flips ONLY 63/64/65.
//
// On x64 Windows there is ONE calling convention (RCX,RDX,R8,R9,stack), so a
// plain `T fn(void* self, ...)` IS the member-call shape (`self`==`this` in RCX)
// — mirroring open_slots.h / read_slots.h / enum_slots.h.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "asset_index.h"

namespace kcdx::fs_takeover {

// === The find-data buffer offsets (design §8 P5 — the engine consumer ABI). ===
// Table data describing the find-data buffer layout (read from the consumers),
// NOT a per-version game-binary address.
constexpr size_t kFindDataAttrOffset = 0x00;  // byte: bit 0x10 = directory.
constexpr size_t kFindDataNameOffset = 0x24;  // inline NUL-terminated base name.
constexpr uint8_t kFindDataDirBit    = 0x10;  // set ⇔ directory; cleared ⇔ file.
// SOURCE: the find-data name field begins at offset 0x24 (verified by the
// find-data ABI recon — body-verified consumer reads). The name region's exact
// CAPACITY past +0x24 was NOT decompiled, so the inline name copy is bounded
// conservatively to MAX_PATH (260): a value that cannot overrun a MAX_PATH-class
// name buffer, and real on-disk/pak entry base names are well under it. A future
// producer-side decompile of the engine FindFirst body can widen this if a
// longer capacity is ever verified.
constexpr int kFindDataNameCap = 260;  // conservative MAX_PATH bound on the name copy.

// === The pure unified-enumeration core (the headless-testable seam) ==========
//
// One entry of the seeded unified set: the base name the find-data carries +
// whether it is a directory (the 0x10 attr bit). The find-handle holds a vector
// of these; FindFirst/FindNext fill the caller's find-data from each in turn.
struct FindEntry {
    std::string name;        // the base name written inline at find-data +0x24.
    bool        isDir = false;  // true → attr 0x10 SET; false (a file) → cleared.
};

// Build the UNIFIED entry set for a directory prefix — the SAME union model
// kcdx_ForEachFile applies (enum_slots.cpp), factored PURE so it is testable
// headless without a live _wfindfirst64 or a live CCryPak object:
//   - `diskNames` are the base names the engine on-disk walk surfaced (the
//     caller runs the real _wfindfirst64 walk and passes its results here; the
//     test injects synthetic disk names). Each becomes a FILE entry (the disk
//     walk's per-entry dir/file flag is carried in `diskIsDir`, parallel to
//     `diskNames`; an empty `diskIsDir` treats every disk name as a file).
//   - `index` + `normPrefix` add the index's PAK-resident vpaths directly under
//     `normPrefix` (single directory level — no deeper subdir), as FILE entries,
//     SKIPPING any whose base name a disk entry already surfaced (the loose-skip
//     de-dup: a loose override is a real disk file the walk already saw). Only
//     Pak sources are index-only — a loose vpath is covered by the disk walk.
// `normPrefix` is the NormalizeVPath'd directory prefix (lowercase + forward
// slash) the index keys compare against. Returns the merged entry vector (disk
// entries first, then the index-only pak deltas). Cold path (enumeration, not the
// per-frame surface) — std::string/std::vector are acceptable here.
std::vector<FindEntry> BuildUnifiedFindEntries(
    const std::vector<std::string>& diskNames,
    const std::vector<bool>& diskIsDir,
    const AssetIndex& index,
    const std::string& normPrefix);

// Fill a caller-provided find-data buffer from one FindEntry (the design §8 P5
// ABI): zero the 36-byte header, set/clear bit 0x10 at +0x00 per entry.isDir,
// write entry.name NUL-terminated at +0x24 bounded to `nameCap`. `nameCap` is the
// caller-buffer capacity past +0x24 (a MAX_PATH-class region). Returns false
// (caller fails loud) on a null buffer. PURE — testable headless.
bool FillFindData(void* findData, size_t nameCap, const FindEntry& entry);

// === The KCDX find-slot impls (the fn pointers the per-slot table KCDX rows
// point at; wired into the kcdx vtable by vtable_swap). =======================

// slot 63 — FindFirst. ABI (design §5.1, BODY-VERIFIED from the consumer:
// `(**(*pak+0x1f8))(pak, pattern, findData, 0)`):
//   intptr_t (this, cstr pattern, void* findData, int)
// Resolves the pattern's directory prefix via slot 1, opens a kcdx find-handle
// (the SAME (id<<1)|1 odd-tagged pool, file_handle.h), seeds it with the unified
// set, fills `findData` with the FIRST entry, returns the find-handle. Returns -1
// (any value < 0) on NO match — the engine contract the consumer loops on:
//   if (-1 < handle) { do {...} while (-1 < FindNext); FindClose; }
intptr_t kcdx_FindFirst(void* self, const char* pattern, void* findData,
                        int flags);

// slot 64 — FindNext. ABI (`(**(*pak+0x200))(pak, handle, findData)`):
//   intptr_t (this, intptr_t handle, void* findData)
// Advances the kcdx find-handle to the next unified-set entry, fills `findData`,
// returns the continue/exhausted signal: ≥0 (specifically 0) continues the
// consumer's `while (-1 < iVar3)`; -1 = exhausted (stop).
intptr_t kcdx_FindNext(void* self, intptr_t handle, void* findData);

// slot 65 — FindClose. ABI (`(**(*pak+0x208))(pak, handle)`):
//   int (this, intptr_t handle)
// Releases the kcdx find-handle (returns its pool slot). Returns 0 on success,
// non-zero on a bad/already-closed handle (logged).
int kcdx_FindClose(void* self, intptr_t handle);

}  // namespace kcdx::fs_takeover
