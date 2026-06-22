// === DIAGNOSTIC (PROBE R) — KI-0028 shader-cache VALIDATION gate ===============
// See dispatch_probe.h for WHY + the outcome->meaning map. NO-RESIDUE on retire.

#include "dispatch_probe.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>

#include "MinHook.h"
#include "../log.h"
#include "../pe_helpers.h"

namespace kcdx::fs_takeover {

namespace {

using KV = ::kcdx::log::KV;
constexpr const char* kCat = "DISPATCH_PROBE";

// Scratch probe RVAs (NOT production resolution — the no-hardcoded-addresses.md
// probe/diagnostic exception). SOURCE: _research/ki0028-cshaderman-pso-consumer-
// recon/FINDINGS.md, body-read against WHGame.dll release_1_5_1164953_841, image
// base 0x180000000.
//   0xb04984 = FUN_180b04984 = the lookupdata.bin LOADER; its return IS the
//              disable-cache gate bool (0 = reject => cache disabled).
//   0xb04478 = FUN_180b04478 = the cache validate DRIVER; calls the loader,
//              fires "Disabling read-only shader cache!" on a 0 return.
constexpr uint32_t kRvaLoader = 0xb04984;
constexpr uint32_t kRvaDriver = 0xb04478;
// PROBE R2 — the precache-submit span (DOWNSTREAM of validation, which PROBE R
// proved ACCEPTS swap-ON). SOURCE: same recon FINDINGS, body-read.
//   0x250dd8c = FUN_18250dd8c = the compile-ORCHESTRATOR (mfLoadShaderList ->
//               FUN_18171a644 -> _PrecacheShaderList); "Starting shader
//               compilation for %s..." at ShaderCache.cpp:0x5a5.
//   0x19d4a54 = FUN_1819d4a54 = mfLoadShaderList; fills the precache list at
//               this+0x300 from %USER%/shaders/shaderlist.txt.
//   0x25091e0 = FUN_1825091e0 = _PrecacheShaderList; submits the list. Its reach
//               swap-ON is the O3 discriminator (reached => gate is in/after the
//               submit; NOT reached => gate is between validation and it).
constexpr uint32_t kRvaOrchestrator = 0x250dd8c;
constexpr uint32_t kRvaLoadList     = 0x19d4a54;
constexpr uint32_t kRvaPrecache     = 0x25091e0;
// PROBE R3 — the RUNTIME PSO-precache (NOT the dead _PrecacheShaderList shaderlist
// path, which R2 + the swap-OFF baseline proved runs on NEITHER path). SOURCE:
// recon FINDINGS Front 1, body-read PipelineStateCacheManager.cpp.
//   0xbb2ad8 = FUN_180bb2ad8 = PSO PRECACHING Graphics ("Precached %u Graphics
//              PSOs ... PipelineStateCacheManager.cpp:0x596") — the actual menu
//              pipeline precache. Its reach swap-ON vs swap-OFF is the O4 trigger
//              discriminator.
//   0xbb23c0 = FUN_180bb23c0 = PSO PRECACHING Compute ("Precached %u Compute PSOs").
constexpr uint32_t kRvaPsoPrecacheGfx  = 0xbb2ad8;
constexpr uint32_t kRvaPsoPrecacheComp = 0xbb23c0;

std::atomic<bool> g_armed{false};

// Per-call tallies (relaxed — diagnostic counters, no happens-before edge).
std::atomic<uint64_t> g_loaderCalls{0};
std::atomic<uint64_t> g_loaderReject{0};   // return 0 (cache disabled)
std::atomic<uint64_t> g_loaderAccept{0};   // return nonzero (cache used)
std::atomic<uint64_t> g_driverCalls{0};
// PROBE R2 — precache-submit span reach counters.
std::atomic<uint64_t> g_orchCalls{0};      // compile orchestrator reached
std::atomic<uint64_t> g_loadListCalls{0};  // mfLoadShaderList reached
std::atomic<uint64_t> g_precacheCalls{0};  // _PrecacheShaderList reached
// PROBE R3 — runtime PSO-precache reach counters (the actual menu pipeline build).
std::atomic<uint64_t> g_psoGfxCalls{0};    // PSO PRECACHING Graphics reached
std::atomic<uint64_t> g_psoCompCalls{0};   // PSO PRECACHING Compute reached

// --- FUN_180b04984 lookupdata.bin loader after-hook ---
// ABI (body-read): undefined8 (longlong p1, undefined8 p2, undefined8 p3,
// char p4). 4-arg fastcall; the return is the gate bool. Mirror it exactly so the
// trampoline forwards every arg untouched.
using LoaderFn_t = uint64_t(__fastcall*)(void* p1, void* p2, void* p3, char p4);
LoaderFn_t g_origLoader = nullptr;

uint64_t __fastcall HookedLoader(void* p1, void* p2, void* p3, char p4) {
    const uint64_t n = g_loaderCalls.fetch_add(1) + 1;
    const uint64_t ret = g_origLoader(p1, p2, p3, p4);
    if (ret == 0) ++g_loaderReject; else ++g_loaderAccept;
    // The decisive line: ret==0 => the cache was REJECTED this call (=> disabled).
    // p4 is the per-call "is this the %USER% copy" flag the body ANDs into the
    // open flags (~-(p4!=0) & 0x10010004) — log it so the ENGINE vs USER arm is
    // distinguishable. Every call logged (this is NOT a hot path — the cache is
    // validated a handful of times at shader-system init).
    LOG_DEBUG_KV(kCat, "lookupdata_loader",
        KV("call_n",   n),
        KV("ret",      ret),
        KV("rejected", ret == 0 ? 1 : 0),
        KV("user_flag", (uint64_t)(uint8_t)p4));
    return ret;
}

// --- FUN_180b04478 validate driver after-hook ---
// ABI (body-read context FUN_180b033a0 calls it `FUN_180b04478(base + 0xf00)`):
// a single-pointer member-ish call. Return is not the gate (the loader's is); we
// only need REACH — was validation driven at all under the swap. Mirror as a
// 1-arg fastcall returning void (the caller discards any return).
using DriverFn_t = void(__fastcall*)(void* p1);
DriverFn_t g_origDriver = nullptr;

void __fastcall HookedDriver(void* p1) {
    const uint64_t n = g_driverCalls.fetch_add(1) + 1;
    // Log BEFORE forwarding — the driver runs the loader internally, so the
    // loader lines for THIS validation appear after this "driver_enter".
    LOG_DEBUG_KV(kCat, "driver_enter",
        KV("call_n", n),
        KV::BareStr("detail",
            "shader-cache validate driver reached; the lookupdata_loader line(s) "
            "that follow are this validation's gate verdict."));
    g_origDriver(p1);
}

// --- PROBE R2: precache-submit span after-hooks (reach discriminators) ---
// Each is a member-ish call taking `this` (the CShaderMan*). We only need REACH
// (did this stage run swap-ON), so each forwards `this` untouched and logs once.
// A re-entrant/many-call site would flood, but these are shader-init-once calls.
using ThisFn_t = void(__fastcall*)(void* self);

ThisFn_t g_origOrch = nullptr;
void __fastcall HookedOrch(void* self) {
    const uint64_t n = g_orchCalls.fetch_add(1) + 1;
    LOG_DEBUG_KV(kCat, "orchestrator_enter", KV("call_n", n),
        KV::BareStr("detail",
            "compile orchestrator (mfLoadShaderList -> _PrecacheShaderList) "
            "REACHED — shader-system compile sequence started this run."));
    g_origOrch(self);
}

ThisFn_t g_origLoadList = nullptr;
void __fastcall HookedLoadList(void* self) {
    const uint64_t n = g_loadListCalls.fetch_add(1) + 1;
    LOG_DEBUG_KV(kCat, "loadlist_enter", KV("call_n", n),
        KV::BareStr("detail",
            "mfLoadShaderList REACHED — about to fill the precache list from "
            "%USER%/shaders/shaderlist.txt (absent on this cache)."));
    g_origLoadList(self);
}

ThisFn_t g_origPrecache = nullptr;
void __fastcall HookedPrecache(void* self) {
    const uint64_t n = g_precacheCalls.fetch_add(1) + 1;
    // THE O3 DISCRIMINATOR: this running swap-ON means the gate is in/after the
    // submit (the per-shader vtable slots or the job dispatch); NOT running means
    // the gate is between cache-validation-accept (PROBE R) and here.
    LOG_DEBUG_KV(kCat, "precache_enter", KV("call_n", n),
        KV::BareStr("detail",
            "_PrecacheShaderList REACHED — the submit ran. The gate is in/after "
            "the per-shader submit or the job-deferred PSO creation, NOT before."));
    g_origPrecache(self);
}

// PROBE R3 — the RUNTIME PSO-precache (PipelineStateCacheManager). 2-arg
// (param_1 = the manager `this`, param_2 = a u4 mode). Read [this+0x10] as a PSO
// list-pointer base for a coarse "did it precache anything" signal (the decompile
// computes the count as a delta of [param_1+0x10] across the inner call). THE O4
// DISCRIMINATOR: this running swap-ON means the menu pipeline precache is reached
// (gate is INSIDE it or downstream); NOT running swap-ON but running swap-OFF means
// THIS is the missing trigger.
using PsoPrecacheFn_t = void(__fastcall*)(void* self, unsigned int mode);

PsoPrecacheFn_t g_origPsoGfx = nullptr;
void __fastcall HookedPsoGfx(void* self, unsigned int mode) {
    const uint64_t n = g_psoGfxCalls.fetch_add(1) + 1;
    uint64_t listBase = 0;
    if (self) std::memcpy(&listBase,
        reinterpret_cast<const uint8_t*>(self) + 0x10, sizeof(listBase));
    LOG_DEBUG_KV(kCat, "pso_precache_gfx_enter", KV("call_n", n),
        KV("mode", (uint64_t)mode), KV("list_base", listBase),
        KV::BareStr("detail",
            "PSO PRECACHING Graphics REACHED (the menu pipeline precache). If this "
            "fires swap-OFF but not swap-ON, THIS is the O4 missing trigger."));
    g_origPsoGfx(self, mode);
}

PsoPrecacheFn_t g_origPsoComp = nullptr;
void __fastcall HookedPsoComp(void* self, unsigned int mode) {
    const uint64_t n = g_psoCompCalls.fetch_add(1) + 1;
    LOG_DEBUG_KV(kCat, "pso_precache_comp_enter", KV("call_n", n),
        KV("mode", (uint64_t)mode),
        KV::BareStr("detail", "PSO PRECACHING Compute REACHED."));
    g_origPsoComp(self, mode);
}

bool HookRva(const kcdx::pe::ModuleView& whgame, uint32_t rva, void* detour,
             void** origOut, const char* label) {
    void* target = reinterpret_cast<void*>(
        reinterpret_cast<uintptr_t>(whgame.base) + rva);
    MH_STATUS s = MH_CreateHook(target, detour, origOut);
    if (s != MH_OK && s != MH_ERROR_ALREADY_CREATED) {
        LOG_ERROR_KV(kCat, "hook_create_failed",
            KV::BareStr("label", label),
            KV("rva", (uint64_t)rva),
            KV("mh_status", (uint64_t)s));
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        LOG_ERROR_KV(kCat, "hook_enable_failed",
            KV::BareStr("label", label), KV("rva", (uint64_t)rva));
        return false;
    }
    LOG_INFO_KV(kCat, "hook_armed",
        KV::BareStr("label", label),
        KV("target", reinterpret_cast<uintptr_t>(target)));
    return true;
}

// Bounded summary watcher — same sanctioned diagnostic-poll shape as PROBE P/K
// (one dedicated thread, nothing suspended). Flushes the tallies so the A/B diff
// is readable without per-call flooding; stops after ~2 min.
constexpr DWORD kSummaryMs = 3000;
std::atomic<bool> g_watcherStarted{false};

DWORD WINAPI SummaryMain(LPVOID) {
    LOG_INFO_KV(kCat, "watcher_started",
        KV::BareStr("detail",
            "KI-0028 watcher armed. R: validation ACCEPTS (proven). R2: the "
            "_PrecacheShaderList shaderlist path runs on NEITHER path (exonerated). "
            "R3 (the O4 discriminator): pso_gfx_calls>0 swap-OFF but ==0 swap-ON => "
            "the runtime PSO-precache (PipelineStateCacheManager) is THE missing "
            "trigger — find what gates it. pso_gfx_calls>0 swap-ON too => it runs "
            "but precaches 0 / the gate is downstream (the job-deferred PSO create)."));
    for (int reads = 0; reads < 40; ++reads) {  // ~2 min
        Sleep(kSummaryMs);
        LOG_INFO_KV(kCat, "summary",
            KV("loader_calls",  g_loaderCalls.load()),
            KV("loader_reject", g_loaderReject.load()),
            KV("loader_accept", g_loaderAccept.load()),
            KV("driver_calls",  g_driverCalls.load()),
            KV("orch_calls",    g_orchCalls.load()),
            KV("loadlist_calls",g_loadListCalls.load()),
            KV("precache_calls",g_precacheCalls.load()),
            KV("pso_gfx_calls", g_psoGfxCalls.load()),
            KV("pso_comp_calls",g_psoCompCalls.load()));
    }
    LOG_INFO_KV(kCat, "watcher_done",
        KV::BareStr("detail", "40 summary reads taken; stopping the watcher."));
    return 0;
}

void StartWatcherOnce() {
    bool expected = false;
    if (!g_watcherStarted.compare_exchange_strong(expected, true,
                                                  std::memory_order_acq_rel)) {
        return;
    }
    HANDLE h = CreateThread(nullptr, 0, SummaryMain, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
    else g_watcherStarted.store(false, std::memory_order_release);
}

}  // namespace

void DispatchProbeStart() {
    bool expected = false;
    if (!g_armed.compare_exchange_strong(expected, true,
                                         std::memory_order_acq_rel)) {
        return;  // already armed
    }
    MH_STATUS mi = MH_Initialize();
    if (mi != MH_OK && mi != MH_ERROR_ALREADY_INITIALIZED) {
        LOG_ERROR_KV(kCat, "mh_init_failed", KV("mh_status", (uint64_t)mi));
        g_armed.store(false, std::memory_order_release);
        return;
    }

    kcdx::pe::ModuleView whgame{};
    if (!kcdx::pe::OpenModule(L"WHGame.dll", whgame) || !whgame.base) {
        LOG_ERROR_KV(kCat, "whgame_not_loaded", KV::BareStr("detail",
            "WHGame.dll not resolvable — cannot arm the dispatch probe (the "
            "validation-gate RVAs cannot be located this boot)."));
        return;
    }

    bool any = false;
    any |= HookRva(whgame, kRvaLoader,
                   reinterpret_cast<void*>(&HookedLoader),
                   reinterpret_cast<void**>(&g_origLoader),
                   "lookupdata_loader_FUN_180b04984");
    any |= HookRva(whgame, kRvaDriver,
                   reinterpret_cast<void*>(&HookedDriver),
                   reinterpret_cast<void**>(&g_origDriver),
                   "validate_driver_FUN_180b04478");
    // PROBE R2 — the precache-submit span (downstream of the now-falsified
    // validation gate).
    any |= HookRva(whgame, kRvaOrchestrator,
                   reinterpret_cast<void*>(&HookedOrch),
                   reinterpret_cast<void**>(&g_origOrch),
                   "compile_orchestrator_FUN_18250dd8c");
    any |= HookRva(whgame, kRvaLoadList,
                   reinterpret_cast<void*>(&HookedLoadList),
                   reinterpret_cast<void**>(&g_origLoadList),
                   "mfLoadShaderList_FUN_1819d4a54");
    any |= HookRva(whgame, kRvaPrecache,
                   reinterpret_cast<void*>(&HookedPrecache),
                   reinterpret_cast<void**>(&g_origPrecache),
                   "PrecacheShaderList_FUN_1825091e0");
    // PROBE R3 — the RUNTIME PSO-precache (the actual menu pipeline build path).
    any |= HookRva(whgame, kRvaPsoPrecacheGfx,
                   reinterpret_cast<void*>(&HookedPsoGfx),
                   reinterpret_cast<void**>(&g_origPsoGfx),
                   "PSOPrecacheGfx_FUN_180bb2ad8");
    any |= HookRva(whgame, kRvaPsoPrecacheComp,
                   reinterpret_cast<void*>(&HookedPsoComp),
                   reinterpret_cast<void**>(&g_origPsoComp),
                   "PSOPrecacheComp_FUN_180bb23c0");

    LOG_INFO_KV(kCat, "dispatch_hooks_armed", KV("ok", any ? 1 : 0),
        KV::BareStr("detail",
            "after-hooked the shader-cache validation gate (lookupdata.bin loader "
            "+ validate driver). The loader's return per call is the gate verdict; "
            "armed before the swap decision so it fires swap-ON and swap-OFF."));
    StartWatcherOnce();
}

}  // namespace kcdx::fs_takeover
