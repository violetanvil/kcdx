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
// THIS spike: all 102 rows are THUNK except slot 36 (FOpen), which is
// KCDX(&kcdx_fopen_marker) — a one-shot marker-then-thunk that proves the swap
// is live (the engine dispatched into kcdx). The real per-slot impls (the file
// family) are a later build; the table SCAFFOLD here is permanent (it is filled
// in, not replaced).

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

// The slot-36 kcdx impl for this spike: logs a one-shot marker on its first
// fire, then forwards to the captured original FOpen body. Defined in
// vtable_swap.cpp (it needs the captured-original storage the swap owns).
// Signature mirrors the engine's FOpen as the swap dispatches it — a member
// call shape: (this, pName, szMode, nFlags) returning the file handle.
void* KcdxFOpenMarker(void* self, const char* pName, const char* szMode,
                      uint32_t nFlags);

// The declarative table: every slot's row. Returns a pointer to the
// kCCryPakSlotCount-entry array and writes the count to *outCount. The array
// is static (process-lifetime, immutable). Construction (vtable_swap) walks
// these rows to build the kcdx vtable.
const SlotRow* GetSlotTable(size_t* outCount);

}  // namespace kcdx::fs_takeover
