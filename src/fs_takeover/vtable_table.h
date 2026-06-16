#pragma once

// The per-slot declarative vtable table — the single point of CCryPak slot
// ownership. Each of the 102 CCryPak vtable slots is one row { slot, role,
// impl }; impl is either KCDX (a kcdx function pointer owns the slot) or THUNK
// (the slot forwards to the engine's original body, captured live at swap
// time).
//
// REVERSIBILITY is a load-bearing property, not an implementation detail:
// flipping a slot from THUNK to KCDX is a ONE-LINE edit of that row's `impl`
// (plus writing the kcdx impl). No code OUTSIDE this table assumes a slot's
// ownership; the construction (vtable_swap) reads ownership only from these
// rows. A reviewer checks that nothing downstream hardcodes "slot N is the
// engine's" or "slot N is kcdx's" — the table is the sole source of that fact.
//
// THE OPEN+READ CUTOVER (step 3.2): the OPEN family (slots 1/35/36) and the
// READ family (slots 38/39/40/41/43/44/46/47/53/54/55/56/57/58/59/66) are KCDX
// — kcdx owns every file open + read on its own CRT (design §4.4/§4.5/§5). Every
// other slot stays THUNK (the pure-internal plumbing). The slot-36 row carries
// KcdxFOpenMarker, which fires the cap-108 seating signal on its first fire then
// delegates to the real kcdx FOpen impl (open_slots.cpp) — so the seating row
// stays PASS while slot 36 is a real kcdx open. The §4.4 load-bearing
// constraint: every handle-operating READ slot MUST be KCDX, never THUNK (a
// thunked read slot would fread the kcdx handle-id on the ENGINE's CRT — the
// cross-CRT straddle the takeover removes).

#include <cstddef>
#include <cstdint>

namespace kcdx::fs_takeover {

// The CCryPak vtable slot count, verified by RTTI COL walk against the binary
// (slot 102 / +0x300 is non-exec = end). A table-data constant describing the
// vtable's own layout — NOT a game-binary address that shifts per version.
constexpr size_t kCCryPakSlotCount = 102;

// Slot 36 = FOpen, the path-resolution open every by-name file consumer
// dispatches through. The seating marker rides this slot. A vtable slot INDEX
// describing the vtable's own layout (table data), not a per-version-volatile
// game-binary target — verified by static analysis of the binary's vtable
// surface, not assumed from a header.
constexpr size_t kSlotFOpen = 36;

// Slot 1 = AdjustFileName, the resolution chokepoint. The swap captures its
// ORIGINAL body (the same way it captures slot 36's original) for kcdx's slot-1
// impl to thunk through on an index MISS (the §5 long-tail resolution). A vtable
// slot INDEX (table data), not a game-binary target.
constexpr size_t kSlotAdjustFileName = 1;

// How a slot is served in the built kcdx vtable.
enum class Impl {
    // kcdx owns the slot: the built vtable entry is the kcdx function pointer
    // named on the row.
    Kcdx,
    // The slot forwards to the engine's original body: the built vtable entry
    // is the ORIGINAL object's slot pointer, read from the live object's
    // current vtable at swap time (so the thunk runs the real engine code
    // against the same — layout-preserved — object).
    Thunk,
};

// One declarative row of the per-slot table.
struct SlotRow {
    size_t      slot;   // the vtable slot index this row describes.
    const char* role;   // human-readable role (the verified slot meaning), for review + logging.
    Impl        impl;   // KCDX vs THUNK — the slot's ownership, the one thing downstream reads.
    // The kcdx function pointer for a Kcdx row; nullptr for a Thunk row (the
    // original slot pointer is supplied at construction time from the live
    // object). For a Thunk row this stays null — construction substitutes the
    // captured original.
    void*       kcdx_fn;
};

// The slot-36 kcdx impl: on its FIRST fire it emits the cap-108 seating signal
// (the swap is live — the engine dispatched into kcdx), then on EVERY fire it
// delegates to the real kcdx FOpen impl (open_slots.cpp kcdx_FOpen — resolve via
// the index, open on kcdx's CRT, mint a kcdx handle). The seating marker is now
// the cap-108 first-fire shim in front of the real open (NOT a thunk to the
// original — slot 36 is a real kcdx open). Defined in vtable_swap.cpp (it owns
// the cap-108 marker latch + report). Member-call shape: (this, pName, szMode,
// nFlags) returning the kcdx handle-id.
void* KcdxFOpenMarker(void* self, const char* pName, const char* szMode,
                      uint32_t nFlags);

// The declarative table: every slot's row. Returns a pointer to the
// kCCryPakSlotCount-entry array and writes the count to *outCount. The array
// is static (process-lifetime, immutable). Construction (vtable_swap) walks
// these rows to build the kcdx vtable.
const SlotRow* GetSlotTable(size_t* outCount);

}  // namespace kcdx::fs_takeover
