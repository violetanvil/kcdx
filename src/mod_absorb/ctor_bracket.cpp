#include "ctor_bracket.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

#include "MinHook.h"

#include "select_detour.h"   // worker-side machinery: g_kcdxReadyEvent +
                              // g_enabledList accessors (kept process-lifetime).
#include "../log.h"
#include "../refdb.h"

// Ctor bracket — see ctor_bracket.h for the architectural framing. This .cpp
// owns the MinHook install on ModManager_ctor (refdb curated name) and the
// HookedCtor callback that runs on the game's main thread inside
// CSystem::Init. The callback FULLY REPLACES the native ctor: kcdx allocates
// the C_ModManager via WHGame's own allocator (so the matching destructor's
// free() lines up), writes all required fields itself, and never calls the
// captured original.

namespace kcdx::mod_absorb {

namespace {

constexpr const char* kCat = "MOD_ABSORB";

// C_ModManager layout (verified live two-boot against the running binary —
// the corrected 0x68-byte layout). Only the offsets the bracket WRITES are
// named; the un-named offsets are left at the allocator's initial value
// (defensively memset to zero by the bracket after allocation — WHGame's
// allocator is not documented to zero its returns; the two-boot observation
// saw zeros at +0x18..+0x28 / +0x48..+0x58 only because the native ctor
// zero-inited them, and our replacement must do the same).
constexpr size_t kObjectSize       = 0x68;
constexpr size_t kOffVtable        = 0x00;
constexpr size_t kOffSys           = 0x08;
constexpr size_t kOffModsDirString = 0x10;
constexpr size_t kOffEnabledBegin  = 0x30;
constexpr size_t kOffEnabledEnd    = 0x38;
constexpr size_t kOffEnabledCap    = 0x40;
constexpr size_t kOffInitFlag      = 0x60;

// ABI of ModManager_ctor — 3-arg __fastcall returning the constructed
// object pointer. Verified row 3101 in seed.csv prose + the capstone E8-sweep
// against the binary. The bracket replacement (HookedCtor) declares the same
// shape inline; the original is never called, so no typedef is kept for it.

// ABIs of the three WHGame helpers the bracket invokes by refdb name. The
// signatures here mirror the seed prose for those entities; the bracket
// resolves each at fire time via refdb::ResolveByName, so an unverified or
// missing row fails LOUD (and the bracket aborts without scribbling on the
// engine).
//
//   WHGame_allocator             — ptr (i64 size)
//   CryString_init_from_string   — ptr (ptr dest, cstr source)
//   CryString_placement_construct — ptr (ptr dest, ptr source)
using AllocatorFn_t        = void* (__fastcall*)(uint64_t size);
using CryStrInitFn_t       = void* (__fastcall*)(void* dest, const char* source);
using CryStrPlacementFn_t  = void* (__fastcall*)(void* dest, void* source);

std::atomic<bool>     g_installed{false};
std::atomic<bool>     g_installSucceeded{false};

// One-shot guard. ModManager_ctor is called exactly once per session in the
// observed boots; the latch is defensive — if a re-entry ever happens, kcdx
// will not double-build / leak a second 0x68 block.
std::atomic<bool>     g_bracketFired{false};

// Cached WHGame base. Captured at install time; the bracket uses it to bias
// the refdb-returned RVAs into VAs for the vtable + the three helpers.
uintptr_t             g_whgameBase = 0;

// Write a pointer-width value into the 0x68 block at `off`.
void WritePtrAt(uint8_t* base, size_t off, const void* value) {
    std::memcpy(base + off, &value, sizeof(value));
}

void WriteByteAt(uint8_t* base, size_t off, uint8_t value) {
    base[off] = value;
}

// Resolve a refdb-curated entity to its VA. Returns 0 on a not-found result
// — every helper resolve in HookedCtor checks the return and bails loud.
uintptr_t ResolveVa(const char* name) {
    const auto res = kcdx::refdb::ResolveByName(name);
    if (!res.found) {
        LOG_ERROR_KV(kCat, "ctor_bracket_resolve_failed",
            kcdx::log::KV::BareStr("name", name),
            kcdx::log::KV::BareStr("detail",
                "refdb::ResolveByName returned not-found — the canonical name "
                "is absent or its row is not verified for the running build; "
                "the kcdx ctor bracket cannot run this fire (see the "
                "preceding REFDB ERROR for the specific reason token)"));
        return 0;
    }
    return g_whgameBase + res.rva;
}

void* __fastcall HookedCtor(void* outResult, void* sys, void* modsDir) {
    // One-shot. Defensive — the ctor is single-call per session in the
    // observed boots; this latch keeps the bracket honest under any future
    // re-entry path (reload-mods, dev hot-restart, …).
    bool expected = false;
    if (!g_bracketFired.compare_exchange_strong(expected, true,
                                                std::memory_order_acq_rel)) {
        LOG_WARN_KV(kCat, "ctor_bracket_reentry",
            kcdx::log::KV::BareStr("detail",
                "HookedCtor fired a second time this session — the bracket "
                "is one-shot; the second fire returns nullptr without "
                "writing. If this is reached, a reload-mods or hot-restart "
                "path is exercising the ctor and needs a separate design "
                "decision (kcdx-owned rebuild of the existing C_ModManager "
                "vs. a new allocation)"));
        return nullptr;
    }

    if (!outResult) {
        LOG_ERROR_KV(kCat, "ctor_bracket_failed",
            kcdx::log::KV::BareStr("reason",
                "outResult (rcx) is null — cannot store the constructed "
                "C_ModManager back to the caller; bracket aborted"));
        return nullptr;
    }

    // Resolve every entity the bracket needs FIRST. A miss here aborts
    // BEFORE allocation, so a failed resolve never leaks the 0x68 block.
    const uintptr_t vtableVa    = ResolveVa("C_ModManager_vtable");
    const uintptr_t allocatorVa = ResolveVa("WHGame_allocator");
    const uintptr_t initVa      = ResolveVa("CryString_init_from_string");
    const uintptr_t placementVa = ResolveVa("CryString_placement_construct");

    if (vtableVa == 0 || allocatorVa == 0 || initVa == 0 || placementVa == 0) {
        LOG_ERROR_KV(kCat, "ctor_bracket_failed",
            kcdx::log::KV::BareStr("reason",
                "one or more refdb entities required by the ctor bracket "
                "did not resolve — see the preceding refdb errors; the "
                "C_ModManager will NOT be constructed this fire (the engine "
                "must run the native ctor for any progress, and the kcdx "
                "bracket has NOT called it — the engine state stays as the "
                "caller had it pre-call: *outResult unchanged)"));
        return nullptr;
    }

    auto allocator       = reinterpret_cast<AllocatorFn_t>(allocatorVa);
    auto crystrInit      = reinterpret_cast<CryStrInitFn_t>(initVa);
    auto crystrPlacement = reinterpret_cast<CryStrPlacementFn_t>(placementVa);

    // Wait for kcdx's worker to finish building g_enabledList. The event
    // handle was created earlier on the worker thread via CreateReadyEvent
    // (BEFORE InstallCtorBracket went live), so the acquire-load below sees
    // a non-null handle the moment the bracket can fire. INFINITE is correct:
    // the worker WILL signal unless it hangs entirely (in which case the game
    // already cannot init).
    HANDLE readyEvent = mod_absorb::GetReadyEventHandle();
    if (readyEvent) {
        LOG_DEBUG_KV(kCat, "ctor_bracket_wait_enter",
            kcdx::log::KV::BareStr("detail",
                "HookedCtor about to WaitForSingleObject on g_kcdxReadyEvent "
                "(INFINITE) — typically blocks ~1-2s on a populated plugin "
                "tree (game thread leads the worker into CSystem::Init); "
                "returns immediately on a steady-state boot where the worker "
                "is already past SetEvent"));
        WaitForSingleObject(readyEvent, INFINITE);
    } else {
        LOG_ERROR_KV(kCat, "ctor_bracket_event_missing",
            kcdx::log::KV::BareStr("detail",
                "g_kcdxReadyEvent is null at HookedCtor entry — "
                "CreateReadyEvent was not called before the bracket install, "
                "or its CreateEventW failed (already logged loud). The "
                "bracket proceeds without waiting; if the worker has not yet "
                "populated g_enabledList, the engine sees an empty list this "
                "boot"));
    }

    // Allocate the 0x68-byte block via WHGame's own allocator so the matching
    // destructor's free() call lines up.
    void* obj = allocator(static_cast<uint64_t>(kObjectSize));
    if (!obj) {
        LOG_ERROR_KV(kCat, "ctor_bracket_alloc_failed",
            kcdx::log::KV("size", static_cast<uint64_t>(kObjectSize)),
            kcdx::log::KV::BareStr("detail",
                "WHGame_allocator returned null for the C_ModManager 0x68 "
                "block — the engine cannot proceed past CSystem::Init "
                "without a C_ModManager; *outResult is left unwritten so the "
                "caller observes a null C_ModManager and fails its own "
                "post-ctor checks"));
        return nullptr;
    }

    // Zero the 0x68 block before we write our slots. WHGame's allocator is
    // not contractually a calloc; the native ctor zero-inits before its own
    // writes (observed live against the running binary), and the bracket
    // replicates that: every unused slot (+0x18/+0x20/+0x28 and +0x48/+0x50/
    // +0x58 plus the upper 7 bytes of +0x60) stays at zero. Defensive memset
    // is the single, clear way to guarantee that contract.
    std::memset(obj, 0, kObjectSize);

    auto* base = static_cast<uint8_t*>(obj);

    // +0x00 vtable.
    WritePtrAt(base, kOffVtable, reinterpret_cast<const void*>(vtableVa));

    // +0x08 sys.
    WritePtrAt(base, kOffSys, sys);

    // +0x10 modsDir CryString — in-place construct via the two WHGame
    // helpers, mirroring the native ctor. Step 1: init a stack-local
    // CryString from arg3 (modsDir). The seed-row prose for the ctor's
    // ABI documents arg3 as a pointer-to-CryString-data (the disassembly's
    // mov rdx, [r14] confirms it's dereffed). The init-from-string helper's
    // source arg is a cstr, so the bracket dereferences modsDir to recover
    // that cstr.
    //
    // CryString stack-locals in the native code are 24 bytes (an 8-byte data
    // ptr + a 16-byte allocator/refcount block typically); reserve 32 bytes
    // (cheap stack alignment slack) for the helper to populate.
    uint8_t local[32] = {0};
    const char* modsDirCstr = nullptr;
    if (modsDir) {
        // arg3 is a pointer to a CryString-shaped object whose data pointer
        // is at offset 0 in the conventional CryString layout; the native
        // ctor's first effective op is `mov rdx, [r14]` which loads
        // [modsDir+0] into rdx as the source-string ptr. Mirror that.
        std::memcpy(&modsDirCstr, modsDir, sizeof(modsDirCstr));
    }
    if (!modsDirCstr) {
        // The native code path would AV here; surface it loud rather than
        // riding the same crash. Fall back to the literal "mods" — the
        // observed live content in both boots against the running binary —
        // so the engine gets a usable modsDir even if arg3 was malformed.
        LOG_WARN_KV(kCat, "ctor_bracket_modsdir_fallback",
            kcdx::log::KV::BareStr("detail",
                "modsDir arg3 dereffed to a null cstr ptr; using literal "
                "\"mods\" (matches the observed live value in two boots)"));
        modsDirCstr = "mods";
    }
    crystrInit(local, modsDirCstr);
    crystrPlacement(base + kOffModsDirString, local);

    // +0x30 / +0x38 / +0x40 enabled-list vector triple. Read the kcdx-built
    // list directly. EMPTY-list case: the live two-boot observation has the
    // native vector as a heap range; an empty kcdx list points all three at
    // the same null (begin == end == cap == 0) so MOUNT computes count =
    // (end-begin)/8 = 0 and never derefs. This matches select_detour.cpp's
    // prior empty-sentinel pattern; with a fresh allocation that just
    // memset-zeroed, the slots are already zero, so an empty kcdx list
    // simply LEAVES them zero.
    const auto& enabled = mod_absorb::GetEnabledListData();
    const size_t n = enabled.size();
    if (n > 0) {
        // The engine reads but does not mutate the array — kcdx owns the
        // storage process-lifetime. Cast through uintptr_t to write the raw
        // address (a pointer-to-pointer) into the vector triple as a plain
        // pointer value.
        const uintptr_t dataAddr = reinterpret_cast<uintptr_t>(enabled.data());
        void* beginVa = reinterpret_cast<void*>(dataAddr);
        void* endVa   = reinterpret_cast<void*>(dataAddr + n * sizeof(void*));
        WritePtrAt(base, kOffEnabledBegin, beginVa);
        WritePtrAt(base, kOffEnabledEnd,   endVa);
        WritePtrAt(base, kOffEnabledCap,   endVa);
    } else {
        LOG_WARN_KV(kCat, "ctor_bracket_empty_list",
            kcdx::log::KV::BareStr("detail",
                "kcdx-built enabled list is empty — the engine will mount "
                "no mods this boot (every discovered mod was disabled, "
                "version-rejected, or failed record synthesis). The 0x68 "
                "block's enabled-list slots are left zero (a valid empty "
                "vector — begin == end == cap == 0; MOUNT computes "
                "count = (end-begin)/8 = 0 and never derefs)"));
    }

    // +0x60 init flag (one byte). The upper seven bytes of the qword stay
    // zero by the memset above; the native ctor writes exactly one byte
    // here, and the bracket matches that.
    WriteByteAt(base, kOffInitFlag, 1);

    // Final epilogue mirror: *outResult = obj; return outResult. The native
    // disassembly ends with `mov [rsi], rbx; mov rax, rsi; ret`. rsi was the
    // ctor's first arg (outResult, a slot the caller passed in); rbx held
    // the allocated heap block. The native ABI therefore writes the heap
    // block into *outResult AND returns outResult — the slot pointer — not
    // the heap block. The engine's caller in CSystem::Init does
    //   mov rdx, rax           ; rdx = ctor return
    //   mov rcx, r13           ; rcx = &csys[+0x2B30] (install slot)
    //   call FUN_1819DDCA4     ; unique_ptr move-assign install helper
    // whose first effective op is `mov rax, [rdx]`: it expects rdx to be a
    // slot pointer it can dereference, NOT the heap block itself.
    //
    // Returning the heap block here (as the bracket previously did) makes
    // the helper read *heap_block = the modMgr's first qword = its vtable
    // VA at +0x00, then install the vtable VA into csys[+0x2B30] as if it
    // were the modMgr pointer. Every later "get modMgr" path then returns
    // the vtable VA; the frame-4 lookup-by-name dispatch walks
    // [vtable+0x30, vtable+0x38) as if it were the enabled-list begin/end
    // and AVs reading [code_bytes+0x60] in FUN_2440C6C. The fix is one
    // line: mirror the native epilogue's actual return.
    std::memcpy(outResult, &obj, sizeof(obj));

    LOG_INFO_KV(kCat, "ctor_bracket_complete",
        kcdx::log::KV("obj",        reinterpret_cast<uintptr_t>(obj)),
        kcdx::log::KV("vtable",     vtableVa),
        kcdx::log::KV("enabled_n",  static_cast<uint64_t>(n)),
        kcdx::log::KV::BareStr("detail",
            "kcdx fully replaced ModManager_ctor — no original ctor call, "
            "no original SELECT call; the enabled list at +0x30/+0x38/+0x40 "
            "points at the kcdx-owned process-lifetime g_enabledList; MOUNT "
            "will iterate kcdx's resolved order verbatim"));

    return outResult;
}

}  // namespace

bool InstallCtorBracket() {
    bool expected = false;
    if (!g_installed.compare_exchange_strong(expected, true,
                                             std::memory_order_acq_rel)) {
        return g_installSucceeded.load(std::memory_order_acquire);
    }

    HMODULE whgame = GetModuleHandleW(L"WHGame.dll");
    if (!whgame) {
        LOG_ERROR_KV(kCat, "ctor_bracket_install_failed",
            kcdx::log::KV::BareStr("reason",
                "WHGame.dll is not mapped at InstallCtorBracket time — the "
                "kcdx mod-loader takeover cannot install this boot. The "
                "game will run with the vanilla ctor + SELECT (no kcdx "
                "absorption)"));
        return false;
    }
    g_whgameBase = reinterpret_cast<uintptr_t>(whgame);

    // Resolve the ctor address by refdb curated name. Same not-found
    // fail-loud pattern as everywhere else in mod_absorb.
    const auto ctorRes = kcdx::refdb::ResolveByName("ModManager_ctor");
    if (!ctorRes.found) {
        LOG_ERROR_KV(kCat, "ctor_bracket_install_failed",
            kcdx::log::KV::BareStr("reason",
                "refdb::ResolveByName(ModManager_ctor) returned not-found — "
                "the canonical name is absent or its row is not verified "
                "for the running build; the kcdx ctor bracket is INACTIVE "
                "this boot. See the preceding REFDB ERROR for the specific "
                "reason token"),
            kcdx::log::KV::BareStr("name", "ModManager_ctor"));
        return false;
    }
    const uintptr_t target = g_whgameBase + ctorRes.rva;

    MH_STATUS si = MH_Initialize();
    if (si != MH_OK && si != MH_ERROR_ALREADY_INITIALIZED) {
        LOG_ERROR_KV(kCat, "ctor_bracket_install_failed",
            kcdx::log::KV::BareStr("reason",
                "MH_Initialize failed; the kcdx ctor bracket is INACTIVE "
                "this boot"),
            kcdx::log::KV("mh_status", static_cast<long long>(si)));
        return false;
    }

    void* targetPtr = reinterpret_cast<void*>(target);
    void* origPtr   = nullptr;
    MH_STATUS s = MH_CreateHook(targetPtr,
                                reinterpret_cast<void*>(&HookedCtor),
                                &origPtr);
    if (s != MH_OK) {
        LOG_ERROR_KV(kCat, "ctor_bracket_install_failed",
            kcdx::log::KV::BareStr("reason",
                "MH_CreateHook on ModManager_ctor failed; the kcdx ctor "
                "bracket is INACTIVE this boot"),
            kcdx::log::KV("target", reinterpret_cast<uintptr_t>(targetPtr)),
            kcdx::log::KV("mh_status", static_cast<long long>(s)));
        return false;
    }
    // origPtr is intentionally discarded — the bracket fully replaces the
    // native ctor and never calls it. MinHook requires the out-slot pointer;
    // the local satisfies the API without keeping the trampoline reachable.
    (void)origPtr;

    s = MH_EnableHook(targetPtr);
    if (s != MH_OK) {
        LOG_ERROR_KV(kCat, "ctor_bracket_install_failed",
            kcdx::log::KV::BareStr("reason",
                "MH_EnableHook on ModManager_ctor failed; the kcdx ctor "
                "bracket is INACTIVE this boot"),
            kcdx::log::KV("target", reinterpret_cast<uintptr_t>(targetPtr)),
            kcdx::log::KV("mh_status", static_cast<long long>(s)));
        return false;
    }

    LOG_INFO_KV(kCat, "ctor_bracket_installed",
        kcdx::log::KV("target", reinterpret_cast<uintptr_t>(targetPtr)),
        kcdx::log::KV::BareStr("name", "ModManager_ctor"),
        kcdx::log::KV::BareStr("detail",
            "kcdx FULL ctor replacement armed — when CSystem::Init reaches "
            "ModManager_ctor on the game's main thread, HookedCtor will "
            "wait on g_kcdxReadyEvent then synthesize the C_ModManager "
            "from scratch with kcdx's resolved enabled list. The native "
            "ctor + native SELECT will NOT run"));
    g_installSucceeded.store(true, std::memory_order_release);
    return true;
}

}  // namespace kcdx::mod_absorb
