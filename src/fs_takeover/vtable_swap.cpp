#include "vtable_swap.h"

#include <atomic>
#include <cstring>

#include "vtable_table.h"
#include "../log.h"
#include "../test.h"

namespace kcdx::fs_takeover {

namespace {

constexpr const char* kCat = "FS_TAKEOVER";

// The test-suite row this seating spike reports into (matches the manifest
// stub's test_names + the matrix row). PASS = the slot-36 marker fired (the
// engine dispatched into kcdx — the swap is live) AND the boot continued
// through the thunked slots normally (every thunked original body ran
// against the swapped object; the marker firing during boot is itself proof a
// chain of thunked slots ran to reach the first open). FALSIFIABLE: never
// reported PASS unless the marker actually fires; a boot where the swap did not
// take leaves the row PENDING (no report), and a boot that crashes with the
// swap active never reaches a later lifecycle PASS — both deny the row.
constexpr const char* kTestRow = "cap-108-fs-takeover-seating";

// The offset of the vtable pointer within the CCryPak object (the standard C++
// object layout: the vptr is the first member). A fixed object-layout fact, not
// a per-version game-binary address.
constexpr size_t kVtablePtrOffset = 0x00;

// The built kcdx vtable — a process-lifetime function-pointer array. It MUST
// outlive the swap for the entire process: the engine reads the vtable pointer
// from the object on every file call, forever, so the array the object points
// at can never be freed. A static buffer is the storage the swap's own
// mechanics require (a stack/heap-freed array would dangle the instant the
// engine next dispatched) — owned process-lifetime, never reclaimed.
void* g_kcdxVtable[kCCryPakSlotCount] = {0};
std::atomic<bool> g_vtableBuilt{false};

// The captured original FOpen body (slot kSlotFOpen) — the marker thunks
// through to it. Captured from the live object's original vtable at swap time.
// A member-call shape: (this, pName, szMode, nFlags) → file handle.
using FOpenFn_t = void* (*)(void* self, const char* pName, const char* szMode,
                            uint32_t nFlags);
std::atomic<FOpenFn_t> g_originalFOpen{nullptr};

// One-shot latch for the seating marker: FOpen is hot, so the marker logs +
// reports exactly once (the FIRST fire), never per-call.
std::atomic<bool> g_markerFired{false};

}  // namespace

// Slot-36 impl: the seating marker. On its FIRST fire it logs + self-reports
// the seating row PASS (the engine dispatched into kcdx — the swap is
// live), then forwards to the captured original FOpen so the engine's open
// behaves exactly as vanilla. Every subsequent fire is a straight thunk (the
// latch is already set) — zero added per-call cost beyond one relaxed atomic
// load.
void* KcdxFOpenMarker(void* self, const char* pName, const char* szMode,
                      uint32_t nFlags) {
    bool expected = false;
    if (g_markerFired.compare_exchange_strong(expected, true,
                                               std::memory_order_relaxed)) {
        // First fire — the swap is proven live: the engine read kcdx's vtable
        // pointer off the object and dispatched slot 36 into this kcdx fn.
        LOG_INFO_KV(kCat, "swap_live_first_open",
            kcdx::log::KV::BareStr("detail",
                "slot 36 (FOpen) dispatched into kcdx on the first vanilla "
                "open — the vtable swap on the CCryPak object is LIVE; the "
                "engine reads kcdx's vtable pointer and routes file calls "
                "into kcdx. The marker now thunks through to the original "
                "FOpen body so the open behaves exactly as vanilla."),
            kcdx::log::KV::BareStr("first_vpath", pName ? pName : "<null>"));

        // The marker firing during boot also evidences the thunked slots: the
        // engine reached its first file open by running a chain of thunked
        // original bodies against the swapped object without crashing. Report
        // the seating row PASS — the live confirmation the matrix reads.
        kcdx::test::ReportResult(kTestRow, true,
            "slot-36 FOpen marker fired (swap live, engine dispatched into "
            "kcdx) and boot reached the first open through thunked slots");
    }

    // Thunk through to the captured original body — the marker is marker-THEN-
    // thunk, never a replacement. If the original was somehow not captured
    // (cannot happen on a completed swap), fail loud rather than return a
    // silent null handle.
    FOpenFn_t orig = g_originalFOpen.load(std::memory_order_acquire);
    if (!orig) {
        LOG_ERROR_KV(kCat, "marker_no_original",
            kcdx::log::KV::BareStr("detail",
                "slot-36 marker fired but the captured original FOpen body is "
                "null — the swap completed without capturing the original "
                "(a programming error). Returning null; the engine sees a "
                "failed open rather than a silently broken handle."));
        return nullptr;
    }
    return orig(self, pName, szMode, nFlags);
}

bool SwapVtableOnObject(void* pCryPak) {
    if (!pCryPak) {
        LOG_ERROR_KV(kCat, "swap_null_object",
            kcdx::log::KV::BareStr("detail",
                "SwapVtableOnObject called with a null CCryPak pointer — the "
                "object was not published into gEnv+0x50 yet, or the read of "
                "that slot returned null. No swap performed; the engine keeps "
                "its own vtable this boot."));
        return false;
    }

    // Read the object's CURRENT vtable pointer from [pCryPak+0x00] — this is
    // the ORIGINAL engine vtable, the source of every THUNK slot's body.
    void** originalVtable = nullptr;
    std::memcpy(&originalVtable,
                static_cast<uint8_t*>(pCryPak) + kVtablePtrOffset,
                sizeof(originalVtable));
    if (!originalVtable) {
        LOG_ERROR_KV(kCat, "swap_null_vtable",
            kcdx::log::KV::BareStr("detail",
                "the CCryPak object's current vtable pointer ([obj+0x00]) is "
                "null — the object is not fully constructed. No swap "
                "performed; the engine keeps its own vtable this boot."));
        return false;
    }

    // Build the kcdx vtable once from the per-slot table: THUNK rows get the
    // original object's slot pointer (so the thunk forwards to the real engine
    // body, run against the SAME object); the KCDX slot-36 row gets the kcdx
    // marker fn. Idempotent — a re-publish re-swaps but never rebuilds.
    bool buildExpected = false;
    if (g_vtableBuilt.compare_exchange_strong(buildExpected, true,
                                              std::memory_order_acq_rel)) {
        size_t count = 0;
        const SlotRow* table = GetSlotTable(&count);
        size_t kcdxOwned = 0;
        for (size_t i = 0; i < count; ++i) {
            const SlotRow& row = table[i];
            if (row.impl == Impl::Kcdx) {
                g_kcdxVtable[i] = row.kcdx_fn;
                ++kcdxOwned;
                // Capture the original body the slot-36 marker thunks through
                // to, from the live object's original vtable at this slot.
                if (row.slot == kSlotFOpen) {
                    g_originalFOpen.store(
                        reinterpret_cast<FOpenFn_t>(originalVtable[row.slot]),
                        std::memory_order_release);
                }
            } else {
                // THUNK: forward to the engine's original body for this slot.
                g_kcdxVtable[i] = originalVtable[row.slot];
            }
        }
        LOG_INFO_KV(kCat, "kcdx_vtable_built",
            kcdx::log::KV("slots", static_cast<uint64_t>(count)),
            kcdx::log::KV("kcdx_owned", static_cast<uint64_t>(kcdxOwned)),
            kcdx::log::KV::BareStr("detail",
                "built the kcdx CCryPak vtable from the per-slot table — every "
                "THUNK slot points at the engine's original body (captured "
                "from the live object), the one KCDX slot (36, FOpen) points "
                "at the kcdx seating marker"));
    }

    // The swap: write the kcdx vtable's address into [pCryPak+0x00] on the
    // EXISTING object. Only the vtable pointer changes — no member relocated,
    // no fresh object — so every THUNK forwards to a body that reads the same
    // intact object layout.
    void* kcdxVtablePtr = static_cast<void*>(g_kcdxVtable);
    std::memcpy(static_cast<uint8_t*>(pCryPak) + kVtablePtrOffset,
                &kcdxVtablePtr, sizeof(kcdxVtablePtr));

    LOG_INFO_KV(kCat, "vtable_swapped",
        kcdx::log::KV("object", reinterpret_cast<uintptr_t>(pCryPak)),
        kcdx::log::KV("original_vtable", reinterpret_cast<uintptr_t>(originalVtable)),
        kcdx::log::KV("kcdx_vtable", reinterpret_cast<uintptr_t>(kcdxVtablePtr)),
        kcdx::log::KV::BareStr("detail",
            "kcdx vtable pointer written onto the existing CCryPak object — "
            "kcdx now owns the object's dispatch. Every subsequent engine "
            "file call reads this pointer and routes through kcdx (slot 36) "
            "or a captured original body (every other slot). The first FOpen "
            "fires the seating marker (proving the swap is live); the boot "
            "reaching the world exercises the thunked slots."));
    return true;
}

}  // namespace kcdx::fs_takeover
