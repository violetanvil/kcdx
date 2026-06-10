#include "trampoline.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>   // snprintf (teaching exhaustion-error formatter)
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "kcdx_trampoline_registry.h"  // record branch-pool ranges as kcdx-owned
                                       // for the foreign-hook classifier (§6.1)
#include "log.h"
#include "pe_helpers.h"
#include "plugin_loader.h"  // AuthorForHandle / NameForHandle (owner -> author/plugin)
#include "symbols.h"        // kcdx::symbols::Register / OwnerOf (export publish)

namespace kcdx::trampoline {

namespace {

// One contiguous reservation from VirtualAlloc. Branch-pool reservations sit
// within ±2 GB of WHGame.dll's .text; local-pool reservations are anywhere.
//
// Memory is reserved with MEM_RESERVE and committed lazily as plugins request
// allocations. We bump-allocate within the reservation — there's no free()
// (matching SKSE's "alloc-only" trampoline pool model).
struct Reservation {
    uint8_t*  base = nullptr;   // VirtualAlloc'd region
    size_t    size = 0;         // total reserved bytes
    size_t    used = 0;         // bytes handed out so far
    bool      branch = false;   // true = branch pool (proximity guaranteed), false = local
    uintptr_t anchor = 0;       // branch only: the VA this reservation was placed
                                // near (WHGame midpoint when nearVa==0, else the
                                // target VA). Used by the rel32-reach reuse test.
    bool      whGameAnchored = false;  // branch only: true iff placed with the
                                       // default WHGame anchor (nearVa==0). The
                                       // nearVa==0 reuse path matches ONLY these;
                                       // a far (nearVa!=0) request never reuses one.
};

std::mutex                g_mutex;
std::vector<Reservation>  g_reservations;

// Branch pool defaults. Sized to handle ~100 small detour trampolines.
constexpr size_t kBranchPoolReservationSize = 64 * 1024;  // 64 KB per reservation
constexpr size_t kLocalPoolReservationSize  = 1024 * 1024; // 1 MB per reservation

// Proactive growth + cap for the BRANCH pool (the rel32-constrained one; the
// local pool grows lazily and is never eagerly expanded — it has no proximity
// constraint and no rel32 pressure). When a branch reservation crosses this
// fraction full, eagerly stage the NEXT ±2 GB-adjacent reservation for the same
// anchor so the next request has headroom (rel32-reachable memory near a hook
// site can be scarce — reserving ahead beats failing a hook at apply time). The
// basis is PER-RESERVATION (the specific reservation just served), not aggregate.
constexpr double kBranchExpandThreshold = 0.80;

// The number of ±2 GB-adjacent branch reservations allowed per anchor group
// before a request is declared exhausted rather than growing the pool without
// bound. 16 * 64 KB = 1 MB of detour bodies reachable from one anchor — far more
// than any realistic hook count near a single site; hitting it means something
// is wrong (a runaway or a near-region starved of free address space), so it is
// an explicit fail-loud, not silent unbounded growth.
constexpr size_t kMaxBranchRegionsPerAnchor = 16;

// rel32 reach window half-width, with safety margin. A signed 32-bit
// displacement spans ±2 GB; we shave it to 0x7FFF0000 so the FAR END of a
// reservation (base + size) still fits when we allocate `size` bytes from the
// chosen base. ReserveNearby's bounds AND the reservation-reuse predicate in
// Allocate() both use this single constant so "did we place it in range?" and
// "is this existing reservation in range?" can never disagree.
constexpr uintptr_t kRel32Margin = 0x7FFF0000ull;

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
    const uintptr_t lowerBound = (nearAddr > kRel32Margin)
                                    ? (nearAddr - kRel32Margin) : 0;
    const uintptr_t upperBound = nearAddr + kRel32Margin - size;

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

// Get a fresh branch-pool reservation anchored near `nearVa` (within ±2 GB so
// a rel32 jmp from a hook site can reach it). When `nearVa == 0`, anchor near
// WHGame.dll's .text midpoint, as before. Returns null on failure (logs the
// reason).
Reservation MakeBranchReservation(size_t bytesNeeded, uintptr_t nearVa) {
    uintptr_t anchor = nearVa;
    if (anchor == 0) {
        pe::ModuleView mod;
        if (!pe::OpenModule(L"WHGame.dll", mod)) {
            log::Error("Trampoline branch pool: WHGame.dll not loaded");
            return {};
        }
        // Pick an anchor address inside .text. We use the module's base + half
        // its size as a reasonable midpoint — keeps the reservation reachable
        // from anywhere in the module.
        anchor = reinterpret_cast<uintptr_t>(mod.baseBytes) + (mod.size / 2);
    }

    size_t reservationSize = kBranchPoolReservationSize;
    if (bytesNeeded > reservationSize) reservationSize = AlignUp(bytesNeeded, 0x10000);

    uint8_t* base = ReserveNearby(anchor, reservationSize);
    if (!base) {
        log::ErrorF("Trampoline branch pool: could not reserve %zu bytes within "
                    "+/-2GB of anchor 0x%p (%s) — no nearby free region",
                    reservationSize, reinterpret_cast<void*>(anchor),
                    nearVa ? "target module" : "WHGame.dll midpoint");
        return {};
    }
    log::InfoF("Trampoline branch pool: reserved %zu bytes at 0x%p (anchor 0x%p)",
               reservationSize, base, reinterpret_cast<void*>(anchor));
    return Reservation{ base, reservationSize, 0, true, anchor,
                        /*whGameAnchored=*/ nearVa == 0 };
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

// Is a branch reservation rel32-reachable from `nearVa`? True iff its WHOLE
// range [base, base+size) lies within ±kRel32Margin of `nearVa` — the same
// window ReserveNearby places a fresh reservation in. Caller holds g_mutex.
//
// This is the guard locked decision #5 requires: a flat first-fit over the
// branch bool would hand a WHGame-anchored block to a far target (in range for
// WHGame, OUT of range for the far target → a silently-wrong trampoline that
// RewriteCallDisplacement would refuse, or worse). We reuse a reservation ONLY
// when both its ends fit.
bool BranchReservationReachesFrom(const Reservation& r, uintptr_t nearVa) {
    const uintptr_t lo = (nearVa > kRel32Margin) ? (nearVa - kRel32Margin) : 0;
    const uintptr_t hi = nearVa + kRel32Margin;  // window ceiling
    const uintptr_t rBase = reinterpret_cast<uintptr_t>(r.base);
    const uintptr_t rEnd  = rBase + r.size;       // one past the last byte
    return rBase >= lo && rEnd <= hi;
}

// Does branch reservation `r` belong to the SAME anchor group a request with
// this `nearVa` would use? This mirrors Allocate()'s reuse split exactly so the
// per-anchor region count and the reuse walk can never disagree:
//   - nearVa==0 (WHGame default): the group is the WHGame-anchored reservations.
//   - nearVa!=0 (far target): the group is every reservation rel32-reachable
//     from nearVa (WHGame- or target-anchored alike). Caller holds g_mutex.
bool BranchReservationServesAnchor(const Reservation& r, uintptr_t nearVa) {
    if (nearVa == 0) return r.whGameAnchored;
    return BranchReservationReachesFrom(r, nearVa);
}

// Count the branch reservations already serving this anchor group, and report
// the fullest one's used-fraction (for the exhaustion error). Caller holds
// g_mutex. `outFullestPct` is 0 when no reservation serves the anchor.
size_t CountBranchRegionsForAnchor(uintptr_t nearVa, double* outFullestPct) {
    size_t count = 0;
    double fullest = 0.0;
    for (const auto& r : g_reservations) {
        if (!r.branch) continue;
        if (!BranchReservationServesAnchor(r, nearVa)) continue;
        ++count;
        if (r.size > 0) {
            double pct = static_cast<double>(r.used) / static_cast<double>(r.size);
            if (pct > fullest) fullest = pct;
        }
    }
    if (outFullestPct) *outFullestPct = fullest;
    return count;
}

// Build the teaching exhaustion error string — the author-facing, falsifiable
// content of genuine branch-pool exhaustion. A pure formatter shared by the
// production exhaustion path AND the self-test, so the test asserts the exact
// text authors will read (pool name, fullest-region %, regions tried, the
// fallback attempted, and an actionable next step) without physically
// exhausting the live pool. Writes into `out` (truncating-safe). Caller-owned
// buffer; returns the number of chars written (excluding the NUL), or 0 on a
// null/zero buffer.
size_t FormatExhaustionError(char* out, size_t outLen, double fullestPct,
                             size_t regionsTried, uintptr_t anchor,
                             bool whGameAnchor) {
    if (!out || outLen == 0) return 0;
    int n = std::snprintf(
        out, outLen,
        "branch trampoline pool exhausted: every one of the %zu rel32-reachable "
        "reservation(s) near %s (anchor 0x%p) is full (fullest %.0f%% used) and "
        "a fresh ReserveNearby found no free region within +/-2GB of the anchor. "
        "The branch pool is capped at %zu regions per anchor (1 MB of detour "
        "bodies reachable from one site). Next step: reduce the number of hooks "
        "targeting functions near this anchor, or free address space within "
        "+/-2GB of it so a new reservation can be placed; if this is a legitimate "
        "need for more than %zu regions at one site, the per-anchor cap "
        "(kMaxBranchRegionsPerAnchor) must be raised in the engine.",
        regionsTried,
        whGameAnchor ? "WHGame.dll" : "the target module",
        reinterpret_cast<void*>(anchor),
        fullestPct * 100.0,
        kMaxBranchRegionsPerAnchor,
        kMaxBranchRegionsPerAnchor);
    if (n < 0) return 0;
    return (static_cast<size_t>(n) < outLen) ? static_cast<size_t>(n)
                                             : (outLen - 1);
}

// Record a BRANCH reservation as a kcdx-owned trampoline range for the
// foreign-hook classifier (§6.1) — branch-pool reservations hold the detour
// BODIES kcdx allocates near hook sites. The local pool is anywhere and is not
// a hook-prologue jump target, so it is never registered. Register the whole
// reservation once on creation (the registry de-dupes an exact repeat); a
// per-bump-allocation register would add many overlapping sub-ranges for no
// benefit. Shared by the on-full growth path and the eager-expansion path so
// both stage a new reservation identically. Caller holds g_mutex.
void RegisterBranchReservation(const Reservation& r) {
    kcdx::kcdx_trampoline_registry::Register(
        reinterpret_cast<uintptr_t>(r.base), r.size,
        kcdx::kcdx_trampoline_registry::Kind::BranchPool);
}

// After a BRANCH reservation crosses kBranchExpandThreshold full, eagerly stage
// the NEXT ±2 GB-adjacent reservation for the same anchor so the next request
// for that anchor has headroom. Best-effort: a failed reservation does NOT fail
// the allocation that already succeeded — it WARNs and returns; the next request
// retries the on-full growth path. Skips when the per-anchor cap is already
// reached (no point pre-staging a region a fresh request could not make either).
// Caller holds g_mutex; `r` is the reservation that was just served.
void MaybeEagerlyExpandBranch(const Reservation& r, uintptr_t nearVa) {
    if (r.size == 0) return;
    if (static_cast<double>(r.used) <= kBranchExpandThreshold *
                                       static_cast<double>(r.size)) {
        return;  // not yet across the threshold — nothing to pre-stage
    }
    // Don't pre-stage past the cap — exhaustion is the on-full path's call.
    if (CountBranchRegionsForAnchor(nearVa, nullptr) >= kMaxBranchRegionsPerAnchor) {
        return;
    }
    // Pass nearVa straight through so the new reservation carries the SAME
    // anchor/whGameAnchored flags a fresh on-full reservation would (the reuse
    // walk later matches it for this anchor exactly as it would the on-full one).
    Reservation next = MakeBranchReservation(kBranchPoolReservationSize, nearVa);
    if (!next.base) {
        // No nearby free region right now — leave the current alloc standing.
        log::WarnF("Trampoline branch pool: eager 80%%-expansion near anchor "
                   "(nearVa=0x%p) found no free region within +/-2GB; the "
                   "current allocation stands, the next request will retry "
                   "on-full.", reinterpret_cast<void*>(nearVa));
        return;
    }
    RegisterBranchReservation(next);
    log::InfoF("Trampoline branch pool: eagerly expanded (reservation crossed "
               "%.0f%% full) — pre-staged %zu bytes at 0x%p for the next request",
               kBranchExpandThreshold * 100.0, next.size, next.base);
    g_reservations.push_back(next);
}

// Try existing reservations of the matching pool first; if none have room,
// make a new reservation.
//
// `nearVa` (branch pool only) anchors a far-target trampoline near its target
// VA; nearVa==0 keeps the WHGame anchor. The local pool ignores nearVa (no
// proximity guarantee). For the branch pool, an existing reservation is reused
// only when it is rel32-reachable from the SAME anchor we'd place a fresh one
// at (the WHGame midpoint for nearVa==0, else nearVa itself) — never hand a
// WHGame-anchored block to a far target, nor vice versa.
//
// The branch pool grows two ways: REACTIVELY (no in-range reservation has room
// → make one) and PROACTIVELY (a served reservation crossed 80% → eagerly stage
// the next, so a hook never fails at apply time waiting for scarce near-site
// memory). Growth is bounded per anchor by kMaxBranchRegionsPerAnchor — past
// that, a request that no reservation can serve is genuine exhaustion: a strong
// teaching error, then a null return (every caller checks for null).
void* Allocate(bool branchPool, kcdxPluginHandle owner, size_t size,
               uintptr_t nearVa = 0) {
    if (size == 0) return nullptr;

    std::lock_guard<std::mutex> lock(g_mutex);

    // Walk existing reservations, first-fit (with a reach test for branch).
    for (auto& r : g_reservations) {
        if (r.branch != branchPool) continue;
        // Branch pool reuse, two cases — never cross them:
        //  - nearVa==0 (WHGame default): reuse ONLY a WHGame-anchored
        //    reservation. A far-target reservation is in rel32 range of its
        //    OWN anchor but NOT of WHGame, so handing it to a WHGame request
        //    would silently break reach — exclude it by the flag, not by a
        //    range test against its own anchor.
        //  - nearVa!=0 (far target): reuse a reservation (WHGame- or
        //    target-anchored) ONLY if its whole range is within rel32 of
        //    nearVa. A WHGame reservation 3 GB away fails this test and is
        //    skipped, forcing a fresh near-target reservation.
        if (branchPool) {
            if (nearVa == 0) {
                if (!r.whGameAnchored) continue;
            } else if (!BranchReservationReachesFrom(r, nearVa)) {
                continue;
            }
        }
        if (void* p = BumpAlloc(r, size)) {
            log::InfoF("Trampoline %s pool: allocated %zu bytes at 0x%p "
                       "(owner=%u, %zu/%zu used)",
                       branchPool ? "branch" : "local", size, p, owner,
                       r.used, r.size);
            // Proactive 80%-expansion: only the branch pool (the local pool has
            // no proximity constraint / no rel32 pressure). The reservation just
            // served is `r`; MaybeEagerlyExpandBranch may push_back a new one,
            // so do NOT touch `r` after this call (the vector may reallocate).
            if (branchPool) MaybeEagerlyExpandBranch(r, nearVa);
            return p;
        }
    }

    // No in-range reservation with room. For the branch pool, enforce the
    // per-anchor cap BEFORE growing: if this anchor already has the maximum
    // number of reservations and none had room, a fresh one would be the
    // (N+1)th — that is genuine exhaustion, not a reason to grow unbounded.
    if (branchPool) {
        double fullestPct = 0.0;
        size_t inRange = CountBranchRegionsForAnchor(nearVa, &fullestPct);
        if (inRange >= kMaxBranchRegionsPerAnchor) {
            char err[768];
            FormatExhaustionError(err, sizeof(err), fullestPct, inRange, nearVa,
                                  /*whGameAnchor=*/ nearVa == 0);
            // Fail loud — a strong specific error, then a null return the
            // caller already handles. Never a silent no-op.
            log::ErrorF("%s", err);
            return nullptr;
        }
    }

    // Within the cap (or local pool) — grow reactively.
    Reservation r = branchPool ? MakeBranchReservation(size, nearVa)
                               : MakeLocalReservation(size);
    if (!r.base) {
        // ReserveNearby found no free region within ±2GB of the anchor even
        // though we are under the per-anchor cap — a different exhaustion
        // (address-space starvation near the anchor, not the cap). For the
        // branch pool, emit the same teaching error so the author sees a
        // strong message rather than only MakeBranchReservation's lower-level
        // ErrorF; the regionsTried count is what is in range right now.
        if (branchPool) {
            double fullestPct = 0.0;
            size_t inRange = CountBranchRegionsForAnchor(nearVa, &fullestPct);
            char err[768];
            FormatExhaustionError(err, sizeof(err), fullestPct, inRange, nearVa,
                                  /*whGameAnchor=*/ nearVa == 0);
            log::ErrorF("%s", err);
        }
        return nullptr;
    }
    void* p = BumpAlloc(r, size);
    g_reservations.push_back(r);
    if (branchPool) RegisterBranchReservation(g_reservations.back());
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

// -----------------------------------------------------------------------
// Version-2 thunks. The all-in-one Allocate + standalone Export are the C++
// mirror of the Lua kcdx.code binder (src/lua_bind_code.cpp Lua_Code) — same
// validate -> alloc -> memcpy/NOP-pad -> export-register sequence, but reading
// from a kcdxCodeOptions struct instead of a Lua table, and deriving owner
// identity from opts->owningPlugin (a kcdxPluginHandle) instead of the Lua
// call-site walk. No Lua stack: a C++ DLL calls directly.
//
// One divergence from the Lua path that is NOT a behavior change: the C++
// caller hands the engine RAW bytes (void* + bytesSize), not a hex string, so
// there is NO ParseBytes here (the Lua path ParseBytes only because Lua gives a
// hex STRING). The bytes are memcpy'd verbatim. Same fill/pad result.
//
// Owner identity uses the SAME AuthorForHandle/NameForHandle mechanism
// bytes_interface.cpp / hook_interface.cpp use, so the export prefix +
// attribution match the Lua path. opts->owningPlugin is already a
// kcdxPluginHandle, so it threads straight into AllocateBranch/AllocateLocal
// (which take a handle for attribution) — exactly as the Lua binder passes its
// resolved ownerHandle.
//
// SHARED-HELPER NOTE: the dotted-name check + symbols::Register + OwnerOf
// collision-diagnostic block is now duplicated in three places (lua_bind_code,
// here twice). Per the step brief this is a deliberate parallel add (same as
// bytes_interface mirroring lua_bind_bytes), NOT a shared-helper extraction.
// If a fourth export site appears, a kcdx::symbols::PublishExport(owner,
// bareName, addr, diagName) helper is clearly warranted — flagged, not built.

void* Thunk_Allocate(const kcdxCodeOptions* opts) {
    if (!opts) {
        log::Error("kcdx code Allocate: opts is null — pass a non-null "
                   "kcdxCodeOptions* (name + bytes/size required).");
        return nullptr;
    }

    // --- name (required) ---
    const char* name = (opts->name && opts->name[0]) ? opts->name : nullptr;
    if (!name) {
        log::Error("kcdx code Allocate: `name` is required — the name used in "
                   "logs and export diagnostics (e.g. \"outfit_gate_logic\").");
        return nullptr;
    }

    // --- bytes OR size rule (mirror lua_bind_code.cpp:195-202) ---
    const size_t bytesSize = (opts->bytes != nullptr) ? opts->bytesSize : 0;
    const bool haveBytes = bytesSize > 0;
    const bool haveSize  = opts->size > 0;
    if (!haveBytes && !haveSize) {
        log::ErrorF("kcdx code Allocate [%s]: must declare either `bytes` "
                    "(with bytesSize > 0) or `size` (a NOP region to fill in "
                    "later), or both.", name);
        return nullptr;
    }

    // --- size must be >= bytesSize (mirror lua_bind_code.cpp:250-258) ---
    if (haveSize && opts->size < bytesSize) {
        log::ErrorF("kcdx code Allocate [%s]: declared size (%zu) is smaller "
                    "than the %zu byte(s) of `bytes`.",
                    name, opts->size, bytesSize);
        return nullptr;
    }

    // size defaults to bytesSize; the tail beyond bytes is NOP-padded.
    const size_t totalSize = haveSize ? opts->size : bytesSize;

    // --- Owner identity (same path bytes_interface.cpp uses) ---
    const kcdxPluginHandle ownerHandle = opts->owningPlugin;
    const std::string author = kcdx::plugins::AuthorForHandle(ownerHandle);
    const std::string plugin = kcdx::plugins::NameForHandle(ownerHandle);

    // --- Allocate (branch default; local is anywhere) — pass the handle
    // straight through, exactly as the Lua binder passes ownerHandle. ---
    void* region = (opts->pool == kcdxCodePool_Local)
                       ? AllocateLocal(ownerHandle, totalSize)
                       : AllocateBranch(ownerHandle, totalSize);
    if (!region) {
        log::ErrorF("kcdx code Allocate [%s]: the '%s' trampoline pool could "
                    "not allocate %zu bytes (out of pool space, or no "
                    "rel32-reachable region for \"branch\"). See kcdx.log.",
                    name, opts->pool == kcdxCodePool_Local ? "local" : "branch",
                    totalSize);
        return nullptr;
    }

    // --- Copy bytes to the head, NOP-pad the tail (mirror lua_bind_code:301-311) ---
    auto* dst = reinterpret_cast<uint8_t*>(region);
    if (haveBytes) {
        std::memcpy(dst, opts->bytes, bytesSize);
    }
    if (totalSize > bytesSize) {
        // NOP-pad so a plugin patching into the unused tail doesn't trip over
        // zero-init bytes (which decode as `add [rax], al`).
        std::memset(dst + bytesSize, 0x90 /* x86 NOP */, totalSize - bytesSize);
    }

    const uintptr_t addr = reinterpret_cast<uintptr_t>(region);
    LOG_DEBUG("CODE", "[%s] allocated %d bytes at 0x%p (pool=%s, plugin=%s)",
              name, static_cast<int>(totalSize), region,
              opts->pool == kcdxCodePool_Local ? "local" : "branch",
              plugin.empty() ? "<anon>" : plugin.c_str());

    // --- Register export IMMEDIATELY if requested. Mirrors the Lua export
    // branch (lua_bind_code.cpp:331-376) EXACTLY, including its ordering: the
    // region is already allocated, but a dotted exportName is a HARD FAILURE
    // (the Lua binder returns (nil, err) at lua_bind_code.cpp:333-343), so we
    // return nullptr too. A collision is likewise a hard failure (the region
    // stands but is unreachable by symbol — the Lua binder returns (nil, err)
    // at :366-374). ---
    if (opts->exportName && opts->exportName[0]) {
        const std::string exportName = opts->exportName;
        // Reject a dotted / prefixed export — the engine supplies the prefix.
        if (exportName.find('.') != std::string::npos) {
            log::ErrorF("kcdx code Allocate [%s]: `exportName` must be a BARE "
                        "name — do NOT type your own \"<plugin>.\" prefix. The "
                        "engine derives it from your [plugin].author/.name and "
                        "publishes the symbol as \"<author>.<plugin>.%s\". "
                        "You wrote \"%s\".",
                        name, exportName.c_str(), exportName.c_str());
            return nullptr;
        }
        // Fully-qualified name for diagnostics (matches the Lua binder's
        // lua_bind_code.cpp:344-346 prefix join).
        const std::string fullName =
            plugin.empty() ? exportName : (plugin + "." + exportName);
        if (kcdx::symbols::Register(exportName, addr, author, plugin)) {
            LOG_DEBUG("CODE", "[%s] exported symbol '%s' -> 0x%p",
                      name, fullName.c_str(), reinterpret_cast<void*>(addr));
        } else {
            const std::string priorOwner = kcdx::symbols::OwnerOf(fullName);
            log::ErrorF("[kcdx code '%s'] symbol export collision: '%s' is "
                        "already registered by '%s' — the region is allocated "
                        "but unreachable by symbol.",
                        name, fullName.c_str(),
                        priorOwner.empty() ? "?" : priorOwner.c_str());
            return nullptr;
        }
    }

    return region;
}

bool Thunk_Export(kcdxPluginHandle owner, const char* bareName,
                  uintptr_t addr) {
    if (!bareName || !bareName[0]) {
        log::Error("kcdx code Export: `bareName` is required — the BARE symbol "
                   "name to publish the address under (no \"<plugin>.\" prefix).");
        return false;
    }
    const std::string name = bareName;
    if (name.find('.') != std::string::npos) {
        log::ErrorF("kcdx code Export: `bareName` must be a BARE name — do NOT "
                    "type your own \"<plugin>.\" prefix. The engine derives it "
                    "from `owner` and publishes \"<author>.<plugin>.%s\". "
                    "You wrote \"%s\".",
                    name.c_str(), name.c_str());
        return false;
    }
    if (addr == 0) {
        log::ErrorF("kcdx code Export [%s]: `addr` is 0 — null exports are "
                    "rejected (the symbol must point at a real address).",
                    name.c_str());
        return false;
    }

    const std::string author = kcdx::plugins::AuthorForHandle(owner);
    const std::string plugin = kcdx::plugins::NameForHandle(owner);
    const std::string fullName =
        plugin.empty() ? name : (plugin + "." + name);
    if (kcdx::symbols::Register(name, addr, author, plugin)) {
        LOG_DEBUG("CODE", "Export: published symbol '%s' -> 0x%p",
                  fullName.c_str(), reinterpret_cast<void*>(addr));
        return true;
    }
    const std::string priorOwner = kcdx::symbols::OwnerOf(fullName);
    log::ErrorF("kcdx code Export: symbol export collision: '%s' is already "
                "registered by '%s' — choose a different bare name.",
                fullName.c_str(), priorOwner.empty() ? "?" : priorOwner.c_str());
    return false;
}

const kcdxTrampolineInterface g_iface = {
    /*AllocateFromBranchPool=*/ Thunk_AllocateFromBranchPool,
    /*AllocateFromLocalPool=*/  Thunk_AllocateFromLocalPool,
    /*Allocate=*/               Thunk_Allocate,
    /*Export=*/                 Thunk_Export,
};

}  // namespace

const kcdxTrampolineInterface* GetInterface() {
    return &g_iface;
}

void* AllocateBranch(kcdxPluginHandle owner, size_t size, uintptr_t nearVa) {
    return Allocate(/*branchPool=*/true, owner, size, nearVa);
}

void* AllocateLocal(kcdxPluginHandle owner, size_t size) {
    return Thunk_AllocateFromLocalPool(owner, size);
}

// --- Test-only introspection (definitions) ---

size_t BranchReservationCountForTest() {
    std::lock_guard<std::mutex> lock(g_mutex);
    size_t n = 0;
    for (const auto& r : g_reservations) {
        if (r.branch) ++n;
    }
    return n;
}

bool FormatExhaustionErrorForTest(size_t syntheticRegionsTried, double fullestPct,
                                  char* outErr, size_t outErrLen) {
    if (!outErr || outErrLen == 0) return false;
    // Use a fixed synthetic WHGame-style anchor so the formatted message is
    // deterministic; the production path passes the real anchor. The pool name,
    // percentage, region count, and next-step text are what the test asserts.
    constexpr uintptr_t kSyntheticAnchor = 0x140000000ull;
    size_t written = FormatExhaustionError(outErr, outErrLen, fullestPct,
                                           syntheticRegionsTried, kSyntheticAnchor,
                                           /*whGameAnchor=*/ true);
    return written > 0;
}

}  // namespace kcdx::trampoline
