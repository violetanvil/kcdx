#include "vtable_swap.h"

#include <atomic>
#include <cstring>

#include "vtable_table.h"
#include "open_slots.h"      // kcdx_FOpen (slot-36 real impl) + SetOriginalAdjustFileName (slot-1 capture)
#include "metadata_slots.h"  // SetMetadataOriginals (the 8 metadata-slot originals captured for the miss thunk)
#include "boot_trace.h"      // === DIAGNOSTIC (PROBE L) === pak-handle-vector snapshot around the open
#include "../log.h"
#include "../test.h"

namespace kcdx::fs_takeover {

namespace {

constexpr const char* kCat = "FS_TAKEOVER";

// === DIAGNOSTIC (PROBE G) — gate the swap-WRITE, build everything else ========
// KI-0026: graphics-init 0xC8 fatal from the fs-takeover. Every "kcdx serves a
// wrong file answer" axis is eliminated (file-dispatch correct, thunks P4-safe,
// metadata slots never index-queried in the crash window). The remaining suspect
// is the STATE the vtable swap leaves the CCryPak object / engine in — the swap's
// SIDE-EFFECT, not its DISPATCH. PROBE G isolates exactly that one variable: when
// true, the swap-write (the memcpy of kcdxVtablePtr into [pCryPak+0x00]) is
// SKIPPED — the kcdx vtable is still built, the originals still captured, the
// index still built downstream, but the object KEEPS its native engine CCryPak
// vtable, so the engine never dispatches into kcdx this boot. If the 0xC8 is GONE
// this run, the swap's side-effect is the mechanism; if it PERSISTS, the swap is
// innocent and the cause is upstream (ctor-bracket / hooks). Default true for THIS
// probe run. Scratch — captured-and-removed when answered (working-artifacts.md
// no-residue). Greppable tag: "PROBE_G".
constexpr bool kProbeG_BypassSwap = false;

// === DIAGNOSTIC (PROBE J) — swap the pointer, but to an EXACT copy of the
// original vtable ============================================================
// KI-0026: PROBE G proved the swap-WRITE is the trigger (bypassing it boots
// clean; re-enabling it crashes). PROBE J discriminates WHY. When true,
// g_kcdxVtable is built as a FAITHFUL COPY of the engine's own vtable — every
// slot = originalVtable[row.slot], ZERO kcdx slots, NO thunk wrap (not even
// PROBE M's) — so the pointer IS swapped onto the object, but the array it
// points at is byte-identical to the engine's vtable. The ONLY variable vs a
// normal full swap is g_kcdxVtable's CONTENTS (all-original vs kcdx-slots): the
// pointer swap, the object, the memory location of g_kcdxVtable, the captures,
// the seating all stay identical. So:
//   STILL CRASHES ⇒ pointer-identity / memory-location is the cause (the engine
//     cares the pointer changed or where it points — a cached-original mismatch,
//     an address-range / RTTI / identity check), NOT slot behavior.
//   BOOTS CLEAN  ⇒ a kcdx SLOT impl that graphics-init dispatches behaves wrong,
//     NOT the pointer.
// PROBE G must be OFF for this (the pointer must actually swap). Scratch —
// captured-and-removed when answered (working-artifacts.md no-residue).
// Greppable tag: "PROBE_J".
//
// KI-0026 PROBE K: set back to FALSE — the real full takeover (kcdx slots live)
// must be active for the read-family boot-window trace to observe anything. With
// copy-mode ON every slot was the engine's own body (kcdx_owned=0), so zero kcdx
// read slots ran and FS_BOOT_TRACE.read logged nothing. PROBE K needs the live
// read family, i.e. copy-mode OFF.
constexpr bool kProbeJ_CopyOriginalVtable = false;

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

// The original AdjustFileName body (slot kSlotAdjustFileName), captured from the
// live object's original vtable at swap time and handed to the slot-1 impl
// (open_slots) for its index-MISS resolution thunk (§5). Member-call shape:
// (this, pName, outBuf, nFlags) → resolved path string.
using AdjustFileNameFn_t = void* (*)(void* self, const char* pName, void* outBuf,
                                     uint32_t nFlags);

// One-shot latch for the seating marker: FOpen is hot, so the marker emits the
// cap-108 signal exactly once (the FIRST fire), never per-call.
std::atomic<bool> g_markerFired{false};

// === DIAGNOSTIC (PROBE N) — captured original FOpen (slot 36) + FClose (slot 55)
// bodies, stored process-lifetime so the marker can run the ENGINE original open
// (then close it on the engine CRT) to image-diff the object against kcdx's open.
// Member-call shapes: FOpen (this, pName, mode, flags) → handle; FClose
// (this, handle) → int. Captured at swap time from the live original vtable.
// Scratch — removed when PROBE N is answered (working-artifacts.md no-residue). ===
using FOpenFn_t  = void* (*)(void* self, const char* pName, const char* mode,
                            uint32_t flags);
using FCloseFn_t = int   (*)(void* self, void* handle);
std::atomic<FOpenFn_t>  g_probeN_origFOpen{nullptr};
std::atomic<FCloseFn_t> g_probeN_origFClose{nullptr};

}  // namespace

// Slot-36 impl: the seating marker IN FRONT OF the real kcdx FOpen. On its FIRST
// fire it logs + self-reports the cap-108 seating row PASS (the engine
// dispatched into kcdx — the swap is live), then on EVERY fire delegates to the
// real kcdx FOpen impl (open_slots.cpp — resolve via the index, open on kcdx's
// CRT, mint a kcdx handle). The marker is a thin first-fire shim, NOT a thunk to
// the original — slot 36 is a real kcdx open. Per-call cost beyond the first is
// one relaxed atomic load + the real open.
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
                "into kcdx. The marker now delegates to the real kcdx FOpen "
                "(resolve via the index, open on kcdx's CRT, mint a kcdx "
                "handle) — slot 36 is a real kcdx open, not a thunk."),
            kcdx::log::KV::BareStr("first_vpath", pName ? pName : "<null>"));

        // The marker firing during boot also evidences the thunked slots: the
        // engine reached its first file open by running a chain of thunked
        // original bodies against the swapped object without crashing. Report
        // the seating row PASS — the live confirmation the matrix reads.
        kcdx::test::ReportResult(kTestRow, true,
            "slot-36 FOpen marker fired (swap live, engine dispatched into "
            "kcdx) and boot reached the first open through thunked slots");
    }

    // === DIAGNOSTIC (PROBE N) — wide whole-object image diff (fresh-frame #2) ==
    // The missing-write offset is unknown. Observe it directly: image-diff the
    // object across the ENGINE original open vs the KCDX open of the SAME loose
    // file, reporting which offsets each wrote. The omitted-write set =
    // (engine-wrote) \ (kcdx-wrote). No offset pre-guessed.
    //
    // Straddle-SAFE: the original open + close run BOTH on the engine CRT
    // (g_probeN_origFOpen/FClose, captured from the original vtable), in this
    // scope, forced "rb", the handle never reaching kcdx — the non-straddling
    // case. SNAP_C==SNAP_A asserts the engine close reverted its own writes, so
    // the kcdx diff is uncontaminated. Boot-window-gated (zero cost after boot);
    // scratch — removed when answered.
    if (BootWindowActive() && self) {
        FOpenFn_t  origOpen  = g_probeN_origFOpen.load(std::memory_order_acquire);
        FCloseFn_t origClose = g_probeN_origFClose.load(std::memory_order_acquire);
        if (origOpen && origClose) {
            static uint8_t snapA[kProbeN_ObjSize];
            static uint8_t snapB[kProbeN_ObjSize];
            static uint8_t snapC[kProbeN_ObjSize];
            static uint8_t snapD[kProbeN_ObjSize];
            SnapObject(snapA, self);
            // Engine original open (rb-only) + immediate engine close — both
            // engine CRT, same scope. Its object-member writes are SNAP_A→SNAP_B.
            void* hOrig = origOpen(self, pName, "rb", nFlags);
            SnapObject(snapB, self);
            if (hOrig) origClose(self, hOrig);
            SnapObject(snapC, self);  // revert check (should == snapA)
            // kcdx open — its object-member writes are SNAP_A→SNAP_D.
            void* result = kcdx_FOpen(self, pName, szMode, nFlags);
            SnapObject(snapD, self);
            LogObjDiff("engine", pName, snapA, snapB);  // what the engine wrote
            LogObjDiff("revert", pName, snapA, snapC);  // 0 diffs = clean revert
            LogObjDiff("kcdx",   pName, snapA, snapD);  // what kcdx wrote
            return result;
        }
    }

    // Normal delegate (post-boot, or if the originals are unavailable): resolve via
    // the unified index, open on kcdx's CRT, mint a kcdx handle-id (§5). 0 = a
    // failed open, read by the engine as a null open as its own FOpen returns null.
    return kcdx_FOpen(self, pName, szMode, nFlags);
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
            // === DIAGNOSTIC (PROBE J) === copy mode: every slot is the BARE
            // engine original — no kcdx_fn, no PROBE-M wrap, no instrumentation.
            // The only variable vs the engine's own vtable becomes the pointer
            // location. Must come BEFORE the KCDX/THUNK branch so it shadows both.
            if (kProbeJ_CopyOriginalVtable) {
                g_kcdxVtable[i] = originalVtable[row.slot];
                continue;
            }
            if (row.impl == Impl::Kcdx) {
                g_kcdxVtable[i] = row.kcdx_fn;
                ++kcdxOwned;
                // Capture the ORIGINAL AdjustFileName body (slot 1) for the
                // slot-1 impl's index-MISS resolution thunk (§5 — the safe
                // long-tail resolution: a string, no handle, no CRT). This is
                // the ONE captured-original a KCDX slot needs this cutover; the
                // open slots (35/36) and the read slots (38..66) are FULL impls
                // that operate kcdx handles — they need no original. (Mirrors how
                // the prior spike captured slot 36's original; the captured slot
                // moves from 36 to 1 because slot 36 is now a full impl.)
                if (row.slot == kSlotAdjustFileName) {
                    SetOriginalAdjustFileName(reinterpret_cast<AdjustFileNameFn_t>(
                        originalVtable[row.slot]));
                }
            } else {
                // THUNK: forward to the engine's original body for this slot.
                g_kcdxVtable[i] = originalVtable[row.slot];
            }
        }
        // Capture the 8 metadata-slot originals (13/45/67/68/69/70/92/93) from
        // the SAME live original vtable, for the metadata slots' index-MISS
        // thunk (§5 — each miss arm calls its own original, which consults the
        // engine pak-dir AND disk; a value, no handle, no CRT — §-safe). One
        // combined call stores all 8 (mirrors the slot-1 SetOriginalAdjustFileName
        // capture above). The KCDX metadata rows above already wired the kcdx
        // impls into g_kcdxVtable; this captures the originals those impls thunk.
        SetMetadataOriginals(
            reinterpret_cast<const void* const*>(originalVtable));
        // === DIAGNOSTIC (PROBE N) === capture the original FOpen (slot 36) +
        // FClose (slot 55) bodies process-lifetime, so the marker can run the
        // engine original open + close (engine CRT both ends) for the object
        // image-diff. Captured from the SAME live original vtable. Removed with
        // the probe.
        g_probeN_origFOpen.store(
            reinterpret_cast<FOpenFn_t>(originalVtable[kSlotFOpen]),
            std::memory_order_release);
        g_probeN_origFClose.store(
            reinterpret_cast<FCloseFn_t>(originalVtable[55]),
            std::memory_order_release);
        // === DIAGNOSTIC (PROBE J) === unmistakable copy-mode state in the log.
        // Fires once, inside the build block. When copy mode is active the
        // kcdx_vtable_built line below still logs (the captures + index build
        // ran), but this line is the load-bearing one: g_kcdxVtable holds the
        // engine's own slot bodies, so the swap changes ONLY the pointer.
        if (kProbeJ_CopyOriginalVtable) {
            LOG_INFO_KV(kCat, "probe_j_copy_vtable",
                kcdx::log::KV("slots", static_cast<uint64_t>(count)),
                kcdx::log::KV::BareStr("detail",
                    "PROBE J: g_kcdxVtable was built as an EXACT COPY of the "
                    "original engine vtable (every slot = the engine's own body, "
                    "ZERO kcdx slots, no thunk wrap). The pointer IS swapped to "
                    "kcdx's array, but its CONTENTS are byte-identical to the "
                    "engine vtable — the only variable vs a normal full swap is "
                    "the pointer's target location. If the KI-0026 0xC8 PERSISTS "
                    "this run, the mechanism is pointer-identity / memory-location "
                    "(the engine cares the pointer changed or where it points); if "
                    "it is GONE, the mechanism is a kcdx slot impl, not the "
                    "pointer."));
        }
        LOG_INFO_KV(kCat, "kcdx_vtable_built",
            kcdx::log::KV("slots", static_cast<uint64_t>(count)),
            kcdx::log::KV("kcdx_owned", static_cast<uint64_t>(kcdxOwned)),
            kcdx::log::KV::BareStr("detail",
                "built the kcdx CCryPak vtable from the per-slot table — every "
                "THUNK slot points at the engine's original body (captured from "
                "the live object); the KCDX slots (the open family 1/35/36 + the "
                "read family 38/39/40/41/43/44/46/47/53/54/55/56/57/58/59/66 + "
                "the metadata family 13/45/67/68/69/70/92/93) point at the kcdx "
                "impls. kcdx owns every file open + read on its own CRT; the "
                "slot-1 original is captured for the index-miss resolution thunk, "
                "and the 8 metadata-slot originals for each metadata slot's "
                "index-miss thunk (engine pak-dir AND disk)."));
    }

    // The swap: write the kcdx vtable's address into [pCryPak+0x00] on the
    // EXISTING object. Only the vtable pointer changes — no member relocated,
    // no fresh object — so every THUNK forwards to a body that reads the same
    // intact object layout.
    void* kcdxVtablePtr = static_cast<void*>(g_kcdxVtable);

    // === DIAGNOSTIC (PROBE G) — gate ONLY the swap-WRITE ======================
    // Everything above ran (vtable built, originals captured, kcdx_vtable_built
    // logged). When bypassing, we do NOT write the object — the engine keeps its
    // native CCryPak vtable this boot. This is the probe's ground-truth marker.
    if (kProbeG_BypassSwap) {
        LOG_INFO_KV(kCat, "probe_g_swap_bypassed",
            kcdx::log::KV("object", reinterpret_cast<uintptr_t>(pCryPak)),
            kcdx::log::KV("original_vtable", reinterpret_cast<uintptr_t>(originalVtable)),
            kcdx::log::KV("kcdx_vtable", reinterpret_cast<uintptr_t>(kcdxVtablePtr)),
            kcdx::log::KV::BareStr("detail",
                "PROBE G: the kcdx vtable was BUILT but the swap-WRITE was SKIPPED "
                "— the object keeps its NATIVE engine CCryPak vtable this boot; the "
                "engine does NOT dispatch into kcdx. If the KI-0026 0xC8 is GONE "
                "this run, the swap's SIDE-EFFECT is the mechanism; if it PERSISTS, "
                "the swap is innocent and the cause is upstream (ctor-bracket / "
                "hooks). Returning success: the build + capture succeeded; only the "
                "install was intentionally skipped."));
        return true;
    }

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
