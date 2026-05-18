#include "trampoline.h"

#include <windows.h>
#include <cstdint>
#include <mutex>
#include <vector>

#include "log.h"
#include "pe_helpers.h"
#include "plugin_loader.h"  // for kEngineVersion + g_plugins lookup helpers if needed

namespace kcdx::trampoline {

namespace {

// One contiguous reservation from VirtualAlloc. Branch-pool reservations sit
// within ±2 GB of WHGame.dll's .text; local-pool reservations are anywhere.
//
// Memory is reserved with MEM_RESERVE and committed lazily as plugins request
// allocations. We bump-allocate within the reservation — there's no free()
// (matching SKSE's "alloc-only" trampoline pool model).
struct Reservation {
    uint8_t* base = nullptr;    // VirtualAlloc'd region
    size_t   size = 0;          // total reserved bytes
    size_t   used = 0;          // bytes handed out so far
    bool     branch = false;    // true = branch pool (proximity guaranteed), false = local
};

std::mutex                g_mutex;
std::vector<Reservation>  g_reservations;

// Branch pool defaults. Sized to handle ~100 small detour trampolines.
constexpr size_t kBranchPoolReservationSize = 64 * 1024;  // 64 KB per reservation
constexpr size_t kLocalPoolReservationSize  = 1024 * 1024; // 1 MB per reservation

// Align `n` up to a multiple of `align`.
inline size_t AlignUp(size_t n, size_t align) {
    return (n + (align - 1)) & ~(align - 1);
}

// Try to reserve `size` bytes of executable memory whose start address is
// within ±2 GB of `nearAddr`. Returns null on failure.
//
// Strategy: VirtualQuery walks the address space one region at a time —
// each call jumps us past the current region's full extent (free or
// committed) without polling fixed-size steps. We walk outward from
// `nearAddr` first below, then above, looking for a MEM_FREE region big
// enough. This is the standard pattern for "place near address X"
// allocators (SKSE's branch trampoline uses it too).
uint8_t* ReserveNearby(uintptr_t nearAddr, size_t size) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    const uintptr_t granularity = si.dwAllocationGranularity;

    // rel32 displacement is signed 32-bit: window is
    // [nearAddr - 0x80000000, nearAddr + 0x7FFFFFFF]. Allow some safety
    // margin so the FAR END of the reservation also fits (we'll be
    // allocating `size` bytes starting at the chosen base).
    const uintptr_t lowerBound = (nearAddr > 0x7FFF0000ull)
                                    ? (nearAddr - 0x7FFF0000ull) : 0;
    const uintptr_t upperBound = nearAddr + 0x7FFF0000ull - size;

    auto tryReserveAt = [size](uintptr_t addr) -> uint8_t* {
        void* result = VirtualAlloc(reinterpret_cast<LPVOID>(addr), size,
                                    MEM_RESERVE | MEM_COMMIT,
                                    PAGE_EXECUTE_READWRITE);
        return reinterpret_cast<uint8_t*>(result);
    };

    // Walk DOWNWARD from nearAddr first — DLL .text sections often have
    // headroom on the low side because PE images grow upward from their
    // base. Then UPWARD if we struck out below.
    for (int direction = 0; direction < 2; ++direction) {
        uintptr_t cursor = nearAddr;
        while (true) {
            MEMORY_BASIC_INFORMATION mbi{};
            SIZE_T q = VirtualQuery(reinterpret_cast<LPCVOID>(cursor), &mbi, sizeof(mbi));
            if (q == 0) break;  // address invalid (past user space, etc.)

            uintptr_t regionBase = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            uintptr_t regionEnd  = regionBase + mbi.RegionSize;

            if (mbi.State == MEM_FREE) {
                // Find an allocation-granularity-aligned base within this
                // free region big enough to hold `size`.
                uintptr_t alignedBase = AlignUp(regionBase, granularity);
                if (alignedBase + size <= regionEnd &&
                    alignedBase >= lowerBound &&
                    alignedBase <= upperBound) {
                    if (uint8_t* p = tryReserveAt(alignedBase)) return p;
                }
            }

            // Step to the next region. Direction 0 = downward, 1 = upward.
            if (direction == 0) {
                if (regionBase == 0) break;  // hit address space floor
                if (regionBase < lowerBound) break;  // walked out of range
                cursor = regionBase - 1;  // VirtualQuery on regionBase-1
            } else {
                if (regionEnd > upperBound) break;  // walked out of range
                if (regionEnd <= cursor) break;  // didn't advance — safety
                cursor = regionEnd;
            }
        }
    }
    return nullptr;
}

// Get a fresh branch-pool reservation near WHGame.dll's .text. Returns null
// on failure (logs the reason).
Reservation MakeBranchReservation(size_t bytesNeeded) {
    pe::ModuleView mod;
    if (!pe::OpenModule(L"WHGame.dll", mod)) {
        log::Error("Trampoline branch pool: WHGame.dll not loaded");
        return {};
    }
    // Pick an anchor address inside .text. We use the module's base + half
    // its size as a reasonable midpoint — keeps the reservation reachable
    // from anywhere in the module.
    uintptr_t anchor = reinterpret_cast<uintptr_t>(mod.baseBytes) + (mod.size / 2);

    size_t reservationSize = kBranchPoolReservationSize;
    if (bytesNeeded > reservationSize) reservationSize = AlignUp(bytesNeeded, 0x10000);

    uint8_t* base = ReserveNearby(anchor, reservationSize);
    if (!base) {
        log::ErrorF("Trampoline branch pool: could not reserve %zu bytes within "
                    "+/-2GB of WHGame.dll (anchor 0x%p) — no nearby free region",
                    reservationSize, reinterpret_cast<void*>(anchor));
        return {};
    }
    log::InfoF("Trampoline branch pool: reserved %zu bytes at 0x%p (anchor 0x%p)",
               reservationSize, base, reinterpret_cast<void*>(anchor));
    return Reservation{ base, reservationSize, 0, true };
}

Reservation MakeLocalReservation(size_t bytesNeeded) {
    size_t reservationSize = kLocalPoolReservationSize;
    if (bytesNeeded > reservationSize) reservationSize = AlignUp(bytesNeeded, 0x10000);

    void* base = VirtualAlloc(nullptr, reservationSize,
                              MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!base) {
        log::ErrorF("Trampoline local pool: VirtualAlloc(%zu) failed (err=%lu)",
                    reservationSize, GetLastError());
        return {};
    }
    log::InfoF("Trampoline local pool: reserved %zu bytes at 0x%p",
               reservationSize, base);
    return Reservation{ reinterpret_cast<uint8_t*>(base), reservationSize, 0, false };
}

// Bump-allocate `size` bytes from `r`. Returns null if r doesn't have room.
// Caller holds g_mutex.
void* BumpAlloc(Reservation& r, size_t size) {
    // 16-byte align allocations — code wants natural alignment for the most
    // common access patterns.
    size_t aligned = AlignUp(size, 16);
    if (r.used + aligned > r.size) return nullptr;
    void* result = r.base + r.used;
    r.used += aligned;
    return result;
}

// Try existing reservations of the matching pool first; if none have room,
// make a new reservation.
void* Allocate(bool branchPool, kcdxPluginHandle owner, size_t size) {
    if (size == 0) return nullptr;

    std::lock_guard<std::mutex> lock(g_mutex);

    // Walk existing reservations, first-fit.
    for (auto& r : g_reservations) {
        if (r.branch != branchPool) continue;
        if (void* p = BumpAlloc(r, size)) {
            log::InfoF("Trampoline %s pool: allocated %zu bytes at 0x%p "
                       "(owner=%u, %zu/%zu used)",
                       branchPool ? "branch" : "local", size, p, owner,
                       r.used, r.size);
            return p;
        }
    }

    // No room — add a new reservation.
    Reservation r = branchPool ? MakeBranchReservation(size) : MakeLocalReservation(size);
    if (!r.base) return nullptr;
    void* p = BumpAlloc(r, size);
    g_reservations.push_back(r);
    log::InfoF("Trampoline %s pool: allocated %zu bytes at 0x%p (owner=%u, %zu/%zu used)",
               branchPool ? "branch" : "local", size, p, owner,
               g_reservations.back().used, g_reservations.back().size);
    return p;
}

void* Thunk_AllocateFromBranchPool(kcdxPluginHandle owner, size_t size) {
    return Allocate(/*branchPool=*/true, owner, size);
}

void* Thunk_AllocateFromLocalPool(kcdxPluginHandle owner, size_t size) {
    return Allocate(/*branchPool=*/false, owner, size);
}

const kcdxTrampolineInterface g_iface = {
    /*AllocateFromBranchPool=*/ Thunk_AllocateFromBranchPool,
    /*AllocateFromLocalPool=*/  Thunk_AllocateFromLocalPool,
};

}  // namespace

const kcdxTrampolineInterface* GetInterface() {
    return &g_iface;
}

void* AllocateBranch(kcdxPluginHandle owner, size_t size) {
    return Thunk_AllocateFromBranchPool(owner, size);
}

void* AllocateLocal(kcdxPluginHandle owner, size_t size) {
    return Thunk_AllocateFromLocalPool(owner, size);
}

}  // namespace kcdx::trampoline
