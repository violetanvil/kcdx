#include "select_detour.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

#include "MinHook.h"

#include "ctor_probe.h"
#include "enabled_list_builder.h"
#include "../address_library.h"
#include "../log.h"

// SELECT detour — see select_detour.h for the full framing. Production
// mod-loader takeover: detour ModManager_Select (Address Library id 3100), let
// the original run, then wholesale-replace the enabled-list vector with kcdx's
// rebuilt list in resolved load order. docs/mod-loader-absorb.md "Step 4".

namespace kcdx::mod_absorb {

namespace {

constexpr const char* kCat = "MOD_ABSORB";

// ModManager_Select — Address Library id 3100. Resolved at install time (never
// a hardcoded RVA — the row carries the per-build address + the verified ABI).
constexpr uint64_t kSelectId = 3100;

// The enabled-mod list at C_ModManager+0x30 is a std::vector-style range of
// 8-byte I_Mod pointers: +0x30 = begin, +0x38 = end, +0x40 = end_of_storage
// (capacity). count = (end - begin) / 8. The vector layout at these three
// offsets is the takeover's load-bearing fact (the I_Mod record layout is
// docs/mod-loader-absorb.md "The I_Mod record layout").
constexpr size_t kListBeginOff   = 0x30;
constexpr size_t kListEndOff     = 0x38;
constexpr size_t kListEndCapOff  = 0x40;

// __fastcall void(C_ModManager* this) — this-only, void return (Address Library
// id 3100, verified ABI).
using SelectFn_t = void (__fastcall*)(void* self);

std::atomic<SelectFn_t> g_orig{nullptr};
std::atomic<bool>       g_installed{false};
std::atomic<bool>       g_tookOver{false};  // one-shot rebuild latch

// === kcdx-OWNED, PROCESS-LIFETIME enabled-list array ========================
//
// The engine vector is repointed at &g_enabledList[0] .. &g_enabledList[N].
// This vector MUST outlive MOUNT + every downstream pass that walks the list,
// so it is module-static (process lifetime) — NEVER built on the stack. The
// I_Mod* elements point at records record_synth owns process-lifetime; this
// array owns only the POINTER storage. It is filled exactly once (the one-shot
// g_tookOver latch) and never reallocated after the repoint.
std::vector<void*> g_enabledList;

// A stable, non-null module-static address for the EMPTY-list case (begin ==
// end == cap point here, a valid empty range). Avoids writing a possibly-null
// vector::data() as begin; the engine computes count = (end-begin)/8 = 0 and
// never dereferences it.
void* g_emptySentinel = nullptr;

void* ReadPtr(const uint8_t* base, size_t off) {
    void* v = nullptr;
    std::memcpy(&v, base + off, sizeof(v));
    return v;
}

void WritePtr(uint8_t* base, size_t off, const void* value) {
    std::memcpy(base + off, &value, sizeof(value));
}

void __fastcall HookedSelect(void* self) {
    // POINT B observation — read the C_ModManager state BEFORE the original
    // SELECT runs (post-ctor zero-init, pre-SELECT-body). One-shot inside the
    // probe; observe-only; never mutates `self`. Riding the existing detour
    // (one MinHook per site) — see ctor_probe.h and the init-cycle-ownership
    // outstanding-work doc. Deleted with the rest of the probe in step 4.
    kcdx::mod_absorb::ctor_probe::OnSelectEntry(self);

    // 1. Run the ORIGINAL SELECT first — it builds the native records AND runs
    //    the per-mod validation pass. The list MUST NOT be mutated before that
    //    completes (growing it mid-validation crashes the engine's own walk);
    //    wholesale-replace is safe only AFTER the original returns.
    SelectFn_t orig = g_orig.load(std::memory_order_acquire);
    if (orig) {
        orig(self);
    } else {
        kcdx::log::Error("MOD_ABSORB: orig SELECT pointer null at dispatch — "
                         "cannot take over the enabled list this boot");
        return;
    }

    // 2. One-shot: rebuild + replace exactly once. SELECT may be reachable more
    //    than once across a session; the takeover applies to the first (boot)
    //    selection.
    bool expected = false;
    if (!g_tookOver.compare_exchange_strong(expected, true,
                                            std::memory_order_acq_rel)) {
        return;
    }
    if (!self) {
        kcdx::log::Error("MOD_ABSORB: SELECT self (C_ModManager) is null — "
                         "cannot repoint the enabled list");
        return;
    }

    auto* base = reinterpret_cast<uint8_t*>(self);

    // Snapshot the original vector pointers (for the log + so the original
    // native records are not silently lost from the diagnostic record).
    void* origBegin = ReadPtr(base, kListBeginOff);
    void* origEnd   = ReadPtr(base, kListEndOff);
    void* origCap   = ReadPtr(base, kListEndCapOff);
    uint64_t origCount = 0;
    if (origBegin && origEnd && origEnd >= origBegin) {
        origCount = (reinterpret_cast<uintptr_t>(origEnd) -
                     reinterpret_cast<uintptr_t>(origBegin)) / 8;
    }

    // 3. Build kcdx's rebuilt enabled list (resolved load order, disabled +
    //    failed-synth records excluded). Build into a LOCAL, then move into the
    //    module-static store so the array the engine points at is
    //    process-lifetime (a stack array would dangle when MOUNT walks it).
    std::vector<EnabledListEntry> entries;
    g_enabledList = BuildEnabledList(&entries);

    const size_t n = g_enabledList.size();

    // Per-record DEBUG breakdown (dev-log-routed). Plain id + path; no probe
    // framing.
    size_t vanilla = 0, plugins = 0;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].isPlugin) ++plugins; else ++vanilla;
        LOG_DEBUG_KV(kCat, "takeover_record",
                     kcdx::log::KV("idx", (uint64_t)i),
                     kcdx::log::KV("id", entries[i].id),
                     kcdx::log::KV("path", entries[i].rootPathSlash),
                     kcdx::log::KV("kind", entries[i].isPlugin ? "plugin" : "pak_mod"));
    }

    // 4. Repoint the vector at the kcdx-owned array. begin = &array[0];
    //    end = end_of_storage = &array[N]. An EMPTY rebuilt list (n == 0) would
    //    leave the engine with zero enabled mods — repoint all three at a stable
    //    non-null sentinel address so begin == end == cap (a valid empty vector)
    //    rather than writing a dangling &array[0] on an empty std::vector.
    if (n == 0) {
        // Empty list: point begin == end == cap at a stable, non-null
        // module-static address; the engine computes count = (end-begin)/8 = 0
        // and mounts nothing (never dereferences the address). Surface it LOUD:
        // zero enabled mods is a real, observable state (every mod disabled /
        // version-rejected, or every record's synthesis failed), not a silent
        // no-op.
        void* emptyAt = static_cast<void*>(&g_emptySentinel);
        WritePtr(base, kListBeginOff,  emptyAt);
        WritePtr(base, kListEndOff,    emptyAt);
        WritePtr(base, kListEndCapOff, emptyAt);
        kcdx::log::WarnF(
            "MOD_ABSORB: kcdx mod-loader takeover rebuilt an EMPTY enabled list "
            "(0 mods) — every discovered mod was disabled, version-rejected, or "
            "failed record synthesis; the game will mount no mods this boot "
            "(original native count was %llu)",
            (unsigned long long)origCount);
        return;
    }

    void* newBegin = static_cast<void*>(&g_enabledList[0]);
    void* newEnd   = static_cast<void*>(&g_enabledList[0] + n);
    WritePtr(base, kListBeginOff,  newBegin);
    WritePtr(base, kListEndOff,    newEnd);
    WritePtr(base, kListEndCapOff, newEnd);

    LOG_INFO_KV(kCat, "takeover_repoint",
                kcdx::log::KV("orig_begin", origBegin),
                kcdx::log::KV("orig_end",   origEnd),
                kcdx::log::KV("orig_cap",   origCap),
                kcdx::log::KV("orig_count", origCount),
                kcdx::log::KV("new_begin",  newBegin),
                kcdx::log::KV("new_end",    newEnd),
                kcdx::log::KV("new_count",  (uint64_t)n));

    kcdx::log::InfoF(
        "kcdx mod-loader takeover: rebuilt enabled list, %llu mods "
        "(%zu vanilla, %zu plugins) in kcdx load order",
        (unsigned long long)n, vanilla, plugins);
}

}  // namespace

bool InstallSelectDetour() {
    bool expected = false;
    if (!g_installed.compare_exchange_strong(expected, true,
                                             std::memory_order_acq_rel)) {
        return true;  // already installed this session
    }

    HMODULE whgame = GetModuleHandleW(L"WHGame.dll");
    if (!whgame) {
        kcdx::log::Warn("MOD_ABSORB: WHGame.dll not mapped yet; cannot install "
                        "the SELECT detour — mod-loader takeover inactive");
        g_installed.store(false, std::memory_order_release);
        return false;
    }

    // Resolve the SELECT address by Address Library id (never a hardcoded RVA —
    // the row carries the per-build address gated on a game-version match).
    const uintptr_t target = address_library::Resolve(kSelectId);
    if (target == 0) {
        kcdx::log::WarnF("MOD_ABSORB: ModManager_Select (Address Library id "
                         "%llu) did not resolve (version mismatch / unverified "
                         "row) — mod-loader takeover inactive this boot",
                         (unsigned long long)kSelectId);
        g_installed.store(false, std::memory_order_release);
        return false;
    }

    // MinHook idempotent init (the worker-thread caller already initialized it).
    MH_STATUS si = MH_Initialize();
    if (si != MH_OK && si != MH_ERROR_ALREADY_INITIALIZED) {
        kcdx::log::WarnF("MOD_ABSORB: MH_Initialize failed: %d", (int)si);
        g_installed.store(false, std::memory_order_release);
        return false;
    }

    void* targetPtr = reinterpret_cast<void*>(target);
    void* origPtr = nullptr;
    MH_STATUS s = MH_CreateHook(targetPtr,
                                reinterpret_cast<void*>(&HookedSelect),
                                &origPtr);
    if (s != MH_OK) {
        kcdx::log::WarnF("MOD_ABSORB: MH_CreateHook(SELECT @ %p) failed: %d "
                         "— mod-loader takeover inactive", targetPtr, (int)s);
        g_installed.store(false, std::memory_order_release);
        return false;
    }
    g_orig.store(reinterpret_cast<SelectFn_t>(origPtr), std::memory_order_release);

    s = MH_EnableHook(targetPtr);
    if (s != MH_OK) {
        kcdx::log::WarnF("MOD_ABSORB: MH_EnableHook(SELECT @ %p) failed: %d "
                         "— mod-loader takeover inactive", targetPtr, (int)s);
        return false;
    }

    kcdx::log::InfoF("MOD_ABSORB: ModManager_Select detour installed at %p "
                     "(Address Library id %llu) — mod-loader takeover armed",
                     targetPtr, (unsigned long long)kSelectId);
    return true;
}

}  // namespace kcdx::mod_absorb
