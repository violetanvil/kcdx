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
bool SwapVtableOnObject(void* pCryPak);

}  // namespace kcdx::fs_takeover
