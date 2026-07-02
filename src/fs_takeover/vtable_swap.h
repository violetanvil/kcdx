#pragma once

// The vtable swap — kcdx takes ownership of the CCryPak object by replacing its
// vtable pointer. Builds a kcdx 102-entry vtable from the per-slot table
// (THUNK rows get the live object's ORIGINAL slot pointers, captured here; the
// KCDX slot-36 row gets the kcdx marker fn), then writes the kcdx vtable's
// address into [pCryPak+0x00] on the EXISTING object.
//
// VTABLE-POINTER-ONLY ON THE EXISTING OBJECT: the swap never builds a fresh
// object and never relocates a member. The object layout is preserved, which
// is precisely what makes a THUNK valid — the original engine body a thunk
// forwards to reads the object's engine member offsets, and they are all still
// there because it is the SAME object. (This is the foundation thunk soundness
// rests on.)

#include <cstdint>

namespace kcdx::fs_takeover {

// Build the kcdx vtable from the per-slot table and write its pointer onto the
// CCryPak object at `pCryPak`. Reads the object's CURRENT vtable pointer from
// [pCryPak+0x00] to capture each THUNK slot's original body pointer, builds a
// process-lifetime kcdx vtable (THUNK → captured original, KCDX → kcdx_fn),
// then stores the kcdx vtable address back into [pCryPak+0x00].
//
// Idempotent: a second call (a re-published CCryPak, if it ever happens)
// re-captures and re-swaps against the then-current pointer, but never double-
// builds the kcdx vtable.
//
// Returns true on a completed swap; false (logged loud) if pCryPak is null or
// its current vtable pointer is null — in which case the object is left
// untouched (the engine keeps its own vtable; no kcdx ownership this boot).
//
// === DIAGNOSTIC (PROBE Z/Z2) — liveFamilyMask: per-FAMILY bisection. A KCDX slot
// runs kcdx logic ONLY if its family's bit is set in the mask; otherwise it
// THUNKS to the engine original. The swap MECHANISM always happens identically
// (same object, the [obj+0x00] overwrite, seat timing, index build) — the mask
// chooses which kcdx slot LOGIC is live. This is the direct, by-construction
// proof of "which family causes the black screen":
//   mask == 0 (kFamNone)  → the PROBE Z no-op swap (all thunk; must render).
//   mask == kFamAll       → a normal FULL swap (all live; must reproduce black —
//                           the confound self-check: if all-live RENDERS, the
//                           premise is contaminated and there is a confound).
//   one family bit        → only that family live (build-up: which one flips black).
//   kFamAll minus one     → only that family thunked (tear-down: removing which
//                           one fixes black).
// NO-RESIDUE: the param + the mask plumbing revert with PROBE Z2.
//
// Family bits (the four KCDX-owned slot families, vtable_table.cpp):
enum FsSlotFamily : uint32_t {
    kFamNone     = 0,
    kFamOpen     = 1u << 0,  // 1/35/36   AdjustFileName/FOpenRaw/FOpen
    kFamRead     = 1u << 1,  // 38..66    handle-operating reads
    kFamMetadata = 1u << 2,  // 13/45/67/68/69/70/92/93  existence/size/stat
    kFamEnum     = 1u << 3,  // 14/63/64/65  ForEachFile + FindFirst/Next/Close
    kFamAll      = kFamOpen | kFamRead | kFamMetadata | kFamEnum,
};
bool SwapVtableOnObject(void* pCryPak, uint32_t liveFamilyMask = kFamAll);

// === DIAGNOSTIC (PROBE U) — the kcdx vtable buffer address kcdx wrote into the
// CCryPak object at the swap, for the post-seat reswap watcher. NO-RESIDUE. ===
void* GetKcdxVtableAddr();

}  // namespace kcdx::fs_takeover
