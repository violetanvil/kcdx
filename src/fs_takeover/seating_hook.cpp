#include "seating_hook.h"

#include <windows.h>

#include <atomic>
#include <cstring>

#include "MinHook.h"

#include "asset_index.h"
#include "vtable_swap.h"
#include "../asset_overlay.h"
#include "../log.h"
#include "../paths.h"
#include "../refdb.h"

namespace kcdx::fs_takeover {

namespace {

constexpr const char* kCat = "FS_TAKEOVER";

// The curated refdb names this hook resolves. The construct-store helper is the
// MinHook target; the pCryPak slot resolves to the VA of the global CCryPak*
// pointer slot (the slot the helper publishes into) — its curated row already
// carries the gEnv+0x50 offset, so kcdx never hand-writes that offset here.
constexpr const char* kNameConstructStore = "CSystem_pCryPak_construct_store";
constexpr const char* kNamePCryPakSlot     = "gEnv_pCryPak";

// ABI of the construct-store helper: a single-arg member-style call
// (the CSystem `this`) returning nothing — the curated signature is
// `void (ptr csystem_this)`. The helper constructs CCryPak and publishes the
// pointer into the global env slot; kcdx lets it run, then swaps.
using ConstructStoreFn_t = void (__fastcall*)(void* csystem);

std::atomic<bool> g_installed{false};
std::atomic<bool> g_installSucceeded{false};

// The captured original helper (the MinHook trampoline). The callback calls it
// so the helper runs verbatim; kept process-lifetime (the callback may fire
// more than once across the process, though once is observed).
std::atomic<ConstructStoreFn_t> g_originalHelper{nullptr};

// One-shot: the swap is performed on the FIRST helper return (the publish that
// matters). A defensive latch — if the helper ever re-runs, the swap is
// idempotent anyway (SwapVtableOnObject re-swaps but never rebuilds), but the
// latch keeps the post-publish swap to the first, race-free, publish.
std::atomic<bool> g_swapped{false};

// One-shot: the asset index is built + stored exactly once, after the swap
// takes, on the first publish — mirroring g_swapped. The index build is cold
// (cold path, at load — asset_index.h §5; memory.md allows allocation off the
// hot path). A re-fire of the single-call helper does not rebuild.
std::atomic<bool> g_indexBuilt{false};

// Defined below HookedConstructStore (which calls it): gate on overlay-ready,
// then build + store the asset index.
void BuildAssetIndexAtSeat();

// Read the published CCryPak pointer from the global pCryPak slot. The slot's
// VA is resolved by curated name (its row carries the gEnv+0x50 offset, so no
// literal offset is written in source); the value at that VA is the CCryPak*.
void* ReadPublishedCCryPak() {
    const uintptr_t slotVa = kcdx::refdb::ResolveAddrByName(kNamePCryPakSlot);
    if (!slotVa) {
        LOG_ERROR_KV(kCat, "pcrypak_slot_resolve_failed",
            kcdx::log::KV::BareStr("name", kNamePCryPakSlot),
            kcdx::log::KV::BareStr("detail",
                "refdb could not resolve the global pCryPak slot by name — the "
                "swap cannot find the published CCryPak object this boot; the "
                "engine keeps its own vtable. See the preceding REFDB error."));
        return nullptr;
    }
    void* pCryPak = nullptr;
    std::memcpy(&pCryPak, reinterpret_cast<const void*>(slotVa), sizeof(pCryPak));
    return pCryPak;
}

void __fastcall HookedConstructStore(void* csystem) {
    // AFTER-hook: let the helper run first so it constructs CCryPak and
    // publishes the pointer into the global slot exactly as vanilla.
    ConstructStoreFn_t orig = g_originalHelper.load(std::memory_order_acquire);
    if (orig) {
        orig(csystem);
    } else {
        // Cannot happen on a completed install (the trampoline is captured
        // before the hook is enabled); fail loud rather than silently skip the
        // engine's own construction (which would leave gEnv+0x50 unpublished).
        LOG_ERROR_KV(kCat, "seating_no_original",
            kcdx::log::KV::BareStr("detail",
                "the construct-store hook fired but the captured original "
                "helper is null — the engine's CCryPak construction did NOT "
                "run this call. The published pointer will be absent; the swap "
                "is skipped."));
        return;
    }

    // The helper has now published the CCryPak pointer into the global slot.
    // Perform the swap exactly once, on this first publish — BEFORE the engine
    // makes its first file call through the slot (which CSystem::Init does
    // right after this helper returns).
    bool expected = false;
    if (!g_swapped.compare_exchange_strong(expected, true,
                                           std::memory_order_acq_rel)) {
        return;  // already swapped this session
    }

    void* pCryPak = ReadPublishedCCryPak();
    LOG_INFO_KV(kCat, "seating_post_publish",
        kcdx::log::KV("pCryPak", reinterpret_cast<uintptr_t>(pCryPak)),
        kcdx::log::KV::BareStr("detail",
            "construct-store helper returned; the CCryPak pointer is published. "
            "Performing the vtable swap now, before the engine's first file "
            "call through the object."));

    if (!SwapVtableOnObject(pCryPak)) {
        // SwapVtableOnObject already logged the specific reason. Leave g_swapped
        // set: a null/failed object will not become valid on a re-fire of this
        // single-call helper, and re-attempting per call would spam. The engine
        // keeps its own vtable this boot.
        LOG_WARN_KV(kCat, "seating_swap_skipped",
            kcdx::log::KV::BareStr("detail",
                "the vtable swap did not complete (see the preceding "
                "FS_TAKEOVER error for the reason) — kcdx does NOT own the "
                "engine filesystem this boot; boot proceeds with the engine's "
                "own CCryPak vtable."));
        // No swap → no kcdx dispatch → no asset index needed this boot. The
        // slots stay the engine's; skip the index build entirely.
        return;
    }

    // The swap took. Build the asset index BEFORE returning — the engine's first
    // file call through the swapped object comes AFTER this helper returns (P1:
    // the construct-store store point PRECEDES the first *(gEnv+0x50) file call),
    // so the index must exist by the time this callback returns. Build exactly
    // once (g_indexBuilt latch, mirroring g_swapped).
    bool indexExpected = false;
    if (g_indexBuilt.compare_exchange_strong(indexExpected, true,
                                             std::memory_order_acq_rel)) {
        BuildAssetIndexAtSeat();
    }
}

// Gate on the overlay-ready event, then build + store the asset index over both
// vanilla pak roots (Data + Engine). The seat
// (game's main thread) must NOT build the index off an empty overlay map: the
// worker fills the overlay map in BuildOverlayMap and SIGNALS overlay-ready
// after it; this WAITS on that gate (the ACQUIRE edge — an explicit
// happens-before edge, never a timing margin; concurrency.md). INFINITE is
// correct: the worker WILL signal unless it hangs entirely (same as the ctor
// bracket's wait). On a wait failure the index is NOT built against a possibly-
// empty overlay map — that degradation is logged LOUD (AP14), never silent.
void BuildAssetIndexAtSeat() {
    HANDLE gate = kcdx::asset_overlay::GetOverlayReadyEventHandle();
    if (!gate) {
        // CreateOverlayReadyEvent did not run / its CreateEventW failed (already
        // logged loud at creation). Without the gate the seat cannot know the
        // overlay map is complete — building the index now risks an empty
        // overlay map. Fail loud + skip the build (AP14: no silent vanilla-only
        // index).
        LOG_ERROR_KV(kCat, "seat_index_no_gate",
            kcdx::log::KV::BareStr("detail",
                "the overlay-ready gate handle is null at the seat — "
                "CreateOverlayReadyEvent did not run before InstallSeatingHook "
                "(or its CreateEventW failed, already logged). The asset index "
                "is NOT built (it could only be built against a possibly-empty "
                "overlay map); kcdx's file slots will resolve nothing this boot "
                "until the index exists. This is a loud degradation, not a "
                "silent vanilla-only index."));
        return;
    }

    LOG_INFO_KV(kCat, "seat_index_wait_enter",
        kcdx::log::KV::BareStr("detail",
            "waiting on overlay-ready gate (INFINITE) before building the asset "
            "index — the worker signals it right after BuildOverlayMap, so this "
            "blocks only until the overlay map is complete (typically already "
            "signaled by the time the seat reaches here)."));

    const DWORD wr = WaitForSingleObject(gate, INFINITE);
    if (wr != WAIT_OBJECT_0) {
        // INFINITE should only ever return WAIT_OBJECT_0 or WAIT_FAILED. A
        // failure means the gate did not resolve — do NOT build the index
        // against a possibly-empty overlay map (AP14).
        LOG_ERROR_KV(kCat, "seat_index_gate_failed",
            kcdx::log::KV("wait_result", (uint64_t)wr),
            kcdx::log::KV("win32_err", (uint64_t)GetLastError()),
            kcdx::log::KV::BareStr("detail",
                "WaitForSingleObject on the overlay-ready gate did not return "
                "WAIT_OBJECT_0 — the asset index could NOT be built because the "
                "overlay-ready gate did not resolve. The index is NOT built (no "
                "silent build against a possibly-empty overlay map); kcdx's file "
                "slots resolve nothing this boot. Loud degradation, not a silent "
                "vanilla-only index."));
        return;
    }

    // Gate resolved — the overlay map is complete. Build the full index over
    // BOTH vanilla pak roots — <game-root>/Data AND <game-root>/Engine — and
    // store it process-lifetime where the slot impls read it. The Engine root
    // carries the engine's own archives (Engine.pak holds e.g.
    // %engine%/config/engine_core.thread_config); covering it makes every
    // engine-pak file an index HIT kcdx serves, not a miss the engine fatals on
    // at graphics-init (KI-0026; design §5 v1.8). BuildAssetIndex emits its own
    // "asset_index_built" DEBUG summary (entry/roots/pak/loose counts).
    LOG_INFO_KV(kCat, "seat_index_building",
        kcdx::log::KV::BareStr("detail",
            "overlay-ready; building asset index over <game-root>/Data + "
            "<game-root>/Engine (the overlay map is complete — loose overrides "
            "will overwrite their pak entries)."));

    const std::wstring dataDir =
        (kcdx::paths::GameRootDirPath() / L"Data").wstring();
    const std::wstring engineDir =
        (kcdx::paths::GameRootDirPath() / L"Engine").wstring();
    AssetIndex index = BuildAssetIndex(dataDir, engineDir);
    const size_t entryCount = index.size();
    SetBuiltIndex(std::move(index));

    LOG_INFO_KV(kCat, "seat_index_stored",
        kcdx::log::KV("entries", (uint64_t)entryCount),
        kcdx::log::KV::BareStr("detail",
            "asset index built + stored at seat (process-lifetime) — kcdx's "
            "file slots will resolve against it on every open. The first engine "
            "file call through the swapped object (which follows this helper's "
            "return) sees the complete index."));
}

}  // namespace

bool InstallSeatingHook() {
    bool expected = false;
    if (!g_installed.compare_exchange_strong(expected, true,
                                             std::memory_order_acq_rel)) {
        return g_installSucceeded.load(std::memory_order_acquire);
    }

    // Resolve the construct-store helper by curated name (refdb's cache, built
    // in refdb::Open() — this install runs after that). The returned VA already
    // includes the WHGame base.
    const uintptr_t targetVa = kcdx::refdb::ResolveAddrByName(kNameConstructStore);
    if (!targetVa) {
        LOG_ERROR_KV(kCat, "seating_install_failed",
            kcdx::log::KV::BareStr("name", kNameConstructStore),
            kcdx::log::KV::BareStr("reason",
                "refdb could not resolve the construct-store helper by name — "
                "the seating hook is INACTIVE this boot (the engine keeps its "
                "own CCryPak vtable; boot proceeds vanilla). See the preceding "
                "REFDB error for the specific reason token."));
        return false;
    }

    MH_STATUS si = MH_Initialize();
    if (si != MH_OK && si != MH_ERROR_ALREADY_INITIALIZED) {
        LOG_ERROR_KV(kCat, "seating_install_failed",
            kcdx::log::KV::BareStr("reason",
                "MH_Initialize failed; the seating hook is INACTIVE this boot"),
            kcdx::log::KV("mh_status", static_cast<long long>(si)));
        return false;
    }

    void* targetPtr = reinterpret_cast<void*>(targetVa);
    void* origPtr   = nullptr;
    MH_STATUS s = MH_CreateHook(targetPtr,
                                reinterpret_cast<void*>(&HookedConstructStore),
                                &origPtr);
    if (s != MH_OK) {
        LOG_ERROR_KV(kCat, "seating_install_failed",
            kcdx::log::KV::BareStr("reason",
                "MH_CreateHook on the construct-store helper failed; the "
                "seating hook is INACTIVE this boot"),
            kcdx::log::KV("target", reinterpret_cast<uintptr_t>(targetPtr)),
            kcdx::log::KV("mh_status", static_cast<long long>(s)));
        return false;
    }

    // Capture the trampoline BEFORE enabling — this hook is an after-hook that
    // CALLS the original (unlike a full-replacement hook, which discards it).
    g_originalHelper.store(reinterpret_cast<ConstructStoreFn_t>(origPtr),
                           std::memory_order_release);

    s = MH_EnableHook(targetPtr);
    if (s != MH_OK) {
        LOG_ERROR_KV(kCat, "seating_install_failed",
            kcdx::log::KV::BareStr("reason",
                "MH_EnableHook on the construct-store helper failed; the "
                "seating hook is INACTIVE this boot"),
            kcdx::log::KV("target", reinterpret_cast<uintptr_t>(targetPtr)),
            kcdx::log::KV("mh_status", static_cast<long long>(s)));
        return false;
    }

    LOG_INFO_KV(kCat, "seating_hook_installed",
        kcdx::log::KV("target", reinterpret_cast<uintptr_t>(targetPtr)),
        kcdx::log::KV::BareStr("name", kNameConstructStore),
        kcdx::log::KV::BareStr("detail",
            "filesystem-takeover seating hook armed — when CSystem::Init "
            "reaches the CCryPak construct-store helper on the game's main "
            "thread, the after-hook lets the helper construct + publish "
            "CCryPak, then swaps kcdx's vtable pointer onto the published "
            "object before the engine's first file call through it"));
    g_installSucceeded.store(true, std::memory_order_release);
    return true;
}

}  // namespace kcdx::fs_takeover
